#pragma once
//==============================================================================
// What the displays draw, refreshed from the TelemetryBus by the editor timer
// (message thread only).
//==============================================================================
#include "../DSP/Telemetry.h"
#include <algorithm>
#include <array>

namespace rb::ui
{
struct LiveState
{
    static constexpr int kHistory = 360;   // 12 s at 30 Hz

    dsp::TelemetryFrame frame;
    dsp::WallArray flash {};             // decaying highlight per wall (0..1)
    dsp::WallArray pendingFracture {};   // fractures not yet turned into debris by the house view
    std::array<float, kHistory> histIntegrity {}, histStress {}, histFracture {};
    int histPos = 0;
    float inMeter = 0.0f, outMeter = 0.0f, keyMeter = 0.0f, cpu = 0.0f;

    LiveState() { histIntegrity.fill (1.0f); frame.integrity.fill (1.0f); }

    void push (const dsp::TelemetryFrame& f)
    {
        frame = f;
        float fracture = 0.0f;
        for (size_t w = 0; w < (size_t) dsp::kNumWalls; ++w)
        {
            const float m = std::clamp (f.fractureFlash[w], 0.0f, 1.5f);
            flash[w] = std::max (flash[w], std::min (m, 1.0f));
            pendingFracture[w] = std::max (pendingFracture[w], m);
            fracture = std::max (fracture, m);
        }
        histPos = (histPos + 1) % kHistory;
        histIntegrity[(size_t) histPos] = meanIntegrity();
        histStress[(size_t) histPos] = maxStress();
        histFracture[(size_t) histPos] = fracture;
    }

    void tick()
    {
        for (auto& f : flash)
            f = std::max (0.0f, f - 0.08f);
    }

    float meanIntegrity() const
    {
        float m = 0.0f;
        for (float I : frame.integrity) m += I / (float) dsp::kNumWalls;
        return m;
    }

    float maxStress() const
    {
        float m = 0.0f;
        for (float s : frame.stressRatio) m = std::max (m, s);
        return m;
    }
};
} // namespace rb::ui
