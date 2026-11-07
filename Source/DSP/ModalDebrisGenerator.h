#pragma once
//==============================================================================
// ModalDebrisGenerator — fracture-triggered modal synthesis.
//
// Voices are complex one-pole resonators (z ← p·z + x, p = r·e^{jω}, out = Im z):
// a second-order bandpass that stays numerically clean at very high Q.
//
//   Fracture event  → 2–8 modes from the material's ratio table, per-wall tuning,
//                     noise-burst excitation, optional high "rebar ping"
//   Chatter         → Poisson scheduler of small high-pitched fragments; rate rises
//                     with recent fracture energy and with loose slabs × input level
//   Contact rattle  → one-sided soft clip (slab slapping its frame) + DC blocker,
//                     depth = material.rattleAmount × mean looseness
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "WallState.h"

namespace rb::dsp
{
class ModalDebrisGenerator
{
public:
    static constexpr int kNumVoices = 32;

    void prepare (double sampleRate);
    void reset();

    void setMaterial (const MaterialProfile* m) noexcept { material = m; }

    /** Control-rate: per-wall looseness and current input activity (peak envelope, linear). */
    void setStructure (const WallArray& integrity, float inputActivity) noexcept;

    /** Schedules a modal burst for a fracture or creak event. */
    void trigger (const FractureEvent& e) noexcept;

    /** Control-rate Poisson chatter. Call once per control block, after trigger(). */
    void updateChatter (float dtSeconds, int blockLength) noexcept;

    /** Renders debris into outL/outR (overwrites). */
    void process (float* outL, float* outR, int numSamples) noexcept;

    int activeVoices() const noexcept;

private:
    struct Voice
    {
        bool  active = false;
        float pr = 0.0f, pi = 0.0f;       // pole
        float zr = 0.0f, zi = 0.0f;       // state
        float panL = 0.707f, panR = 0.707f;
        int   startDelay = 0, exciteRemaining = 0;
        float exciteAmp = 0.0f, exciteEnv = 1.0f, exciteDecay = 0.9f;
        Rng   noise { 1u };               // per voice → output independent of host slicing
    };

    Voice& allocateVoice() noexcept;
    void startMode (float freqHz, float t60, float amp, int delaySamples, int burstSamples, float pan) noexcept;

    float fs = 48000.0f;
    const MaterialProfile* material = nullptr;
    std::array<Voice, kNumVoices> voices;
    WallArray looseness {}, chatterEnergy {};
    float activity = 0.0f, rattleMix = 0.0f, rattleGap = 1.0f;
    OnePoleTpt dcL, dcR;
    Rng rng { 0xDEB815u };
};
} // namespace rb::dsp
