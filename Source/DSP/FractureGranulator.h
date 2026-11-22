#pragma once
//==============================================================================
// FractureGranulator — optional grains and pitch shifts on the breaking itself.
//
// It listens to the impact and the debris, not the reverb. Amount 0 adds nothing:
// no grain is scheduled, and the buffers handed back are left alone, so silence,
// presets and the held-tone checks stay where they were. Above that, a crack onset
// throws a short grain and, as the amount rises, a lighter spray follows the debris
// while it is still ringing.
//
// Pitch follows the material. Ice and glass flick upward, steel drops and spreads,
// rubble crunches downward. Grains read a ring a few milliseconds behind the write
// head and die before they can catch it, so they never read a sample that has not
// happened yet. Scheduling is per sample, so the host block size does not change it.
//==============================================================================
#include "DspUtils.h"
#include <array>

namespace rb::dsp
{
class FractureGranulator
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    void setAmount (float a) noexcept { amount = clamp01 (a); }
    /** metal is the voicing chirp (steel, ice), brittle is shatter (glass, ice). */
    void setCharacter (float metalAmount, float brittleAmount) noexcept
    {
        metal = clamp01 (metalAmount);
        brittle = clamp01 (brittleAmount);
    }

    /** Records inL/inR and adds grains onto addL/addR. Amount 0 leaves addL/addR untouched. */
    void process (const float* inL, const float* inR, float* addL, float* addR, int numSamples) noexcept;

private:
    static constexpr int kSize = 16384;   // power of two; ~340 ms at 48 kHz, ~85 ms at 192 kHz
    static constexpr int kMask = kSize - 1;
    static constexpr int kGrains = 10;

    struct Grain
    {
        bool  on = false;
        float delay = 0.0f;    // samples behind the write head
        float ratio = 1.0f;
        float phase = 0.0f;    // 0..1 Hann window
        float dPhase = 0.0f;
        float amp = 0.0f;
    };

    void spawn (float level) noexcept;

    std::array<float, kSize> bufL {}, bufR {};
    std::array<Grain, kGrains> grains {};
    int write = 0;
    int hold = 0;             // samples before another onset grain
    int spray = 0;            // samples before the next debris grain
    float amount = 0.0f, metal = 0.0f, brittle = 0.0f;
    float env = 0.0f, prev = 0.0f;
    float fs = 48000.0f;
    Rng rng { 0x6A09E667u };
};
} // namespace rb::dsp
