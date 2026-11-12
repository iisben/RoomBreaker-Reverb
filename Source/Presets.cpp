#include "Presets.h"
#include "Parameters.h"
#include <string_view>

namespace rb
{
namespace
{
    constexpr int Concrete = 0, Ceramic = 1, Silo = 2,
                  RustedHangar = 3, Brutalist = 4, GlassAtrium = 5, CathedralStone = 6,
                  SubwayTile = 7, CrackedIce = 8, BurntTimber = 9, SunkenBulkhead = 10;
    constexpr int FreeField = 0, Infinite = 1,
                  IndustrialTunnel = 2, Abyssal = 3, BurningCity = 4, Flooded = 5, Canyon = 6;
    constexpr float Permadeath = kPermadeathSeconds;

    FactoryPreset ruin (FactoryPreset p, dsp::WallArray start)
    {
        p.hasStartState = true;
        p.startIntegrity = start;
        return p;
    }
}

const std::vector<FactoryPreset>& getFactoryPresets()
{
    //                         material  yield  bias  asym  void       heal        size  decay debris mix
    static const std::vector<FactoryPreset> presets = [] {
    std::vector<FactoryPreset> v
    {
        // ---------------------------------------------------------------- MIX
        { "Mix", "Default Bunker",
          "The factory default. A concrete room that cracks on loud peaks and heals in about 20 seconds.",
          Concrete, -12.0f, 0.35f, 0.30f, FreeField, 20.0f,      1.00f, 2.8f, 0.60f, 0.35f },

        { "Mix", "Vocal Plate (Unbreakable)",
          "Bright, smooth ceramic space that shrugs off vocal levels. Use it as a normal reverb on vocals and leads.",
          Ceramic,    0.0f, 0.20f, 0.10f, FreeField, 1.0f,       0.60f, 1.8f, 0.00f, 0.20f },

        { "Mix", "Ceramic Bathroom",
          "Small tiled room. Only the hardest hits chip a tile, and it repairs itself within a second or two.",
          Ceramic,   -4.0f, 0.30f, 0.20f, FreeField, 1.5f,       0.45f, 0.7f, 0.25f, 0.25f },

        // -------------------------------------------------------------- DRUMS
        { "Drums", "Drum Room - Hairline Cracks",
          "Tight room for drum kits. Loud hits crack the walls slightly, so the ambience moves with the performance.",
          Concrete, -12.0f, 0.30f, 0.30f, FreeField, 3.0f,       0.70f, 1.1f, 0.35f, 0.25f },

        { "Drums", "Snare Shatter Chamber",
          "Brittle chamber that splinters on snare cracks, adding glassy debris and a brighter, scattered tail.",
          Ceramic,   -6.0f, 0.10f, 0.50f, FreeField, 2.5f,       0.60f, 1.4f, 0.60f, 0.30f },

        { "Drums", "Breathing Bunker",
          "Heals almost instantly: the room gives way on every hit and rebuilds between them, so the reverb pumps with the groove.",
          Concrete, -19.0f, 0.50f, 0.20f, FreeField, 0.5f,       1.00f, 2.5f, 0.25f, 0.30f },

        { "Drums", "Gunshot Bunker",
          "Hard concrete slapback for gunshots, foley and percussive FX. Sharp transients knock chunks off with audible debris.",
          Concrete, -12.0f, 0.00f, 0.30f, FreeField, 5.0f,       0.80f, 1.8f, 0.70f, 0.40f },

        // --------------------------------------------------------------- BASS
        { "Bass", "Sub-Bass Silo Groan",
          "Ductile metal silo. Sustained 808s and sub bass slowly dent the walls into groaning, resonant panels.",
          Silo,     -12.0f, 0.85f, 0.40f, FreeField, 8.0f,       1.40f, 3.5f, 0.50f, 0.30f },

        { "Bass", "Underground Dub Bunker",
          "Long dark tail for dub sirens and bass stabs. Heavy low end vents through the cracks into an endless exterior echo.",
          Concrete, -18.0f, 0.70f, 0.30f, Infinite,  6.0f,       1.30f, 5.0f, 0.40f, 0.28f },

        // ---------------------------------------------------------- CINEMATIC
        { "Cinematic", "Trailer Hit Demolition",
          "Huge hall that caves in on big impacts: rebar pings, falling rubble and a distant outside tail. Press Rebuild between hits.",
          Concrete, -11.0f, 0.20f, 0.70f, Infinite,  30.0f,      2.00f, 6.0f, 0.90f, 0.45f },

        { "Cinematic", "Collapse to Open Sky",
          "Fragile and never heals. Whatever you play wears the room away until only open air (almost dry) remains. Rebuild to start over.",
          Concrete, -24.0f, 0.40f, 0.60f, FreeField, Permadeath, 1.60f, 5.0f, 0.80f, 0.40f },

        { "Cinematic", "Earthquake Room",
          "Driven by fatigue only: rumbles and long low notes shake the structure into continuous creaking and falling debris.",
          Silo,     -18.0f, 1.00f, 0.80f, FreeField, 12.0f,      1.80f, 4.0f, 1.00f, 0.45f },

        // ------------------------------------------------------------ AMBIENT
        ruin ({ "Ambient", "Bombed Cathedral",
                "Starts as a ruin: no roof, broken walls, open sky beyond. Sturdy enough not to crumble further. Great on pads and vocals.",
                Concrete, -6.0f, 0.40f, 0.50f, Infinite, Permadeath, 2.00f, 8.0f, 0.40f, 0.40f },
              { 0.15f, 0.60f, 0.05f, 0.40f, 0.00f, 0.90f }),

        ruin ({ "Ambient", "Haunted Silo",
                "Loose panels from the start: metallic resonances and rattles bloom under sustained sounds.",
                Silo, -3.0f, 0.50f, 0.40f, Infinite, Permadeath, 1.50f, 6.0f, 0.70f, 0.40f },
              { 0.50f, 0.55f, 0.45f, 0.60f, 0.50f, 0.65f }),

        ruin ({ "Ambient", "Infinite Void Drone",
                "The room is almost gone, so nearly everything leaks into the endless exterior tail. For drones and washed-out textures.",
                Concrete, -3.0f, 0.50f, 0.30f, Infinite, Permadeath, 2.00f, 12.0f, 0.20f, 0.60f },
              { 0.05f, 0.10f, 0.00f, 0.08f, 0.00f, 0.20f }),

        // ------------------------------------------------------- SOUND DESIGN
        { "Sound Design", "Tin Shed Rattle",
          "Small corrugated shed that buckles and rattles on anything loud, then pops back into shape.",
          Silo,     -14.0f, 0.50f, 0.60f, FreeField, 1.2f,       0.50f, 0.9f, 0.80f, 0.35f },

        { "Sound Design", "Slow Erosion (Permadeath)",
          "Wears down gradually over a whole song and never recovers. Automate Rebuild at section changes.",
          Concrete, -16.0f, 0.60f, 0.40f, FreeField, Permadeath, 1.20f, 3.0f, 0.50f, 0.35f },

        { "Sound Design", "Lopsided Vault",
          "Extreme asymmetry: the weak walls shatter first while the others hold, so the stereo image tilts as it breaks.",
          Ceramic,  -16.0f, 0.00f, 1.00f, FreeField, 6.0f,       1.00f, 2.2f, 0.70f, 0.35f },

        // ----------------------------------------------------- DARK CINEMATIC
        // Slow, heavy, distorted, enormous. Built on the dark material + void set.
        ruin ({ "Dark Cinematic", "Ash Cathedral",
                "Burnt-out stone nave open to a bottomless dark. Slow pads and vocals smear into an endless, low drone.",
                CathedralStone, -6.0f, 0.40f, 0.40f, Abyssal, Permadeath, 2.00f, 9.0f, 0.35f, 0.45f },
              { 0.35f, 0.70f, 0.20f, 0.50f, 0.10f, 0.95f }),

        { "Dark Cinematic", "Hangar Bass Dread",
          "Distorted sub and reese bass dent a rusted steel hangar; the groans leak into a gritty, burning exterior.",
          RustedHangar, -14.0f, 0.85f, 0.50f, BurningCity, 10.0f, 1.80f, 5.5f, 0.60f, 0.35f },

        { "Dark Cinematic", "Monolith Footsteps",
          "Massive brutalist hall for slow, heavy drums. Only the biggest hits break it, and broken walls throw slapback echoes across a concrete canyon.",
          Brutalist, -18.0f, 0.35f, 0.30f, Canyon, 15.0f, 2.00f, 7.0f, 0.70f, 0.35f },

        { "Dark Cinematic", "Drowned Signal",
          "Underwater steel hull: muffled and wobbling, with sonar-like pings and pressure groans when the low end pushes.",
          SunkenBulkhead, -16.0f, 0.70f, 0.40f, Flooded, 20.0f, 1.60f, 6.0f, 0.60f, 0.40f },

        { "Dark Cinematic", "Glass Rain Collapse",
          "A glass atrium that shatters on transients and rains bright shards into a long, cold tail. Rebuild for the next hit.",
          GlassAtrium, 0.0f, 0.00f, 0.60f, Infinite, 40.0f, 1.50f, 4.5f, 0.85f, 0.40f },

        ruin ({ "Dark Cinematic", "Black Ice Requiem",
                "Frozen chamber already split with fractures; sustained notes make the ice sing over an abyssal drone.",
                CrackedIce, 0.0f, 0.30f, 0.30f, Abyssal, Permadeath, 1.80f, 10.0f, 0.50f, 0.50f },
              { 0.55f, 0.60f, 0.50f, 0.62f, 0.48f, 0.70f }),

        { "Dark Cinematic", "Tunnel Pressure Wave",
          "Tiled transit tunnel with metallic flutter. Heavy kicks and risers crack the tiles and push a pressure wave down the tunnel.",
          SubwayTile, -12.0f, 0.50f, 0.50f, IndustrialTunnel, 6.0f, 1.20f, 3.5f, 0.60f, 0.35f },

        { "Dark Cinematic", "Burning Rafters",
          "Charred wooden frame that creaks, splinters and crackles under load, with a distorted, fire-lit city beyond.",
          BurntTimber, -10.0f, 0.60f, 0.40f, BurningCity, 25.0f, 1.40f, 4.0f, 0.80f, 0.40f },

        { "Dark Cinematic", "Sub Collapse Sequence",
          "Built for slow builds: sustained sub gradually crushes a concrete monolith until it drops into the abyss. Rebuild on the drop.",
          Brutalist, -24.0f, 1.00f, 0.60f, Abyssal, Permadeath, 2.00f, 8.0f, 0.70f, 0.40f },

        ruin ({ "Dark Cinematic", "Dread Engine Room",
                "Loose, rattling steel panels in a machine room. Gritty leads and synth stabs set the whole structure buzzing.",
                RustedHangar, -4.0f, 0.50f, 0.50f, IndustrialTunnel, Permadeath, 1.00f, 3.0f, 0.75f, 0.35f },
              { 0.50f, 0.45f, 0.55f, 0.50f, 0.60f, 0.42f }),
    };

    // v2 sound controls per preset (anything not named keeps its default).
    struct Entry { const char* name; PresetFx fx; };
    static const Entry kFx[] =
    {
        { "Default Bunker",              { .impact = 0.5f,  .rumble = 0.3f } },
        { "Vocal Plate (Unbreakable)",   { .impact = 0.0f,  .rumble = 0.0f, .predelay = 28.0f, .density = 0.9f, .motion = 0.35f,
                                           .lowCut = 160.0f, .highCut = 12000.0f, .duck = 0.3f } },
        { "Ceramic Bathroom",            { .impact = 0.3f,  .rumble = 0.1f, .lowCut = 80.0f } },
        { "Drum Room - Hairline Cracks", { .impact = 0.35f, .rumble = 0.15f, .predelay = 4.0f, .density = 0.6f, .motion = 0.15f, .lowCut = 60.0f } },
        { "Snare Shatter Chamber",       { .impact = 0.6f,  .rumble = 0.2f, .lowCut = 90.0f, .width = 1.2f } },
        { "Breathing Bunker",            { .impact = 0.3f,  .rumble = 0.15f, .duck = 0.35f } },
        { "Gunshot Bunker",              { .impact = 0.85f, .rumble = 0.4f, .density = 0.55f, .motion = 0.1f } },
        { "Sub-Bass Silo Groan",         { .impact = 0.4f,  .rumble = 0.6f, .motion = 0.3f, .lowCut = 25.0f } },
        { "Underground Dub Bunker",      { .impact = 0.4f,  .rumble = 0.3f, .predelay = 60.0f, .motion = 0.35f, .drive = 0.2f,
                                           .lowCut = 45.0f, .duck = 0.25f } },
        { "Trailer Hit Demolition",      { .impact = 1.0f,  .rumble = 0.8f, .predelay = 8.0f, .density = 0.8f, .width = 1.3f } },
        { "Collapse to Open Sky",        { .impact = 0.8f,  .rumble = 0.7f, .motion = 0.3f } },
        { "Earthquake Room",             { .impact = 0.7f,  .rumble = 1.0f, .motion = 0.5f } },
        { "Bombed Cathedral",            { .impact = 0.4f,  .rumble = 0.4f, .predelay = 40.0f, .density = 0.85f, .motion = 0.45f, .width = 1.3f } },
        { "Haunted Silo",                { .impact = 0.4f,  .rumble = 0.35f, .motion = 0.6f, .drive = 0.15f, .width = 1.2f } },
        { "Infinite Void Drone",         { .impact = 0.2f,  .rumble = 0.2f, .predelay = 20.0f, .density = 0.9f, .motion = 0.7f,
                                           .lowCut = 60.0f, .width = 1.5f } },
        { "Tin Shed Rattle",             { .impact = 0.5f,  .rumble = 0.3f, .lowCut = 70.0f } },
        { "Slow Erosion (Permadeath)",   { .impact = 0.5f,  .rumble = 0.6f, .motion = 0.3f } },
        { "Lopsided Vault",              { .impact = 0.5f,  .rumble = 0.3f, .width = 1.6f } },
        { "Ash Cathedral",               { .impact = 0.6f,  .rumble = 0.5f, .predelay = 30.0f, .density = 0.85f, .motion = 0.5f,
                                           .highCut = 9000.0f, .width = 1.4f } },
        { "Hangar Bass Dread",           { .impact = 0.8f,  .rumble = 0.7f, .motion = 0.3f, .drive = 0.35f, .lowCut = 30.0f } },
        { "Monolith Footsteps",          { .impact = 0.9f,  .rumble = 0.6f, .predelay = 15.0f, .density = 0.6f } },
        { "Drowned Signal",              { .impact = 0.6f,  .rumble = 0.5f, .predelay = 20.0f, .motion = 0.6f, .highCut = 4500.0f } },
        { "Glass Rain Collapse",         { .impact = 0.7f,  .rumble = 0.3f, .motion = 0.3f, .lowCut = 120.0f, .width = 1.5f } },
        { "Black Ice Requiem",           { .impact = 0.5f,  .rumble = 0.3f, .predelay = 25.0f, .density = 0.85f, .motion = 0.55f,
                                           .highCut = 11000.0f, .width = 1.6f } },
        { "Tunnel Pressure Wave",        { .impact = 0.85f, .rumble = 0.6f, .density = 0.6f, .drive = 0.15f } },
        { "Burning Rafters",             { .impact = 0.7f,  .rumble = 0.6f, .motion = 0.35f, .drive = 0.4f } },
        { "Sub Collapse Sequence",       { .impact = 1.0f,  .rumble = 0.9f, .motion = 0.3f, .lowCut = 25.0f } },
        { "Dread Engine Room",           { .impact = 0.6f,  .rumble = 0.4f, .drive = 0.5f, .lowCut = 50.0f, .duck = 0.2f } },
    };
    for (auto& p : v)
        for (const auto& e : kFx)
            if (std::string_view (p.name) == e.name)
                p.fx = e.fx;

    // Tags for the preset browser: what it is for, how it behaves, how it sounds.
    struct Tags { const char* name; const char* tags; };
    static const Tags kTags[] =
    {
        { "Default Bunker",              "Drums,Self-healing" },
        { "Vocal Plate (Unbreakable)",   "Vocals,Unbreakable,Bright" },
        { "Ceramic Bathroom",            "Vocals,Drums,Short,Self-healing,Bright" },
        { "Drum Room - Hairline Cracks", "Drums,Short,Self-healing" },
        { "Snare Shatter Chamber",       "Drums,Fragile,Bright,Wide" },
        { "Breathing Bunker",            "Drums,Self-healing" },
        { "Gunshot Bunker",              "Drums,Sound FX,Short" },
        { "Sub-Bass Silo Groan",         "Bass,Metallic" },
        { "Underground Dub Bunker",      "Bass,Long tail,Dark" },
        { "Trailer Hit Demolition",      "Sound FX,Drums,Long tail,Wide" },
        { "Collapse to Open Sky",        "Sound FX,Fragile,Permadeath" },
        { "Earthquake Room",             "Bass,Sound FX,Metallic" },
        { "Bombed Cathedral",            "Keys & Pads,Vocals,Starts ruined,Long tail,Wide" },
        { "Haunted Silo",                "Keys & Pads,Starts ruined,Metallic,Long tail" },
        { "Infinite Void Drone",         "Keys & Pads,Starts ruined,Long tail,Wide" },
        { "Tin Shed Rattle",             "Sound FX,Drums,Metallic,Short,Self-healing" },
        { "Slow Erosion (Permadeath)",   "Keys & Pads,Permadeath" },
        { "Lopsided Vault",              "Sound FX,Fragile,Wide" },
        { "Ash Cathedral",               "Keys & Pads,Vocals,Starts ruined,Dark,Long tail" },
        { "Hangar Bass Dread",           "Bass,Gritty,Metallic,Dark" },
        { "Monolith Footsteps",          "Drums,Dark,Long tail" },
        { "Drowned Signal",              "Sound FX,Dark,Metallic" },
        { "Glass Rain Collapse",         "Sound FX,Fragile,Bright,Wide" },
        { "Black Ice Requiem",           "Keys & Pads,Starts ruined,Long tail,Wide" },
        { "Tunnel Pressure Wave",        "Drums,Metallic,Gritty" },
        { "Burning Rafters",             "Sound FX,Gritty,Dark" },
        { "Sub Collapse Sequence",       "Bass,Permadeath,Dark" },
        { "Dread Engine Room",           "Keys & Pads,Starts ruined,Gritty,Metallic" },
    };
    for (auto& p : v)
        for (const auto& t : kTags)
            if (std::string_view (p.name) == t.name)
                p.tags = t.tags;
    return v;
    }();
    return presets;
}

std::array<float, kNumPresetParams> plainValues (const FactoryPreset& p)
{
    return { (float) p.material, p.yieldDb, p.bias, p.asymmetry, (float) p.voidType, p.healSeconds, p.size,
             p.decaySeconds, p.debris, p.mix, p.fx.impact, p.fx.rumble, p.fx.predelay, p.fx.density, p.fx.motion,
             p.fx.drive, p.fx.lowCut, p.fx.highCut, p.fx.width, p.fx.duck };
}

const juce::StringArray& presetTagVocabulary()
{
    static const juce::StringArray tags {
        "Vocals", "Drums", "Bass", "Keys & Pads", "Sound FX",                      // use
        "Unbreakable", "Fragile", "Self-healing", "Permadeath", "Starts ruined",    // behaviour
        "Short", "Long tail", "Dark", "Bright", "Metallic", "Gritty", "Wide" };    // sound
    return tags;
}
} // namespace rb
