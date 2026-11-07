#pragma once
//==============================================================================
// APVTS layout + a lock-free per-block snapshot into rb::dsp::EngineParams.
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Engine.h"

namespace rb
{
namespace ids
{
    inline constexpr const char* material  = "material";
    inline constexpr const char* yield     = "yield";
    inline constexpr const char* bias      = "bias";
    inline constexpr const char* asymmetry = "asymmetry";
    inline constexpr const char* voidType  = "voidType";
    inline constexpr const char* heal      = "heal";
    inline constexpr const char* size      = "size";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* debris    = "debris";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* reset     = "reset";

    // v2
    inline constexpr const char* impact       = "impact";
    inline constexpr const char* rumble       = "rumble";
    inline constexpr const char* predelay     = "predelay";
    inline constexpr const char* density      = "density";
    inline constexpr const char* motion       = "motion";
    inline constexpr const char* freeze       = "freeze";
    inline constexpr const char* detonate     = "detonate";
    inline constexpr const char* drive        = "drive";
    inline constexpr const char* lowCut       = "lowcut";
    inline constexpr const char* highCut      = "highcut";
    inline constexpr const char* width        = "width";
    inline constexpr const char* duck         = "duck";
    inline constexpr const char* inputGain    = "input";
    inline constexpr const char* outputGain   = "output";
    inline constexpr const char* limiter      = "limiter";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* stressSource = "stresssrc";
}

/** Oversampling choices: index → factor. */
inline constexpr int kOversamplingFactors[] = { 1, 2, 4 };
inline constexpr int kNumOversampling = 3;

/** The top of the Tectonic Healing range means "never heal". */
inline constexpr float kPermadeathSeconds = 600.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Caches the APVTS atomics once; snapshot() is wait-free and safe on the audio thread. */
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& state);

    dsp::EngineParams snapshot() const noexcept;
    bool resetHeld() const noexcept { return reset->load (std::memory_order_relaxed) > 0.5f; }
    int voidIndex() const noexcept { return (int) std::lround (voidType->load (std::memory_order_relaxed)); }
    float decaySeconds() const noexcept { return decay->load (std::memory_order_relaxed); }
    bool detonateHeld() const noexcept { return detonate->load (std::memory_order_relaxed) > 0.5f; }
    bool limiterOn() const noexcept { return limiter->load (std::memory_order_relaxed) > 0.5f; }
    int oversamplingIndex() const noexcept { return std::clamp ((int) std::lround (oversampling->load (std::memory_order_relaxed)), 0, kNumOversampling - 1); }
    float inputGainDb() const noexcept { return inputGain->load (std::memory_order_relaxed); }
    float outputGainDb() const noexcept { return outputGain->load (std::memory_order_relaxed); }
    float mixAmount() const noexcept { return mix->load (std::memory_order_relaxed); }

private:
    std::atomic<float>* material  = nullptr;
    std::atomic<float>* yield     = nullptr;
    std::atomic<float>* bias      = nullptr;
    std::atomic<float>* asymmetry = nullptr;
    std::atomic<float>* voidType  = nullptr;
    std::atomic<float>* heal      = nullptr;
    std::atomic<float>* size      = nullptr;
    std::atomic<float>* decay     = nullptr;
    std::atomic<float>* debris    = nullptr;
    std::atomic<float>* mix       = nullptr;
    std::atomic<float>* reset     = nullptr;
    std::atomic<float>* impact = nullptr, * rumble = nullptr, * predelay = nullptr, * density = nullptr,
                      * motion = nullptr, * freeze = nullptr, * detonate = nullptr, * drive = nullptr,
                      * lowCut = nullptr, * highCut = nullptr, * width = nullptr, * duck = nullptr,
                      * inputGain = nullptr, * outputGain = nullptr, * limiter = nullptr,
                      * oversampling = nullptr, * stressSource = nullptr;
};
} // namespace rb
