#pragma once
//==============================================================================
// ImpactGenerator — the physical "hit" of a structure failing.
//
// The modal debris engine gives each fracture its ringing pitch. This layer adds
// the weight a real collapse has, voiced per material (MaterialVoicing):
//
//   Boom    sub sine whose pitch falls an octave (f0 → f0/2) with an exponential
//           amplitude decay. Mono (centred) so the low end stays solid.
//   Crack   short band-passed noise burst at the material's crack frequency,
//           panned to the wall that broke.
//   Rubble  Poisson-scheduled noise grains (falling chunks) plus a low rumble bed,
//           both driven by "rubble energy": every fracture adds to it and it
//           settles over ~2.5 s. Loose slabs under load keep it trickling.
//
// Impact scales boom + crack, Rumble scales rubble. Every voice owns its RNG, so the
// output does not depend on how the host slices audio.
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "WallState.h"

namespace rb::dsp
{
class ImpactGenerator
{
public:
    static constexpr int kNumBooms  = 4;
    static constexpr int kNumNoises = 24;   // cracks and rubble grains share one pool

    void prepare (double sampleRate);
    void reset();

    void setVoicing (const MaterialVoicing* v) noexcept { voicing = v; }
    void setAmounts (float impact, float rumble) noexcept { impactAmount = clamp01 (impact); rumbleAmount = clamp01 (rumble); }

    /** Control rate: one fracture / creak / detonation. */
    void trigger (const FractureEvent& e) noexcept;

    /** Control rate: rubble scheduler. meanLooseness and activity (input peak env) keep it trickling. */
    void update (float dtSeconds, int blockLength, float meanLooseness, float activity) noexcept;

    /** Renders into outL/outR (overwrites). */
    void process (float* outL, float* outR, int numSamples) noexcept;

    float rubbleEnergy() const noexcept { return rubble; }

private:
    struct Boom
    {
        bool  active = false;
        int   startDelay = 0;
        float phase = 0.0f;
        float freqEnd = 40.0f, freqSpan = 40.0f, freqDecay = 0.999f; // f = end + span · decay^n
        float amp = 0.0f, ampDecay = 0.999f;
        float attack = 0.0f, attackInc = 0.01f;
        int   age = 0;                    // samples since it started sounding
    };

    struct Noise
    {
        bool  active = false;
        int   startDelay = 0;
        SvfTpt band;
        float amp = 0.0f, decay = 0.99f;
        float panL = 0.707f, panR = 0.707f;
        Rng   rng { 1u };
    };

    Noise& allocateNoise() noexcept;
    void startBoom (int wall, float magnitude, float amp, int offset) noexcept;
    void startNoise (float hz, float q, float amp, float decayMs, int delay, float pan) noexcept;

    float fs = 48000.0f;
    const MaterialVoicing* voicing = nullptr;
    float impactAmount = 0.5f, rumbleAmount = 0.3f;

    std::array<Boom, kNumBooms> booms;
    std::array<Noise, kNumNoises> noises;

    float rubble = 0.0f;                 // rubble energy (0..~3)
    float bedAmp = 0.0f, bedAmpInc = 0.0f;
    SvfTpt bedL, bedR;
    Rng bedRngL { 0xB0DE1u }, bedRngR { 0xB0DE2u };
    OnePoleTpt dcL, dcR;
    Rng rng { 0x1A9AC7u };
};
} // namespace rb::dsp
