#pragma once
//==============================================================================
// Exterior void archetypes — what lies beyond a breached wall.
//
// Every breach vents energy out of the room (DynamicFDN). The void decides what
// happens to it: discarded (Free Field Loss), or collected by the ExteriorTank,
// a 4-line FDN whose character comes from this table:
//
//   delayMs    line lengths (size of the outside world)
//   rt60       decay of the outside world (seconds)
//   dampHz     in-loop low-pass (distance / darkness)
//   hpHz       output high-pass (thin it out, block DC from grit)
//   diffusion  1 = full Householder wash, → 0 = lines decouple into discrete
//              echoes / flutter (A = I − d·(2/N)·11ᵀ, eigenvalues 1 and 1 − 2d).
//              It also sets the gain of the allpass inside each loop, which smears
//              every pass: smooth voids stay smooth, echo voids keep their echoes.
//   modDepthMs, modRateHz   slow per-line delay wobble (water, heat haze)
//   drive      0 = clean; > 0 asymmetric tanh grit on the tank output
//   returnGain level of the exterior in the wet mix
//==============================================================================
#include <algorithm>

namespace rb::dsp
{
struct VoidProfile
{
    const char* name;
    bool  collects;
    float delayMs[4];
    float rt60;
    float dampHz;
    float hpHz;
    float diffusion;
    float modDepthMs;
    float modRateHz;
    float drive;
    float returnGain;
};

inline constexpr VoidProfile kVoidProfiles[] =
{
    { "Free Field Loss",          false, { 113.7f, 151.3f, 197.1f, 241.9f }, 45.0f, 1400.0f, 20.0f, 1.0f,  0.0f, 0.0f,  0.0f, 1.0f },
    { "Infinite Secondary Tail",  true,  { 113.7f, 151.3f, 197.1f, 241.9f }, 45.0f, 1400.0f, 20.0f, 1.0f,  1.0f, 0.09f, 0.0f, 1.0f },  // slow drift: a 45 s tail on fixed lines rings

    // Dark cinematic set
    { "Industrial Tunnel",        true,  {  41.3f,  47.9f,  53.1f,  61.7f },  7.0f, 3800.0f, 60.0f, 0.35f, 0.3f, 0.15f, 0.0f, 0.8f },  // metallic flutter
    { "Abyssal Drone",            true,  { 311.0f, 433.0f, 547.0f, 691.0f }, 90.0f,  600.0f, 25.0f, 1.0f,  2.5f, 0.07f, 0.0f, 1.3f },  // bottomless, dark, slowly moving
    { "Burning City",             true,  {  97.0f, 131.0f, 173.0f, 223.0f }, 14.0f, 5000.0f, 120.0f, 0.9f, 0.8f, 0.4f,  6.0f, 0.9f },  // distorted, gritty, heat haze
    { "Flooded Undercroft",       true,  { 157.0f, 199.0f, 263.0f, 317.0f }, 22.0f,  350.0f, 30.0f, 1.0f,  4.0f, 0.21f, 0.0f, 1.4f },  // muffled underwater wobble
    { "Concrete Canyon",          true,  { 187.0f, 263.0f, 359.0f, 433.0f },  6.0f, 2500.0f, 80.0f, 0.1f,  0.0f, 0.0f,  0.0f, 0.9f },  // discrete slapback echoes
};

inline constexpr int kNumVoids = (int) (sizeof (kVoidProfiles) / sizeof (kVoidProfiles[0]));
inline constexpr float kMaxVoidDelayMs = 760.0f; // longest line + modulation headroom

inline const VoidProfile& getVoidProfile (int index) noexcept
{
    return kVoidProfiles[std::clamp (index, 0, kNumVoids - 1)];
}
/** One-line descriptions for the info panel (same order as kVoidProfiles). */
inline constexpr const char* kVoidBlurbs[] =
{
    "Open air. Energy that escapes through a breach is simply gone, so a broken room gets drier and shorter.",
    "A huge hall behind the walls. Every breach lets the sound spill into a long, smooth outer tail.",
    "A short metal corridor outside. Breaches add a tight, metallic flutter.",
    "A bottomless dark space with a 90-second, slowly drifting decay. Breaches feed an endless drone.",
    "A gritty, distorted city with a heat-haze wobble. Breaches bleed into a crunchy, saturated tail.",
    "Water-filled vaults. Escaping sound turns muffled and wobbles as if under water.",
    "Tall buildings far away. Breaches come back as separate slapback echoes rather than a wash.",
};
static_assert ((int) (sizeof (kVoidBlurbs) / sizeof (kVoidBlurbs[0])) == kNumVoids, "Void blurbs out of sync");

inline const char* getVoidBlurb (int index) noexcept { return kVoidBlurbs[std::clamp (index, 0, kNumVoids - 1)]; }
} // namespace rb::dsp
