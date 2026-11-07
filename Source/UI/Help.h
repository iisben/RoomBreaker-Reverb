#pragma once
//==============================================================================
// Text for the INFO panel. Keyed by parameter ID, plus a few non-parameter keys
// for the buttons and displays. Every control sets its key as the "info" property.
//==============================================================================
#include <juce_core/juce_core.h>

namespace rb::ui
{
struct HelpEntry
{
    const char* key;
    const char* title;
    const char* text;
};

inline constexpr HelpEntry kHelp[] =
{
    { "material", "Material",
      "What the walls are made of. Sets strength, how brittle they are, how fast they fatigue, how loose slabs ring, "
      "and the sound of the impact, debris and rubble. The room status (move the mouse off the controls) describes the current one." },
    { "voidType", "Exterior Void",
      "What is outside the walls. When a wall breaches, sound escapes into this space. Free Field Loss throws it away, "
      "so a broken room gets drier. The others collect it into a tail of their own: a hall, a tunnel, an abyss, a burning city, "
      "flooded vaults or a canyon." },
    { "yield", "Yield Threshold",
      "How loud the input must be before the walls start to give. Lower is a weaker room that breaks easily; at 0 dB only "
      "full-scale peaks do damage. Tip: set it just under your loudest hits so only the accents crack the room." },
    { "bias", "Brittle / Ductile",
      "What kind of energy does the damage. Brittle (0 %): sharp transients crack the walls in sudden fractures. "
      "Ductile (100 %): sustained low end below 120 Hz slowly fatigues and dents them." },
    { "asymmetry", "Asymmetry",
      "How uneven the construction is. At 0 % every wall is identical. Higher values make some walls much weaker than others, "
      "so the room breaks lopsided and the stereo image shifts as it falls apart." },
    { "heal", "Tectonic Healing",
      "How fast damaged walls repair once the input calms down. Short times make the room breathe with the music. "
      "At the top of the range (Permadeath) damage is permanent until you press REBUILD." },
    { "impact", "Impact",
      "The weight of each fracture: a sub boom that drops an octave, a crack transient panned to the wall that broke, "
      "and a brief flex of the room's pitch. 0 % keeps destruction subtle; 100 % is trailer-hit territory." },
    { "rumble", "Rumble",
      "Falling rubble after the hit: crumbling grains and a low rumble bed that build with every fracture and settle over "
      "a few seconds. Loose walls keep shedding dust while the music plays." },
    { "debris", "Debris",
      "Level of the ringing fragments: resonators tuned to the material, triggered by fractures, plus chatter and contact "
      "rattle from loose slabs. This is the pitched part of the destruction." },
    { "drive", "Drive",
      "Saturates the wet signal with an asymmetric, tube-style curve. Adds grit and density to the tail and makes collapses "
      "sound overloaded. It runs inside the oversampled engine, so it stays free of aliasing at 2x and 4x." },
    { "size", "Room Size",
      "Scales the room's geometry: reflection times, tank delay lengths and the pitch of loose-slab resonances. "
      "1.00x is a 14 x 10 x 5.5 m bunker." },
    { "decay", "Decay",
      "Reverb time (RT60) of the intact room. Damage changes it: loose slabs ring longer at their own pitch, while breaches "
      "leak energy and shorten the tail." },
    { "predelay", "Pre-Delay",
      "Delay before the room responds. The whole room is delayed, including its stress analysis, so cracks and booms land in "
      "time with the reverb. 10-40 ms keeps vocals and drums upfront." },
    { "density", "Density",
      "How quickly the tail becomes a smooth wash. High values diffuse the sound before it enters the tank (no flutter, no "
      "grain); low values keep a more discrete, rougher onset." },
    { "motion", "Motion",
      "Slow, random movement of the tank's delay lines. Removes metallic ringing and gives long tails a subtle, chorus-like life. "
      "Lower it for drums and tight rooms, raise it for pads and drones." },
    { "width", "Width",
      "Stereo width of the wet signal, from mono (0 %) to extra wide (200 %). Everything below 120 Hz always stays mono, "
      "so the low end is solid and mono-compatible." },
    { "lowcut", "Low Cut",
      "High-pass filter on the wet signal (12 dB/oct). Clears mud from the tail and the booms. The dry signal is never filtered." },
    { "highcut", "High Cut",
      "Low-pass filter on the wet signal (12 dB/oct). Darkens the room: try 4-8 kHz for distant, cinematic spaces." },
    { "mix", "Dry / Wet",
      "Balance between the dry signal and the room, with an equal-power crossfade. Use 100 % on a send or aux bus." },
    { "duck", "Ducking",
      "Turns the wet signal down while the dry signal plays and lets it bloom back in the gaps. Keeps vocals and leads clear "
      "without shortening the tail." },
    { "freeze", "Freeze",
      "Holds the current tail indefinitely and stops time for the structure: no new sound enters, and walls neither break nor heal. "
      "Play over a frozen collapse, then release." },
    { "detonate", "Detonate",
      "Makes every wall fail at once, with full impact, debris and rubble. Strong materials lose less. "
      "It is automatable, so you can place it on a hit in your arrangement." },
    { "reset", "Rebuild",
      "Restores every wall to pristine and clears the rubble. The rebuild glides in over a few milliseconds, so it never clicks. Automatable." },
    { "input", "Input Gain",
      "Level into the room. The walls react to level, so this also drives destruction: push it to break the room harder "
      "without touching Yield." },
    { "output", "Output Gain",
      "Final level after the dry/wet mix, before the safety limiter." },
    { "limiter", "Safety Limiter",
      "Soft-knee limiter on the output between -1 dBFS and 0 dBFS. Catches big collapses and detonations without touching "
      "normal levels. Leave it on unless you limit later in the chain." },
    { "oversampling", "Oversampling",
      "Runs the whole room engine at 2x or 4x the session rate. Drive and the Burning City grit stop aliasing, and moving "
      "delays keep more air. The dry signal stays at the session rate, so there is no added latency. "
      "The wet path costs roughly 2x / 4x the CPU. Switching lets the old tail ring out instead of cutting it." },
    { "stresssrc", "Stress Source",
      "What breaks the walls. Input: the signal being reverberated. Sidechain: another track (a kick, say) breaks the room "
      "while you hear the reverb of this one. Input + Sidechain: both together." },
    { "preset", "Preset",
      "28 factory rooms in 7 categories. Loading one sets every sound control and rebuilds the room; some presets start as ruins. "
      "Oversampling, gains, the limiter and the stress source are left as they are." },
    { "monitor", "The house",
      "The room, drawn as a house. The four walls are North, South (the front door), East and West; the roof is the Ceiling and "
      "the lawn is the Floor. Cracks start at the corners of doors and windows, the render falls away to show the material "
      "underneath, holes open, glass breaks, tiles slip, the chimney leans and the house settles off level. Fractures throw "
      "debris that stays on the lawn until the room heals or you press Rebuild. Walls tremble when loaded past yield. "
      "Drag to turn the house, double-click to face it again, and hover a wall for its numbers." },
    { "survey", "Wall survey",
      "Each wall's integrity (how much is still standing) and its current load against the yield point: the tick marks yield, "
      "so a bar past the tick is doing damage right now." },
    { "history", "Last 12 seconds",
      "Average integrity (light line), peak load (orange, with the yield level dashed) and fractures (ticks along the bottom)." },
    { "meters", "Level Meters",
      "Peak input (after Input Gain) and output, in dBFS." },
    { "status", "Engine Status",
      "Session sample rate, the rate the room engine runs at, the plugin's share of the audio thread's time budget, and "
      "whether a sidechain is connected." },
};

inline const HelpEntry* findHelp (const juce::String& key)
{
    for (const auto& h : kHelp)
        if (key == h.key)
            return &h;
    return nullptr;
}
} // namespace rb::ui
