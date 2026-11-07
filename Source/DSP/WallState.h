#pragma once
//==============================================================================
// The six boundaries and the damage "phases" every engine derives from integrity.
//
//   integrity 1.0 ── pristine ── 0.7 ── cracked ── 0.4 ── loose ── 0.15 ── breached ── 0.0 void
//
//   crackAmount     : jitter, specular→diffuse scatter, extra HF loss
//   loosenessAmount : resonant slab modes in the FDN, rattle in the debris engine
//   breachAmount    : HP venting, broadband loss, matrix bleed, exterior send
//==============================================================================
#include "DspUtils.h"
#include <array>

namespace rb::dsp
{
enum Wall : int { North = 0, South, East, West, Ceiling, Floor, kNumWalls };
using WallArray = std::array<float, kNumWalls>;

inline const char* wallName (int w) noexcept
{
    static constexpr const char* names[] = { "North", "South", "East", "West", "Ceiling", "Floor" };
    return names[w];
}

/** 0 = x (East/West), 1 = y (North/South), 2 = z (Ceiling/Floor) */
inline int axisOf (int w) noexcept     { return (w == East || w == West) ? 0 : (w == North || w == South) ? 1 : 2; }
inline int oppositeOf (int w) noexcept { return w ^ 1; } // N<->S, E<->W, C<->F by enum layout

inline float crackAmount (float I) noexcept     { return smoothstep (1.0f, 0.4f, I); }
inline float loosenessAmount (float I) noexcept { return smoothstep (0.75f, 0.4f, I) * (1.0f - smoothstep (0.25f, 0.05f, I)); }
inline float breachAmount (float I) noexcept    { return smoothstep (0.3f, 0.0f, I); }

struct FractureEvent
{
    int   wall = 0;
    float magnitude = 0.0f;  // ~0..1.5
    int   sampleOffset = 0;  // inside the current control block
    bool  brittle = true;    // false = ductile creak/groan
};
} // namespace rb::dsp
