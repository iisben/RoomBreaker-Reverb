#pragma once
//==============================================================================
// DynamicFDN — 8-line late tank whose matrix and loop filters follow the walls.
//
// Feedback matrix (Householder with diagonal bleed):
//     A(b) = (1 - b) * H + b * I,     H = I - (2/N) 1 1ᵀ
//     A(b) x = x - (1 - b) (2/N) Σx          ← O(N), no matrix multiply
//   Eigenvalues: +1 (multiplicity N-1) and 2b - 1 on the 1-vector.
//   So |λ| ≤ 1 for every b ∈ [0, 1]: as boundaries breach, the tank loses
//   mixing (lines decouple into combs) and the uniform mode becomes lossy,
//   while staying non-expansive. b is a single global scalar on purpose —
//   per-row bleed, diag(1-b_k)H + diag(b_k), can have gain > 1.
//
// Per-line loop filter, morphing with the two walls the line "bounces" between:
//     pristine : high-shelf damping (one-pole TPT shelf)
//     loose    : normalised bell at the slab frequency (peak gain 1, rest 1/G)
//                → the slab mode rings longer than the rest of the tail
//     breached : TPT high-pass venting + broadband loss
//   Each stage has |H| ≤ 1 and the line gain is < 1, so the loop stays stable.
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
    std::array<float, N> baseD {}, D {}, DInc {}, gains {}, lfoPhase {};
    std::array<OnePoleTpt, N> damping;
    OnePoleTpt hpL, hpR;
    float diffusion = 1.0f, diffusionInc = 0.0f;
    float inputGain = 0.0f, inputGainInc = 0.0f;
    float returnGain = 1.0f, returnGainInc = 0.0f;
    float drive = 0.0f;
    bool  initialised = false;
};

//==============================================================================
class DynamicFDN
{
public:
    static constexpr int N = 8;

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
    struct Line
    {
        DelayLine delay;
        float D = 1000.0f, DInc = 0.0f;           // fractional length (samples) + per-sample ramp
        float gain = 0.0f, gainInc = 0.0f;        // broadband loop gain (RT60 · looseness · venting)
        OnePoleTpt shelf;  float hfGain = 1.0f;   // stage 1: high-shelf damping
        SvfTpt slab;       float slabGm1 = 0.0f, slabNorm = 1.0f; // stage 2: loose-slab bell
        SvfTpt vent;       float ventAmount = 0.0f;               // stage 3: HP venting
        float creakNoise = 0.0f;
        float lfoPhase = 0.0f, lfoRate = 0.5f, wander = 0.0f;   // motion
    };
    static constexpr int kDiffusers = 4;

    float fs = 48000.0f, maxDelay = 1000.0f;
    std::array<Line, N> lines;
    float bleed = 0.0f, bleedInc = 0.0f;
    float freeze = 0.0f, freezeInc = 0.0f;
    float densitySm = 0.7f, motionSm = 0.25f;
    float apGain = 0.525f, apGainInc = 0.0f;   // input allpass gain, ramped per sample   // glided per tick: no clicks when Density / Motion jump
    bool  controlInit = false;
    std::array<Allpass, kDiffusers> diffL, diffR;
    ExteriorTank exterior;
    Rng rng { 0xFD0C0DE5u };
};
} // namespace rb::dsp
