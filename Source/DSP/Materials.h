#pragma once
//==============================================================================
// Material archetypes. Every value is a voicing constant, not a measured one.
//==============================================================================
#include <algorithm>
#include <array>

namespace rb::dsp
{
enum class Material : int
{
    ConcreteBunker = 0, CeramicChamber, CorrugatedSilo,
    // Dark cinematic set
    RustedHangar, BrutalistMonolith, ShatteredGlassAtrium, CathedralStone,
    SubwayTileTunnel, CrackedIceSheet, BurntTimberFrame, SunkenBulkhead,
    kCount
};

struct MaterialProfile
{
    const char* name;

    // Structural mechanics (StressTracker)
    float strengthScale;    // multiplies the user Yield Threshold
    float brittleness;      // 0..1 — how close to yield a transient must get to fracture
    float fatigueRate;      // integrity lost per second per unit of over-stress (plastic creep)
    float fractureChunk;    // integrity removed by a nominal brittle fracture

    // Acoustic surface (EarlyReflectionEngine + DynamicFDN)
    float reflectivity;     // pristine specular reflection coefficient
    float scatterExponent;  // specular fraction = integrity ^ scatterExponent
    float jitterMaxMs;      // reflection-time jitter of a fully cracked boundary
    float hfDampingHz;      // pristine high-shelf corner
    float hfShelfGain;      // HF gain per 50 ms of travel (pristine)
    std::array<float, 6> slabHz; // loose-slab resonance per wall (N, S, E, W, Ceiling, Floor)
    float slabQ;

    // Debris (ModalDebrisGenerator)
    float modeBaseHz;
    std::array<float, 8> modeRatios;
    float modeT60;          // seconds, lowest mode
    float pingHz;           // rebar ping / shard ring
    float pingT60;
    float rattleAmount;     // contact-distortion depth when slabs are loose
};

inline constexpr MaterialProfile kMaterialProfiles[] =
{
    { "Concrete Bunker",
      1.6f, 0.45f, 0.22f, 0.06f,
      0.92f, 1.2f, 2.5f, 3500.0f, 0.55f,
      { 68.0f, 74.0f, 81.0f, 59.0f, 96.0f, 52.0f }, 6.0f,
      180.0f, { 1.0f, 1.53f, 2.31f, 3.07f, 4.62f, 6.9f, 9.8f, 13.1f }, 0.35f,
      3100.0f, 0.9f, 0.4f },

    { "Ceramic Chamber",                       // shatters on transients, shrugs off LF
      0.9f, 0.9f, 0.07f, 0.10f,
      0.96f, 0.7f, 1.2f, 8000.0f, 0.8f,
      { 210.0f, 190.0f, 240.0f, 225.0f, 310.0f, 170.0f }, 10.0f,
      900.0f, { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f, 24.8f, 31.9f }, 0.6f,
      6200.0f, 0.45f, 0.7f },

    { "Corrugated Silo",                       // ductile: dents and groans under sustained LF
      1.1f, 0.15f, 0.9f, 0.03f,
      0.97f, 1.6f, 4.0f, 6000.0f, 0.75f,
      { 38.0f, 41.0f, 45.0f, 36.0f, 55.0f, 33.0f }, 14.0f,
      140.0f, { 1.0f, 1.41f, 2.13f, 2.87f, 3.92f, 5.11f, 6.37f, 8.06f }, 1.8f,
      1700.0f, 2.5f, 0.9f },

    //------------------------------------------------------------------ dark cinematic set
    { "Rusted Hangar",                         // vast corroded steel: deep dents, long metallic groans
      1.0f, 0.25f, 0.7f, 0.04f,
      0.95f, 1.4f, 5.0f, 4500.0f, 0.7f,
      { 30.0f, 34.0f, 37.0f, 28.0f, 44.0f, 26.0f }, 16.0f,
      95.0f, { 1.0f, 1.37f, 1.93f, 2.61f, 3.4f, 4.33f, 5.4f, 6.9f }, 2.8f,
      1250.0f, 3.5f, 1.0f },

    { "Brutalist Monolith",                    // enormous dark concrete: hard to break, falls like a mountain
      2.4f, 0.4f, 0.15f, 0.05f,
      0.94f, 1.3f, 1.8f, 1800.0f, 0.45f,
      { 42.0f, 46.0f, 50.0f, 39.0f, 58.0f, 35.0f }, 5.0f,
      70.0f, { 1.0f, 1.6f, 2.4f, 3.3f, 4.7f, 6.5f, 8.8f, 11.9f }, 0.6f,
      1900.0f, 1.2f, 0.3f },

    { "Shattered Glass Atrium",                // weakest, most brittle: explodes into bright shards
      0.6f, 1.0f, 0.04f, 0.14f,
      0.97f, 0.6f, 0.8f, 11000.0f, 0.85f,
      { 340.0f, 310.0f, 380.0f, 355.0f, 450.0f, 290.0f }, 12.0f,
      1400.0f, { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f, 16.0f, 19.8f }, 0.9f,
      8200.0f, 1.4f, 0.8f },

    { "Cathedral Stone",                       // dense limestone: grand, slightly bright, crumbles in blocks
      1.9f, 0.55f, 0.12f, 0.07f,
      0.95f, 1.1f, 1.5f, 5200.0f, 0.6f,
      { 55.0f, 61.0f, 66.0f, 50.0f, 48.0f, 44.0f }, 7.0f,
      120.0f, { 1.0f, 1.5f, 2.25f, 3.1f, 4.4f, 6.0f, 8.1f, 10.7f }, 0.8f,
      2600.0f, 0.8f, 0.25f },

    { "Subway Tile Tunnel",                    // tile over concrete: clacky, mid-forward, flutters
      1.2f, 0.7f, 0.12f, 0.08f,
      0.95f, 0.9f, 1.6f, 6500.0f, 0.7f,
      { 120.0f, 115.0f, 135.0f, 128.0f, 160.0f, 100.0f }, 9.0f,
      520.0f, { 1.0f, 2.1f, 3.6f, 5.2f, 7.4f, 9.9f, 12.6f, 15.9f }, 0.45f,
      4300.0f, 0.6f, 0.55f },

    { "Cracked Ice Sheet",                     // brittle and cold: fractures ring and "sing" for seconds
      0.8f, 0.95f, 0.05f, 0.09f,
      0.98f, 0.65f, 0.9f, 9500.0f, 0.88f,
      { 150.0f, 170.0f, 140.0f, 160.0f, 190.0f, 130.0f }, 18.0f,
      650.0f, { 1.0f, 1.73f, 2.91f, 4.2f, 5.85f, 7.7f, 9.9f, 12.4f }, 2.2f,
      5400.0f, 3.0f, 0.5f },

    { "Burnt Timber Frame",                    // charred wood: warm, absorbent, creaks and splinters
      0.8f, 0.3f, 0.8f, 0.05f,
      0.85f, 1.5f, 3.0f, 2200.0f, 0.5f,
      { 90.0f, 95.0f, 105.0f, 85.0f, 120.0f, 80.0f }, 4.0f,
      210.0f, { 1.0f, 1.8f, 2.7f, 3.9f, 5.3f, 7.0f, 9.1f, 11.6f }, 0.25f,
      1500.0f, 0.3f, 0.6f },

    { "Sunken Bulkhead",                       // submarine steel under pressure: dark, sonar pings, huge groans
      1.3f, 0.1f, 0.65f, 0.03f,
      0.96f, 1.7f, 6.0f, 1200.0f, 0.6f,
      { 24.0f, 27.0f, 29.0f, 22.0f, 33.0f, 20.0f }, 20.0f,
      60.0f, { 1.0f, 1.29f, 1.71f, 2.2f, 2.84f, 3.6f, 4.5f, 5.6f }, 4.0f,
      880.0f, 5.0f, 1.0f },
};

inline constexpr int kNumMaterials = (int) (sizeof (kMaterialProfiles) / sizeof (kMaterialProfiles[0]));
static_assert (kNumMaterials == (int) Material::kCount, "Material enum and profile table out of sync");

inline const MaterialProfile& getMaterialProfile (int index) noexcept
{
    return kMaterialProfiles[std::clamp (index, 0, kNumMaterials - 1)];
}
//==============================================================================
/** Impact voicing + a one-line description per material (same order as kMaterialProfiles).
    Kept in a parallel table so the acoustic table above stays readable.

    How a material breaks (ImpactGenerator):
      spreadMs     how long a crack takes to run through it: a fracture is a burst of
                   micro-fractures over this time, not one click (glass 3 ms, timber 60 ms)
      grains       how many micro-fractures and falling pieces it breaks into (1 = concrete)
      restitution  how far a falling piece bounces (0 = lands dead, 0.5 = steel plate)
      shatter      pieces that burst into tinkling fragments when they land (glass 1)
      chirp        dispersion: in a thin plate high frequencies travel faster, so a crack's
                   click arrives smeared highs-first, a "tsheww" (ice, a buckling steel sheet)
      plateHz      how bright the crack that drives the plate is
      plateMs      how far one crossing of the plate smears it (ms between the highs and ~400 Hz)
      plateRing    seconds the sheet goes on shimmering as the wave crosses and re-crosses it
      creakHz / creakQ                  what a slow, overloaded wall groans through
      creakRate    stick-slip rate: a creak is a train of tiny slips, heard as a pitch
      creakGrit    0 = a clean, pitched creak (steel, wood) .. 1 = grinding (stone, concrete) */
struct MaterialVoicing
{
    const char* blurb;
    float boomHz;      // where the thump of a fracture sits (the blast pulse and its rumble)
    float boomDecay;   // seconds the low rumble after it lasts (amplitude time constant)
    float crackHz;     // centre of the crack transient's noise band
    float crackMs;     // crack transient decay
    float rubbleHz;    // centre of falling-rubble grains

    float spreadMs, grains, restitution, shatter;
    float chirp, plateHz, plateMs, plateRing;
    float creakHz, creakQ, creakRate, creakGrit;
};

inline constexpr MaterialVoicing kMaterialVoicings[] =
{
    //                                                                                                                 boom   decay  crack    ms    rubble
    //   spread grains rest shatter | chirp  plateHz  ms    ring  | creak  Q    rate  grit
    { "Thick poured concrete. Cracks on loud peaks, sheds heavy chunks and rings with low, short modes.",              55.0f, 0.35f, 1800.0f, 18.0f,  700.0f,
       22.0f, 1.0f, 0.25f, 0.0f,    0.0f,     0.0f,   0.0f,   0.0f,   340.0f, 3.0f, 22.0f, 0.80f },
    { "Glazed tile over brick. Bright and smooth until a sharp transient chips it; shrugs off sustained bass.",        90.0f, 0.18f, 4200.0f,  9.0f, 2400.0f,
        6.0f, 0.9f, 0.35f, 0.6f,    0.25f, 9000.0f,   3.0f,  0.08f, 1400.0f, 6.0f, 45.0f, 0.50f },
    { "Thin ribbed steel. Hard to crack, but sustained sub-bass dents it into groaning, resonant panels.",             48.0f, 0.50f, 2600.0f, 25.0f, 1500.0f,
       14.0f, 0.6f, 0.50f, 0.0f,    0.70f, 6000.0f,  18.0f,  0.90f,  180.0f, 12.0f, 28.0f, 0.20f },
    { "Vast corroded steel shed. Deep dents, long metallic groans and a dark, wide tail.",                             40.0f, 0.60f, 2200.0f, 30.0f, 1200.0f,
       18.0f, 0.7f, 0.45f, 0.0f,    0.60f, 4500.0f,  24.0f,  1.20f,  120.0f, 14.0f, 18.0f, 0.25f },
    { "Enormous dark concrete. Takes huge pressure before it gives, then falls like a mountain.",                      34.0f, 0.80f, 1300.0f, 26.0f,  500.0f,
       35.0f, 1.2f, 0.20f, 0.0f,    0.0f,     0.0f,   0.0f,   0.0f,   220.0f, 2.5f, 14.0f, 0.90f },
    { "The weakest, most brittle surface. Explodes into bright shards on the first hard hit.",                        110.0f, 0.12f, 6500.0f,  7.0f, 4500.0f,
        3.0f, 1.4f, 0.30f, 1.0f,    0.35f, 12000.0f,  2.5f,  0.06f, 2500.0f, 8.0f, 60.0f, 0.30f },
    { "Dense limestone vaults. Grand and slightly bright; crumbles in blocks with stone-on-stone clatter.",            45.0f, 0.55f, 1500.0f, 22.0f,  800.0f,
       30.0f, 1.1f, 0.22f, 0.0f,    0.0f,     0.0f,   0.0f,   0.0f,   300.0f, 3.0f, 16.0f, 0.85f },
    { "Tile over concrete in a long tube. Clacky, mid-forward and fluttery; cracks with a hard tick.",                 62.0f, 0.30f, 3200.0f, 12.0f, 1900.0f,
        8.0f, 0.9f, 0.35f, 0.5f,    0.15f, 7000.0f,   3.0f,  0.10f,  900.0f, 4.0f, 35.0f, 0.60f },
    { "Brittle and cold. Fractures ring and sing for seconds, with glassy, high debris.",                              75.0f, 0.25f, 5200.0f, 14.0f, 3800.0f,
       10.0f, 0.9f, 0.30f, 0.7f,    1.00f, 8000.0f,  45.0f,  0.60f,  700.0f, 10.0f, 40.0f, 0.35f },
    { "Charred wood. Warm and absorbent; creaks, splinters and crackles instead of shattering.",                       70.0f, 0.22f,  900.0f, 35.0f, 1100.0f,
       60.0f, 1.3f, 0.40f, 0.0f,    0.0f,     0.0f,   0.0f,   0.0f,   650.0f, 7.0f, 24.0f, 0.35f },
    { "Submarine steel under water pressure. Dark and muffled, with sonar-like pings and huge groans.",                38.0f, 0.70f, 1100.0f, 40.0f,  450.0f,
       25.0f, 0.5f, 0.50f, 0.0f,    0.80f, 2500.0f,  30.0f,  1.60f,   90.0f, 16.0f, 12.0f, 0.20f },
};
static_assert ((int) (sizeof (kMaterialVoicings) / sizeof (kMaterialVoicings[0])) == kNumMaterials,
               "Material voicing table out of sync");

inline const MaterialVoicing& getMaterialVoicing (int index) noexcept
{
    return kMaterialVoicings[std::clamp (index, 0, kNumMaterials - 1)];
}
} // namespace rb::dsp
