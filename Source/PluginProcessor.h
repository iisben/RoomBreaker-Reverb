#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSP/Engine.h"
#include "DSP/Telemetry.h"

//==============================================================================
/** Signal flow at the host rate:

      in ─► input gain ─┬─────────────────────────────── dry ─────────────┐
                        └─► [up ×1/2/4] ─► rb::dsp::Engine (wet) ─► [down] ─┴─► + ─► output gain ─► limiter ─► out
      sidechain ─► (linear-interpolated to the engine rate) ─► stress source

    One Engine is prepared per oversampling factor, so switching never allocates on the
    audio thread: the new engine inherits the old one's damage and takes the input, while
    the old one rings out its existing tail (faded over 1.2 s). The dry path never goes through the resampling filters, so oversampling adds no
    latency to it; the wet path picks up a few samples of filter delay (less than the
    reverb's own pre-delay), so nothing is reported to the host. */
class RoomBreakerAudioProcessor final : public juce::AudioProcessor
{
public:
    RoomBreakerAudioProcessor();
    ~RoomBreakerAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Factory presets, exposed to the host as programs.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;   // sets every sound parameter + rebuilds the room
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    rb::dsp::TelemetryBus telemetry;          // audio → UI, lock-free

    // UI read-outs (relaxed atomics, written by the audio thread)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, keyPeak { 0.0f };
    std::atomic<float> preLimiterPeak { 0.0f };   // held max before the safety limiter (tests / diagnostics)
    std::atomic<bool>  sidechainConnected { false };
    std::atomic<int>   activeFactor { 1 };
    std::atomic<int>   detonateCount { 0 };
    float getCpuLoad() const { return (float) loadMeasurer.getLoadAsProportion(); }
    double getHostSampleRate() const { return hostRate; }

private:
    void renderWet (int engineIndex, const float* dryL, const float* dryR,
                    const float* keyL, const float* keyR, float* wetL, float* wetR, int n);
    void startOversamplingSwitch (int newIndex);
    void restoreAllEngines (const rb::dsp::WallArray& integrity);

    rb::ParameterCache params;
    std::array<rb::dsp::Engine, rb::kNumOversampling> engines;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, rb::kNumOversampling> oversamplers; // [0] unused
    int activeIndex = 0, fadingIndex = -1, fadePos = 0, fadeLength = 1;

    juce::AudioBuffer<float> dryBuf, wetBuf, fadeBuf, silenceBuf, keyBuf, keyUpBuf;
    float lastKeyL = 0.0f, lastKeyR = 0.0f;
    int maxChunk = 512;
    double hostRate = 48000.0;

    juce::SmoothedValue<float> inGain, outGain, dryGain;
    bool lastResetState = false, lastDetonateState = false;
    int currentProgram = 0;
    juce::AudioProcessLoadMeasurer loadMeasurer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomBreakerAudioProcessor)
};
