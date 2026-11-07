#pragma once
//==============================================================================
// EarlyReflectionEngine — shoebox image-source tapped delay (orders 1 and 2).
//
// 24 taps: 6 first-order images + 12 cross-axis second-order + 6 same-axis
// second-order (ordered pairs, e.g. West→East and East→West).
//
// Per tap, from the integrity of the walls it bounced off:
//   R      = reflectivity * (1 - breach)            breached walls stop reflecting
//   spec   = Π integrity ^ scatterExponent          share that stays specular
//   specularGain = geo * R * spec
//   diffuseGain  = geo * R * sqrt(1 - spec²)        energy-preserving split → diffuser
//   delay  = imageDelay + jitterDepth(crack) * smoothedNoise
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "WallState.h"

namespace rb::dsp
{
class EarlyReflectionEngine
{
public:
    static constexpr int kNumTaps = 24;
    static constexpr int kDiffuserStages = 4;

    void prepare (double sampleRate);
    void reset();

    /** Room scale (1 = 14 x 10 x 5.5 m). Cheap; call whenever the smoothed size moves. */
    void setGeometry (float sizeScale) noexcept;

    /** Control-rate update: computes per-tap targets and per-sample ramps for the next block. */
    void updateControl (const WallArray& integrity, const MaterialProfile& m, int blockLength) noexcept;

    /** Mono source in, stereo reflections out (overwrites outputs). */
    void process (const float* in, float* outL, float* outR, int numSamples) noexcept;

private:
    struct Tap
    {
        int wallA = 0, wallB = -1;
        float baseDelay = 2.0f, geoGain = 0.0f, panL = 0.707f, panR = 0.707f;
        float noise = 0.0f;
        float delay = 2.0f, delayInc = 0.0f;
        float gL = 0.0f, gR = 0.0f, dL = 0.0f, dR = 0.0f;            // specular / diffuse, per side
        float gLInc = 0.0f, gRInc = 0.0f, dLInc = 0.0f, dRInc = 0.0f;
    };

    void buildTopology() noexcept;

    float fs = 48000.0f;
    float currentSize = -1.0f;
    DelayLine line;
    std::array<Tap, kNumTaps> taps;
    std::array<Allpass, kDiffuserStages> diffuserL, diffuserR;
    Rng rng { 0xE4C0FFEEu };
};
} // namespace rb::dsp
