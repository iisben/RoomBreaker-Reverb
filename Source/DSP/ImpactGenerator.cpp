#include "ImpactGenerator.h"

namespace rb::dsp
{
namespace
{
    constexpr WallArray kWallPan       { 0.0f, 0.0f, 0.8f, -0.8f, 0.15f, -0.1f };
    constexpr WallArray kWallBoomScale { 1.0f, 0.95f, 1.05f, 1.08f, 1.15f, 0.82f };  // floor drops lowest
    constexpr float kBoomLevel   = 0.55f;
    constexpr float kCrackLevel  = 0.9f;
    constexpr float kGrainLevel  = 0.22f;
    constexpr float kBedLevel    = 0.35f;
    constexpr float kRubbleTau   = 2.5f;    // s
    constexpr float kKnee        = 0.5f;    // impact bus: linear below, tanh-rounded above (ceiling ~0.9)

    inline float softKnee (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kKnee) return x;
        return std::copysign (kKnee + 0.4f * std::tanh ((a - kKnee) / 0.4f), x);
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
    for (auto& b : booms)  b = Boom {};
    for (auto& n : noises) n = Noise {};
    rubble = bedAmp = bedAmpInc = 0.0f;
    bedL.reset(); bedR.reset();
    dcL.reset(); dcR.reset();
}

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
    n.band.set (std::min (hz, 0.45f * fs), q, fs);
    n.amp = amp;
    n.decay = std::exp (-1.0f / (std::max (decayMs, 0.5f) * 1.0e-3f * fs));
    n.rng = Rng (rng.next());
    const float theta = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    n.panL = std::cos (theta);
    n.panR = std::sin (theta);
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
    const float f0 = v.boomHz * kWallBoomScale[w] * (1.0f + 0.35f * std::min (m, 1.0f)) * (1.0f + 0.04f * rng.bi());
    const float decay = v.boomDecay * (0.7f + 0.5f * std::min (m, 1.0f));
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

void ImpactGenerator::trigger (const FractureEvent& e) noexcept
{
    if (voicing == nullptr)
        return;

    const auto& v = *voicing;
    const int w = std::clamp (e.wall, 0, kNumWalls - 1);
    const float m = std::clamp (e.magnitude, 0.0f, 1.5f);

    // Creaks feed the rubble a little but don't boom.
    rubble = std::min (3.0f, rubble + (e.brittle ? 0.55f : 0.12f) * m);
    if (! e.brittle || impactAmount <= 0.0f)
        return;

    // --- boom. Several walls failing in the same instant are ONE collapse: merge into the boom
    //     that just started (energy sum, capped) instead of stacking coherent sines.
    const float newAmp = kBoomLevel * impactAmount * std::min (m, 1.2f);
    bool merged = false;
    for (auto& cand : booms)
    {
        if (cand.active && cand.age < (int) (0.04f * fs))
        {
            cand.amp = std::min (std::sqrt (cand.amp * cand.amp + 0.35f * newAmp * newAmp), 1.25f * std::max (cand.amp, newAmp));
            cand.ampDecay = std::max (cand.ampDecay, std::exp (-1.0f / (v.boomDecay * 1.2f * fs)));
            merged = true;
            break;
        }
    }
    if (! merged)
        startBoom (w, m, newAmp, e.sampleOffset);

    // --- crack: one sharp burst, plus a second smaller splinter for big hits
    const float pan = kWallPan[w] + 0.25f * rng.bi();
    startNoise (v.crackHz * (0.85f + 0.3f * rng.uni()), 1.1f,
                kCrackLevel * impactAmount * std::min (m, 1.2f), v.crackMs * (0.8f + 0.4f * rng.uni()),
                e.sampleOffset, pan);
    if (m > 0.6f)
        startNoise (v.crackHz * (1.6f + 0.6f * rng.uni()), 1.6f,
                    0.5f * kCrackLevel * impactAmount * (m - 0.4f), 0.6f * v.crackMs,
                    e.sampleOffset + (int) ((0.004f + 0.012f * rng.uni()) * fs), pan + 0.3f * rng.bi());
}

void ImpactGenerator::update (float dt, int blockLength, float meanLooseness, float activity) noexcept
{
    if (voicing == nullptr)
        return;

    // Loose slabs under load keep shedding dust.
    rubble += 0.35f * meanLooseness * clamp01 (activity * 2.0f) * dt;
    rubble = std::min (3.0f, rubble) * std::exp (-dt / kRubbleTau);

    const float bedTarget = kBedLevel * rumbleAmount * std::min (rubble, 1.5f);
    bedAmpInc = (bedTarget - bedAmp) / (float) std::max (blockLength, 1);
    const float bedHz = std::clamp (voicing->boomHz * 1.6f, 45.0f, 160.0f);
    bedL.set (bedHz, 0.8f, fs);
    bedR.set (bedHz * 1.07f, 0.8f, fs);

    if (rumbleAmount <= 0.0f)
        return;

    // Falling chunks: Poisson grains, heavier (lower, longer) when lots is coming down.
    const float rate = 70.0f * std::min (rubble, 2.0f) * rumbleAmount;   // grains / s
    float expected = rate * dt;
    while (expected > 0.0f)
    {
        if (rng.uni() < std::min (expected, 1.0f))
        {
            const float size = rng.uni();
            const float hz   = voicing->rubbleHz * (0.45f + 1.4f * (1.0f - size)) * (1.0f + 0.1f * rng.bi());
            const float amp  = kGrainLevel * rumbleAmount * (0.25f + 0.75f * size) * std::min (1.0f, 0.4f + 0.4f * rubble);
            startNoise (hz, 0.7f, amp, 3.0f + 22.0f * size, (int) (rng.uni() * (float) blockLength), 0.9f * rng.bi());
        }
        expected -= 1.0f;
    }
}

void ImpactGenerator::process (float* outL, float* outR, int n) noexcept
{
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

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
            b.phase += kTwoPi * f / fs;
            if (b.phase > kTwoPi) b.phase -= kTwoPi;
            b.attack = std::min (1.0f, b.attack + b.attackInc);
            // A touch of second harmonic so the boom reads on small speakers.
            const float s = (std::sin (b.phase) + 0.18f * std::sin (2.0f * b.phase)) * b.amp * b.attack;
            b.amp *= b.ampDecay;
            outL[i] += s;
            outR[i] += s;
        }
        if (b.amp < 1.0e-5f)
            b.active = false;
    }

    for (auto& v : noises)
    {
        if (! v.active)
            continue;
        for (int i = 0; i < n; ++i)
        {
            if (v.startDelay > 0) { --v.startDelay; continue; }
            const float s = v.band.process (v.rng.bi()).bp * v.amp;
            v.amp *= v.decay;
            outL[i] += s * v.panL;
            outR[i] += s * v.panR;
        }
        if (v.amp < 1.0e-5f)
            v.active = false;
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
