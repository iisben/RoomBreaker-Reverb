#pragma once
//==============================================================================
// Factory presets. Exposed to hosts as programs and in the editor's preset bar.
// Loading a preset sets every parameter and rebuilds the room: pristine, or the
// preset's own starting damage (for presets that begin as ruins).
//==============================================================================
#include <vector>
#include "DSP/WallState.h"

namespace rb
{
/** v2 sound controls. Defaults equal the parameter defaults, so a preset only names what it changes.
    Global settings (oversampling, gains, limiter, stress source) are never touched by presets. */
struct PresetFx
{
    float impact   = 0.5f;
    float rumble   = 0.3f;
    float predelay = 0.0f;      // ms
    float density  = 0.7f;
    float motion   = 0.25f;
    float drive    = 0.0f;
    float lowCut   = 20.0f;     // Hz
    float highCut  = 20000.0f;  // Hz
    float width    = 1.0f;
    float duck     = 0.0f;
};

struct FactoryPreset
{
    const char* category;
    const char* name;
    const char* description;

    int   material;       // 0 Concrete Bunker, 1 Ceramic Chamber, 2 Corrugated Silo
    float yieldDb;
    float bias;           // 0 brittle .. 1 ductile
    float asymmetry;
    int   voidType;       // 0 Free Field Loss, 1 Infinite Secondary Tail
    float healSeconds;    // kPermadeathSeconds = never heals
    float size;
    float decaySeconds;
    float debris;
    float mix;

    PresetFx fx {};
    bool hasStartState = false;
    dsp::WallArray startIntegrity { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }; // N, S, E, W, Ceiling, Floor
};

const std::vector<FactoryPreset>& getFactoryPresets();
} // namespace rb
