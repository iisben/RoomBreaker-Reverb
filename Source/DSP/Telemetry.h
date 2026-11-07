#pragma once
//==============================================================================
// Telemetry — lock-free audio → UI hand-off for per-wall integrity.
//
// Three channels, all wait-free on the audio thread:
//   1. std::atomic<float> per wall — "latest value" for anything that just needs
//      a current reading (state saving, host thread, tooltips). Tearing *across*
//      walls is possible and harmless.
//   2. Triple buffer of TelemetryFrame — a consistent snapshot of all six walls,
//      published ~120 Hz. The UI always receives the NEWEST complete frame; frames
//      it never got to see are simply overwritten. Neither side ever blocks, and a
//      UI that was closed or stalled can never be shown stale data.
//      (A FIFO is the wrong shape here: with no editor open it fills with old frames
//      and then drops every new one, so the editor would open on stale state.)
//   3. std::atomic<float> fracture flash per wall — max-accumulated by the audio
//      thread, taken (exchange → 0) by the UI, so a crack is never lost even when
//      the frame that carried it is overwritten.
//==============================================================================
#include "WallState.h"
#include <atomic>
#include <cstdint>

namespace rb::dsp
{
struct TelemetryFrame
{
    WallArray integrity {};
    WallArray stressRatio {};
    WallArray fractureFlash {};   // max fracture magnitude per wall since the UI last looked
    float inputPeakDb  = -120.0f;
    float lfFatigue    = 0.0f;
    float matrixBleed  = 0.0f;
    int   activeDebris = 0;
    float rubble       = 0.0f;   // ImpactGenerator rubble energy (0..3)
    float shock        = 0.0f;   // room-flex envelope (0..~1.2)
    float duckGain     = 1.0f;   // current ducking gain on the wet signal
    bool  frozen       = false;
    uint64_t samplePosition = 0;
};

/** Single-producer / single-consumer triple buffer: latest value wins, wait-free both sides. */
template <typename T>
class TripleBuffer
{
public:
    void write (const T& value) noexcept                       // producer only
    {
        slots[backIndex] = value;
        const int previous = middle.exchange (backIndex | kFresh, std::memory_order_acq_rel);
        backIndex = previous & kIndexMask;
    }

    bool read (T& value) noexcept                               // consumer only
    {
        if ((middle.load (std::memory_order_relaxed) & kFresh) == 0)
            return false;
        const int previous = middle.exchange (frontIndex, std::memory_order_acq_rel);
        frontIndex = previous & kIndexMask;
        value = slots[frontIndex];
        return true;
    }

private:
    static constexpr int kFresh = 4, kIndexMask = 3;
    T slots[3] {};
    int backIndex = 0;                         // producer-owned
    int frontIndex = 1;                        // consumer-owned
    alignas (64) std::atomic<int> middle { 2 };
};

class TelemetryBus
{
public:
    static_assert (std::atomic<float>::is_always_lock_free, "atomic<float> must be lock-free");
    static_assert (std::atomic<int>::is_always_lock_free, "atomic<int> must be lock-free");

    TelemetryBus() noexcept
    {
        for (auto& a : latestIntegrity) a.store (1.0f);
        for (auto& a : pendingFlash)    a.store (0.0f);
    }

    // ---- audio thread
    void publish (const TelemetryFrame& frame) noexcept
    {
        for (size_t w = 0; w < (size_t) kNumWalls; ++w)
        {
            latestIntegrity[w].store (frame.integrity[w], std::memory_order_relaxed);

            // fetch-max: only the UI's exchange(0) can interleave, so this settles in 1–2 iterations.
            float current = pendingFlash[w].load (std::memory_order_relaxed);
            while (frame.fractureFlash[w] > current
                   && ! pendingFlash[w].compare_exchange_weak (current, frame.fractureFlash[w], std::memory_order_relaxed))
            {}
        }
        frames.write (frame);
    }

    // ---- any thread
    float integrity (int wall) const noexcept { return latestIntegrity[(size_t) wall].load (std::memory_order_relaxed); }

    // ---- message thread: newest frame, with every flash since the last call merged in
    bool popLatest (TelemetryFrame& out) noexcept
    {
        if (! frames.read (out))
            return false;
        for (size_t w = 0; w < (size_t) kNumWalls; ++w)
            out.fractureFlash[w] = pendingFlash[w].exchange (0.0f, std::memory_order_relaxed);
        return true;
    }

private:
    TripleBuffer<TelemetryFrame> frames;
    std::array<std::atomic<float>, kNumWalls> latestIntegrity;
    std::array<std::atomic<float>, kNumWalls> pendingFlash;
};
} // namespace rb::dsp
