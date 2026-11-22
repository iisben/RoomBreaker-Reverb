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

        // ---------------------------------------------------------- SIGNATURE
        // Each one is built around one behaviour of the structure, not just a sound.
        { "Signature", "Consonant Shards",
          "Glass that only the hardest consonants can break. Sung notes stay smooth; plosives and t's throw a shimmer of shards into a long tail.",
          GlassAtrium, 0.0f, 0.05f, 0.20f, Infinite, 1.5f, 0.80f, 2.2f, 0.70f, 0.18f },

        { "Signature", "Kick-Torn Cathedral",
          "Set Break by to Key and feed it the kick: every kick tears the nave open and the pad spills into the outer hall, then the stone seals again.",
          CathedralStone, -16.0f, 0.10f, 0.40f, Infinite, 0.8f, 1.60f, 5.5f, 0.25f, 0.45f },

        { "Signature", "Chorus Memory",
          "Takes 75 seconds to heal. The loud chorus wrecks it, the next verse plays in the drier, rougher wreck, and you hear it rebuild as the song goes on.",
          Concrete, -14.0f, 0.35f, 0.30f, FreeField, 75.0f, 1.20f, 2.6f, 0.45f, 0.30f },

        ruin ({ "Signature", "Growing Canyon",
                "Side walls already cracked onto a concrete canyon. Every big hit opens more of it for good, so the slapback echoes multiply as the track builds.",
                Brutalist, -20.0f, 0.20f, 0.30f, Canyon, Permadeath, 1.40f, 3.0f, 0.55f, 0.35f },
              { 1.00f, 1.00f, 0.42f, 0.48f, 1.00f, 1.00f }),

        { "Signature", "Garage Amp Shed",
          "A tiny steel shed with a driven room mic. Soft picking stays clean; dig in and the panels buckle, ping and rattle, then pop back.",
          Silo, -15.0f, 0.30f, 0.40f, FreeField, 0.6f, 0.45f, 0.55f, 0.50f, 0.30f },

        { "Signature", "Porcelain Dollhouse",
          "A toy-sized porcelain room over a bottomless dark. Accented notes chip it for good, and the cue sinks further into the abyss the longer it plays.",
          Ceramic, -14.0f, 0.10f, 0.50f, Abyssal, Permadeath, 0.40f, 2.0f, 0.75f, 0.40f },

        { "Signature", "High Vault Aftermath",
          "A vault twice the normal height. The hit lands, then the stone it shook loose takes up to a second to fall: the aftermath arrives on its own beat.",
          CathedralStone, -14.0f, 0.15f, 0.80f, Infinite, 30.0f, 2.00f, 7.0f, 0.80f, 0.45f },

        { "Signature", "Tile Snare Exciter",
          "Tiny tiled booth that heals in a quarter second. Accents crack the tiles, ghost notes don't, so every loud hit gets its own, never-repeating crack.",
          SubwayTile, -8.5f, 0.00f, 0.60f, FreeField, 0.25f, 0.40f, 0.35f, 0.80f, 0.30f },

        { "Signature", "Riser Tension",
          "A steel hangar that never gives way to a swell. As a riser climbs, it crackles and ticks under the load, louder and faster, until the drop's first hit breaks it.",
          RustedHangar, -13.0f, 0.00f, 0.30f, IndustrialTunnel, 6.0f, 1.30f, 3.5f, 0.60f, 0.35f },

        { "Signature", "Singing Lake Ice",
          "Open lake ice. Every hit sends the falling 'pew' of a crack running through a frozen sheet, then the surface refreezes in a couple of seconds.",
          CrackedIce, -4.0f, 0.05f, 0.40f, FreeField, 2.5f, 1.80f, 1.6f, 0.50f, 0.35f },

        { "Signature", "Controlled Demolition",
          "Music can't touch this monolith; only Detonate can. Each one is a rolling chain of charges, collapse and aftershocks, and it ends up open air.",
          Brutalist, -6.0f, 0.40f, 0.30f, FreeField, Permadeath, 1.80f, 4.5f, 0.90f, 0.35f },

        { "Signature", "Hearth Crackle",
          "Charred timber that splinters on every note and re-sets between them: the reverb crackles, pops and creaks like a fire, as busy as you play.",
          BurntTimber, -18.0f, 0.25f, 0.50f, BurningCity, 0.3f, 0.70f, 1.4f, 0.70f, 0.30f },

        ruin ({ "Signature", "Breath Abyss",
                "Almost nothing is left of this hull but the abyss behind it, ducked all the way: nothing smears the words, and the dark rises in every pause.",
                SunkenBulkhead, -3.0f, 0.50f, 0.30f, Abyssal, Permadeath, 1.60f, 7.0f, 0.30f, 0.40f },
              { 0.06f, 0.09f, 0.07f, 0.05f, 0.04f, 0.10f }),

        ruin ({ "Signature", "Sinkhole",
                "The floor has already caved into a flooded cavern: the sub's tail sinks and wobbles under water while the top stays a tight concrete room.",
                Concrete, -16.0f, 0.60f, 0.30f, Flooded, Permadeath, 1.20f, 3.2f, 0.50f, 0.30f },
              { 0.95f, 0.95f, 0.95f, 0.95f, 1.00f, 0.04f }),

        { "Signature", "Thunder Sheet",
          "A hanging steel sheet in a stone canyon. Claps, snares and footsteps make it roar and buckle, and the thunder rolls back off the canyon walls.",
          RustedHangar, -14.0f, 0.00f, 0.50f, Canyon, 0.5f, 0.80f, 2.5f, 0.60f, 0.40f },

        ruin ({ "Signature", "Rising From Rubble",
                "Starts as rubble in an open void and rebuilds itself while the music is quiet: across an intro, the space closes in into a bright, smooth room.",
                Ceramic, -2.0f, 0.20f, 0.30f, Infinite, 25.0f, 1.10f, 2.2f, 0.35f, 0.35f },
              { 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f }),

        { "Signature", "Booth to Stadium",
          "A dead booth with a stadium behind its walls. Verse-level hits stay tight; chorus hits breach it and the huge space blooms through, then it seals.",
          Concrete, -12.0f, 0.30f, 0.35f, Infinite, 8.0f, 0.50f, 0.6f, 0.40f, 0.30f },

        { "Signature", "Breakage Foley Printer",
          "100 % wet glass that breaks on every hit and re-glazes at once. Print a drum loop through it: a breaking-glass layer locked to the groove.",
          GlassAtrium, -16.0f, 0.05f, 0.60f, FreeField, 0.25f, 0.90f, 0.5f, 1.00f, 1.00f },

        { "Signature", "Glacier Groan",
          "Ice that only long, low notes can strain. Held bass and drones make the shelf groan and pop with falling whistles over a bottomless drone.",
          CrackedIce, -18.0f, 0.95f, 0.40f, Abyssal, 20.0f, 1.80f, 9.0f, 0.60f, 0.45f },

        { "Signature", "Distant Front Line",
          "Your sound a kilometre away: a quarter second of travel, the highs lost to distance, every hit answered by far-off blasts over a burning city.",
          Concrete, -18.0f, 0.30f, 0.50f, BurningCity, 10.0f, 2.00f, 4.0f, 0.60f, 0.80f },
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

        { "Consonant Shards",            { .impact = 0.35f, .rumble = 0.15f, .predelay = 20.0f, .density = 0.85f, .motion = 0.3f,
                                           .lowCut = 350.0f, .highCut = 16000.0f, .width = 1.4f, .duck = 0.35f } },
        { "Kick-Torn Cathedral",         { .impact = 0.15f, .rumble = 0.1f, .predelay = 35.0f, .density = 0.9f, .motion = 0.4f,
                                           .lowCut = 120.0f, .highCut = 10000.0f, .width = 1.5f } },
        { "Chorus Memory",               { .impact = 0.45f, .rumble = 0.35f, .density = 0.75f, .lowCut = 80.0f, .width = 1.2f } },
        { "Growing Canyon",              { .impact = 0.7f,  .rumble = 0.5f, .predelay = 12.0f, .density = 0.5f, .motion = 0.1f,
                                           .lowCut = 60.0f, .highCut = 12000.0f, .width = 1.3f } },
        { "Garage Amp Shed",             { .impact = 0.4f,  .rumble = 0.25f, .predelay = 2.0f, .density = 0.6f, .motion = 0.15f, .drive = 0.55f,
                                           .lowCut = 90.0f, .highCut = 7500.0f, .width = 0.8f, .duck = 0.15f } },
        { "Porcelain Dollhouse",         { .impact = 0.25f, .rumble = 0.35f, .density = 0.8f, .motion = 0.35f,
                                           .lowCut = 150.0f, .highCut = 18000.0f, .width = 1.3f } },
        { "High Vault Aftermath",        { .impact = 0.9f,  .rumble = 1.0f, .predelay = 25.0f, .density = 0.85f, .motion = 0.3f,
                                           .lowCut = 35.0f, .highCut = 14000.0f, .width = 1.4f } },
        { "Tile Snare Exciter",          { .impact = 0.6f,  .rumble = 0.2f, .density = 0.4f, .motion = 0.0f, .lowCut = 180.0f, .width = 1.1f } },
        { "Riser Tension",               { .impact = 0.85f, .rumble = 0.6f, .predelay = 10.0f, .motion = 0.2f, .drive = 0.15f,
                                           .lowCut = 40.0f, .highCut = 16000.0f, .width = 1.3f } },
        { "Singing Lake Ice",            { .impact = 0.9f,  .rumble = 0.15f, .predelay = 30.0f, .density = 0.3f, .motion = 0.1f,
                                           .lowCut = 200.0f, .width = 1.6f } },
        { "Controlled Demolition",       { .impact = 1.0f,  .rumble = 1.0f, .motion = 0.2f, .drive = 0.25f, .lowCut = 30.0f,
                                           .highCut = 15000.0f, .width = 1.5f } },
        { "Hearth Crackle",              { .impact = 0.55f, .rumble = 0.45f, .predelay = 5.0f, .density = 0.6f, .motion = 0.3f, .drive = 0.35f,
                                           .lowCut = 220.0f, .highCut = 9000.0f, .width = 1.4f } },
        { "Breath Abyss",                { .impact = 0.3f,  .rumble = 0.3f, .predelay = 60.0f, .density = 0.9f, .motion = 0.5f,
                                           .lowCut = 150.0f, .highCut = 6000.0f, .width = 1.6f, .duck = 1.0f } },
        { "Sinkhole",                    { .impact = 0.6f,  .rumble = 0.7f, .motion = 0.45f, .lowCut = 25.0f, .highCut = 12000.0f } },
        { "Thunder Sheet",               { .impact = 1.0f,  .rumble = 0.3f, .density = 0.5f, .motion = 0.2f, .drive = 0.2f,
                                           .lowCut = 60.0f, .highCut = 14000.0f, .width = 1.5f } },
        { "Rising From Rubble",          { .impact = 0.3f,  .rumble = 0.25f, .predelay = 15.0f, .density = 0.85f, .motion = 0.3f,
                                           .lowCut = 100.0f, .highCut = 15000.0f, .width = 1.3f } },
        { "Booth to Stadium",            { .impact = 0.5f,  .rumble = 0.4f, .density = 0.6f, .motion = 0.2f, .lowCut = 70.0f,
                                           .highCut = 16000.0f, .width = 1.3f } },
        { "Breakage Foley Printer",      { .impact = 1.0f,  .rumble = 0.8f, .density = 0.3f, .motion = 0.0f, .lowCut = 120.0f, .width = 1.2f } },
        { "Glacier Groan",               { .impact = 0.6f,  .rumble = 0.5f, .predelay = 20.0f, .density = 0.85f, .motion = 0.5f,
                                           .lowCut = 40.0f, .highCut = 12000.0f, .width = 1.6f } },
        { "Distant Front Line",          { .impact = 1.0f,  .rumble = 0.8f, .predelay = 250.0f, .density = 0.9f, .motion = 0.3f, .drive = 0.3f,
                                           .lowCut = 30.0f, .highCut = 2500.0f, .width = 1.6f } },
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

        { "Consonant Shards",            "Vocals,Fragile,Self-healing,Bright,Wide" },
        { "Kick-Torn Cathedral",         "Keys & Pads,Self-healing,Long tail,Wide" },
        { "Chorus Memory",               "Drums,Vocals,Self-healing" },
        { "Growing Canyon",              "Drums,Sound FX,Permadeath,Starts ruined,Long tail" },
        { "Garage Amp Shed",             "Bass,Short,Self-healing,Gritty,Metallic" },
        { "Porcelain Dollhouse",         "Keys & Pads,Sound FX,Fragile,Permadeath,Bright,Dark" },
        { "High Vault Aftermath",        "Sound FX,Drums,Long tail,Wide" },
        { "Tile Snare Exciter",          "Drums,Short,Self-healing,Bright" },
        { "Riser Tension",               "Sound FX,Metallic,Gritty" },
        { "Singing Lake Ice",            "Drums,Sound FX,Fragile,Self-healing,Bright,Wide" },
        { "Controlled Demolition",       "Sound FX,Permadeath,Long tail,Wide" },
        { "Hearth Crackle",              "Keys & Pads,Sound FX,Self-healing,Gritty" },
        { "Breath Abyss",                "Vocals,Starts ruined,Dark,Long tail" },
        { "Sinkhole",                    "Bass,Drums,Permadeath,Starts ruined,Dark" },
        { "Thunder Sheet",               "Sound FX,Drums,Self-healing,Metallic" },
        { "Rising From Rubble",          "Vocals,Keys & Pads,Starts ruined,Self-healing,Bright" },
        { "Booth to Stadium",            "Drums,Self-healing,Wide" },
        { "Breakage Foley Printer",      "Sound FX,Drums,Fragile,Short,Bright" },
        { "Glacier Groan",               "Bass,Keys & Pads,Long tail,Wide" },
        { "Distant Front Line",          "Sound FX,Drums,Long tail,Dark,Gritty" },
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
             p.decaySeconds, p.debris, p.mix, p.fx.impact, p.fx.rumble, p.fx.granular, p.fx.predelay, p.fx.density, p.fx.motion,
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
