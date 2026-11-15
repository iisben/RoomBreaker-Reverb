#pragma once
//==============================================================================
// DynamicFDN — 16-line late tank whose matrix and loop filters follow the walls.
//
// Feedback matrix (Hadamard with diagonal bleed):
//     A(b) = (1 - b) * H + b * I,     H = H16 / 4  (orthogonal, symmetric)
//   H is applied with a fast Walsh-Hadamard transform: 64 adds, no multiplies.
//   Every line feeds every other line equally (|h_ij| = 1/4), so echo density
//   grows as 16^passes. (A Householder matrix at this size keeps 1 - 2/N of each
//   line on itself: the lines behave as weakly coupled combs and density builds
//   slowly.) H has eigenvalues ±1, so A(b) has eigenvalues 1 and 2b - 1:
//   |λ| ≤ 1 for every b ∈ [0, 1]. As boundaries breach, b rises, the tank loses
//   mixing and the lines decouple into combs, while staying non-expansive.
//   b is a single global scalar on purpose: per-row bleed can have gain > 1.
//
//   Input and output use four mutually orthogonal *bent* sign sequences. A bent
//   sequence has a flat Walsh spectrum, so H maps it to another ±1 sequence: the
//   injected energy stays spread over all 16 lines on every pass. (An ordinary
//   Walsh ±1 pattern would be focused by H into a single line every other pass.)
//
// Per-line loop filter, morphing with the two walls the line "bounces" between:
//     pristine : high-shelf damping (one-pole TPT shelf), proportional to length
//     loose    : normalised bell at the slab frequency (peak gain 1, rest 1/G)
//                → the slab mode rings longer than the rest of the tail
//     breached : TPT high-pass venting + broadband loss
//   Each stage has |H| ≤ 1 and the line gain is < 1, so the loop stays stable.
//   The bell and the vent are exact pass-throughs while no wall is loose or breached,
//   so those two stages are skipped entirely for an intact room.
//   Filter state is stored structure-of-arrays so the 16 filters of each stage
//   run as plain element-wise loops the compiler turns into SIMD.
//
// Exterior void: the energy each line vents is summed into an ExteriorTank whose
// character comes from a VoidProfile (Voids.h), or discarded for Free Field Loss.
//
// Quality / performance controls (TankControl):
//   density  4 input allpasses per side smear the injection into a dense wash
//            before it reaches the loop (0 = plain delay, discrete onset)
//   motion   each line's length drifts on its own slow sine + random walk
//            (±1.2 ms at full) so modes never sit still: no metallic ringing
//   shock    big fractures stretch every line briefly (≤ 1.6 %): the room flexes
//   freeze   crossfades every line to a lossless path (gain 1, no filters, no
//            injection, no venting). |H| stays ≤ 1 throughout, so it cannot blow up.
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "Voids.h"
#include "WallState.h"

namespace rb::dsp
{
//==============================================================================
class ExteriorTank
{
public:
    static constexpr int N = 4;

    void prepare (double sampleRate);
    void reset();

    /** Control-rate: glide toward the void's geometry/character; ramps for the next block. */
    void setProfile (const VoidProfile& v, int blockLength) noexcept;
    void processSample (float in, float& outL, float& outR) noexcept;

private:
    float fs = 48000.0f;
    std::array<DelayLine, N> lines;
    std::array<Allpass, N> smear;                 // one allpass inside each loop: density per pass
    std::array<float, N> baseD {}, D {}, DInc {}, gains {}, lfoPhase {};
    std::array<OnePoleTpt, N> damping;
    OnePoleTpt hpL, hpR;
    float diffusion = 1.0f, diffusionInc = 0.0f;
    float inputGain = 0.0f, inputGainInc = 0.0f;
    float returnGain = 1.0f, returnGainInc = 0.0f;
    float drive = 0.0f;
    float level = 0.0f;                           // peak of what is circulating, for the idle test
    int   quietTicks = 0;
    bool  idle = true;                            // nothing in, nothing circulating: skip the work
    bool  initialised = false;
};

//==============================================================================
class DynamicFDN
{
public:
    static constexpr int N = 16;

    void prepare (double sampleRate);
    void reset();

    struct TankControl
    {
        float decaySeconds = 2.8f;
        float sizeScale    = 1.0f;
        float density      = 0.7f;   // 0..1
        float motion       = 0.25f;  // 0..1
        float shock        = 0.0f;   // 0..~1.2
        float freeze       = 0.0f;   // 0..1 (smoothed by the caller)
    };

    /** Control-rate update: delay lengths, loop gains, filter morph and matrix bleed for the next block. */
    void updateControl (const WallArray& integrity, const MaterialProfile& m,
                        const TankControl& tc, const VoidProfile& exteriorVoid, int blockLength) noexcept;

    /** Stereo in → main tank out (overwrites) + exterior tail out (overwrites). */
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, float* extL, float* extR, int numSamples) noexcept;

    float matrixBleed() const noexcept { return bleed; }

private:
    static constexpr int kDiffusers = 4;

    /** Per-sample state of all lines, one array per quantity. */
    struct Bank
    {
        alignas (64) float D[N], DInc[N];                       // fractional length (samples) + per-sample ramp
        alignas (64) float gain[N], gainInc[N];                 // broadband loop gain (RT60 · looseness · venting)
        alignas (64) float shelfG[N], shelfS[N], hfGain[N];     // stage 1: high-shelf damping
        alignas (64) float shelfGInc[N], hfGainInc[N];          //          corner and depth ramp across a tick
        alignas (64) float slabA1[N], slabA2[N], slabA3[N], slabS1[N], slabS2[N], slabGm1k[N], slabNorm[N];   // stage 2: loose-slab bell
        alignas (64) float ventA1[N], ventA2[N], ventA3[N], ventK[N], ventS1[N], ventS2[N];                  // stage 3: high-pass venting
        alignas (64) float ventAmount[N], ventKeep[N];
    };
    /** Control-rate state per line. */
    struct Motion
    {
        // Random drift is smoothed twice: once for its position and once for its rate. Pitch follows
        // the rate a line's length changes, so a single stage left white jitter in the pitch, every
        // tick, and spread every tone in the tail into a skirt of noise (-22 dBc around 8 kHz).
        float creakNoise = 0.0f, creakRaw = 0.0f, wander = 0.0f, wanderRaw = 0.0f;
        float lfoC = 1.0f, lfoS = 0.0f;       // unit phasor: the line's slow drift oscillator
        float rotC = 1.0f, rotS = 0.0f;       // its rotation per control tick
        float baseD = 1000.0f, loose = 0.0f, targetGain = 0.0f, targetShelfG = 0.0f, targetHf = 1.0f;
    };

    void updateFilters (const WallArray& integrity, const MaterialProfile& m, float decaySeconds, float sizeScale) noexcept;

    float fs = 48000.0f, maxDelay = 1000.0f;
    std::array<DelayLine, N> delay;
    Bank bank {};
    std::array<Motion, N> motion;
    float bleed = 0.0f, bleedInc = 0.0f, targetBleed = 0.0f;
    float freeze = 0.0f, freezeInc = 0.0f;
    float densitySm = 0.7f, motionSm = 0.25f;                   // glided per tick: no clicks when Density / Motion jump
    float apGain = 0.525f, apGainInc = 0.0f;                    // input allpass gain, ramped per sample
    bool  controlInit = false;
    int   lfoBlock = 0;                                         // tick length the per-tick constants were made for
    float densityA = 0.0f, motionA = 0.0f, wanderA = 0.0f;

    // What the filter coefficients were last computed from: they are only recomputed when it changes.
    WallArray filterIntegrity {};
    const MaterialProfile* filterMaterial = nullptr;
    float filterDecay = -1.0f, filterSize = -1.0f;
    bool  filtersSettled = false;
    bool  slabOn = false, ventOn = false;   // stages 2 and 3 only run while some wall is loose / breached

    std::array<Allpass, kDiffusers> diffL, diffR;
    ExteriorTank exterior;
    Rng rng { 0xFD0C0DE5u };
};
} // namespace rb::dsp
