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
//
// Every bounce also loses treble to the wall (a first-order shelf from the material):
// the source is shelved once on its way in, and second-order images once more, so
// later reflections are darker, as they are in a real room.
//
// Cost: a tap that is not moving reads a whole sample (no interpolation, no treble
// loss); only taps that are being jittered or resized use the cubic read. Gains and
// jitter depths are recomputed only when the walls, the material or the size change.
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
    static constexpr int kFirstOrder = 6;   // taps [0, 6) bounce once, the rest twice

    struct Tap
    {
        int wallA = 0, wallB = -1;
        float baseDelay = 2.0f, geoGain = 0.0f, panL = 0.707f, panR = 0.707f;
        float noise = 0.0f, noiseRaw = 0.0f, jitter = 0.0f;   // jitter walk, smoothed twice (see DynamicFDN::Motion)
        float delayTarget = 2.0f;
        int   whole = 2;                      // the delay in whole samples, used while `still`
        bool  still = false;
    };

    // Tap gains, one array per output bus (specular L, R / diffuse L, R), so the four mixes are
    // dot products the compiler can vectorise instead of one long chain of additions.
    alignas (32) float gain[4][kNumTaps] {}, gainInc[4][kNumTaps] {}, gainTarget[4][kNumTaps] {};
    alignas (32) int wholeDelay[kNumTaps] {};
    alignas (32) float tapDelay[kNumTaps] {}, tapDelayInc[kNumTaps] {};   // the moving read positions
    bool steady = false;   // this tick: every tap still and no gain moving → the fast path
    bool cubicTaps = true; // moving taps: cubic reads at 44.1/48 kHz, linear once the engine runs oversampled
    int  diffuseHold = 0;  // samples the diffusers must keep running (their ring-out after the last diffuse input)

    void buildTopology() noexcept;

    float fs = 48000.0f;
    float currentSize = -1.0f;
    DelayLine line;
    std::array<Tap, kNumTaps> taps;
    std::array<Allpass, kDiffuserStages> diffuserL, diffuserR;
    Rng rng { 0xE4C0FFEEu };

    // per-bounce treble loss
    OnePoleTpt inShelf;
    alignas (16) float bounceS[4] {};
    float bounceG = 0.0f, bounceGInc = 0.0f, bounceGTarget = 0.0f;   // shelf corner (as the one-pole's G), ramped
    float bounceHf = 1.0f, bounceHfInc = 0.0f, bounceHfTarget = 1.0f;  // shelf depth, ramped
    float diffuserGain = 0.45f, diffuserGainInc = 0.0f, diffuserGainTarget = 0.45f;

    // what the gains were last computed from
    WallArray gainIntegrity {};
    const MaterialProfile* gainMaterial = nullptr;
    bool geometryDirty = true, gainsSettled = false;
    bool resizing = false;   // the room changed size since the last tick: taps follow it exactly, unrounded
};
} // namespace rb::dsp
