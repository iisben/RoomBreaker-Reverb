#include "ImpactGenerator.h"

namespace rb::dsp
{
namespace
{
    constexpr WallArray kWallPan       { 0.0f, 0.0f, 0.8f, -0.8f, 0.15f, -0.1f };
    constexpr WallArray kWallBoomScale { 1.0f, 0.95f, 1.05f, 1.08f, 1.15f, 0.82f };  // floor drops lowest
    constexpr float kBoomLevel     = 0.45f;
    constexpr float kThudLevel     = 0.9f;    // broadband body of the hit, relative to the boom
    constexpr float kCrackLevel    = 0.9f;
    constexpr float kChirpLevel    = 0.3f;
    constexpr float kWarnLevel     = 0.07f;
    constexpr float kPieceLevel    = 0.5f;
    constexpr float kCreakLevel    = 0.9f;
    constexpr float kGrainLevel    = 0.22f;
    constexpr float kBedLevel      = 0.35f;
    constexpr float kRubbleTau     = 2.5f;    // s
    constexpr float kRubbleSettled = 0.03f;   // rubble energy below which everything has come to rest
    constexpr float kStoreyMetres  = 2.8f;    // ceiling height at Size 1
    constexpr float kGravity       = 9.81f;
    constexpr float kOmoriC        = 0.15f;   // s: aftershock rate ~ k / (t + c)^p
    constexpr float kOmoriP        = 1.15f;
    constexpr float kSettleSeconds = 5.0f;
    constexpr float kLoadMemorySeconds = 2.0f;   // Kaiser memory: how long a wall remembers its peak load
    constexpr float kLoadRiseFull = 0.25f;       // load climbing this fast (× capacity per s) crackles at full rate
    constexpr float kKnee          = 0.5f;    // impact bus: linear below, tanh-rounded above (ceiling ~0.9)

    inline float softKnee (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kKnee) return x;
        return std::copysign (kKnee + 0.4f * std::tanh ((a - kKnee) / 0.4f), x);
    }

    inline void panGains (float pan, float& l, float& r) noexcept
    {
        const float theta = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        l = std::cos (theta);
        r = std::sin (theta);
    }
}

void ImpactGenerator::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    dcL.set (18.0f, fs);
    dcR.set (18.0f, fs);
    reset();
}

void ImpactGenerator::reset()
{
    for (auto& b : booms)     b = Boom {};
    for (auto& n : noises)    n = Noise {};
    for (auto& c : chirps)    c = Chirp {};
    for (auto& c : creaks)    c = Creak {};
    for (auto& s : sequences) s = Sequence {};
    numPending = 0;
    stressRatio.fill (0.0f);
    integrityNow.fill (1.0f);
    loadMemory.fill (0.0f);
    loadRise.fill (0.0f);
    rubble = bedAmp = bedAmpInc = 0.0f;
    bedL.reset(); bedR.reset();
    dcL.reset(); dcR.reset();
}

//==============================================================================
ImpactGenerator::Noise& ImpactGenerator::allocateNoise() noexcept
{
    Noise* quietest = &noises[0];
    for (auto& n : noises)
    {
        if (! n.active)
            return n;
        if (n.startDelay == 0 && n.amp < quietest->amp)
            quietest = &n;
    }
    return *quietest;
}

void ImpactGenerator::startNoise (float hz, float q, float amp, float decayMs, int delay, float pan) noexcept
{
    if (amp <= 1.0e-5f)
        return;
    auto& n = allocateNoise();
    n.active = true;
    n.startDelay = std::max (0, delay);
    n.band.reset();
    n.band.set (std::clamp (hz, 25.0f, std::min (16000.0f, 0.45f * fs)), q, fs);
    n.amp = amp;
    n.decay = std::exp (-1.0f / (std::max (decayMs, 0.3f) * 1.0e-3f * fs));
    n.rng = Rng (rng.next());
    panGains (pan, n.panL, n.panR);
}

void ImpactGenerator::startChirp (float hiHz, float loHz, float ms, float amp, int delay, float pan) noexcept
{
    if (amp <= 1.0e-5f || hiHz <= loHz)
        return;
    Chirp* c = &chirps[0];
    for (auto& cand : chirps)
    {
        if (! cand.active) { c = &cand; break; }
        if (cand.startDelay == 0 && cand.amp < c->amp) c = &cand;
    }
    const float n = std::max (ms, 2.0f) * 1.0e-3f * fs;
    c->active     = true;
    c->startDelay = std::max (0, delay);
    c->phase      = 0.0f;
    c->freq       = std::min (hiHz, 0.4f * fs);
    c->freqMul    = std::pow (loHz / hiHz, 1.0f / n);            // exponential sweep, hi → lo over ms
    c->freqMin    = 0.5f * loHz;
    c->amp        = amp;
    c->ampDecay   = std::exp (-1.0f / (0.45f * n));               // ~ -19 dB by the end of the sweep
    c->attack     = 0.0f;
    c->attackInc  = 1.0f / (0.0003f * fs);
    panGains (pan, c->panL, c->panR);
}

void ImpactGenerator::startBoom (int w, float m, float amp, int offset) noexcept
{
    const auto& v = *voicing;
    Boom* b = &booms[0];
    for (auto& cand : booms)
    {
        if (! cand.active) { b = &cand; break; }
        if (cand.amp < b->amp) b = &cand;
    }
    const float mm = std::min (m, 1.0f);
    const float f0 = v.boomHz * kWallBoomScale[(size_t) w] * (1.2f - 0.3f * mm) * (1.0f + 0.04f * rng.bi());  // bigger falls deeper
    const float decay = v.boomDecay * (0.7f + 0.5f * mm);
    b->active     = true;
    b->startDelay = std::max (0, offset);
    b->phase      = 0.0f;
    b->freqEnd    = 0.5f * f0;
    b->freqSpan   = 0.5f * f0;
    b->freqDecay  = std::exp (-1.0f / (0.09f * fs));      // pitch falls an octave over ~90 ms
    b->amp        = amp;
    b->ampDecay   = std::exp (-1.0f / (decay * fs));
    b->attack     = 0.0f;
    b->attackInc  = 1.0f / (0.0015f * fs);                // 1.5 ms: no click, still a thump
    b->age        = 0;
}

void ImpactGenerator::startCreak (int w, float m, int delay) noexcept
{
    const auto& v = *voicing;
    const float level = kCreakLevel * (0.4f * impactAmount + 0.6f * rumbleAmount) * (0.4f + 0.6f * std::min (2.5f * m, 1.0f));
    if (level <= 1.0e-5f || v.creakHz <= 0.0f)
        return;

    // A wall that is already groaning keeps groaning (and a little harder) rather than starting over.
    for (auto& c : creaks)
        if (c.active && c.wall == w && c.age < c.hold)
        {
            c.hold = std::min (c.hold + (int) (0.25f * fs), c.age + (int) (1.2f * fs));
            c.amp  = std::min (c.amp * 1.15f + 0.2f * level, 1.6f * level);
            return;
        }

    Creak* c = &creaks[0];
    for (auto& cand : creaks)
    {
        if (! cand.active) { c = &cand; break; }
        if (cand.env < c->env) c = &cand;
    }
    const float hold = (0.15f + 0.6f * rng.uni() * std::min (1.0f, 2.0f * m)) * fs;
    const float rate = v.creakRate * (0.7f + 0.6f * rng.uni());
    const float rateEnd = rate * (0.55f + rng.uni());              // slips speed up or bog down
    const float hz = v.creakHz * (1.0f + 0.15f * rng.bi());
    c->active     = true;
    c->wall       = w;
    c->startDelay = std::max (0, delay);
    c->age        = 0;
    c->hold       = (int) hold;
    c->phase      = rng.uni();
    c->rate       = rate;
    c->rateInc    = (rateEnd - rate) / hold;
    c->rateLo     = 0.4f * v.creakRate;
    c->rateHi     = 2.0f * v.creakRate;
    c->amp        = level;
    c->env        = 0.0f;
    c->grit       = v.creakGrit;
    c->norm       = 1.0f / std::sqrt (v.creakQ);                    // the resonance rings ~Q times longer: keep it level
    c->res.reset();
    c->res.set (std::min (hz, 0.4f * fs), v.creakQ, fs);
    c->rng = Rng (rng.next());
    panGains (kWallPan[(size_t) w] + 0.3f * rng.bi(), c->panL, c->panR);
}

//==============================================================================
void ImpactGenerator::crack (int w, float m, int offset, float level) noexcept
{
    if (level <= 0.0f)
        return;
    const auto& v = *voicing;
    const float mm = std::min (m, 1.2f);
    const float weight = mm / 1.2f;                       // 0 hairline .. 1 collapse
    const float bright = 1.3f - 0.55f * weight;           // small cracks tick high, big ones snap low
    const float pan0 = kWallPan[(size_t) w] + 0.25f * rng.bi();
    const float dir = rng.uni() < 0.5f ? -1.0f : 1.0f;    // which way the crack runs
    const float mainAmp = kCrackLevel * level * std::pow (mm, 0.85f);

    // The snap.
    startNoise (v.crackHz * bright * (0.85f + 0.3f * rng.uni()), 1.1f, mainAmp,
                v.crackMs * (0.6f + 0.7f * weight) * (0.8f + 0.4f * rng.uni()), offset, pan0);

    // The crack running through: micro-fractures spread over the material's crack time, coming
    // faster as it accelerates. Their sizes follow a power law (a few loud, many faint), and the
    // louder ones are lower: bigger pieces of material letting go.
    const float spread = v.spreadMs * 1.0e-3f * fs * (0.5f + 0.9f * weight);
    const int count = std::clamp ((int) std::lround ((2.0f + 11.0f * weight) * v.grains), 1, 18);
    const float tickMs = v.crackMs / 18.0f;
    for (int k = 1; k <= count; ++k)
    {
        const float x = (float) k / (float) count;
        const float t = spread * std::pow (x, 0.7f) * (0.85f + 0.3f * rng.uni());
        const float size = std::pow (rng.uni(), 2.2f);
        const float a = 0.55f * mainAmp * size * (1.0f - 0.45f * x);
        const float hz = v.crackHz * bright * (0.6f + 1.8f * rng.uni()) * (1.3f - 0.5f * size);
        startNoise (hz, 1.4f, a, (0.4f + 2.5f * rng.uni()) * tickMs, offset + (int) t,
                    pan0 + dir * 0.45f * x * (0.5f + 0.5f * weight));
    }

    // Dispersion: thin plates whistle. Bigger breaks sweep lower and longer, with more sweeps.
    if (v.chirp > 0.0f)
    {
        const int sweeps = 1 + (int) (v.chirp * weight * 3.0f * rng.uni());
        for (int k = 0; k < sweeps; ++k)
            startChirp (v.chirpHiHz * (0.8f + 0.4f * rng.uni()),
                        v.chirpLoHz * (0.7f + 0.6f * rng.uni()) * (1.2f - 0.4f * weight),
                        v.chirpMs * (0.6f + 0.8f * rng.uni()) * (0.7f + 0.6f * weight),
                        kChirpLevel * level * v.chirp * mm * (0.5f + 0.5f * rng.uni()),
                        offset + (int) (rng.uni() * spread), pan0 + 0.4f * rng.bi());
    }
}

void ImpactGenerator::dropPieces (int w, float m, int offset) noexcept
{
    if (rumbleAmount <= 0.0f)
        return;
    const auto& v = *voicing;
    const float mm = std::min (m, 1.3f);
    const int count = std::clamp ((int) std::lround (rumbleAmount * (1.0f + 7.0f * mm) * v.grains), 0, 14);
    const float storey = kStoreyMetres * roomSize;

    for (int i = 0; i < count; ++i)
    {
        // Size: mostly small, the odd big piece, bigger breaks shed bigger pieces.
        const float s = rng.uni() * rng.uni() * (0.35f + 0.65f * std::min (mm, 1.0f));
        // Where it falls from: anywhere up the wall, the full height from the ceiling, a few
        // centimetres when the floor itself gives way.
        const float h = w == Ceiling ? storey * (0.85f + 0.15f * rng.uni())
                      : w == Floor   ? 0.05f + 0.1f * rng.uni()
                                     : storey * (0.15f + 0.85f * rng.uni());
        const float fall = std::sqrt (2.0f * h / kGravity);
        const float lag  = (0.01f + 0.25f * rng.uni() * rng.uni()) * (0.5f + mm);   // not everything lets go at once
        const float v0   = std::sqrt (2.0f * kGravity * h);

        Pending p;
        p.due     = offset + (int) ((lag + fall) * fs);
        p.hz      = v.rubbleHz * (0.35f + 1.5f * (1.0f - s)) * (1.0f + 0.12f * rng.bi());
        p.q       = 0.9f;
        p.amp     = kPieceLevel * rumbleAmount * (0.2f + 0.8f * s) * std::min (1.0f, 0.35f + 0.13f * v0);
        p.decayMs = 3.0f + 35.0f * s;
        p.pan     = 0.8f * kWallPan[(size_t) w] + 0.4f * rng.bi();
        p.thump   = s > 0.5f ? 0.8f * p.amp : 0.0f;
        p.rubble  = 0.03f + 0.1f * s;
        p.shatter = v.shatter * (1.0f + 4.0f * s);
        schedule (p);

        // Bounces: each rebound leaves at e times the speed, so it flies for 2·e·v/g and lands e times softer.
        float speed = v0, at = (float) p.due, amp = p.amp;
        const float e = v.restitution * (0.8f + 0.4f * rng.uni());
        const int bounces = s < 0.15f ? 3 : 1 + (int) (2.0f * rng.uni());
        for (int b = 1; b <= bounces; ++b)
        {
            speed *= e;
            amp   *= e;
            if (speed < 0.25f || amp < 1.0e-4f)
                break;
            at += 2.0f * speed / kGravity * fs;
            Pending q;
            q.due     = (int) at;
            q.hz      = p.hz * std::pow (1.08f, (float) b);
            q.q       = 1.0f;
            q.amp     = amp;
            q.decayMs = 0.7f * p.decayMs;
            q.pan     = p.pan + 0.08f * rng.bi();
            schedule (q);
        }
    }
}

void ImpactGenerator::schedule (const Pending& p) noexcept
{
    if (numPending < kQueueSize && p.amp > 1.0e-5f)
        pending[(size_t) numPending++] = p;
}

void ImpactGenerator::land (const Pending& p, int offset) noexcept
{
    const auto& v = *voicing;
    startNoise (p.hz, p.q, p.amp, p.decayMs, offset, p.pan);
    if (p.thump > 0.0f)
        startNoise (v.boomHz * 2.2f * (1.0f + 0.1f * rng.bi()), 0.7f, p.thump, 40.0f, offset, 0.6f * p.pan);
    rubble = std::min (3.0f, rubble + p.rubble);

    // Brittle pieces burst on landing: a spray of tiny, bright fragments.
    const int fragments = (int) (p.shatter + rng.uni());
    for (int k = 0; k < fragments; ++k)
        startNoise (v.crackHz * (1.2f + 1.3f * rng.uni()), 2.5f, 0.35f * p.amp * (0.3f + 0.7f * rng.uni()),
                    1.0f + 3.0f * rng.uni(), offset + (int) (rng.uni() * 0.025f * fs), p.pan + 0.2f * rng.bi());
}

//==============================================================================
void ImpactGenerator::trigger (const FractureEvent& e) noexcept
{
    if (voicing == nullptr)
        return;

    const auto& v = *voicing;
    const int w = std::clamp (e.wall, 0, kNumWalls - 1);
    const float m = std::clamp (e.magnitude, 0.0f, 1.5f);

    if (! e.brittle)
    {
        // Ductile: the wall groans; thin plates "oil-can" (pop through with a little sweep);
        // a big creak shakes a little dust down.
        rubble = std::min (3.0f, rubble + 0.12f * m);
        startCreak (w, m, e.sampleOffset);
        if (v.chirp > 0.4f && rng.uni() < 0.35f * v.chirp)
            startChirp (0.5f * v.chirpHiHz, v.chirpLoHz, 0.6f * v.chirpMs,
                        0.5f * kChirpLevel * impactAmount * v.chirp * std::min (2.0f * m, 1.0f),
                        e.sampleOffset + (int) (rng.uni() * 0.1f * fs), kWallPan[(size_t) w] + 0.3f * rng.bi());
        if (m > 0.3f)
            dropPieces (w, 0.15f * m, e.sampleOffset);
        return;
    }

    rubble = std::min (3.0f, rubble + 0.2f * m);   // dust: the pieces add the rest when they land

    if (impactAmount > 0.0f)
    {
        // --- boom. Several walls failing in the same instant are ONE collapse: merge into a boom
        //     starting within 40 ms of this one (energy sum, capped) instead of stacking coherent sines.
        //     Delayed charges are their own bangs.
        const float newAmp = kBoomLevel * impactAmount * std::min (m, 1.2f);
        bool merged = false;
        for (auto& cand : booms)
        {
            const int candStart = cand.startDelay > 0 ? cand.startDelay : -cand.age;
            if (cand.active && std::abs (candStart - e.sampleOffset) < (int) (0.04f * fs))
            {
                cand.amp = std::min (std::sqrt (cand.amp * cand.amp + 0.35f * newAmp * newAmp), 1.25f * std::max (cand.amp, newAmp));
                cand.ampDecay = std::max (cand.ampDecay, std::exp (-1.0f / (v.boomDecay * 1.2f * fs)));
                merged = true;
                break;
            }
        }
        if (! merged)
            startBoom (w, m, newAmp, e.sampleOffset);

        // The thud: the broadband body of the hit, lower and longer for bigger breaks.
        const float mm = std::min (m, 1.0f);
        startNoise (v.boomHz * (1.6f - 0.4f * mm) * (1.0f + 0.1f * rng.bi()), 0.55f,
                    kThudLevel * newAmp * (merged ? 0.5f : 1.0f), 30.0f + 50.0f * mm, e.sampleOffset, 0.3f * kWallPan[(size_t) w]);

        crack (w, m, e.sampleOffset, impactAmount);

        // A big break starts a settling sequence (its aftershocks), or adds to the one on this wall.
        if (m > 0.45f)
        {
            const float k = 0.8f * (m - 0.3f) * v.grains;
            Sequence* s = nullptr;
            for (auto& cand : sequences)
                if (cand.active && cand.wall == w) { s = &cand; break; }
            if (s != nullptr)
                s->k = std::min (2.0f, s->k * std::exp (-s->t / 1.5f) + k);
            else
            {
                s = &sequences[0];
                for (auto& cand : sequences)
                {
                    if (! cand.active) { s = &cand; break; }
                    if (cand.k < s->k) s = &cand;
                }
                if (! s->active || s->k < k)
                    *s = { true, w, -(float) std::max (0, e.sampleOffset) / fs, k };
            }
        }
    }

    dropPieces (w, m, e.sampleOffset);
}

void ImpactGenerator::update (float dt, int blockLength, float meanLooseness, float activity) noexcept
{
    if (voicing == nullptr)
        return;
    const auto& v = *voicing;
    const auto block = (float) blockLength;

    // --- pieces landing in this tick
    for (int i = 0; i < numPending;)
    {
        auto& p = pending[(size_t) i];
        if (p.due < blockLength)
        {
            land (p, std::max (0, p.due));
            pending[(size_t) i] = pending[(size_t) --numPending];
        }
        else
        {
            p.due -= blockLength;
            ++i;
        }
    }

    // --- settling: aftershocks at a rate that falls off as k / (t + c)^p
    for (auto& s : sequences)
    {
        if (! s.active)
            continue;
        s.t += dt;
        if (s.t <= 0.0f)
            continue;
        if (s.t > kSettleSeconds)
        {
            s.active = false;
            continue;
        }
        const float rate = 2.5f * s.k / std::pow (s.t + kOmoriC, kOmoriP);
        if (rng.uni() < rate * dt)
        {
            // usually the same wall, sometimes a neighbour taking the load
            int w = s.wall;
            if (rng.uni() < 0.3f)
                w = (s.wall + 2 + (int) (rng.uni() * 4.0f)) % kNumWalls;
            const float m = 0.12f + 0.25f * rng.uni() * rng.uni() * std::min (1.0f, s.k);
            const int offset = (int) (rng.uni() * block);
            crack (w, m, offset, impactAmount);
            if (rng.uni() < 0.5f)
                dropPieces (w, 0.3f * m, offset);
        }
    }

    // --- warning: an overloaded wall crackles before it breaks, while its load is climbing.
    // Kaiser effect: a material only emits once the load passes what it has recently carried,
    // so the crackle follows a rising load (a riser, a crescendo) and stops on a steady one. The
    // memory fades over a couple of seconds once the load drops (healing, and the Felicity effect).
    {
        const float relax = std::exp (-dt / kLoadMemorySeconds);
        const float riseA = 1.0f - std::exp (-dt / 0.05f);
        for (int w = 0; w < kNumWalls; ++w)
        {
            const float r = stressRatio[(size_t) w];
            float& memory = loadMemory[(size_t) w];
            const float passing = std::max (0.0f, r - memory) / std::max (dt, 1.0e-6f);   // ratio / s above the memory
            loadRise[(size_t) w] += (passing - loadRise[(size_t) w]) * riseA;
            memory = std::max (r, r + (memory - r) * relax);
        }
    }
    if (impactAmount > 0.0f)
    {
        for (int w = 0; w < kNumWalls; ++w)
        {
            if (integrityNow[(size_t) w] < 0.02f)
                continue;
            const float x = smoothstep (0.55f, 1.05f, stressRatio[(size_t) w])
                          * clamp01 (loadRise[(size_t) w] / kLoadRiseFull);
            if (x <= 0.0f)
                continue;
            if (rng.uni() < 12.0f * x * x * dt)
            {
                const float size = rng.uni();
                startNoise (v.crackHz * (1.6f + 1.6f * rng.uni()), 2.0f,
                            kWarnLevel * impactAmount * (0.2f + 0.8f * size * size * size) * (0.5f + 0.5f * x),
                            0.4f + 0.8f * rng.uni(), (int) (rng.uni() * block), kWallPan[(size_t) w] + 0.3f * rng.bi());
            }
        }
    }

    // --- rubble: loose slabs under load keep shedding dust
    rubble += 0.35f * meanLooseness * clamp01 (activity * 2.0f) * dt;
    // A pile settles faster as less is left to fall: the time constant shrinks with the rubble.
    rubble = std::min (3.0f, rubble) * std::exp (-dt / (kRubbleTau * std::clamp (rubble, 0.3f, 1.0f)));

    // Once the last of it has settled the room goes quiet. Measured above the settle floor, the
    // bed and the trickle both reach zero there, and then the rubble is cleared: an exponential
    // alone never ends, and the grains used to keep dropping for 10-25 s after a break.
    const float loose = std::max (0.0f, rubble - kRubbleSettled);
    if (rubble < kRubbleSettled)
        rubble = 0.0f;

    const float bedTarget = kBedLevel * rumbleAmount * std::min (loose, 1.5f);
    bedAmpInc = (bedTarget - bedAmp) / (float) std::max (blockLength, 1);
    const float bedHz = std::clamp (v.boomHz * 1.6f, 45.0f, 160.0f);
    bedL.set (bedHz, 0.8f, fs);
    bedR.set (bedHz * 1.07f, 0.8f, fs);

    if (rumbleAmount <= 0.0f)
        return;

    // Trickle: Poisson grains, mostly small, heavier when lots is coming down. The last ones are
    // fewer and smaller too: dust, not stones.
    const float rate = 70.0f * std::min (loose, 2.0f) * rumbleAmount;   // grains / s
    const float last = smoothstep (0.0f, 0.3f, loose);
    float expected = rate * dt;
    while (expected > 0.0f)
    {
        if (rng.uni() < std::min (expected, 1.0f))
        {
            const float size = rng.uni() * rng.uni();
            const float hz   = v.rubbleHz * (0.45f + 1.4f * (1.0f - size)) * (1.0f + 0.1f * rng.bi());
            const float amp  = kGrainLevel * rumbleAmount * (0.25f + 0.75f * size) * std::min (1.0f, 0.4f + 0.4f * rubble) * last;
            startNoise (hz, 0.7f, amp, 3.0f + 22.0f * size, (int) (rng.uni() * block), 0.9f * rng.bi());
        }
        expected -= 1.0f;
    }
}

//==============================================================================
void ImpactGenerator::process (float* outL, float* outR, int n) noexcept
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    const float radPerHz = kTwoPi / fs;
    for (auto& b : booms)
    {
        if (! b.active)
            continue;
        for (int i = 0; i < n; ++i)
        {
            if (b.startDelay > 0) { --b.startDelay; continue; }
            ++b.age;
            const float f = b.freqEnd + b.freqSpan;
            b.freqSpan *= b.freqDecay;
            b.phase += radPerHz * f;
            if (b.phase > kTwoPi) b.phase -= kTwoPi;
            b.attack = std::min (1.0f, b.attack + b.attackInc);
            // A touch of second harmonic so the boom reads on small speakers: sin 2p = 2 sin p cos p.
            const float sn = fastSin (b.phase);
            const float cs = fastSin (b.phase < 1.5f * kPi ? b.phase + kHalfPi : b.phase - 1.5f * kPi);
            const float s = sn * (1.0f + 0.36f * cs) * b.amp * b.attack;
            b.amp *= b.ampDecay;
            outL[i] += s;
            outR[i] += s;
        }
        if (b.startDelay == 0 && b.amp < 1.0e-5f)
            b.active = false;
    }

    for (auto& c : chirps)
    {
        if (! c.active)
            continue;
        for (int i = 0; i < n; ++i)
        {
            if (c.startDelay > 0) { --c.startDelay; continue; }
            c.phase += radPerHz * c.freq;
            if (c.phase > kTwoPi) c.phase -= kTwoPi;
            c.freq = std::max (c.freqMin, c.freq * c.freqMul);
            c.attack = std::min (1.0f, c.attack + c.attackInc);
            const float s = fastSin (c.phase) * c.amp * c.attack;
            c.amp *= c.ampDecay;
            outL[i] += s * c.panL;
            outR[i] += s * c.panR;
        }
        if (c.startDelay == 0 && c.amp < 1.0e-5f)
            c.active = false;
    }

    for (auto& c : creaks)
    {
        if (! c.active)
            continue;
        const float envA = std::exp (-1.0f / (0.03f * fs));
        for (int i = 0; i < n; ++i)
        {
            if (c.startDelay > 0) { --c.startDelay; continue; }
            const float target = c.age < c.hold ? 1.0f : 0.0f;
            c.env = target + envA * (c.env - target);
            ++c.age;

            // Stick-slip: the surfaces hold, then let go in a tiny slip, over and over.
            c.rate = std::clamp (c.rate + c.rateInc, c.rateLo, c.rateHi);
            c.phase += c.rate / fs;
            float x = c.grit * 0.03f * c.rng.bi();                    // grinding between the slips
            if (c.phase >= 1.0f)
            {
                c.phase -= 1.0f - 0.6f * c.grit * (c.rng.uni() - 0.5f);   // grit makes the slips irregular
                x += 1.0f + 0.5f * c.grit * c.rng.bi();
            }
            const float s = c.res.process (x * c.amp * c.env).bp * c.norm;
            outL[i] += s * c.panL;
            outR[i] += s * c.panR;
        }
        if (c.age > c.hold && c.env < 1.0e-3f)
            c.active = false;
    }

    // Cracks, micro-fractures, landings and grains: band-passed noise bursts. They are independent
    // of each other, so the sounding ones are packed into arrays and advanced together each sample
    // (SIMD): noise generators, filters and envelopes for up to 64 voices in a few vector steps.
    {
        constexpr int kMax = kNumNoises;
        static_assert (kMax % 8 == 0, "noise count must be a multiple of 8");
        alignas (32) float a1[kMax], a2[kMax], a3[kMax], s1[kMax], s2[kMax], amp[kMax], decay[kMax], panL[kMax], panR[kMax], tl[kMax], tr[kMax];
        alignas (32) float gate[kMax];
        alignas (32) uint32_t seed[kMax];
        int slot[kMax], waiting[kMax], count = 0, numWaiting = 0;

        for (int k = 0; k < kNumNoises; ++k)
        {
            auto& v = noises[(size_t) k];
            if (! v.active) continue;
            a1[count] = v.band.a1;  a2[count] = v.band.a2;  a3[count] = v.band.a3;
            s1[count] = v.band.ic1eq;  s2[count] = v.band.ic2eq;
            amp[count] = v.amp;  decay[count] = v.decay;  panL[count] = v.panL;  panR[count] = v.panR;
            seed[count] = v.rng.state;
            gate[count] = v.startDelay > 0 ? 0.0f : 1.0f;
            if (v.startDelay > 0) waiting[numWaiting++] = count;
            slot[count++] = k;
        }

        if (count > 0)
        {
            const int m = (count + 7) & ~7;
            for (int k = count; k < m; ++k)
            {
                a1[k] = a2[k] = a3[k] = s1[k] = s2[k] = amp[k] = panL[k] = panR[k] = gate[k] = 0.0f;
                decay[k] = 1.0f;
                seed[k] = 1u;
            }

            for (int i = 0; i < n; ++i)
            {
                // A voice that has not started yet is held silent: gate 0 freezes its envelope.
                for (int w = 0; w < numWaiting;)
                {
                    const int k = waiting[w];
                    auto& v = noises[(size_t) slot[k]];
                    if (v.startDelay == 0)
                    {
                        gate[k] = 1.0f;
                        waiting[w] = waiting[--numWaiting];
                    }
                    else
                    {
                        --v.startDelay;
                        ++w;
                    }
                }

                for (int k = 0; k < m; ++k)
                {
                    uint32_t r = seed[k];
                    r ^= r << 13;  r ^= r >> 17;  r ^= r << 5;
                    seed[k] = r;
                    const float white = ((float) (r >> 8) * (2.0f / 16777216.0f) - 1.0f) * gate[k];

                    const float v3 = white - s2[k];
                    const float v1 = a1[k] * s1[k] + a2[k] * v3;
                    const float v2 = s2[k] + a2[k] * s1[k] + a3[k] * v3;
                    s1[k] = 2.0f * v1 - s1[k];
                    s2[k] = 2.0f * v2 - s2[k];

                    const float s = v1 * amp[k];
                    amp[k] *= 1.0f + gate[k] * (decay[k] - 1.0f);
                    tl[k] = s * panL[k];
                    tr[k] = s * panR[k];
                }
                outL[i] += sumLanes (tl, m);
                outR[i] += sumLanes (tr, m);
            }

            for (int k = 0; k < count; ++k)
            {
                auto& v = noises[(size_t) slot[k]];
                v.band.ic1eq = s1[k];
                v.band.ic2eq = s2[k];
                v.amp = amp[k];
                v.rng.state = seed[k];
                if (v.startDelay == 0 && v.amp < 1.0e-5f)
                    v.active = false;
            }
        }
    }

    for (int i = 0; i < n; ++i)
    {
        bedAmp += bedAmpInc;
        if (bedAmp > 1.0e-6f)
        {
            outL[i] += bedL.process (bedRngL.bi()).lp * bedAmp;
            outR[i] += bedR.process (bedRngR.bi()).lp * bedAmp;
        }
        // Gentle knee: stacked booms round off instead of spiking the mix.
        outL[i] = softKnee (dcL.hp (outL[i]));
        outR[i] = softKnee (dcR.hp (outR[i]));
    }
}
} // namespace rb::dsp
