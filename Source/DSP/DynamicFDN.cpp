#include "DynamicFDN.h"

namespace rb::dsp
{
namespace
{
    constexpr float kBaseMs[DynamicFDN::N] = { 31.7f, 37.3f, 41.9f, 47.3f, 53.9f, 61.1f, 68.7f, 79.3f };

    // The two boundaries each line's energy bounces between. Every wall feeds 2–3 lines.
    constexpr int kLineWalls[DynamicFDN::N][2] =
    {
        { North, South }, { East, West }, { Ceiling, Floor }, { North, East },
        { South, West },  { Ceiling, North }, { Floor, East }, { West, Ceiling }
    };

    // Orthogonal ±1 patterns for input injection and output pickup (decorrelated L/R).
    constexpr float kInL[DynamicFDN::N]  = { +1, +1, -1, -1, +1, -1, +1, -1 };
    constexpr float kInR[DynamicFDN::N]  = { +1, -1, +1, +1, -1, -1, +1, -1 };
    constexpr float kOutL[DynamicFDN::N] = { +1, -1, +1, -1, +1, -1, +1, -1 };
    constexpr float kOutR[DynamicFDN::N] = { +1, +1, -1, -1, +1, +1, -1, -1 };

    constexpr float kInputGain  = 0.35f;
    constexpr float kOutputGain = 0.30f;
    constexpr float kMaxSize    = 2.0f;

    constexpr float kExteriorIn[ExteriorTank::N] = { 0.5f, -0.5f, 0.5f, 0.5f };

    // Input diffusers (ms): mutually prime-ish, L/R offset so the wash is decorrelated.
    constexpr float kDiffMsL[4] = { 1.13f, 1.71f, 2.89f, 4.37f };
    constexpr float kDiffMsR[4] = { 1.27f, 1.93f, 3.17f, 4.71f };
    constexpr float kMotionMs   = 1.2f;     // full-scale line-length drift
    constexpr float kShockStretch = 0.016f; // full-scale fracture stretch
    constexpr float kGritBias = 0.25f;
}

//==============================================================================
void ExteriorTank::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    for (auto& l : lines)
        l.prepare ((int) std::ceil (kMaxVoidDelayMs * 1.0e-3f * fs) + 8);
    initialised = false;
    reset();
}

void ExteriorTank::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& d : damping) d.reset();
    hpL.reset(); hpR.reset();
    inputGain = inputGainInc = returnGainInc = diffusionInc = 0.0f;
    lfoPhase = { 0.0f, 1.57f, 3.14f, 4.71f };
    initialised = false;
}

void ExteriorTank::setProfile (const VoidProfile& v, int n) noexcept
{
    const float invN = 1.0f / (float) std::max (n, 1);
    const float dt = (float) n / fs;
    const float maxD = lines[0].maxDelay() - 2.0f;
    const float glide = 1.0f - std::exp (-dt / 0.15f);   // geometry glides ~150 ms when the void changes

    for (int k = 0; k < N; ++k)
    {
        const float target = v.delayMs[k] * 1.0e-3f * fs;
        baseD[(size_t) k] = initialised ? baseD[(size_t) k] + (target - baseD[(size_t) k]) * glide : target;

        lfoPhase[(size_t) k] += kTwoPi * v.modRateHz * dt * (1.0f + 0.13f * (float) k);
        if (lfoPhase[(size_t) k] > kTwoPi) lfoPhase[(size_t) k] -= kTwoPi;

        const float modulated = std::clamp (baseD[(size_t) k] + v.modDepthMs * 1.0e-3f * fs * std::sin (lfoPhase[(size_t) k]), 4.0f, maxD);
        if (! initialised) D[(size_t) k] = modulated;
        DInc[(size_t) k] = (modulated - D[(size_t) k]) * invN;

        gains[(size_t) k] = std::pow (10.0f, -3.0f * modulated / (v.rt60 * fs));
        damping[(size_t) k].set (v.dampHz, fs);
    }

    if (! initialised) { diffusion = v.diffusion; returnGain = v.returnGain; }
    diffusionInc  = (v.diffusion - diffusion) * invN;
    returnGainInc = (v.returnGain - returnGain) * invN;
    inputGainInc  = ((v.collects ? 1.0f : 0.0f) - inputGain) * invN;
    drive = v.drive;
    hpL.set (v.hpHz, fs);
    hpR.set (v.hpHz, fs);
    initialised = true;
}

void ExteriorTank::processSample (float in, float& outL, float& outR) noexcept
{
    inputGain  = clamp01 (inputGain + inputGainInc);
    diffusion  = clamp01 (diffusion + diffusionInc);
    returnGain += returnGainInc;

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

    float sum = 0.0f;
    for (int k = 0; k < N; ++k)
    {
        s[(size_t) k] = damping[(size_t) k].lp (s[(size_t) k]) * gains[(size_t) k];
        sum += s[(size_t) k];
    }

    // Householder with diffusion: eigenvalues 1 and 1 − 2·diffusion, so |λ| ≤ 1 always.
    const float mix = diffusion * 0.5f * sum;
    const float x = in * inputGain;
    for (int k = 0; k < N; ++k)
        lines[(size_t) k].push (s[(size_t) k] - mix + x * kExteriorIn[k]);
}

//==============================================================================
void DynamicFDN::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    // Longest line, largest room, fully breached (+12 %), plus creak headroom.
    const int maxSamples = (int) std::ceil (kBaseMs[N - 1] * 1.0e-3f * fs * kMaxSize * 1.15f + 0.01f * fs);
    for (auto& l : lines)
        l.delay.prepare (maxSamples);
    maxDelay = lines[0].delay.maxDelay() - 2.0f;

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
    for (int k = 0; k < N; ++k)
    {
        auto& L = lines[(size_t) k];
        L.delay.reset();
        L.D = kBaseMs[k] * 1.0e-3f * fs;
        L.DInc = L.gain = L.gainInc = 0.0f;
        L.shelf.reset(); L.slab.reset(); L.vent.reset();
        L.hfGain = 1.0f; L.slabGm1 = 0.0f; L.slabNorm = 1.0f; L.ventAmount = 0.0f;
        L.creakNoise = 0.0f;
        L.lfoPhase = 0.73f * (float) k;                       // spread the start phases
        L.lfoRate  = 0.23f + 0.083f * (float) ((k * 5) % 8);  // 0.23 .. 0.81 Hz, no two alike
        L.wander   = 0.0f;
    }
    for (auto& d : diffL) d.reset();
    for (auto& d : diffR) d.reset();
    freeze = freezeInc = 0.0f;
    bleed = bleedInc = 0.0f;
    controlInit = false;
    exterior.reset();
}

void DynamicFDN::updateControl (const WallArray& I, const MaterialProfile& m,
                                const TankControl& tc, const VoidProfile& exteriorVoid, int n) noexcept
{
    const float decaySeconds = tc.decaySeconds;
    const float sizeScale = tc.sizeScale;
    const float invN = 1.0f / (float) std::max (n, 1);
    const float dt = (float) n / fs;
    // Density and Motion glide, so turning either knob fast never clicks or warbles.
    if (! controlInit)
    {
        densitySm = clamp01 (tc.density); motionSm = clamp01 (tc.motion);
        apGain = 0.75f * densitySm;
        controlInit = true;
    }
    densitySm = clamp01 (tc.density) + std::exp (-dt / 0.08f) * (densitySm - clamp01 (tc.density));
    motionSm  = clamp01 (tc.motion)  + std::exp (-dt / 0.35f) * (motionSm  - clamp01 (tc.motion));
    const float motionDepth = motionSm * kMotionMs * 1.0e-3f * fs;
    const float stretch = 1.0f + kShockStretch * std::clamp (tc.shock, 0.0f, 1.2f);

    apGainInc = (0.75f * densitySm - apGain) * invN;
    freezeInc = (clamp01 (tc.freeze) - freeze) * invN;
    const float slabSizeScale = 1.0f / std::sqrt (std::max (sizeScale, 0.1f)); // bigger slabs ring lower
    float meanBreach = 0.0f, meanLoose = 0.0f;

    for (int k = 0; k < N; ++k)
    {
        auto& L = lines[(size_t) k];
        const int a = kLineWalls[k][0], b = kLineWalls[k][1];

        const float la = loosenessAmount (I[a]), lb = loosenessAmount (I[b]);
        const float loose  = std::max (la, lb);
        const int slabWall = la >= lb ? a : b;
        const float crack  = std::max (crackAmount (I[a]), crackAmount (I[b]));
        const float breach = 0.5f * (breachAmount (I[a]) + breachAmount (I[b]));
        meanBreach += breach / (float) N;
        meanLoose  += loose  / (float) N;

        // --- delay length: room "opens up" as walls breach; loose slabs creak (slow wobble).
        L.creakNoise += (rng.bi() - L.creakNoise) * 0.02f;

        // --- motion: slow sine + smoothed random walk, a different rate per line.
        L.lfoPhase += kTwoPi * L.lfoRate * dt;
        if (L.lfoPhase > kTwoPi) L.lfoPhase -= kTwoPi;
        L.wander += (rng.bi() - L.wander) * (1.0f - std::exp (-dt / 0.4f));
        const float drift = motionDepth * (0.7f * std::sin (L.lfoPhase) + 0.6f * L.wander);

        const float baseD = kBaseMs[k] * 1.0e-3f * fs * sizeScale * (1.0f + 0.12f * breach);
        const float targetD = std::clamp (baseD * stretch + drift + loose * 0.0018f * fs * L.creakNoise,
                                          4.0f, maxDelay);
        L.DInc = (targetD - L.D) * invN;

        // --- broadband loop gain: RT60, stretched where slabs resonate, cut by venting.
        //     Uses the unmodulated length so motion and shock don't pump the decay.
        const float gRt = std::pow (10.0f, -3.0f * baseD / (std::max (decaySeconds, 0.05f) * fs));
        const float targetGain = std::pow (gRt, 1.0f - 0.6f * loose) * (1.0f - 0.65f * breach);
        L.gainInc = (targetGain - L.gain) * invN;

        // --- stage 1: high-shelf damping (fades out as the other mechanisms take over).
        const float shelfAmount = 1.0f - std::max (loose, breach);
        const float hf = std::max (0.02f, std::pow (m.hfShelfGain, targetD / (0.05f * fs)) * (1.0f - 0.25f * crack));
        L.hfGain = lerp (1.0f, hf, shelfAmount);
        L.shelf.set (m.hfDampingHz * (1.0f - 0.35f * crack), fs);

        // --- stage 2: loose-slab resonance, normalised so the bell's peak is unity.
        const float G = dbToGain (20.0f * loose);
        L.slab.set (m.slabHz[(size_t) slabWall] * (1.0f + 0.037f * (float) k) * slabSizeScale, m.slabQ, fs);
        L.slabGm1  = G - 1.0f;
        L.slabNorm = 1.0f / G;

        // --- stage 3: high-pass venting (12 Hz DC guard when intact → ~900 Hz when void).
        L.vent.set (12.0f + 900.0f * std::pow (breach, 1.3f), 0.7071f, fs);
        L.ventAmount = breach;
    }

    const float targetBleed = std::clamp (0.9f * meanBreach + 0.3f * meanLoose, 0.0f, 0.95f);
    bleedInc = (targetBleed - bleed) * invN;
    exterior.setProfile (exteriorVoid, n);
}

void DynamicFDN::process (const float* inL, const float* inR,
                          float* outL, float* outR, float* extL, float* extR, int n) noexcept
{
    constexpr float householder = 2.0f / (float) N;
    std::array<float, N> f {};

    for (int i = 0; i < n; ++i)
    {
        float yL = 0.0f, yR = 0.0f, sum = 0.0f, vented = 0.0f;
        freeze += freezeInc;
        const float live = 1.0f - freeze;

        for (int k = 0; k < N; ++k)
        {
            auto& L = lines[(size_t) k];
            const float s = L.delay.tap (L.D);
            L.D += L.DInc;
            L.gain += L.gainInc;

            yL += kOutL[k] * s;
            yR += kOutR[k] * s;

            // Stage 1 — high shelf
            const float lp = L.shelf.lp (s);
            float x = lp + L.hfGain * (s - lp);

            // Stage 2 — normalised slab bell: (1 + (G-1)·BP̂) / G, BP̂ = k·bp has unity peak
            const float bp = L.slab.process (x).bp;
            x = (x + L.slabGm1 * L.slab.k * bp) * L.slabNorm;

            // Stage 3 — venting
            const float hp = L.vent.process (x).hp;
            // Freeze: crossfade to the raw, lossless line (convex mix keeps |H| ≤ 1).
            const float y  = live * hp * L.gain + freeze * 0.99998f * s;

            // What left the room through the breach: the LF the HPF removed + the broadband vent loss.
            vented += live * L.ventAmount * (x - (1.0f - 0.65f * L.ventAmount) * hp);

            f[(size_t) k] = y;
            sum += y;
        }

        bleed += bleedInc;
        const float mix = (1.0f - bleed) * householder * sum;
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

        for (int k = 0; k < N; ++k)
            lines[(size_t) k].delay.push (f[(size_t) k] - mix + kInL[k] * xl + kInR[k] * xr);

        outL[i] = yL * kOutputGain;
        outR[i] = yR * kOutputGain;
        exterior.processSample (vented * 0.5f, extL[i], extR[i]);
    }
}
} // namespace rb::dsp
