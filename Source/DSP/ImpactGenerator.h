#pragma once
//==============================================================================
// ImpactGenerator — the physical sound of a structure failing.
//
// The modal debris engine gives each fracture its ringing pitch. This layer is
// everything else a real failure makes, voiced per material (MaterialVoicing) and
// scaled by how big the break is, so a hairline crack and a collapse are different
// sounds, not the same sound at two volumes:
//
//   Warning   before anything breaks, an overloaded wall crackles: sparse micro-
//             fractures whose rate climbs as the load nears what the wall can take
//             (acoustic emission). Each tick is the crack starting, then arresting a
//             fraction of a millisecond later. Like real materials (the Kaiser effect)
//             it only does so while the load climbs past what the wall recently
//             carried: a held note or a steady sweep is silent.
//   Crack     a fracture runs through the material instead of happening at once: a
//             main snap, then micro-fractures over the material's crack time, faint
//             ones far more common than loud ones, travelling across the stereo field.
//             Big breaks snap lower and run longer, with a crunch under them; small
//             ones tick. Every one is a broadband noise hit: the pitched ringing of the
//             material is the modal debris engine's job, so nothing here is a tone.
//   Plate     thin plates (ice, steel, glass) are dispersive: high frequencies travel
//             faster, so the crack's click arrives smeared highs-first. Modelled as the
//             wave itself: the click runs through a chain of allpass sections (the
//             dispersion of one crossing) inside a loop (the sheet's edges), so every
//             crossing smears it further and the echoes blur into a metallic shimmer.
//             There is no oscillator, so it cannot turn into a sine sweep.
//   Boom      the body of the hit: the pressure pulse of the impact (a Friedlander
//             wave: a sharp push, then a longer pull, no net offset) and the low rumble
//             of the structure settling after it. Noise, not a tone, so a collapse
//             thumps instead of playing a falling note. Mono, so the low end stays solid.
//   Debris    pieces come loose, fall and land: each one has a size (many small, a few
//             big), drops from a height set by Size and the wall it came from, and lands
//             after its fall time. An irregular piece does not land on one point: it
//             clicks, its body thuds a moment later, and it rocks onto another corner, so
//             a landing is a short clatter. Bounces follow 2·e·v/g and the material's
//             restitution. Glass and ice burst into fragments. Landings feed the rubble.
//   Settling  a big break is followed by aftershocks: smaller cracks whose rate falls
//             off as 1/t over a few seconds (Omori's law), as the structure re-seats.
//   Creak     slow, ductile failure: a stick-slip pulse train through the material's
//             resonance, a pitched creak in steel and wood, a grind in stone.
//   Rubble    the lingering bed and trickle of grains, driven by "rubble energy".
//
// Impact scales the warning, crack, plate, boom and settling; Rumble scales the
// falling debris and rubble; creaks take both. Every voice owns its RNG and every
// scheduled sound is timed from the control grid, so the output does not depend on
// how the host slices audio.
//==============================================================================
#include "DspUtils.h"
#include "Materials.h"
#include "WallState.h"

namespace rb::dsp
{
class ImpactGenerator
{
public:
    static constexpr int kNumBooms     = 4;
    static constexpr int kNumNoises    = 64;   // cracks, micro-fractures, landings, grains share one pool
    static constexpr int kNumCreaks    = 4;
    static constexpr int kNumSequences = 6;    // aftershock sequences
    static constexpr int kQueueSize    = 384;  // landings and bounces still in the air
    /** Longest a break keeps sounding after the input stops: aftershocks run 5 s, and rubble at
        its cap settles in about 6 s. The plugin adds this to the tail it reports to the host. */
    static constexpr float kMaxSettleSeconds = 7.0f;

    void prepare (double sampleRate);
    void reset();

    void setVoicing (const MaterialVoicing* v) noexcept { if (v != voicing) { voicing = v; tunePlate(); } }
    void setAmounts (float impact, float rumble) noexcept { impactAmount = clamp01 (impact); rumbleAmount = clamp01 (rumble); }
    /** Room Size (1 = default): sets how far pieces fall. */
    void setRoomSize (float s) noexcept { roomSize = std::clamp (s, 0.3f, 2.5f); }
    /** Control rate: stress / capacity per wall (all zero while frozen) and integrity, for the warning crackle. */
    void setStress (const WallArray& ratio, const WallArray& integrity) noexcept { stressRatio = ratio; integrityNow = integrity; }

    /** Control rate: one fracture / creak / detonation. sampleOffset may lie beyond this tick (a delayed charge). */
    void trigger (const FractureEvent& e) noexcept;

    /** Control rate: debris in the air, aftershocks, warning crackle, rubble. Call once per tick, after trigger(). */
    void update (float dtSeconds, int blockLength, float meanLooseness, float activity) noexcept;

    /** Renders into outL/outR (overwrites). */
    void process (float* outL, float* outR, int numSamples) noexcept;

    float rubbleEnergy() const noexcept { return rubble; }
    int piecesInTheAir() const noexcept { return numPending; }

private:
    struct Boom                           // blast pulse + the rumble after it
    {
        bool  active = false;
        int   startDelay = 0;
        float pulse = 0.0f, pulseT = 0.0f, pulseE = 1.0f, pulseR = 0.999f, pulseStep = 0.0f;   // A·(1 - t/T)·e^(-t/T)
        float amp = 0.0f, ampDecay = 0.999f, rumbleGain = 1.0f;   // rumble level, its decay, and noise → level
        float attack = 0.0f, attackInc = 0.01f;
        SvfTpt rumble;
        Rng   rng { 1u };
        int   age = 0;                    // samples since it started sounding
    };

    struct Noise
    {
        bool  active = false;
        int   startDelay = 0;
        SvfTpt band;
        float amp = 0.0f, decay = 0.99f;
        float panL = 0.707f, panR = 0.707f;
        Rng   rng { 1u };
    };

    /** A thin sheet. The crack's click runs through a chain of first-order allpasses with the
        pole near DC (low frequencies delayed most): that is one crossing, the direct sound, kept
        left/right. The sheet's reverberation is four crossing paths of different lengths mixed by
        a Hadamard matrix, each adding its own dispersion and losing highs on every pass, so the
        echoes smear further each time and blur into a dense metallic shimmer instead of a comb. */
    struct Plate
    {
        static constexpr int kMaxStages = 64;     // direct chain
        static constexpr int kPaths     = 4;
        static constexpr int kPathStages = 6;     // dispersion added per crossing
        static constexpr int kExcites   = 16;
        struct Excite
        {
            bool  active = false;
            int   delay = 0, left = 0;
            float amp = 0.0f, decay = 1.0f, panL = 0.707f, panR = 0.707f;
            Rng   rng { 1u };
        };
        std::array<Excite, kExcites> excites;
        std::array<float, kMaxStages> xL {}, yL {}, xR {}, yR {};
        std::array<std::array<float, kPathStages>, kPaths> px {}, py {};
        std::array<DelayLine, kPaths> paths;
        std::array<OnePoleTpt, kPaths> damp;
        std::array<int, kPaths> length {};
        std::array<float, kPaths> gain {};
        OnePoleTpt brightL, brightR;
        float a = -0.95f;
        int   stages = 0, quiet = 0;
        bool  idle = true;
    };

    struct Creak                          // stick-slip pulses through a resonance
    {
        bool  active = false;
        int   wall = 0, startDelay = 0, age = 0, hold = 0;
        float phase = 0.0f, rate = 20.0f, rateInc = 0.0f, rateLo = 5.0f, rateHi = 100.0f;
        float amp = 0.0f, env = 0.0f, grit = 0.5f, norm = 1.0f;
        float panL = 0.707f, panR = 0.707f;
        SvfTpt res;
        Rng   rng { 1u };
    };

    struct Pending                        // a piece in the air, or a bounce still to come
    {
        int   due = 0;                    // samples from the start of the current tick
        float hz = 1000.0f, q = 1.0f, amp = 0.0f, decayMs = 5.0f, pan = 0.0f;
        float thump = 0.0f;               // heavy pieces thud the floor
        float rubble = 0.0f;              // what the landing adds to the rubble energy
        float shatter = 0.0f;             // fragments it bursts into
    };

    struct Sequence                       // aftershocks of one big break
    {
        bool  active = false;
        int   wall = 0;
        float t = 0.0f, k = 0.0f;
    };

    Noise& allocateNoise() noexcept;
    void startBoom (int wall, float magnitude, float amp, int offset) noexcept;
    void startNoise (float hz, float q, float amp, float decayMs, int delay, float pan) noexcept;
    void excitePlate (float amp, float ms, int delay, float pan) noexcept;
    void tunePlate() noexcept;
    void clearPlate() noexcept;
    void processPlate (float* outL, float* outR, int numSamples) noexcept;
    void startCreak (int wall, float magnitude, int delay) noexcept;
    void crack (int wall, float magnitude, int offset, float level) noexcept;
    void dropPieces (int wall, float magnitude, int offset) noexcept;
    void schedule (const Pending&) noexcept;
    void land (const Pending&, int offset) noexcept;

    float fs = 48000.0f;
    const MaterialVoicing* voicing = nullptr;
    float impactAmount = 0.5f, rumbleAmount = 0.3f, roomSize = 1.0f;
    WallArray stressRatio {}, integrityNow { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    WallArray loadMemory {}, loadRise {};   // Kaiser effect: the load each wall recently carried, and how fast it is passing it

    std::array<Boom, kNumBooms> booms;
    std::array<Noise, kNumNoises> noises;
    Plate plate;
    std::array<Creak, kNumCreaks> creaks;
    std::array<Sequence, kNumSequences> sequences;
    std::array<Pending, kQueueSize> pending;
    int numPending = 0;

    float rubble = 0.0f;                 // rubble energy (0..~3)
    float bedAmp = 0.0f, bedAmpInc = 0.0f;
    SvfTpt bedL, bedR;
    Rng bedRngL { 0xB0DE1u }, bedRngR { 0xB0DE2u };
    OnePoleTpt dcL, dcR;
    Rng rng { 0x1A9AC7u };
};
} // namespace rb::dsp
