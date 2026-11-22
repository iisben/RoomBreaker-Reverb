#include "Engine.h"

namespace rb::dsp
{
namespace
{
    // Routing / voicing constants
    constexpr float kInToTank      = 0.5f;
    constexpr float kErToTank      = 0.35f;
    constexpr float kDebrisToTank  = 0.4f;   // debris happens *inside* the room, so it reverberates
    constexpr float kImpactToTank  = 0.45f;  // so does the boom: the room swallows its own collapse
    constexpr float kErWet         = 0.7f;
    constexpr float kTankWet       = 1.0f;
    constexpr float kExteriorWet   = 1.2f;
    constexpr float kDebrisWet     = 0.9f;
    constexpr float kImpactWet     = 0.8f;
    constexpr float kIntegrityTau  = 0.025f; // s — de-zippers instantaneous fracture drops for the DSP
    constexpr float kSizeTau       = 0.4f;   // s — slow enough that resizing bends pitch gently
    constexpr float kShockTau      = 0.35f;  // s — room-flex recovery
    constexpr float kFreezeTau     = 0.06f;  // s — freeze crossfade
    constexpr float kCutoffTau     = 0.05f;  // s — Low/High Cut glide (in octaves)
    constexpr float kToneTau       = 0.04f;  // s — Drive / Width / Ducking glide
    constexpr float kPredelayFade  = 0.04f;  // s — pre-delay change crossfade
    constexpr float kTelemetryHz   = 120.0f;
    constexpr float kDriveBias     = 0.2f;   // asymmetry → even harmonics
    constexpr float kMonoBassHz    = 120.0f; // wet side channel is high-passed here (mono-safe low end)

    float glide (float current, float target, float dt, float tau) noexcept
    {
        return target + std::exp (-dt / tau) * (current - target);
    }
}

void Engine::prepare (double newSampleRate)
{
    // Keep the room's damage across sample-rate changes / re-prepares,
    // unless a saved state is already waiting to be restored.
    if (prepared && ! restorePending.load (std::memory_order_acquire))
        requestStructureRestore (currentIntegrity());

    sampleRate = newSampleRate;
    fs = (float) newSampleRate;

    // ~750 Hz control rate at any sample rate: 64 @ 44.1/48 k, 128 @ 88.2/96 k, 256 @ 192 k ...
    controlBlock = std::clamp (kBaseControlBlock * std::max (1, (int) std::lround (sampleRate / 48000.0)),
                               kBaseControlBlock, kMaxControlBlock);

    stress.prepare (sampleRate);
    er.prepare (sampleRate);
    fdn.prepare (sampleRate);
    debris.prepare (sampleRate);
    impact.prepare (sampleRate);

    const int preSamples = (int) std::ceil (kMaxPredelayMs * 1.0e-3f * fs) + 8;
    preL.prepare (preSamples);
    preR.prepare (preSamples);
    preKeyL.prepare (preSamples);
    preKeyR.prepare (preSamples);

    debrisSmoother.setTime (0.05f, fs);
    sideLow.set (kMonoBassHz, fs);
    duckAtt = onePoleCoeff (0.004f, fs);
    duckRel = onePoleCoeff (0.25f, fs);
    publishInterval = std::max (controlBlock, (int) (fs / kTelemetryHz));

    reset();
    applyPendingRestore();   // audio is suspended: land a waiting recall before the next save
    prepared = true;
}

void Engine::reset()
{
    stress.reset();
    er.reset();
    fdn.reset();
    debris.reset();
    impact.reset();
    preL.reset(); preR.reset(); preKeyL.reset(); preKeyR.reset();
    lowCutL.reset(); lowCutR.reset(); highCutL.reset(); highCutR.reset();
    sideLow.reset();

    smoothedIntegrity.fill (1.0f);
    sizeSmoothed = sizeStage = params.size;
    er.setGeometry (sizeSmoothed);
    shock = 0.0f;
    freezeSmoothed = params.freeze ? 1.0f : 0.0f;
    predelayCur = predelayNext = std::max (2, (int) std::lround (std::clamp (params.predelayMs, 0.0f, kMaxPredelayMs) * 1.0e-3f * fs));
    predelayFade = predelayFadeInc = 0.0f;
    lowCutSm = std::clamp (params.lowCutHz, 10.0f, 2000.0f);
    highCutSm = std::clamp (params.highCutHz, 500.0f, 22000.0f);
    driveAmt = clamp01 (params.drive);
    driveGain = dbToGain (30.0f * driveAmt);
    driveMakeup = std::pow (driveGain, 0.3f);
    driveGainInc = driveMakeupInc = 0.0f;
    driveBlend = std::min (1.0f, driveAmt * 10.0f);
    driveBlendInc = 0.0f;
    width = widthSm = std::clamp (params.width, 0.0f, 2.0f);
    widthInc = 0.0f;
    duckAmt = duckAmtSm = clamp01 (params.duck);
    duckAmtInc = 0.0f;
    liveGain = params.freeze ? 0.0f : 1.0f;
    liveGainInc = 0.0f;
    duckEnv = 0.0f;
    duckGainShown = 1.0f;
    detonateRequested = false;

    mixTarget = mixSm = clamp01 (params.mix);
    mixCoeff = std::exp (-1.0f / (0.03f * fs));
    wetMix = std::sin (mixSm * kHalfPi);
    dryMix = std::cos (mixSm * kHalfPi);
    wetMixInc = dryMixInc = 0.0f;
    lowCutSet = highCutSet = -1.0f;
    debrisSmoother.reset (params.debrisLevel);
    flashAccumulator.fill (0.0f);
    samplesSincePublish = 0;
    tickPosition = 0;
    // A recall already queued (sample-rate change, session load, oversampling handoff) must
    // survive this wipe. Publishing 1 here is what made a save during the handoff forget the room.
    if (! restorePending.load (std::memory_order_acquire))
        for (auto& a : publishedIntegrity) a.store (1.0f, std::memory_order_relaxed);
}

void Engine::resetStructure() noexcept
{
    stress.resetStructure();
    // smoothedIntegrity is left to glide back up over kIntegrityTau — the rebuild is audible but click-free.
}

void Engine::setParams (const EngineParams& p) noexcept
{
    params = p;
    material = &getMaterialProfile (p.material);

    StressConfig cfg;
    cfg.yieldLinear = dbToGain (p.yieldDb);
    cfg.ductileBias = clamp01 (p.ductileBias);
    cfg.asymmetry   = clamp01 (p.asymmetry);
    cfg.healSeconds = p.healSeconds;
    cfg.permadeath  = p.permadeath;
    cfg.material    = material;
    stress.setConfig (cfg);

    debris.setMaterial (material);
    impact.setVoicing (&getMaterialVoicing (p.material));
    impact.setAmounts (p.impact, p.rumble);
}

void Engine::requestStructureRestore (const WallArray& integrity) noexcept
{
    for (int w = 0; w < kNumWalls; ++w)
        pendingRestore[(size_t) w].store (clamp01 (integrity[(size_t) w]), std::memory_order_relaxed);
    restorePending.store (true, std::memory_order_release);
}

void Engine::applyPendingRestore() noexcept
{
    if (! restorePending.exchange (false, std::memory_order_acquire))
        return;

    WallArray I {};
    for (int w = 0; w < kNumWalls; ++w)
        I[(size_t) w] = pendingRestore[(size_t) w].load (std::memory_order_relaxed);

    stress.restoreIntegrity (I);
    smoothedIntegrity = I; // jump: this is a recall, not an event
    for (int w = 0; w < kNumWalls; ++w)
        publishedIntegrity[(size_t) w].store (I[(size_t) w], std::memory_order_relaxed);
}

WallArray Engine::currentIntegrity() const noexcept
{
    WallArray I {};
    if (restorePending.load (std::memory_order_acquire))
    {
        for (int w = 0; w < kNumWalls; ++w)
            I[(size_t) w] = pendingRestore[(size_t) w].load (std::memory_order_relaxed);
        return I;
    }
    for (int w = 0; w < kNumWalls; ++w)
        I[(size_t) w] = publishedIntegrity[(size_t) w].load (std::memory_order_relaxed);
    return I;
}

void Engine::process (float* left, float* right, int numSamples, const float* keyL, const float* keyR) noexcept
{
    applyPendingRestore();

    int offset = 0;
    while (offset < numSamples)
    {
        if (tickPosition == 0)
            controlTick();

        const int len = std::min (controlBlock - tickPosition, numSamples - offset);
        renderSlice (left + offset, right + offset,
                     keyL != nullptr ? keyL + offset : nullptr,
                     keyR != nullptr ? keyR + offset : nullptr, len);

        offset += len;
        tickPosition += len;
        if (tickPosition == controlBlock)
            tickPosition = 0;
    }
}

//==============================================================================
void Engine::controlTick() noexcept
{
    const int n = controlBlock;
    const float invN = 1.0f / (float) n;
    const float dt = (float) n / fs;

    //--------------------------------------------------------------------------
    // 1. ANALYSIS — close the stress window fed during the previous tick
    //--------------------------------------------------------------------------
    stress.finishAnalysis();

    freezeSmoothed = glide (freezeSmoothed, params.freeze ? 1.0f : 0.0f, dt, kFreezeTau);
    const bool frozen = params.freeze;

    FractureEvent events[kMaxEventsPerBlock];
    int numEvents = 0;
    if (detonateRequested)
    {
        detonateRequested = false;
        numEvents = stress.detonate (events, kMaxEventsPerBlock);
    }
    // Freeze stops time for the structure too: no damage, no healing.
    if (! frozen)
        numEvents += stress.updateStructure (dt, events + numEvents, kMaxEventsPerBlock - numEvents);

    //--------------------------------------------------------------------------
    // 2. STATE MODULATION — integrity → delay times, gains, filters, matrix bleed
    //    Every engine computes targets + per-sample ramps spanning this tick.
    //--------------------------------------------------------------------------
    const auto& rawIntegrity = stress.integrity();
    const float integrityA = std::exp (-dt / kIntegrityTau);
    float meanLoose = 0.0f;
    for (int w = 0; w < kNumWalls; ++w)
    {
        smoothedIntegrity[(size_t) w] = rawIntegrity[(size_t) w] + integrityA * (smoothedIntegrity[(size_t) w] - rawIntegrity[(size_t) w]);
        meanLoose += loosenessAmount (smoothedIntegrity[(size_t) w]) / (float) kNumWalls;
    }

    // Two smoothers in a row: the glide starts and ends with no jump in speed. One alone starts at
    // full speed, and since every delay line follows Size, that kink is heard as a tick.
    sizeStage    = glide (sizeStage, params.size, dt, 0.5f * kSizeTau);
    sizeSmoothed = glide (sizeSmoothed, sizeStage, dt, 0.5f * kSizeTau);

    // Fractures flex the room: the shock envelope jumps with the hit and relaxes.
    shock *= std::exp (-dt / kShockTau);
    for (int e = 0; e < numEvents; ++e)
        if (events[e].brittle)
            shock = std::max (shock, params.impact * std::min (events[e].magnitude, 1.2f));

    er.setGeometry (sizeSmoothed);
    er.updateControl (smoothedIntegrity, *material, n);

    DynamicFDN::TankControl tc;
    tc.decaySeconds = params.decaySeconds;
    tc.sizeScale    = sizeSmoothed;
    tc.density      = params.density;
    tc.motion       = params.motion;
    tc.shock        = shock;
    tc.freeze       = freezeSmoothed;
    fdn.updateControl (smoothedIntegrity, *material, tc, getVoidProfile (params.voidType), n);

    debris.setStructure (smoothedIntegrity, stress.inputPeakEnvelope());
    for (int e = 0; e < numEvents; ++e)
    {
        debris.trigger (events[e]);   // sampleOffset is reused inside this tick → sample-accurate, 1 tick late
        impact.trigger (events[e]);
        auto& flash = flashAccumulator[(size_t) events[e].wall];
        flash = std::max (flash, events[e].magnitude);
    }
    debris.updateChatter (dt, n);
    impact.setRoomSize (sizeSmoothed);
    impact.setStress (frozen ? WallArray {} : stress.stressRatio(), smoothedIntegrity);
    impact.update (dt, n, frozen ? 0.0f : meanLoose, stress.inputPeakEnvelope());

    // Pre-delay: when the target moves, crossfade from the current tap to the new one.
    const int predelayTarget = std::max (2, (int) std::lround (std::clamp (params.predelayMs, 0.0f, kMaxPredelayMs) * 1.0e-3f * fs));
    if (predelayFadeInc == 0.0f && predelayTarget != predelayCur)
    {
        predelayNext = predelayTarget;
        predelayFade = 0.0f;
        predelayFadeInc = 1.0f / (kPredelayFade * fs);
    }

    // Wet tone: glide every target, then ramp per sample across the tick.
    const float toneA = std::exp (-dt / kToneTau);
    const float cutA  = std::exp (-dt / kCutoffTau);
    lowCutSm  = std::clamp (params.lowCutHz, 10.0f, 2000.0f)    * std::pow (lowCutSm  / std::clamp (params.lowCutHz, 10.0f, 2000.0f), cutA);
    highCutSm = std::clamp (params.highCutHz, 500.0f, 22000.0f) * std::pow (highCutSm / std::clamp (params.highCutHz, 500.0f, 22000.0f), cutA);
    if (std::abs (lowCutSm - lowCutSet) > 1.0e-4f * lowCutSm)       // the tan() is only paid while a cutoff moves
    {
        lowCutSet = lowCutSm;
        lowCutL.set (lowCutSm, 0.7071f, fs);
        lowCutR.set (lowCutSm, 0.7071f, fs);
    }
    if (std::abs (highCutSm - highCutSet) > 1.0e-4f * highCutSm)
    {
        highCutSet = highCutSm;
        highCutL.set (std::min (highCutSm, 0.45f * fs), 0.7071f, fs);
        highCutR.set (std::min (highCutSm, 0.45f * fs), 0.7071f, fs);
    }

    // Mix is applied per sample (sin/cos of one smoothed angle). Ramping sin and cos
    // independently dips about 2 dB on a full throw. The plugin smooths the angle itself
    // and asks for wet only; this path is the standalone / offline equal-power mix.
    mixTarget = clamp01 (params.mix);
    mixCoeff = std::exp (-1.0f / (0.03f * fs));
    wetMixInc = dryMixInc = 0.0f;

    driveAmt = clamp01 (params.drive) + toneA * (driveAmt - clamp01 (params.drive));
    if (driveAmt < 1.0e-5f) driveAmt = 0.0f;
    const float gTarget = dbToGain (30.0f * driveAmt);
    driveGainInc   = (gTarget - driveGain) * invN;
    driveMakeupInc = (std::pow (gTarget, 0.3f) - driveMakeup) * invN;
    driveBlendInc  = (std::min (1.0f, driveAmt * 10.0f) - driveBlend) * invN;   // 0..10 % fades the curve in

    widthSm = std::clamp (params.width, 0.0f, 2.0f) + toneA * (widthSm - std::clamp (params.width, 0.0f, 2.0f));
    widthInc = (widthSm - width) * invN;
    duckAmtSm = clamp01 (params.duck) + toneA * (duckAmtSm - clamp01 (params.duck));
    duckAmtInc = (duckAmtSm - duckAmt) * invN;
    liveGainInc = ((1.0f - freezeSmoothed) - liveGain) * invN;

    //--------------------------------------------------------------------------
    // 5. TELEMETRY — wait-free publish (~120 Hz)
    //--------------------------------------------------------------------------
    for (int w = 0; w < kNumWalls; ++w)
        publishedIntegrity[(size_t) w].store (rawIntegrity[(size_t) w], std::memory_order_relaxed);

    samplePosition += (uint64_t) n;
    samplesSincePublish += n;
    if (telemetry != nullptr && samplesSincePublish >= publishInterval)
    {
        samplesSincePublish = 0;
        TelemetryFrame frame;
        frame.integrity      = rawIntegrity;
        frame.stressRatio    = stress.stressRatio();
        frame.fractureFlash  = flashAccumulator;
        frame.inputPeakDb    = gainToDb (stress.inputPeakEnvelope());
        frame.lfFatigue      = stress.lfAccumulator();
        frame.matrixBleed    = fdn.matrixBleed();
        frame.activeDebris   = debris.activeVoices();
        frame.rubble         = impact.rubbleEnergy();
        frame.shock          = shock;
        frame.duckGain       = duckGainShown;
        frame.frozen         = frozen;
        frame.samplePosition = samplePosition;
        telemetry->publish (frame);
        flashAccumulator.fill (0.0f);
    }
}

//==============================================================================
void Engine::renderSlice (float* L, float* R, const float* keyL, const float* keyR, int n) noexcept
{
    //--------------------------------------------------------------------------
    // 0. PRE-DELAY + STRESS SOURCE
    //--------------------------------------------------------------------------
    const auto source = static_cast<StressSource> (params.stressSource);
    const bool haveKey = keyL != nullptr && keyR != nullptr;

    for (int i = 0; i < n; ++i)
    {
        const size_t s = (size_t) i;

        float kl = L[i], kr = R[i];
        if (haveKey && source == StressSource::Sidechain) { kl = keyL[i]; kr = keyR[i]; }
        else if (haveKey && source == StressSource::Both) { kl += keyL[i]; kr += keyR[i]; }

        pdL[s]  = tapPre (preL);     preL.push (L[i]);
        pdR[s]  = tapPre (preR);     preR.push (R[i]);
        srcL[s] = tapPre (preKeyL);  preKeyL.push (kl);
        srcR[s] = tapPre (preKeyR);  preKeyR.push (kr);

        if (predelayFadeInc > 0.0f && (predelayFade += predelayFadeInc) >= 1.0f)
        {
            predelayCur = predelayNext;
            predelayFade = predelayFadeInc = 0.0f;
        }
    }

    // Analysis feed for the *next* control tick.
    stress.accumulate (srcL.data(), srcR.data(), n);

    //--------------------------------------------------------------------------
    // 3. RENDER — ER → (input + ER + debris + impact) → FDN (+ exterior void)
    //--------------------------------------------------------------------------
    for (int i = 0; i < n; ++i)   // frozen rooms take no new sound in
    {
        liveGain += liveGainInc;
        mono[(size_t) i] = 0.5f * (pdL[(size_t) i] + pdR[(size_t) i]) * liveGain;
    }

    er.process (mono.data(), erL.data(), erR.data(), n);
    debris.process (debL.data(), debR.data(), n);
    impact.process (impL.data(), impR.data(), n);

    for (int i = 0; i < n; ++i)
    {
        const size_t s = (size_t) i;
        tankInL[s] = kInToTank * pdL[s] + kErToTank * erL[s] + kDebrisToTank * debL[s] + kImpactToTank * impL[s];
        tankInR[s] = kInToTank * pdR[s] + kErToTank * erR[s] + kDebrisToTank * debR[s] + kImpactToTank * impR[s];
    }

    fdn.process (tankInL.data(), tankInR.data(), tankL.data(), tankR.data(), extL.data(), extR.data(), n);

    //--------------------------------------------------------------------------
    // 4. WET TONE + RECOMBINE
    //--------------------------------------------------------------------------
    const float driveOffset = std::tanh (kDriveBias);
    const float driveNorm   = 1.0f / (1.0f - driveOffset * driveOffset);   // unity small-signal slope

    for (int i = 0; i < n; ++i)
    {
        const size_t s = (size_t) i;
        const float deb = debrisSmoother.process (params.debrisLevel);

        float wetL = kErWet * erL[s] + kTankWet * tankL[s] + kExteriorWet * extL[s] + kDebrisWet * deb * debL[s] + kImpactWet * impL[s];
        float wetR = kErWet * erR[s] + kTankWet * tankR[s] + kExteriorWet * extR[s] + kDebrisWet * deb * debR[s] + kImpactWet * impR[s];

        // Drive: asymmetric tanh, unity slope at zero, gentle make-up so it gets louder *and* denser.
        driveGain   += driveGainInc;
        driveMakeup += driveMakeupInc;
        driveBlend  += driveBlendInc;
        if (driveBlend > 1.0e-6f)
        {
            const float g = driveGain, k = driveNorm * driveMakeup / g;
            wetL += driveBlend * ((std::tanh (g * wetL + kDriveBias) - driveOffset) * k - wetL);
            wetR += driveBlend * ((std::tanh (g * wetR + kDriveBias) - driveOffset) * k - wetR);
        }

        // Low / High Cut (12 dB/oct). The low cut also removes the drive's DC.
        wetL = highCutL.process (lowCutL.process (wetL).hp).lp;
        wetR = highCutR.process (lowCutR.process (wetR).hp).lp;

        // Width, with the side channel high-passed so the low end is always mono-safe.
        width += widthInc;
        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR);
        const float sideHigh = side - sideLow.lp (side);
        wetL = mid + width * sideHigh;
        wetR = mid - width * sideHigh;

        // Ducking: the wet signal makes room for the dry one, and blooms back between notes.
        const float level = std::max (std::abs (L[i]), std::abs (R[i]));
        duckEnv = level > duckEnv ? level + duckAtt * (duckEnv - level) : level + duckRel * (duckEnv - level);
        duckAmt += duckAmtInc;
        float duckGain = 1.0f;
        if (duckAmt > 1.0e-5f)
            duckGain = 1.0f - 0.9f * duckAmt * clamp01 ((gainToDb (duckEnv) + 40.0f) / 30.0f);

        if (mixDry)
        {
            mixSm = mixTarget + mixCoeff * (mixSm - mixTarget);
            const float th = mixSm * kHalfPi;
            wetMix = std::sin (th);
            dryMix = std::cos (th);
            L[i] = dryMix * L[i] + wetMix * duckGain * wetL;
            R[i] = dryMix * R[i] + wetMix * duckGain * wetR;
        }
        else
        {
            L[i] = duckGain * wetL;
            R[i] = duckGain * wetR;
        }
        duckGainShown = duckGain;
    }
}
} // namespace rb::dsp
