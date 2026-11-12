#include "DynamicFDN.h"

namespace rb::dsp
{
namespace
{
    // Line lengths (ms at Size 1): spread over two octaves and chosen so that no pair sits near a
    // simple ratio (2:1, 3:2 …), which would stack their modes on the same frequencies.
    // Total 784 ms → 0.78 modes per Hz, twice the density of the old 8-line set.
    constexpr float kBaseMs[DynamicFDN::N] = { 20.6f, 23.3f, 26.5f, 27.7f, 32.1f, 34.1f, 37.7f, 40.6f,
                                               45.0f, 49.5f, 58.3f, 63.7f, 71.3f, 78.1f, 84.1f, 91.5f };

    // The two boundaries each line's energy bounces between: all 15 wall pairs, plus the
    // floor-ceiling pair again (the room's shortest axis). Every wall feeds 5 or 6 lines.
    constexpr int kLineWalls[DynamicFDN::N][2] =
    {
        { North, South },   { East, West },    { Ceiling, Floor }, { North, East },
        { South, West },    { Ceiling, North }, { Floor, East },   { West, Ceiling },
        { North, West },    { South, East },   { Ceiling, South }, { Floor, West },
        { Ceiling, East },  { Floor, North },  { Floor, South },   { Ceiling, Floor }
    };

    // Bent sequences (flat Walsh spectrum), mutually orthogonal: the Hadamard matrix maps each
    // to another ±1 pattern, so injected sound is never focused into a few lines, and the left
    // and right pickups stay decorrelated.
    constexpr float kInL[DynamicFDN::N]  = { +1, +1, +1, +1, +1, -1, +1, -1, +1, +1, -1, -1, +1, -1, -1, +1 };
    constexpr float kInR[DynamicFDN::N]  = { +1, -1, +1, +1, -1, +1, -1, -1, +1, -1, +1, +1, +1, -1, +1, +1 };
    constexpr float kOutL[DynamicFDN::N] = { +1, +1, -1, -1, -1, +1, +1, -1, +1, +1, +1, +1, -1, +1, -1, +1 };
    constexpr float kOutR[DynamicFDN::N] = { +1, +1, -1, +1, +1, +1, -1, +1, -1, -1, +1, -1, +1, +1, -1, +1 };

    constexpr float kInputGain  = 0.245f;   // with 16 lines these give the same tail level as the 8-line tank
    constexpr float kOutputGain = 0.30f;
    constexpr float kMaxSize    = 2.0f;

    constexpr float kExteriorIn[ExteriorTank::N] = { 0.5f, -0.5f, 0.5f, 0.5f };
    constexpr float kSmearMs[ExteriorTank::N]    = { 23.7f, 31.1f, 41.3f, 53.9f };
    constexpr float kSmearGain = 0.55f;
    constexpr float kIdleLevel = 1.0e-7f;   // -140 dB: below this the exterior is treated as silent

    // Input diffusers (ms): mutually prime-ish, L/R offset so the wash is decorrelated.
    constexpr float kDiffMsL[4] = { 1.13f, 1.71f, 2.89f, 4.37f };
    constexpr float kDiffMsR[4] = { 1.27f, 1.93f, 3.17f, 4.71f };
    constexpr float kMotionMs   = 1.2f;     // full-scale line-length drift
    constexpr float kShockStretch = 0.016f; // full-scale fracture stretch
    constexpr float kGritBias = 0.25f;

    /** In-place fast Walsh-Hadamard transform, unnormalised (scale by 1/4 for an orthogonal H16). */
    inline void hadamard16 (float* a) noexcept
    {
        for (int h = 1; h < 16; h <<= 1)
            for (int i = 0; i < 16; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float x = a[j], y = a[j + h];
                    a[j] = x + y;
                    a[j + h] = x - y;
                }
    }

    /** Sum of 16 values as a fixed tree, so it vectorises without relaxing floating-point rules. */
    inline float sum16 (const float* a) noexcept
    {
        float t[4];
        for (int k = 0; k < 4; ++k)
            t[k] = (a[k] + a[k + 4]) + (a[k + 8] + a[k + 12]);
        return (t[0] + t[1]) + (t[2] + t[3]);
    }
}

//==============================================================================
void ExteriorTank::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    for (auto& l : lines)
        l.prepare ((int) std::ceil (kMaxVoidDelayMs * 1.0e-3f * fs) + 8);
    for (int k = 0; k < N; ++k)
        smear[(size_t) k].prepare ((int) std::lround (kSmearMs[k] * 1.0e-3f * fs));
    initialised = false;
    reset();
}

void ExteriorTank::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& a : smear) a.reset();
    for (auto& d : damping) d.reset();
    hpL.reset(); hpR.reset();
    inputGain = inputGainInc = returnGainInc = diffusionInc = 0.0f;
    lfoPhase = { 0.0f, 1.57f, 3.14f, 4.71f };
    level = 0.0f;
    quietTicks = 0;
    idle = true;
    initialised = false;
}

void ExteriorTank::setProfile (const VoidProfile& v, int n) noexcept
{
    const float invN = 1.0f / (float) std::max (n, 1);
    const float dt = (float) n / fs;
    const float maxD = lines[0].maxDelay() - 2.0f;
    const float glide = 1.0f - std::exp (-dt / 0.15f);   // geometry glides ~150 ms when the void changes

    // Nothing coming in and nothing left circulating: stop running the tank until a breach feeds it.
    if (level < kIdleLevel) { if (++quietTicks > 8) idle = true; }
    else                    quietTicks = 0;
    level = 0.0f;

    if (! initialised) { diffusion = v.diffusion; returnGain = v.returnGain; }
    diffusionInc  = (v.diffusion - diffusion) * invN;
    returnGainInc = (v.returnGain - returnGain) * invN;
    inputGainInc  = ((v.collects ? 1.0f : 0.0f) - inputGain) * invN;
    drive = v.drive;

    for (int k = 0; k < N; ++k)
    {
        const size_t i = (size_t) k;
        const float target = v.delayMs[k] * 1.0e-3f * fs;
        baseD[i] = initialised ? baseD[i] + (target - baseD[i]) * glide : target;

        lfoPhase[i] += kTwoPi * v.modRateHz * dt * (1.0f + 0.13f * (float) k);
        if (lfoPhase[i] > kTwoPi) lfoPhase[i] -= kTwoPi;

        if (idle && initialised)
        {
            D[i] = std::clamp (baseD[i], 4.0f, maxD);   // follow the geometry silently
            DInc[i] = 0.0f;
            continue;
        }

        const float modulated = std::clamp (baseD[i] + v.modDepthMs * 1.0e-3f * fs * std::sin (lfoPhase[i]), 4.0f, maxD);
        if (! initialised) D[i] = modulated;
        DInc[i] = (modulated - D[i]) * invN;

        // RT60 over the whole loop: the line plus the allpass inside it (its mean delay is its length).
        const float loop = modulated + kSmearMs[k] * 1.0e-3f * fs;
        gains[i] = std::pow (10.0f, -3.0f * loop / (v.rt60 * fs));
        damping[i].set (v.dampHz, fs);
    }

    hpL.set (v.hpHz, fs);
    hpR.set (v.hpHz, fs);
    initialised = true;
}

void ExteriorTank::processSample (float in, float& outL, float& outR) noexcept
{
    inputGain  = clamp01 (inputGain + inputGainInc);
    diffusion  = clamp01 (diffusion + diffusionInc);
    returnGain += returnGainInc;

    const float x = in * inputGain;
    if (idle)
    {
        if (x == 0.0f) { outL = outR = 0.0f; return; }
        idle = false;
        quietTicks = 0;
    }

    std::array<float, N> s {};
    for (int k = 0; k < N; ++k)
    {
        D[(size_t) k] += DInc[(size_t) k];
        s[(size_t) k] = lines[(size_t) k].tap (D[(size_t) k]);
    }

    float l = 0.5f * (s[0] - s[1] + s[2] - s[3]);
    float r = 0.5f * (s[0] + s[1] - s[2] - s[3]);

    // Grit lives outside the loop, so it can never destabilise the tank.
    // Asymmetric tanh: unity slope for quiet tails, crunchy even harmonics when loud.
    if (drive > 0.0f)
    {
        const float offset = std::tanh (kGritBias);
        l = (std::tanh (drive * l + kGritBias) - offset) / drive;
        r = (std::tanh (drive * r + kGritBias) - offset) / drive;
    }
    outL = hpL.hp (l) * returnGain;
    outR = hpR.hp (r) * returnGain;

    // Loop: damping and decay, then one allpass per line. The allpass is lossless, so stability is
    // untouched, but every pass is smeared: a smooth void stays smooth instead of repeating
    // what was vented into it. Its gain follows `diffusion`, so echo voids keep their echoes.
    const float smearGain = kSmearGain * diffusion;
    float sum = 0.0f, peak = std::abs (x);
    for (int k = 0; k < N; ++k)
    {
        const size_t i = (size_t) k;
        smear[i].setGain (smearGain);
        s[i] = smear[i].process (damping[i].lp (s[i]) * gains[i]);
        sum += s[i];
        peak = std::max (peak, std::abs (s[i]));
    }
    level = std::max (level, peak);

    // Householder with diffusion: eigenvalues 1 and 1 − 2·diffusion, so |λ| ≤ 1 always.
    const float mix = diffusion * 0.5f * sum;
    for (int k = 0; k < N; ++k)
        lines[(size_t) k].push (s[(size_t) k] - mix + x * kExteriorIn[k]);
}

//==============================================================================
void DynamicFDN::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    // Longest line, largest room, fully breached (+12 %), plus creak headroom.
    const int maxSamples = (int) std::ceil (kBaseMs[N - 1] * 1.0e-3f * fs * kMaxSize * 1.15f + 0.01f * fs);
    for (auto& d : delay)
        d.prepare (maxSamples);
    maxDelay = delay[0].maxDelay() - 2.0f;

    for (int k = 0; k < kDiffusers; ++k)
    {
        diffL[(size_t) k].prepare ((int) std::lround (kDiffMsL[k] * 1.0e-3f * fs));
        diffR[(size_t) k].prepare ((int) std::lround (kDiffMsR[k] * 1.0e-3f * fs));
    }

    exterior.prepare (sampleRate);
    reset();
}

void DynamicFDN::reset()
{
    bank = {};
    for (int k = 0; k < N; ++k)
    {
        delay[(size_t) k].reset();
        bank.D[k] = kBaseMs[k] * 1.0e-3f * fs;
        bank.hfGain[k] = 1.0f;
        bank.slabNorm[k] = 1.0f;
        bank.ventK[k] = 1.41421356f;
        bank.ventKeep[k] = 1.0f;

        auto& mo = motion[(size_t) k];
        mo = {};
        mo.baseD = bank.D[k];
        mo.lfoC = std::cos (0.73f * (float) k);   // spread the start phases
        mo.lfoS = std::sin (0.73f * (float) k);
    }
    for (auto& d : diffL) d.reset();
    for (auto& d : diffR) d.reset();
    freeze = freezeInc = 0.0f;
    bleed = bleedInc = targetBleed = 0.0f;
    controlInit = false;
    lfoBlock = 0;
    filterMaterial = nullptr;   // forces the first updateControl to compute every coefficient
    filtersSettled = false;
    slabOn = ventOn = false;
    exterior.reset();
}

void DynamicFDN::updateFilters (const WallArray& I, const MaterialProfile& m, float decaySeconds, float sizeScale) noexcept
{
    // Runs on every tick while the walls are moving, for 16 lines, so it avoids pow(): every
    // power here is an exp() of something worked out once, and the pre-warps use fastTan.
    const float slabSizeScale = 1.0f / std::sqrt (std::max (sizeScale, 0.1f)); // bigger slabs ring lower
    const float invFs = 1.0f / fs;
    const float decayRate = -6.9077553f / (std::max (decaySeconds, 0.05f) * fs);        // ln(10^-3) per sample of loop
    const float shelfRate = std::log (std::max (m.hfShelfGain, 1.0e-4f)) / (0.05f * fs); // ln(shelf gain) per sample
    const float kq = 1.0f / std::max (m.slabQ, 0.05f);
    float meanBreach = 0.0f, meanLoose = 0.0f;

    auto svf = [invFs, this] (float hz, float q, float& a1, float& a2, float& a3) noexcept
    {
        const float g = fastTan (kPi * std::clamp (hz, 5.0f, 0.49f * fs) * invFs);
        a1 = 1.0f / (1.0f + g * (g + q));
        a2 = g * a1;
        a3 = g * a2;
    };

    for (int k = 0; k < N; ++k)
    {
        auto& mo = motion[(size_t) k];
        const int a = kLineWalls[k][0], b = kLineWalls[k][1];

        const float la = loosenessAmount (I[(size_t) a]), lb = loosenessAmount (I[(size_t) b]);
        const float loose  = std::max (la, lb);
        const int slabWall = la >= lb ? a : b;
        const float crack  = std::max (crackAmount (I[(size_t) a]), crackAmount (I[(size_t) b]));
        const float breach = 0.5f * (breachAmount (I[(size_t) a]) + breachAmount (I[(size_t) b]));
        meanBreach += breach / (float) N;
        meanLoose  += loose  / (float) N;

        // --- nominal length: the room "opens up" as walls breach.
        mo.baseD = kBaseMs[k] * 1.0e-3f * fs * sizeScale * (1.0f + 0.12f * breach);
        mo.loose = loose;

        // --- broadband loop gain: RT60 (g = 10^(-3·D / (T60·fs))), stretched where slabs resonate,
        //     cut by venting. Uses the unmodulated length so motion and shock don't pump the decay.
        mo.targetGain = std::exp (decayRate * mo.baseD * (1.0f - 0.6f * loose)) * (1.0f - 0.65f * breach);

        // --- stage 1: high-shelf damping, proportional to the line's length so every line decays
        //     at the same rate per second (fades out as the other mechanisms take over).
        const float shelfAmount = 1.0f - std::max (loose, breach);
        const float hf = std::max (0.02f, std::exp (shelfRate * mo.baseD) * (1.0f - 0.25f * crack));
        mo.targetHf = lerp (1.0f, hf, shelfAmount);
        const float gs = fastTan (kPi * std::clamp (m.hfDampingHz * (1.0f - 0.35f * crack), 1.0f, 0.49f * fs) * invFs);
        mo.targetShelfG = gs / (1.0f + gs);

        // --- stage 2: loose-slab resonance, normalised so the bell's peak is unity (G = +20 dB · loose).
        const float G = loose > 0.0f ? std::exp (2.3025851f * loose) : 1.0f;
        svf (m.slabHz[(size_t) slabWall] * (1.0f + 0.017f * (float) k) * slabSizeScale, kq,
             bank.slabA1[k], bank.slabA2[k], bank.slabA3[k]);
        bank.slabGm1k[k] = (G - 1.0f) * kq;
        bank.slabNorm[k] = 1.0f / G;

        // --- stage 3: high-pass venting (12 Hz when a breach begins → ~900 Hz when the wall is gone).
        bank.ventK[k] = 1.41421356f;
        svf (12.0f + (breach > 0.0f ? 900.0f * std::exp (1.3f * std::log (breach)) : 0.0f), bank.ventK[k],
             bank.ventA1[k], bank.ventA2[k], bank.ventA3[k]);
        bank.ventAmount[k] = breach;
        bank.ventKeep[k] = 1.0f - 0.65f * breach;
    }
    targetBleed = std::clamp (0.9f * meanBreach + 0.3f * meanLoose, 0.0f, 0.95f);

    // With no wall loose the bell is exactly unity, and with none breached the vent only removes
    // sub-audio: both stages are skipped then. Their state is cleared as they switch off, so
    // they start clean the next time a wall gives way.
    const bool wantSlab = meanLoose > 0.0f, wantVent = meanBreach > 0.0f;
    if (slabOn && ! wantSlab)
        for (int k = 0; k < N; ++k) bank.slabS1[k] = bank.slabS2[k] = 0.0f;
    if (ventOn && ! wantVent)
        for (int k = 0; k < N; ++k) bank.ventS1[k] = bank.ventS2[k] = 0.0f;
    slabOn = wantSlab;
    ventOn = wantVent;
}

void DynamicFDN::updateControl (const WallArray& I, const MaterialProfile& m,
                                const TankControl& tc, const VoidProfile& exteriorVoid, int n) noexcept
{
    const float invN = 1.0f / (float) std::max (n, 1);
    const float dt = (float) n / fs;

    // Everything that depends only on the tick length is worked out once.
    if (n != lfoBlock)
    {
        lfoBlock = n;
        for (int k = 0; k < N; ++k)
        {
            const float rate = 0.23f + 0.039f * (float) ((k * 5) % N);   // 0.23 .. 0.82 Hz, no two alike
            motion[(size_t) k].rotC = std::cos (kTwoPi * rate * dt);
            motion[(size_t) k].rotS = std::sin (kTwoPi * rate * dt);
        }
        densityA = std::exp (-dt / 0.08f);
        motionA  = std::exp (-dt / 0.35f);
        wanderA  = 1.0f - std::exp (-dt / 0.4f);
    }

    // Density and Motion glide, so turning either knob fast never clicks or warbles.
    if (! controlInit)
    {
        densitySm = clamp01 (tc.density);
        motionSm = clamp01 (tc.motion);
        apGain = 0.75f * densitySm;
        controlInit = true;
    }
    densitySm = clamp01 (tc.density) + densityA * (densitySm - clamp01 (tc.density));
    motionSm  = clamp01 (tc.motion)  + motionA  * (motionSm  - clamp01 (tc.motion));
    const float motionDepth = motionSm * kMotionMs * 1.0e-3f * fs;
    const float stretch = 1.0f + kShockStretch * std::clamp (tc.shock, 0.0f, 1.2f);

    apGainInc = (0.75f * densitySm - apGain) * invN;
    freezeInc = (clamp01 (tc.freeze) - freeze) * invN;

    // Loop gains and filter coefficients cost a dozen pow/tan per line. They depend only on the
    // walls, the material, Decay and Size, so they are recomputed only when one of those moves.
    bool dirty = filterMaterial != &m || std::abs (tc.decaySeconds - filterDecay) > 1.0e-5f
                 || std::abs (tc.sizeScale - filterSize) > 1.0e-6f;
    // A wall has to move by 0.02 % before its lines are recomputed: slow healing then costs a
    // refresh every few ticks instead of every tick, and the step is far too small to hear.
    for (int w = 0; w < kNumWalls && ! dirty; ++w)
        dirty = std::abs (I[(size_t) w] - filterIntegrity[(size_t) w]) > 2.0e-4f;

    if (dirty)
    {
        filterMaterial = &m;
        filterDecay = tc.decaySeconds;
        filterSize = tc.sizeScale;
        filterIntegrity = I;
        updateFilters (I, m, tc.decaySeconds, tc.sizeScale);
        const bool first = bank.shelfG[0] <= 0.0f;   // nothing to ramp from yet
        for (int k = 0; k < N; ++k)
        {
            const auto& mo = motion[(size_t) k];
            if (first) { bank.shelfG[k] = mo.targetShelfG; bank.hfGain[k] = mo.targetHf; }
            bank.gainInc[k]   = (mo.targetGain - bank.gain[k]) * invN;
            bank.shelfGInc[k] = (mo.targetShelfG - bank.shelfG[k]) * invN;   // a stepped corner ticks: ramp it
            bank.hfGainInc[k] = (mo.targetHf - bank.hfGain[k]) * invN;
        }
        bleedInc = (targetBleed - bleed) * invN;
        filtersSettled = false;
    }
    else if (! filtersSettled)
    {
        // last tick's ramps have landed: pin the values and stop ramping
        for (int k = 0; k < N; ++k)
        {
            const auto& mo = motion[(size_t) k];
            bank.gain[k] = mo.targetGain;
            bank.shelfG[k] = mo.targetShelfG;
            bank.hfGain[k] = mo.targetHf;
            bank.gainInc[k] = bank.shelfGInc[k] = bank.hfGainInc[k] = 0.0f;
        }
        bleed = targetBleed;
        bleedInc = 0.0f;
        filtersSettled = true;
    }

    // Line lengths move every tick: motion (slow sine + random walk, a different rate per line),
    // the fracture shock, and the creak of loose slabs.
    for (int k = 0; k < N; ++k)
    {
        auto& mo = motion[(size_t) k];
        mo.creakNoise += (rng.bi() - mo.creakNoise) * 0.02f;
        mo.wander += (rng.bi() - mo.wander) * wanderA;

        const float c = mo.lfoC * mo.rotC - mo.lfoS * mo.rotS, s = mo.lfoS * mo.rotC + mo.lfoC * mo.rotS;
        const float keep = 1.5f - 0.5f * (c * c + s * s);   // hold the phasor on the unit circle
        mo.lfoC = c * keep;
        mo.lfoS = s * keep;

        const float drift = motionDepth * (0.7f * mo.lfoS + 0.6f * mo.wander);
        const float targetD = std::clamp (mo.baseD * stretch + drift + mo.loose * 0.0018f * fs * mo.creakNoise, 4.0f, maxDelay);
        bank.DInc[k] = (targetD - bank.D[k]) * invN;
    }

    exterior.setProfile (exteriorVoid, n);
}

void DynamicFDN::process (const float* inL, const float* inR,
                          float* outL, float* outR, float* extL, float* extR, int n) noexcept
{
    auto& b = bank;

    for (int i = 0; i < n; ++i)
    {
        alignas (64) float p0[N], p1[N], p2[N], p3[N], fr[N];
        alignas (64) float s[N], y[N], v[N], tL[N], tR[N];

        // Read every line (the only part that cannot be done in parallel).
        for (int k = 0; k < N; ++k)
            delay[(size_t) k].fetch (b.D[k], p0[k], p1[k], p2[k], p3[k], fr[k]);

        freeze += freezeInc;
        const float live = 1.0f - freeze, hold = freeze * 0.99998f;

        // All 16 loop filters, stage by stage. Plain element-wise arithmetic on arrays: SIMD.
        alignas (64) float x[N], hp[N];

        // Interpolate, then stage 1 — high shelf.
        for (int k = 0; k < N; ++k)
        {
            const float sk = catmullRom (p0[k], p1[k], p2[k], p3[k], fr[k]);
            b.D[k] += b.DInc[k];
            b.gain[k] += b.gainInc[k];
            b.shelfG[k] += b.shelfGInc[k];
            b.hfGain[k] += b.hfGainInc[k];

            const float sv = (sk - b.shelfS[k]) * b.shelfG[k];
            const float lp = sv + b.shelfS[k];
            b.shelfS[k] = lp + sv;
            x[k] = lp + b.hfGain[k] * (sk - lp);
            s[k] = sk;
        }

        // Stage 2 — normalised slab bell: (1 + (G-1)·BP̂) / G, BP̂ = k·bp has unity peak.
        if (slabOn)
            for (int k = 0; k < N; ++k)
            {
                const float v3 = x[k] - b.slabS2[k];
                const float v1 = b.slabA1[k] * b.slabS1[k] + b.slabA2[k] * v3;
                const float v2 = b.slabS2[k] + b.slabA2[k] * b.slabS1[k] + b.slabA3[k] * v3;
                b.slabS1[k] = 2.0f * v1 - b.slabS1[k];
                b.slabS2[k] = 2.0f * v2 - b.slabS2[k];
                x[k] = (x[k] + b.slabGm1k[k] * v1) * b.slabNorm[k];
            }

        // Stage 3 — venting, and what left the room through the breach: the LF the high-pass
        // removed plus the broadband vent loss.
        float vented = 0.0f;
        if (ventOn)
        {
            for (int k = 0; k < N; ++k)
            {
                const float v3 = x[k] - b.ventS2[k];
                const float v1 = b.ventA1[k] * b.ventS1[k] + b.ventA2[k] * v3;
                const float v2 = b.ventS2[k] + b.ventA2[k] * b.ventS1[k] + b.ventA3[k] * v3;
                b.ventS1[k] = 2.0f * v1 - b.ventS1[k];
                b.ventS2[k] = 2.0f * v2 - b.ventS2[k];
                hp[k] = x[k] - b.ventK[k] * v1 - v2;
                v[k] = b.ventAmount[k] * (x[k] - b.ventKeep[k] * hp[k]);
            }
            vented = live * sum16 (v);
        }
        else
            for (int k = 0; k < N; ++k)
                hp[k] = x[k];

        // Loop gain. Freeze crossfades to the raw, lossless line (convex mix keeps |H| ≤ 1).
        for (int k = 0; k < N; ++k)
        {
            y[k] = live * hp[k] * b.gain[k] + hold * s[k];
            tL[k] = kOutL[k] * s[k];
            tR[k] = kOutR[k] * s[k];
        }

        outL[i] = sum16 (tL) * kOutputGain;
        outR[i] = sum16 (tR) * kOutputGain;

        // Input diffusion, then injection (muted while frozen).
        apGain += apGainInc;
        float dl = inL[i], dr = inR[i];
        for (int k = 0; k < kDiffusers; ++k)
        {
            const float gk = k < 2 ? apGain : 0.85f * apGain;
            diffL[(size_t) k].setGain (gk);
            diffR[(size_t) k].setGain (gk);
            dl = diffL[(size_t) k].process (dl);
            dr = diffR[(size_t) k].process (dr);
        }
        const float xl = dl * kInputGain * live, xr = dr * kInputGain * live;

        // Feedback: (1 − bleed)·H·y + bleed·y, plus the new sound.
        bleed += bleedInc;
        const float mixH = (1.0f - bleed) * 0.25f, keep = bleed;
        for (int k = 0; k < N; ++k)
            s[k] = y[k];
        hadamard16 (s);
        for (int k = 0; k < N; ++k)
            y[k] = mixH * s[k] + keep * y[k] + kInL[k] * xl + kInR[k] * xr;
        for (int k = 0; k < N; ++k)
            delay[(size_t) k].push (y[k]);

        exterior.processSample (vented * 0.5f, extL[i], extR[i]);
    }
}
} // namespace rb::dsp
