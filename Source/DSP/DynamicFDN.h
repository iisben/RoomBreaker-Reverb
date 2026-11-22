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
//   The two output pickups are mutually orthogonal bent ±1 sequences, so H maps each
//   to another ±1 sequence. The inputs are a bent pair rotated by 22.5°: inL, inR,
//   inL+inR and inL−inR then have no zero entries (a centered source drives all 16
//   lines) while norms and the four-way orthogonality are unchanged. Each input's
//   Walsh spectrum is equal-ripple with crest 1+√2.
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
//   freeze   crossfades every line to the raw delay (no filters, no injection, no
//            venting) held at kFreezeLoopGain per circulation. |H| stays ≤ 1, so it
//            cannot blow up, and the tail still reaches -60 dB.
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "Voids.h"
#include "WallState.h"
#include <atomic>

namespace rb::dsp
{
// Line lengths at Size 1 (ms). Spread over two octaves. No pair is an exact small-integer
// ratio; every 2:1 is at least 3% off, and the nearest q≤6 approach left in the set is ~0.2%.
// A full 2% gap to every such ratio is not possible inside this span without clustering lines.
inline constexpr float kFdnBaseMs[16] = { 20.86f, 23.84f, 25.96f, 27.95f, 32.72f, 33.96f, 37.45f, 40.23f,
                                          44.16f, 49.63f, 58.10f, 63.07f, 70.15f, 77.63f, 83.67f, 91.99f };
// Once per circulation while frozen. -60 dB is about 3.45e5 loops, not 0.99998 per sample.
inline constexpr float kFreezeLoopGain = 0.99998f;

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

    /** Peak level circulating at the last control tick. 0 when the tank is idle. */
    float circulatingLevel() const noexcept { return circulating.load (std::memory_order_relaxed); }

private:
    float fs = 48000.0f;
    std::array<DelayLine, N> lines;
    std::array<Allpass, N> smear;                 // one allpass inside each loop: density per pass
    std::array<float, N> baseD {}, D {}, DInc {}, gains {}, gainInc {}, lfoPhase {};
    std::array<float, N> dampInc {};
    std::array<OnePoleTpt, N> damping;
    float hpGInc = 0.0f;
    std::atomic<float> circulating { 0.0f };
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
    float exteriorLevel() const noexcept { return exterior.circulatingLevel(); }

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
        alignas (64) float slabA1T[N], slabA2T[N], slabA3T[N], slabGm1kT[N], slabNormT[N];
        alignas (64) float slabA1Inc[N], slabA2Inc[N], slabA3Inc[N], slabGm1kInc[N], slabNormInc[N];
        alignas (64) float ventA1[N], ventA2[N], ventA3[N], ventK[N], ventS1[N], ventS2[N];                  // stage 3: high-pass venting
        alignas (64) float ventA1T[N], ventA2T[N], ventA3T[N], ventAmountT[N], ventKeepT[N];
        alignas (64) float ventA1Inc[N], ventA2Inc[N], ventA3Inc[N], ventAmountInc[N], ventKeepInc[N];
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
