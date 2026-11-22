#pragma once
//==============================================================================
// Text for the info line in the footer. Keyed by parameter ID, plus a few non-parameter keys
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
      "and the sound of the impact, debris and rubble. The house is drawn in the same material." },
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
      "The weight of each fracture: the thump and low rumble of the hit, a crack that runs through the wall as a series "
      "of snaps, and in ice, glass and steel the shimmer of the sheet as the crack smears across it. Warning ticks sound "
      "while a wall is loaded past what it recently carried. 0 % keeps destruction subtle; 100 % is trailer-hit territory." },
    { "rumble", "Rumble",
      "Pieces letting go, falling for the time the room's height allows, then hitting and bouncing. Ice and glass tick "
      "and shatter, steel clangs, stone thuds. Loose walls keep shedding dust while the music plays." },
    { "granular", "Granular",
      "Pitch-shifted grains of the cracks and the debris. At 0 % the destruction is untouched. Turn it up and breaks throw "
      "short grains: ice and glass flick upward, steel drops and rings, rubble crunches downward. It only touches the "
      "breaking, not the reverb of the room." },
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
    { "preset", "Presets",
      "Click the name to browse: search by name, tag or description, narrow by category and tags, and each preset loads as you "
      "select it. Arrows step through all presets. A dot before the name means a control has moved since it was loaded. "
      "Presets change the sound only: gains, oversampling, the limiter and the stress source stay as they are." },
    { "presetFolder", "Presets Folder",
      "Opens the folder your own presets are saved in (.rbpreset files). Copy files in or out to share them; the browser "
      "picks them up the next time it opens." },
    { "monitor", "The House",
      "The room, drawn as a house: four walls (South has the door), the roof is the ceiling and the lawn is the floor. Cracks, "
      "spalling, holes and collapse follow the Material; the sky follows Outside. Drag to turn it, double-click to face it again, "
      "and hover a surface to read its condition." },
    { "condition", "Condition",
      "How much of the room is standing, and each surface on its own: North, South, East, West, Roof, Floor. The tick on each bar "
      "is its load against the yield threshold: past the end of the bar it is taking damage now." },
    { "meters", "Meters",
      "Stereo peak (light) and RMS (solid) from -60 to +6 dBFS, with a 1.5 s peak hold. Input is measured after Input gain, "
      "output after the limiter. The number is the highest peak so far and turns red at 0 dBFS. Click a meter to reset it." },
    { "bypass", "Bypass",
      "Fades to the untouched input over 20 ms. The room stops listening while bypassed, so it neither rings on nor breaks. "
      "This is the same switch as the host's bypass." },
    { "scale", "Interface Size",
      "Sets the size of the window, from 75 % to 200 %. You can also drag the corner. The size is saved with the session." },
    { "status", "Engine",
      "Session sample rate, the rate the room engine runs at, and the plug-in's share of the audio thread's time budget." },
};

inline const HelpEntry* findHelp (const juce::String& key)
{
    for (const auto& h : kHelp)
        if (key == h.key)
            return &h;
    return nullptr;
}
} // namespace rb::ui
