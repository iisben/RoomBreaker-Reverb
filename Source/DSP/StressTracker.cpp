#include "StressTracker.h"

namespace rb::dsp
{
namespace
{
    // Fixed per-wall "construction quality" offsets; Asymmetry scales how much they matter.
    constexpr WallArray kAsymOffsets { +0.9f, -0.6f, +0.3f, -0.2f, +0.7f, -0.9f };
    // How strongly sustained LF pressure loads each boundary (the floor carries the most).
    constexpr WallArray kLfCoupling  { 0.8f, 0.8f, 0.7f, 0.7f, 0.5f, 1.0f };

    constexpr float kFractureCooldown = 0.04f; // s between brittle fractures on one wall
    constexpr float kCreakQuantum     = 0.03f; // integrity lost to creep before a creak event
}

void StressTracker::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    lfL.set (kLfCutoffHz, 0.7071f, fs);
    lfR.set (kLfCutoffHz, 0.7071f, fs);
    reset();
}

void StressTracker::reset()
{
    lfL.reset();
    lfR.reset();
    lfAcc = 0.0f;
    blockPeak.fill (0.0f);
    peakEnv.fill (0.0f);
    prevPeakEnv.fill (0.0f);
    inputPeak_ = inputPeakEnv = 0.0f;
    accPkL = accPkR = accPkM = accPkS = accPkAny = accLfEnergy = 0.0f;
    accIndex = accCount = 0;
    resetStructure();
}

void StressTracker::resetStructure()
{
    integrity_.fill (1.0f);
    stressRatio_.fill (0.0f);
    cooldown.fill (0.0f);
    creakAccumulator.fill (0.0f);
}

void StressTracker::restoreIntegrity (const WallArray& values)
{
    for (int w = 0; w < kNumWalls; ++w)
        integrity_[w] = clamp01 (values[w]);
}

void StressTracker::accumulate (const float* L, const float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        const float al = std::abs (l), ar = std::abs (r);
        accPkL = std::max (accPkL, al);
        accPkR = std::max (accPkR, ar);
        accPkM = std::max (accPkM, std::abs (0.5f * (l + r)));
        accPkS = std::max (accPkS, std::abs (0.5f * (l - r)));

        const float a = std::max (al, ar);
        if (a > accPkAny) { accPkAny = a; accIndex = accCount + i; }

        const float lf = 0.5f * (lfL.process (l).lp + lfR.process (r).lp);
        accLfEnergy += lf * lf;
    }
    accCount += n;
}

void StressTracker::finishAnalysis() noexcept
{
    const int n = std::max (accCount, 1);
    const float pkL = accPkL, pkR = accPkR, pkM = accPkM, pkS = accPkS, pkAny = accPkAny;
    const int idx = accIndex;
    const float dt = (float) n / fs;

    // Crest-normalised LF RMS: a full-scale sub sine reads ~1.0, the same scale as Peak.
    const float lfRms = std::sqrt (accLfEnergy / (float) n) * 1.41421356f;
    lfAcc += (lfRms - lfAcc) * (1.0f - std::exp (-dt / kFatigueTau));

    accPkL = accPkR = accPkM = accPkS = accPkAny = accLfEnergy = 0.0f;
    accIndex = accCount = 0;

    // Directional load mapping (listener faces North, source is in front).
    blockPeak[North]   = pkM;
    blockPeak[South]   = 0.7f * pkM + pkS;          // rear wall sees the decorrelated (side) energy
    blockPeak[East]    = pkR;
    blockPeak[West]    = pkL;
    blockPeak[Ceiling] = 0.85f * std::max (pkL, pkR);
    blockPeak[Floor]   = 0.6f * pkM;                // floor is mostly LF-loaded (see kLfCoupling)

    const float release = std::exp (-dt / kPeakRelease);
    for (int w = 0; w < kNumWalls; ++w)
    {
        prevPeakEnv[w] = peakEnv[w];
        peakEnv[w] = std::max (blockPeak[w], peakEnv[w] * release);
    }

    inputPeak_      = pkAny;
    inputPeakEnv    = std::max (pkAny, inputPeakEnv * release);
    peakIndex       = idx;
    lastBlockLength = n;
}

int StressTracker::detonate (FractureEvent* events, int maxEvents) noexcept
{
    const float brittle  = cfg.material != nullptr ? cfg.material->brittleness   : 0.5f;
    const float strength = cfg.material != nullptr ? cfg.material->strengthScale : 1.0f;
    const float scale = (0.75f + 0.5f * brittle) / std::sqrt (std::max (strength, 0.3f));
    int numEvents = 0;

    for (int w = 0; w < kNumWalls; ++w)
    {
        float& I = integrity_[w];
        const float chunk = std::clamp ((0.2f + 0.18f * rng.uni()) * scale, 0.12f, 0.6f);
        I = std::max (0.0f, I - chunk);
        cooldown[w] = kFractureCooldown;
        if (numEvents < maxEvents)
            events[numEvents++] = { w, 1.0f + 0.3f * rng.uni(), (int) (rng.uni() * 0.25f * (float) lastBlockLength), true };
    }
    return numEvents;
}

int StressTracker::updateStructure (float dt, FractureEvent* events, int maxEvents) noexcept
{
    if (cfg.material == nullptr)
        return 0;

    const auto& m          = *cfg.material;
    const float alpha      = 1.0f - cfg.ductileBias;
    const float beta       = cfg.ductileBias;
    const float healStep   = cfg.permadeath ? 0.0f : 1.0f - std::exp (-dt / std::max (cfg.healSeconds, 0.01f));
    const float fractureAt = lerp (1.6f, 0.9f, m.brittleness); // × capacity
    const WallArray before = integrity_;                         // load redistribution uses start-of-tick state
    int numEvents = 0;

    for (int w = 0; w < kNumWalls; ++w)
    {
        // Load redistribution: damaged neighbours (the 4 non-opposite walls) shift load onto this one.
        float adjacentDamage = 0.0f;
        for (int o = 0; o < kNumWalls; ++o)
            if (o != w && o != oppositeOf (w))
                adjacentDamage += 1.0f - before[o];
        adjacentDamage *= 0.25f;

        const float load = (1.0f + 0.8f * cfg.asymmetry * kAsymOffsets[w]) * (1.0f + 0.35f * adjacentDamage);

        float& I = integrity_[w];
        const float capacity = std::max (1.0e-6f, cfg.yieldLinear * m.strengthScale * (0.35f + 0.65f * I));

        const float brittleStress = alpha * peakEnv[w] * load;
        const float fatigueStress = beta * lfAcc * kLfCoupling[w] * load;
        const float ratio = (brittleStress + fatigueStress) / capacity;
        stressRatio_[w] = ratio;

        // 1. Brittle fracture — rising transient over the material's fracture point.
        cooldown[w] = std::max (0.0f, cooldown[w] - dt);
        const float transient = alpha * blockPeak[w] * load;
        const float threshold = capacity * fractureAt;
        const bool  rising    = blockPeak[w] > 1.25f * prevPeakEnv[w] + 1.0e-4f;

        if (I > 0.0f && cooldown[w] <= 0.0f && rising && transient > threshold)
        {
            const float excess = transient / threshold - 1.0f;
            const float chunk  = m.fractureChunk * std::clamp (0.3f + excess, 0.3f, 1.5f) * (0.6f + 0.8f * rng.uni());
            I = std::max (0.0f, I - chunk);
            cooldown[w] = kFractureCooldown;

            if (numEvents < maxEvents)
                events[numEvents++] = { w, std::clamp (0.3f + excess, 0.0f, 1.5f), peakIndex, true };
        }

        // 2. Plastic yield / fatigue — continuous creep while overloaded, weighted by the
        //    share of the load that is sustained LF (transients do their damage as fractures).
        if (ratio > 1.0f)
        {
            if (I > 0.0f)
            {
                const float ductileShare = fatigueStress / std::max (brittleStress + fatigueStress, 1.0e-9f);
                const float damage = m.fatigueRate * (ratio - 1.0f) * ductileShare * dt;
                I = std::max (0.0f, I - damage);
                creakAccumulator[w] += damage;

                if (creakAccumulator[w] >= kCreakQuantum)
                {
                    creakAccumulator[w] = 0.0f;
                    if (numEvents < maxEvents)
                        events[numEvents++] = { w, 0.12f + 0.3f * std::min (1.0f, ratio - 1.0f),
                                                (int) (rng.uni() * (float) lastBlockLength), false };
                }
            }
        }
        // 3. Tectonic healing — only once the wall is unloaded.
        else if (ratio < 0.7f)
        {
            I += (1.0f - I) * healStep;
        }

        I = clamp01 (I);
    }

    return numEvents;
}
} // namespace rb::dsp
