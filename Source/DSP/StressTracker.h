#pragma once
//==============================================================================
// StressTracker — Kinetic Stress Analyzer + structural integrity model.
//
// Runs once per control block (Engine::kControlBlock samples):
//
//   Stress_w = load_w * ( alpha * PeakEnv_w  +  beta * LF_Accumulator * lfCoupling_w )
//
//   alpha = 1 - ductileBias          (brittle: transient peak sensitivity)
//   beta  = ductileBias              (ductile: sustained sub-120 Hz fatigue)
//   load_w = asymmetry susceptibility * load redistribution from damaged neighbours
//
// Damage mechanisms per wall:
//   1. Brittle fracture  — a rising transient above capacity * f(brittleness)
//                          removes a chunk of integrity instantly and emits an event.
//   2. Plastic yield     — while Stress > capacity, integrity creeps down continuously.
//   3. Healing           — while unloaded (ratio < 0.7), exponential recovery toward 1
//                          with the Tectonic Healing time constant (none in permadeath).
// Capacity shrinks as integrity drops, so failure accelerates once it starts.
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "WallState.h"

namespace rb::dsp
{
struct StressConfig
{
    float yieldLinear  = 0.25f;  // pristine-wall capacity in normalised stress units (≈ linear peak)
    float ductileBias  = 0.35f;  // 0 = brittle .. 1 = ductile
    float asymmetry    = 0.3f;   // 0 = all walls identical .. 1 = strongly uneven construction
    float healSeconds  = 20.0f;
    bool  permadeath   = false;
    const MaterialProfile* material = nullptr;
};

class StressTracker
{
public:
    static constexpr float kLfCutoffHz  = 120.0f;
    static constexpr float kFatigueTau  = 1.5f;   // s — LF energy integration window
    static constexpr float kPeakRelease = 0.12f;  // s — peak "SPL" envelope release

    void prepare (double sampleRate);
    void reset();             // analysis state + structure
    void resetStructure();    // every wall back to pristine
    void restoreIntegrity (const WallArray& values);
    void setConfig (const StressConfig& c) noexcept { cfg = c; }

    /** Analysis feed: accumulate stereo input statistics. May be called several times per
        control block (host buffers need not line up with the control grid). */
    void accumulate (const float* left, const float* right, int numSamples) noexcept;

    /** Closes the analysis window: peak envelopes, LF fatigue integrator, directional loads. */
    void finishAnalysis() noexcept;

    /** Convenience: accumulate + finishAnalysis for one complete block. */
    void analyse (const float* left, const float* right, int numSamples) noexcept
    {
        accumulate (left, right, numSamples);
        finishAnalysis();
    }

    /** Forced failure (DETONATE): every wall loses a material-dependent chunk at once.
        Strong materials lose less, brittle ones more; always audible. Returns events written. */
    int detonate (FractureEvent* events, int maxEvents) noexcept;

    /** Integrates damage / healing for this block. Writes up to maxEvents fracture events. */
    int updateStructure (float dtSeconds, FractureEvent* events, int maxEvents) noexcept;

    const WallArray& integrity() const noexcept   { return integrity_; }
    const WallArray& stressRatio() const noexcept { return stressRatio_; } // stress / capacity
    float lfAccumulator() const noexcept          { return lfAcc; }
    float inputPeak() const noexcept              { return inputPeak_; }
    float inputPeakEnvelope() const noexcept      { return inputPeakEnv; }

private:
    StressConfig cfg;
    float fs = 48000.0f;

    SvfTpt lfL, lfR;
    float lfAcc = 0.0f;
    WallArray blockPeak {}, peakEnv {}, prevPeakEnv {};
    int   peakIndex = 0, lastBlockLength = 1;
    float inputPeak_ = 0.0f, inputPeakEnv = 0.0f;

    // open analysis window
    float accPkL = 0.0f, accPkR = 0.0f, accPkM = 0.0f, accPkS = 0.0f, accPkAny = 0.0f, accLfEnergy = 0.0f;
    int   accIndex = 0, accCount = 0;

    WallArray integrity_ {}, stressRatio_ {}, cooldown {}, creakAccumulator {};
    Rng rng { 0x51A8B00Du };
};
} // namespace rb::dsp
