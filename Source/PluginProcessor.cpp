#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    const juce::Identifier kStructureTag { "Structure" };
    const juce::Identifier kPresetProperty { "preset" };
    juce::Identifier wallKey (int w) { return juce::Identifier (juce::String ("wall") + juce::String (w)); }

    constexpr double kTailHandoverSeconds = 1.2;   // outgoing engine's tail fades out over this
    constexpr float  kLimiterThreshold  = 0.891f;  // -1 dBFS: soft knee from here up to 0 dBFS

    inline float softLimit (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kLimiterThreshold)
            return x;
        constexpr float room = 1.0f - kLimiterThreshold;
        return std::copysign (kLimiterThreshold + room * std::tanh ((a - kLimiterThreshold) / room), x);
    }
}

RoomBreakerAudioProcessor::RoomBreakerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "RoomBreaker", rb::createParameterLayout()),
      params (apvts)
{
    for (auto& e : engines)
        e.setMixDry (false);   // dry is mixed here, at the host rate
    activeIndex = params.oversamplingIndex();
    engines[(size_t) activeIndex].setTelemetry (&telemetry);
}

bool RoomBreakerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo out always (the image-source pan law and wall map are stereo); mono or stereo in.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // Optional sidechain: off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void RoomBreakerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // All allocation happens here, for every oversampling factor up front.
    hostRate = sampleRate;
    maxChunk = juce::jlimit (32, 4096, samplesPerBlock > 0 ? samplesPerBlock : 512);

    for (int i = 0; i < rb::kNumOversampling; ++i)
    {
        const int factor = rb::kOversamplingFactors[i];
        if (i > 0)
        {
            oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
                2, (size_t) juce::roundToInt (std::log2 (factor)),
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            oversamplers[(size_t) i]->initProcessing ((size_t) maxChunk);
        }
        engines[(size_t) i].setParams (params.snapshot());
        engines[(size_t) i].prepare (sampleRate * factor);
    }

    dryBuf.setSize (2, maxChunk);
    wetBuf.setSize (2, maxChunk);
    fadeBuf.setSize (2, maxChunk);
    silenceBuf.setSize (2, maxChunk);
    silenceBuf.clear();
    keyBuf.setSize (2, maxChunk);
    keyUpBuf.setSize (2, maxChunk * rb::kOversamplingFactors[rb::kNumOversampling - 1]);
    lastKeyL = lastKeyR = 0.0f;

    inGain.reset (sampleRate, 0.03);
    outGain.reset (sampleRate, 0.03);
    dryGain.reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb()));
    dryGain.setCurrentAndTargetValue (std::cos (params.mixAmount() * juce::MathConstants<float>::halfPi));

    for (auto& e : engines) e.setTelemetry (nullptr);
    activeIndex = params.oversamplingIndex();
    fadingIndex = -1;
    engines[(size_t) activeIndex].setTelemetry (&telemetry);
    activeFactor.store (rb::kOversamplingFactors[activeIndex]);

    lastResetState = params.resetHeld();
    lastDetonateState = params.detonateHeld();
    loadMeasurer.reset (sampleRate, maxChunk);
}

double RoomBreakerAudioProcessor::getTailLengthSeconds() const
{
    // Room tail, or the exterior void's own RT60 when it collects energy (Abyssal Drone: 90 s).
    const double roomTail = (double) params.decaySeconds() * 1.5 + 2.0 + 0.25;
    const auto& exteriorVoid = rb::dsp::getVoidProfile (params.voidIndex());
    return exteriorVoid.collects ? std::max (roomTail, (double) exteriorVoid.rt60 * 1.2) : roomTail;
}

//==============================================================================
void RoomBreakerAudioProcessor::restoreAllEngines (const rb::dsp::WallArray& integrity)
{
    for (auto& e : engines)
        e.requestStructureRestore (integrity);
}

void RoomBreakerAudioProcessor::startOversamplingSwitch (int newIndex)
{
    // Audio thread. The incoming engine starts clean but keeps the room's damage and takes the
    // input from now on. The outgoing one stops receiving input but keeps rendering, so the tail
    // that was already ringing carries on (fading out over ~1 s) instead of being cut off.
    auto& incoming = engines[(size_t) newIndex];
    const auto integrity = engines[(size_t) activeIndex].currentIntegrity();
    incoming.setParams (params.snapshot());
    incoming.reset();
    incoming.requestStructureRestore (integrity);
    if (oversamplers[(size_t) newIndex] != nullptr)
        oversamplers[(size_t) newIndex]->reset();

    engines[(size_t) activeIndex].setTelemetry (nullptr);
    incoming.setTelemetry (&telemetry);

    fadingIndex = activeIndex;
    activeIndex = newIndex;
    fadePos = 0;
    fadeLength = std::max (1, (int) (hostRate * kTailHandoverSeconds));
    activeFactor.store (rb::kOversamplingFactors[newIndex], std::memory_order_relaxed);
}

void RoomBreakerAudioProcessor::renderWet (int index, const float* dryL, const float* dryR,
                                           const float* keyL, const float* keyR, float* wetL, float* wetR, int n)
{
    auto& engine = engines[(size_t) index];
    const int factor = rb::kOversamplingFactors[index];

    if (factor == 1)
    {
        std::copy (dryL, dryL + n, wetL);
        std::copy (dryR, dryR + n, wetR);
        engine.process (wetL, wetR, n, keyL, keyR);
        return;
    }

    // Up-sample the dry signal, run the room at the higher rate, filter back down.
    float* in[] = { const_cast<float*> (dryL), const_cast<float*> (dryR) };
    juce::dsp::AudioBlock<float> dryBlock (in, 2, (size_t) n);
    auto& os = *oversamplers[(size_t) index];
    auto up = os.processSamplesUp (dryBlock);
    const int upN = (int) up.getNumSamples();

    // The key only drives the stress analysis (peaks + sub-120 Hz energy), so linear
    // interpolation is plenty and costs no latency.
    const float* upKeyL = nullptr;
    const float* upKeyR = nullptr;
    if (keyL != nullptr && keyR != nullptr)
    {
        auto* kl = keyUpBuf.getWritePointer (0);
        auto* kr = keyUpBuf.getWritePointer (1);
        float pl = lastKeyL, pr = lastKeyR;
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < factor; ++j)
            {
                const float t = (float) (j + 1) / (float) factor;
                kl[i * factor + j] = pl + (keyL[i] - pl) * t;
                kr[i * factor + j] = pr + (keyR[i] - pr) * t;
            }
            pl = keyL[i]; pr = keyR[i];
        }
        upKeyL = kl; upKeyR = kr;
    }

    engine.process (up.getChannelPointer (0), up.getChannelPointer (1), upN, upKeyL, upKeyR);

    float* out[] = { wetL, wetR };
    juce::dsp::AudioBlock<float> wetBlock (out, 2, (size_t) n);
    os.processSamplesDown (wetBlock);
}

//==============================================================================
void RoomBreakerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // FTZ/DAZ: the FDN and resonators decay into denormals otherwise
    juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer (loadMeasurer, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut < 2)
        return;

    // Sidechain first: with a mono main input its channel shares index 1 with the right output.
    const auto* scBus = getBus (true, 1);
    const bool haveKey = scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;
    sidechainConnected.store (haveKey, std::memory_order_relaxed);
    const int mainIn = getBus (true, 0) != nullptr ? getBus (true, 0)->getNumberOfChannels() : 2;

    // One wait-free parameter snapshot per host block; the engines smooth internally.
    const auto snapshot = params.snapshot();

    // Oversampling change → crossfade to the engine prepared for that rate.
    if (const int wanted = params.oversamplingIndex(); wanted != activeIndex && fadingIndex < 0)
        startOversamplingSwitch (wanted);

    // Momentary buttons — edge triggered so a held or automated "on" only fires once.
    const bool resetNow = params.resetHeld();
    if (resetNow && ! lastResetState)
        for (auto& e : engines) e.resetStructure();
    lastResetState = resetNow;

    const bool detonateNow = params.detonateHeld();
    if (detonateNow && ! lastDetonateState)
    {
        engines[(size_t) activeIndex].detonate();
        detonateCount.fetch_add (1, std::memory_order_relaxed);
    }
    lastDetonateState = detonateNow;

    engines[(size_t) activeIndex].setParams (snapshot);
    if (fadingIndex >= 0)
        engines[(size_t) fadingIndex].setParams (snapshot);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb()));
    dryGain.setTargetValue (std::cos (snapshot.mix * juce::MathConstants<float>::halfPi));
    const bool limiterOn = params.limiterOn();

    auto scBuffer = haveKey ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    float peakIn = 0.0f, peakOut = 0.0f, peakKey = 0.0f, peakRaw = 0.0f;

    for (int offset = 0; offset < numSamples; offset += maxChunk)
    {
        const int n = std::min (maxChunk, numSamples - offset);
        auto* dl = dryBuf.getWritePointer (0);
        auto* dr = dryBuf.getWritePointer (1);

        // Dry input (mono → centred stereo) with input gain.
        for (int i = 0; i < n; ++i)
        {
            const float g = inGain.getNextValue();
            const float l = buffer.getSample (0, offset + i);
            const float r = mainIn > 1 ? buffer.getSample (1, offset + i) : l;
            dl[i] = l * g;
            dr[i] = r * g;
            peakIn = std::max (peakIn, std::max (std::abs (dl[i]), std::abs (dr[i])));
        }

        const float* kl = nullptr;
        const float* kr = nullptr;
        if (haveKey)
        {
            auto* bl = keyBuf.getWritePointer (0);
            auto* br = keyBuf.getWritePointer (1);
            const int scCh = scBuffer.getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                bl[i] = scBuffer.getSample (0, offset + i);
                br[i] = scCh > 1 ? scBuffer.getSample (1, offset + i) : bl[i];
                peakKey = std::max (peakKey, std::max (std::abs (bl[i]), std::abs (br[i])));
            }
            kl = bl; kr = br;
        }

        auto* wl = wetBuf.getWritePointer (0);
        auto* wr = wetBuf.getWritePointer (1);
        renderWet (activeIndex, dl, dr, kl, kr, wl, wr, n);

        if (fadingIndex >= 0)
        {
            auto* fl = fadeBuf.getWritePointer (0);
            auto* fr = fadeBuf.getWritePointer (1);
            renderWet (fadingIndex, silenceBuf.getReadPointer (0), silenceBuf.getReadPointer (1), nullptr, nullptr, fl, fr, n);
            for (int i = 0; i < n; ++i)
            {
                const float t = std::min (1.0f, (float) (fadePos + i) / (float) fadeLength);
                const float b = 0.5f + 0.5f * std::cos (t * juce::MathConstants<float>::pi);   // raised cosine
                wl[i] += b * fl[i];
                wr[i] += b * fr[i];
            }
            fadePos += n;
            if (fadePos >= fadeLength)
            {
                engines[(size_t) fadingIndex].reset();   // drop the old tail so a later switch back starts clean
                fadingIndex = -1;
            }
        }

        if (haveKey) { lastKeyL = kl[n - 1]; lastKeyR = kr[n - 1]; }

        for (int i = 0; i < n; ++i)
        {
            const float dg = dryGain.getNextValue();
            const float og = outGain.getNextValue();
            float l = (dg * dl[i] + wl[i]) * og;
            float r = (dg * dr[i] + wr[i]) * og;
            peakRaw = std::max (peakRaw, std::max (std::abs (l), std::abs (r)));
            if (limiterOn) { l = softLimit (l); r = softLimit (r); }
            outL[offset + i] = l;
            outR[offset + i] = r;
            peakOut = std::max (peakOut, std::max (std::abs (l), std::abs (r)));
        }
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Peak hold for the meters: the UI takes and decays them.
    auto holdMax = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    };
    holdMax (inPeak, peakIn);
    holdMax (outPeak, peakOut);
    holdMax (keyPeak, peakKey);
    holdMax (preLimiterPeak, peakRaw);
}

//==============================================================================
int RoomBreakerAudioProcessor::getNumPrograms()
{
    return (int) rb::getFactoryPresets().size();
}

const juce::String RoomBreakerAudioProcessor::getProgramName (int index)
{
    const auto& presets = rb::getFactoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return {};
    return juce::String (presets[(size_t) index].category) + " - " + presets[(size_t) index].name;
}

void RoomBreakerAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = rb::getFactoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;

    currentProgram = index;
    const auto& p = presets[(size_t) index];

    auto set = [this] (const char* id, float plainValue)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    };

    set (rb::ids::material,  (float) p.material);
    set (rb::ids::yield,     p.yieldDb);
    set (rb::ids::bias,      p.bias);
    set (rb::ids::asymmetry, p.asymmetry);
    set (rb::ids::voidType,  (float) p.voidType);
    set (rb::ids::heal,      p.healSeconds);
    set (rb::ids::size,      p.size);
    set (rb::ids::decay,     p.decaySeconds);
    set (rb::ids::debris,    p.debris);
    set (rb::ids::mix,       p.mix);
    set (rb::ids::reset,     0.0f);

    set (rb::ids::impact,    p.fx.impact);
    set (rb::ids::rumble,    p.fx.rumble);
    set (rb::ids::predelay,  p.fx.predelay);
    set (rb::ids::density,   p.fx.density);
    set (rb::ids::motion,    p.fx.motion);
    set (rb::ids::drive,     p.fx.drive);
    set (rb::ids::lowCut,    p.fx.lowCut);
    set (rb::ids::highCut,   p.fx.highCut);
    set (rb::ids::width,     p.fx.width);
    set (rb::ids::duck,      p.fx.duck);
    set (rb::ids::freeze,    0.0f);
    set (rb::ids::detonate,  0.0f);

    // A preset is a fresh room: pristine, or the preset's own starting damage.
    restoreAllEngines (p.startIntegrity);
}

//==============================================================================
void RoomBreakerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Parameters + the current structural damage, so a wrecked (or permadeath) room is
    // recalled exactly as it was left when the session reopens.
    auto state = apvts.copyState();
    state.setProperty (kPresetProperty, currentProgram, nullptr);
    auto structure = state.getOrCreateChildWithName (kStructureTag, nullptr);
    const auto integrity = engines[(size_t) activeIndex].currentIntegrity();   // atomics: safe from the message thread
    for (int w = 0; w < rb::dsp::kNumWalls; ++w)
        structure.setProperty (wallKey (w), integrity[(size_t) w], nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RoomBreakerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    // Remember which preset the session was based on (for display), without re-applying it.
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty (kPresetProperty, 0));
    tree.removeProperty (kPresetProperty, nullptr);
    const auto structure = tree.getChildWithName (kStructureTag);
    if (structure.isValid())
    {
        rb::dsp::WallArray integrity {};
        for (int w = 0; w < rb::dsp::kNumWalls; ++w)
            integrity[(size_t) w] = (float) structure.getProperty (wallKey (w), 1.0);
        restoreAllEngines (integrity);   // applied by the audio thread at its next block
        tree.removeChild (structure, nullptr);
    }

    // Older (v1) sessions have no v2 parameters: replaceState leaves those at their defaults.
    apvts.replaceState (tree);
}

//==============================================================================
juce::AudioProcessorEditor* RoomBreakerAudioProcessor::createEditor()
{
    return new RoomBreakerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoomBreakerAudioProcessor();
}
