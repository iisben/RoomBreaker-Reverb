#pragma once
//==============================================================================
// Engine — owns the DSP systems and runs the RoomBreaker signal chain.
//
//                 ┌──────────── StressTracker (control rate) ─────────────┐
//   stress src ──►│ analyse → updateStructure / detonate → integrity[6]   │
//   (in / key)    └────┬──────────────┬───────────────────┬───────────────┘
//                      │ (smoothed)   │ events            │ events, shock
//   in ─► pre-delay ─► EarlyReflections   ModalDebris   ImpactGenerator
//                      │      │ (ER send)     │               │
//                      ├──────┴───────────────┴───────────────┴──► DynamicFDN ─► tank
//                      │                                              └─ vented ─► ExteriorTank
//                      ▼
//   wet = ER + tank + exterior + debris + impact ─► Drive ─► Low/High Cut
//         ─► Width (mono below 120 Hz) ─► Ducking ─► wet gain ─┐
//   dry ─────────────────────────────────────────── dry gain ──┴─► out
//
// Control runs on a fixed grid that carries across host buffers: a control tick
// fires every controlBlockSize() samples (64 at 44.1/48 kHz, scaled with the rate
// so the control rate stays ~750 Hz when oversampled), regardless of how the host
// slices audio, so output is bit-identical at any host buffer size.
//
// setMixDry (false) makes process() output the wet signal only; the plugin uses that
// to run the engine oversampled while the dry path stays at the host rate.
//==============================================================================
#include "DynamicFDN.h"
#include "EarlyReflectionEngine.h"
#include "ImpactGenerator.h"
#include "ModalDebrisGenerator.h"
#include "StressTracker.h"
#include "Telemetry.h"

namespace rb::dsp
{
enum class StressSource : int { Input = 0, Sidechain, Both };

struct EngineParams
{
    // Structure
    int   material     = 0;
    float yieldDb      = -12.0f;
    float ductileBias  = 0.35f;   // 0 brittle .. 1 ductile
    float asymmetry    = 0.3f;
    int   voidType     = 0;       // Exterior Void Type (index into kVoidProfiles)
    float healSeconds  = 20.0f;
    bool  permadeath   = false;
    int   stressSource = 0;       // StressSource

    // Destruction
    float impact       = 0.5f;
    float rumble       = 0.3f;
    float debrisLevel  = 0.6f;

    // Space
    float size         = 1.0f;
    float decaySeconds = 2.8f;
    float predelayMs   = 0.0f;
    float density      = 0.7f;
    float motion       = 0.25f;
    bool  freeze       = false;

    // Wet tone
    float drive        = 0.0f;
    float lowCutHz     = 20.0f;
    float highCutHz    = 20000.0f;
    float width        = 1.0f;    // 0 mono .. 2 extra wide
    float duck         = 0.0f;

    float mix          = 0.35f;
};

class Engine
{
public:
    static constexpr int kBaseControlBlock = 64;
    static constexpr int kMaxControlBlock  = 1024;
    static constexpr int kMaxEventsPerBlock = 16;
    static constexpr float kMaxPredelayMs  = 250.0f;

    Engine() noexcept
    {
        for (auto& a : publishedIntegrity) a.store (1.0f); // a never-prepared engine reports a pristine room
        for (auto& a : pendingRestore)     a.store (1.0f);
    }

    void prepare (double sampleRate);
    void reset();                    // all DSP state + structure
    void resetStructure() noexcept;  // Manual Reset: rebuild every wall (audio thread)
    void detonate() noexcept { detonateRequested = true; }   // audio thread; applied at the next control tick

    void setParams (const EngineParams& p) noexcept;  // audio thread, once per host block
    void setTelemetry (TelemetryBus* bus) noexcept { telemetry = bus; }
    void setMixDry (bool shouldMix) noexcept { mixDry = shouldMix; }

    /** In-place stereo processing. Input is the dry signal; output is dry + wet (or wet only,
        see setMixDry). keyL/keyR: optional sidechain at the same rate (nullptr = not connected). */
    void process (float* left, float* right, int numSamples,
                  const float* keyL = nullptr, const float* keyR = nullptr) noexcept;

    /** Message thread → audio thread: restore a saved structure (applied at the next block). */
    void requestStructureRestore (const WallArray& integrity) noexcept;

    /** Latest integrity, readable from any thread. */
    WallArray currentIntegrity() const noexcept;

    int controlBlockSize() const noexcept { return controlBlock; }
    const StressTracker& stressTracker() const noexcept { return stress; }

private:
    void controlTick() noexcept;
    void renderSlice (float* L, float* R, const float* keyL, const float* keyR, int n) noexcept;
    void applyPendingRestore() noexcept;

    double sampleRate = 48000.0;
    float  fs = 48000.0f;
    bool   prepared = false, mixDry = true, detonateRequested = false;
    int    controlBlock = kBaseControlBlock;

    EngineParams params;
    const MaterialProfile* material = &getMaterialProfile (0);

    StressTracker         stress;
    EarlyReflectionEngine er;
    DynamicFDN            fdn;
    ModalDebrisGenerator  debris;
    ImpactGenerator       impact;

    int tickPosition = 0;                 // samples into the current control tick
    WallArray smoothedIntegrity {};
    float sizeSmoothed = 1.0f, shock = 0.0f, freezeSmoothed = 0.0f;
    OnePoleSmoother mixSmoother, debrisSmoother;

    // Pre-delay (room input and stress source both go through it, so the walls react in sync)
    DelayLine preL, preR, preKeyL, preKeyR;
    // Changes crossfade between two taps (40 ms) instead of sliding the read head: no pitch warble, no click.
    float predelayCur = 2.0f, predelayNext = 2.0f, predelayFade = 0.0f, predelayFadeInc = 0.0f;
    float tapPre (DelayLine& d) const noexcept
    {
        const float a = d.tap (predelayCur);
        return predelayFadeInc > 0.0f ? a + predelayFade * (d.tap (predelayNext) - a) : a;
    }

    // Wet post chain (targets set per tick, ramped per sample)
    SvfTpt lowCutL, lowCutR, highCutL, highCutR;
    float lowCutSm = 20.0f, highCutSm = 20000.0f;
    OnePoleTpt sideLow;
    float driveGain = 1.0f, driveGainInc = 0.0f, driveMakeup = 1.0f, driveMakeupInc = 0.0f;
    float driveAmt = 0.0f;                         // glided Drive (0..1)
    float driveBlend = 0.0f, driveBlendInc = 0.0f; // shaped vs. clean: Drive fades in from true bypass
    float width = 1.0f, widthInc = 0.0f, widthSm = 1.0f;
    float duckAmt = 0.0f, duckAmtInc = 0.0f, duckAmtSm = 0.0f;
    float liveGain = 1.0f, liveGainInc = 0.0f;     // input mute while frozen, ramped per sample
    float duckEnv = 0.0f, duckAtt = 0.0f, duckRel = 0.0f, duckGainShown = 1.0f;

    // Per-control-block scratch (fixed size, no allocation).
    std::array<float, kMaxControlBlock> pdL {}, pdR {}, srcL {}, srcR {}, mono {}, erL {}, erR {}, debL {}, debR {},
                                        impL {}, impR {}, tankInL {}, tankInR {}, tankL {}, tankR {}, extL {}, extR {};

    // Telemetry
    TelemetryBus* telemetry = nullptr;
    WallArray flashAccumulator {};
    int samplesSincePublish = 0, publishInterval = 400;
    uint64_t samplePosition = 0;

    // Cross-thread state
    std::array<std::atomic<float>, kNumWalls> publishedIntegrity {};
    std::array<std::atomic<float>, kNumWalls> pendingRestore {};
    std::atomic<bool> restorePending { false };
};
} // namespace rb::dsp
