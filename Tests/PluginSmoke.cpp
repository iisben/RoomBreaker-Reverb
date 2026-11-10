//==============================================================================
// Headless smoke test of the real RoomBreaker AudioProcessor — exercised the way
// a host would, without installing anything.
//
//   rb_plugin_smoke [outputDir]
//
// Checks: parameter layout, processBlock with random host block sizes, state
// save/recall including structural damage, Manual Reset edge trigger, editor
// construction + telemetry, and writes a PNG snapshot of the editor.
//==============================================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

juce::AudioProcessorParameter* findParam (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            if (withId->paramID == id)
                return param;
    return nullptr;
}

void setParam (juce::AudioProcessor& p, const juce::String& id, float plainValue)
{
    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (findParam (p, id)))
        ranged->setValueNotifyingHost (ranged->convertTo0to1 (plainValue));
}

std::array<float, 6> savedIntegrity (juce::AudioProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    std::array<float, 6> I {};
    I.fill (-1.0f);
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
        if (auto* s = xml->getChildByName ("Structure"))
            for (int w = 0; w < 6; ++w)
                I[(size_t) w] = (float) s->getDoubleAttribute ("wall" + juce::String (w), -1.0);
    return I;
}

float minOf (const std::array<float, 6>& a) { return *std::min_element (a.begin(), a.end()); }
float meanOf (const std::array<float, 6>& a) { float s = 0.0f; for (float v : a) s += v; return s / 6.0f; }

int findProgram (juce::AudioProcessor& p, const juce::String& nameContains)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName (i).contains (nameContains))
            return i;
    return -1;
}

/** 10 s audition: drums @ -6 dBFS (0-4 s), 50 Hz bass + kicks (4-6.5 s), one 0 dBFS impact (6.5 s), 3.5 s tail. */
void auditionSample (double t, juce::Random& rng, float& l, float& r)
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    double x = 0.0, side = 0.0;
    if (t < 4.0)
    {
        const double tk = std::fmod (t, 0.5), ts = std::fmod (t + 0.25, 0.5), th = std::fmod (t, 0.125);
        x = 0.5 * (std::sin (twoPi * (48.0 * tk + 4.0 * (1.0 - std::exp (-30.0 * tk)))) * std::exp (-9.0 * tk)
                   + 0.7 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-22.0 * ts))
            + 0.06 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-60.0 * th);
        side = 0.04 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-60.0 * th);
    }
    else if (t < 6.5)
    {
        const double tk = std::fmod (t, 0.5);
        x = 0.4 * std::sin (twoPi * 50.0 * t) + 0.4 * std::sin (twoPi * 45.0 * tk) * std::exp (-10.0 * tk);
    }
    else if (t < 6.8)
    {
        const double ti = t - 6.5;
        x = std::exp (-12.0 * ti) * (0.6 * std::sin (twoPi * 40.0 * ti) + 0.4 * (rng.nextDouble() * 2.0 - 1.0));
    }
    l = (float) (x + side);
    r = (float) (x - side);
}

struct PresetResult { float raw = 0.0f; float peak = 0.0f; bool finite = true; float minAfterDrums = 1.0f, meanAfterDrums = 1.0f, minAfterBass = 1.0f, meanAfterBass = 1.0f, meanEnd = 1.0f; };

float rawPeak (juce::AudioProcessor& p)
{
    return dynamic_cast<RoomBreakerAudioProcessor&> (p).preLimiterPeak.exchange (0.0f);
}

PresetResult renderPreset (juce::AudioProcessor& p, int program, const juce::File& wavFile, juce::Random& rng)
{
    p.setCurrentProgram (program);
    rawPeak (p);
    const int total = 10 * 48000;
    juce::AudioBuffer<float> out (2, total);
    juce::MidiBuffer midi;
    PresetResult res;

    for (int pos = 0; pos < total;)
    {
        const int n = juce::jmin (total - pos, 1 + rng.nextInt (1024));
        juce::AudioBuffer<float> buf (2, n);
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            auditionSample ((double) (pos + i) / 48000.0, rng, l, r);
            buf.setSample (0, i, l);
            buf.setSample (1, i, r);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buf, ch, 0, n);

        const int before = pos;
        pos += n;
        if (before < 4 * 48000 && pos >= 4 * 48000)       { const auto I = savedIntegrity (p); res.minAfterDrums = minOf (I); res.meanAfterDrums = meanOf (I); }
        if (before < 6.5 * 48000 && pos >= 6.5 * 48000)   { const auto I = savedIntegrity (p); res.minAfterBass = minOf (I); res.meanAfterBass = meanOf (I); }
    }
    res.meanEnd = meanOf (savedIntegrity (p));
    res.raw = rawPeak (p);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (ch, i);
            res.finite = res.finite && std::isfinite (v);
            res.peak = std::max (res.peak, std::abs (v));
        }

    wavFile.deleteFile();
    if (auto* stream = new juce::FileOutputStream (wavFile); stream->openedOk())
    {
        juce::WavAudioFormat wav;
        if (std::unique_ptr<juce::AudioFormatWriter> writer { wav.createWriterFor (stream, 48000.0, 2, 24, {}, 0) })
            writer->writeFromAudioSampleBuffer (out, 0, total);
        else
            delete stream;
    }
    else
        delete stream;

    return res;
}

/** Kicks + noise bursts at full scale; returns peak |out|, flags non-finite output. */
float pump (juce::AudioProcessor& p, double seconds, float gain, juce::Random& rng, bool& finite)
{
    juce::MidiBuffer midi;
    const int total = (int) (seconds * 48000.0);
    float peak = 0.0f;
    for (int pos = 0; pos < total;)
    {
        const int n = juce::jmin (total - pos, 1 + rng.nextInt (1024));   // hosts do this
        juce::AudioBuffer<float> buf (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / 48000.0;
            const double tk = std::fmod (t, 0.4);
            const float x = gain * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * tk) * std::exp (-8.0 * tk)
                                            + (tk < 0.004 ? rng.nextFloat() * 2.0 - 1.0 : 0.0));
            buf.setSample (0, i, x);
            buf.setSample (1, i, x * 0.9f);
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (ch, i);
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
        pos += n;
    }
    return peak;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : ".");
    juce::Random rng (42);

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    std::printf ("\n== %s ==\n", plugin->getName().toRawUTF8());

    //--------------------------------------------------------------------------
    std::printf ("\n-- Parameters --\n");
    for (auto* param : plugin->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            std::printf ("    %-10s %-24s default: %s\n", withId->paramID.toRawUTF8(), param->getName (32).toRawUTF8(),
                         param->getText (param->getDefaultValue(), 32).toRawUTF8());
    check (plugin->getParameters().size() == 28, "28 parameters (11 v1 + 17 v2)");
    {
        auto* heal = findParam (*plugin, "heal");
        check (heal != nullptr && heal->getText (1.0f, 32) == "Permadeath", "Healing at max reads \"Permadeath\"");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Buses --\n");
    {
        juce::AudioProcessor::BusesLayout stereo, monoIn, surround;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());  stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        monoIn.inputBuses.add (juce::AudioChannelSet::mono());    monoIn.outputBuses.add (juce::AudioChannelSet::stereo());
        surround.inputBuses.add (juce::AudioChannelSet::create5point1()); surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        for (auto* l : { &stereo, &monoIn, &surround })
            l->inputBuses.add (juce::AudioChannelSet::disabled());   // the (optional) sidechain bus
        check (plugin->checkBusesLayoutSupported (stereo), "stereo -> stereo supported");
        check (plugin->checkBusesLayoutSupported (monoIn), "mono -> stereo supported");
        check (! plugin->checkBusesLayoutSupported (surround), "5.1 rejected");
        auto withSc = stereo;
        withSc.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        check (plugin->checkBusesLayoutSupported (withSc), "stereo + stereo sidechain supported");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Processing (random host block sizes 1..1024) --\n");
    plugin->setRateAndBufferSizeDetails (48000.0, 1024);
    plugin->prepareToPlay (48000.0, 1024);
    setParam (*plugin, "heal", 600.0f);   // permadeath, so damage persists for the state tests
    setParam (*plugin, "mix", 0.5f);

    bool finite = true;
    const float quietPeak = pump (*plugin, 2.0, 0.01f, rng, finite);
    check (minOf (savedIntegrity (*plugin)) == 1.0f, "quiet input leaves the room intact");
    const float loudPeak = pump (*plugin, 6.0, 1.0f, rng, finite);
    const auto damaged = savedIntegrity (*plugin);
    std::printf ("    integrity after loud section: N %.2f S %.2f E %.2f W %.2f C %.2f F %.2f\n",
                 damaged[0], damaged[1], damaged[2], damaged[3], damaged[4], damaged[5]);
    check (finite, "output finite");
    check (quietPeak < 0.05f && loudPeak < 4.0f, "output bounded (loud peak " + juce::String (loudPeak, 3) + ")");
    check (minOf (damaged) < 0.9f, "loud input damages walls");

    //--------------------------------------------------------------------------
    std::printf ("\n-- State save / recall --\n");
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);

        std::unique_ptr<juce::AudioProcessor> second (createPluginFilter());
        second->setStateInformation (state.getData(), (int) state.getSize());
        second->setRateAndBufferSizeDetails (48000.0, 512);
        second->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> silence (2, 512);
        silence.clear();
        juce::MidiBuffer midi;
        second->processBlock (silence, midi);   // restore is applied on the audio thread

        const auto recalled = savedIntegrity (*second);
        float maxDiff = 0.0f;
        for (int w = 0; w < 6; ++w) maxDiff = std::max (maxDiff, std::abs (recalled[(size_t) w] - damaged[(size_t) w]));
        check (maxDiff < 1.0e-4f, "structural damage survives save/recall");

        auto* healA = findParam (*plugin, "heal");
        auto* healB = findParam (*second, "heal");
        check (healA && healB && std::abs (healA->getValue() - healB->getValue()) < 1.0e-6f, "parameters survive save/recall");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Manual Reset (rising edge) --\n");
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> silence (2, 256);
        auto* reset = findParam (*plugin, "reset");
        reset->setValueNotifyingHost (1.0f);
        silence.clear(); plugin->processBlock (silence, midi);
        check (minOf (savedIntegrity (*plugin)) == 1.0f, "Rebuild restores every wall");
        bool f = true;
        pump (*plugin, 3.0, 1.0f, rng, f);   // reset still held: must not keep rebuilding
        check (minOf (savedIntegrity (*plugin)) < 0.95f, "holding Reset does not block new damage");
        reset->setValueNotifyingHost (0.0f);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Factory presets (10 s audition each, random host blocks) --\n");
    {
        const int count = plugin->getNumPrograms();
        juce::StringArray names;
        for (int i = 0; i < count; ++i) names.addIfNotAlreadyThere (plugin->getProgramName (i));
        check (count >= 28, juce::String (count) + " presets");
        check (names.size() == count && ! names.contains ({}), "preset names unique and non-empty");

        const auto presetDir = outDir.getChildFile ("presets");
        presetDir.deleteRecursively();
        presetDir.createDirectory();

        std::printf ("    %-48s  drums(min/mean) bass(min/mean)  end    peak  pre-limiter\n", "preset (integrity after each section)");
        bool allOk = true;
        for (int i = 0; i < count; ++i)
        {
            const auto name = plugin->getProgramName (i);
            const auto file = presetDir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (name) + ".wav");
            const auto r = renderPreset (*plugin, i, file, rng);
            std::printf ("    %-48s  %.2f / %.2f     %.2f / %.2f     %.2f   %.2f  %.2f\n", name.toRawUTF8(), r.minAfterDrums, r.meanAfterDrums, r.minAfterBass, r.meanAfterBass, r.meanEnd, r.peak, r.raw);
            const bool ok = r.finite && r.peak <= 1.0f && r.raw < 2.0f && file.getSize() > 1000000;
            if (! ok) std::printf ("      ^ problem: finite=%d peak=%.3f wav=%lld bytes\n", (int) r.finite, r.peak, (long long) file.getSize());
            allOk = allOk && ok;
        }
        check (allOk, "every preset: finite, limited peak <= 0 dBFS, pre-limiter peak < 2, audition WAV written");

        // Loading a preset sets the parameters...
        const int vocal = findProgram (*plugin, "Vocal Plate");
        plugin->setCurrentProgram (vocal);
        check (findParam (*plugin, "material")->getCurrentValueAsText() == "Ceramic Chamber"
                   && findParam (*plugin, "mix")->getCurrentValueAsText() == "20 %",
               "loading \"Vocal Plate\" sets its parameters");
        check (findParam (*plugin, "impact")->getCurrentValueAsText() == "0 %"
                   && findParam (*plugin, "predelay")->getCurrentValueAsText() == "28 ms",
               "...including the v2 controls (Impact 0 %, Pre-Delay 28 ms)");

        // ...and the room's starting state.
        juce::AudioBuffer<float> silence (2, 64);
        juce::MidiBuffer midi;
        plugin->setCurrentProgram (findProgram (*plugin, "Bombed Cathedral"));
        silence.clear(); plugin->processBlock (silence, midi);
        const auto ruined = savedIntegrity (*plugin);
        check (std::abs (ruined[4] - 0.0f) < 1.0e-4f && std::abs (ruined[5] - 0.9f) < 1.0e-4f, "\"Bombed Cathedral\" starts as a ruin (no ceiling)");
        plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
        silence.clear(); plugin->processBlock (silence, midi);
        check (minOf (savedIntegrity (*plugin)) == 1.0f, "loading a normal preset rebuilds a pristine room");

        // The preset index is remembered by the session.
        juce::MemoryBlock mb;
        plugin->setCurrentProgram (vocal);
        plugin->getStateInformation (mb);
        std::unique_ptr<juce::AudioProcessor> other (createPluginFilter());
        other->setStateInformation (mb.getData(), (int) mb.getSize());
        check (other->getCurrentProgram() == vocal, "current preset survives save/recall");
    }


    //--------------------------------------------------------------------------
    std::printf ("\n-- Detonate, Freeze, Limiter --\n");
    {
        juce::MidiBuffer midi;
        plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
        setParam (*plugin, "heal", 600.0f);
        juce::AudioBuffer<float> silence (2, 512);
        silence.clear(); plugin->processBlock (silence, midi);
        check (minOf (savedIntegrity (*plugin)) == 1.0f, "pristine before DETONATE");

        auto* det = findParam (*plugin, "detonate");
        det->setValueNotifyingHost (1.0f);
        for (int b = 0; b < 8; ++b) { silence.clear(); plugin->processBlock (silence, midi); }
        const auto blasted = savedIntegrity (*plugin);
        float detPeak = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            silence.clear(); plugin->processBlock (silence, midi);
            detPeak = std::max (detPeak, silence.getMagnitude (0, 512));
        }
        const auto stillBlasted = savedIntegrity (*plugin);
        det->setValueNotifyingHost (0.0f);
        std::printf ("    after DETONATE: mean %.2f, max %.2f, boom peak %.3f\n", meanOf (blasted), *std::max_element (blasted.begin(), blasted.end()), detPeak);
        check (*std::max_element (blasted.begin(), blasted.end()) < 0.9f, "DETONATE damages every wall");
        check (std::abs (meanOf (stillBlasted) - meanOf (blasted)) < 1.0e-6f, "holding DETONATE fires once (rising edge)");
        check (detPeak > 0.02f, "DETONATE is audible on silence (impact + debris)");

        // Freeze: feed noise, freeze, then silence → the tail holds.
        setParam (*plugin, "reset", 1.0f); silence.clear(); plugin->processBlock (silence, midi); setParam (*plugin, "reset", 0.0f);
        setParam (*plugin, "mix", 1.0f);
        setParam (*plugin, "impact", 0.0f); setParam (*plugin, "debris", 0.0f); setParam (*plugin, "rumble", 0.0f);
        setParam (*plugin, "yield", 0.0f);
        juce::AudioBuffer<float> noise (2, 512);
        for (int b = 0; b < 94; ++b)   // ~1 s of noise at -12 dBFS
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) noise.setSample (ch, i, 0.25f * (rng.nextFloat() * 2.0f - 1.0f));
            plugin->processBlock (noise, midi);
        }
        setParam (*plugin, "freeze", 1.0f);
        auto rmsOver = [&] (double seconds, bool feedNoise)
        {
            double acc = 0.0; int count = 0;
            for (int b = 0; b < (int) (seconds * 48000.0 / 512.0); ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i) noise.setSample (ch, i, feedNoise ? 0.5f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f);
                plugin->processBlock (noise, midi);
                for (int i = 0; i < 512; ++i) { acc += (double) noise.getSample (0, i) * noise.getSample (0, i); ++count; }
            }
            return (float) std::sqrt (acc / std::max (count, 1));
        };
        const float frozenA = rmsOver (1.0, false);
        const float frozenB = rmsOver (6.0, false);
        std::printf ("    frozen tail RMS: after 1 s %.4f, after 7 s %.4f\n", frozenA, frozenB);
        check (frozenA > 1.0e-3f && frozenB > 0.6f * frozenA, "FREEZE holds the tail (7 s later still > 60 %)");
        setParam (*plugin, "freeze", 0.0f);
        const float released = rmsOver (8.0, false);
        const float releasedLater = rmsOver (2.0, false);
        std::printf ("    released tail RMS: %.4f then %.5f\n", released, releasedLater);
        check (releasedLater < 0.2f * frozenB, "releasing FREEZE lets the tail decay");

        // Limiter: detonation + full-scale kicks + output +12 dB never pass 0 dBFS.
        plugin->setCurrentProgram (findProgram (*plugin, "Trailer Hit Demolition"));
        setParam (*plugin, "output", 12.0f);
        bool f = true;
        rawPeak (*plugin);
        const float limited = pump (*plugin, 3.0, 1.0f, rng, f);
        const float raw = rawPeak (*plugin);
        std::printf ("    +12 dB into the limiter: raw peak %.2f -> output peak %.3f\n", raw, limited);
        check (f && limited <= 1.0f && raw > 1.0f, "safety limiter holds the output at or below 0 dBFS");
        setParam (*plugin, "output", 0.0f);
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Oversampling --\n");
    {
        juce::MidiBuffer midi;
        // Aliasing: Drive at 100 % on a 7 kHz sine, wet only, nothing that adds noise.
        auto aliasDb = [&] (int osIndex)
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            p->setRateAndBufferSizeDetails (48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            setParam (*p, "oversampling", (float) osIndex);
            for (auto [id, v] : { std::pair<const char*, float> { "mix", 1.0f }, { "drive", 1.0f }, { "impact", 0.0f }, { "rumble", 0.0f },
                                  { "debris", 0.0f }, { "yield", 0.0f }, { "motion", 0.0f }, { "limiter", 0.0f }, { "lowcut", 20.0f } })
                setParam (*p, id, v);
            constexpr int fftOrder = 15, fftSize = 1 << fftOrder;
            std::vector<float> capture;
            double phase = 0.0;
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 300; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.5f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 7000.0 / 48000.0;
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
                p->processBlock (buf, midi);
                if (b >= 200) for (int i = 0; i < 512; ++i) capture.push_back (buf.getSample (0, i));
            }
            std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] = capture[(size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));
            juce::dsp::FFT fft (fftOrder);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            double harmonic = 0.0, other = 0.0;
            for (int k = 1; k < fftSize / 2; ++k)
            {
                const double hz = (double) k * 48000.0 / fftSize;
                if (hz < 100.0 || hz > 20000.0) continue;
                const double e = (double) fftData[(size_t) k] * fftData[(size_t) k];
                bool isHarm = false;
                for (int h = 1; h * 7000 <= 21000; ++h) isHarm = isHarm || std::abs (hz - 7000.0 * h) < 250.0;
                (isHarm ? harmonic : other) += e;
            }
            return 10.0 * std::log10 ((other + 1e-30) / (harmonic + 1e-30));
        };
        const double a1 = aliasDb (0), a2 = aliasDb (1), a4 = aliasDb (2);
        std::printf ("    Drive 100 %% on 7 kHz: aliasing (energy off the harmonic series) 1x %.1f dB, 2x %.1f dB, 4x %.1f dB\n", a1, a2, a4);
        check (a4 < a1 - 10.0 && a2 < a1, "oversampling lowers aliasing (4x at least 10 dB below 1x)");

        // Switching while sound is playing: crossfaded, no blow-up, structure kept.
        plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
        setParam (*plugin, "heal", 600.0f);
        setParam (*plugin, "oversampling", 0.0f);
        bool f = true;
        pump (*plugin, 3.0, 1.0f, rng, f);
        const auto before = savedIntegrity (*plugin);
        float worstJump = 0.0f, prev = 0.0f;
        bool first = true;
        for (int step = 0; step < 6; ++step)
        {
            setParam (*plugin, "oversampling", (float) (step % 3));
            for (int b = 0; b < 20; ++b)
            {
                juce::AudioBuffer<float> buf (2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    const float x = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) (b * 256 + i) / 48000.0);
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
                plugin->processBlock (buf, midi);
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (0, i);
                    f = f && std::isfinite (v);
                    if (! first) worstJump = std::max (worstJump, std::abs (v - prev));
                    prev = v; first = false;
                }
            }
        }
        const auto after = savedIntegrity (*plugin);
        float drift = 0.0f;
        for (int w = 0; w < 6; ++w) drift = std::max (drift, std::abs (after[(size_t) w] - before[(size_t) w]));
        std::printf ("    6 switches during a 220 Hz tone: largest sample-to-sample step %.4f (a full-scale click would be ~1)\n", worstJump);
        check (f && worstJump < 0.12f, "switching oversampling while playing is click-free");
        check (drift < 0.05f, "switching keeps the room's damage");

        // CPU per factor
        for (int os = 0; os < 3; ++os)
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            p->setRateAndBufferSizeDetails (48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            setParam (*p, "oversampling", (float) os);
            juce::AudioBuffer<float> buf (2, 512);
            const int blocks = 48000 * 10 / 512;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i) buf.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
                p->processBlock (buf, midi);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("    %dx: 10 s rendered in %.0f ms = %.0fx real time\n", 1 << os, ms, 10000.0 / ms);
        }
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Sidechain stress source --\n");
    {
        juce::MidiBuffer midi;
        auto run = [&] (int source)
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            auto layout = p->getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            const bool ok = p->setBusesLayout (layout);
            p->setRateAndBufferSizeDetails (48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            setParam (*p, "stresssrc", (float) source);
            setParam (*p, "heal", 600.0f);
            const int chans = p->getTotalNumInputChannels();
            juce::AudioBuffer<float> buf (std::max (chans, 2), 512);
            for (int b = 0; b < 48000 * 4 / 512; ++b)
            {
                buf.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (double) (b * 512 + i) / 48000.0;
                    const float pad = 0.03f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * t);
                    const double tk = std::fmod (t, 0.4);
                    const float kick = (float) (std::sin (juce::MathConstants<double>::twoPi * 50.0 * tk) * std::exp (-8.0 * tk));
                    buf.setSample (0, i, pad); buf.setSample (1, i, pad);
                    if (chans >= 4) { buf.setSample (2, i, kick); buf.setSample (3, i, kick); }
                }
                p->processBlock (buf, midi);
            }
            return std::make_pair (ok && chans >= 4, meanOf (savedIntegrity (*p)));
        };
        const auto [okIn, inputOnly] = run (0);
        const auto [okSc, keyed] = run (1);
        std::printf ("    quiet pad in, loud kicks on the key: integrity with source=Input %.2f, source=Sidechain %.2f\n", inputOnly, keyed);
        check (okIn && okSc, "sidechain bus can be enabled (4 input channels)");
        check (inputOnly > 0.99f && keyed < 0.9f, "Sidechain source breaks the room from the key, Input source ignores it");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Editor --\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() >= 760, "editor created (" + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) + ")");

        auto* ed = dynamic_cast<RoomBreakerAudioProcessorEditor*> (editor.get());
        check (ed != nullptr, "editor is the RoomBreaker editor");

        auto snap = [&] (const juce::String& fileName)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto png = outDir.getChildFile (fileName);
            png.deleteFile();
            juce::FileOutputStream stream (png);
            check (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream), "editor snapshot written: " + png.getFileName());
        };
        // Audio and UI clocks advance together, so debris flies and lands as it would live.
        auto play = [&] (double seconds, float level)
        {
            bool fl = true;
            for (double t = 0.0; t < seconds; t += 0.1)
            {
                pump (*plugin, 0.1, level, rng, fl);
                if (ed != nullptr) ed->advance (0.1f);
            }
        };

        if (ed != nullptr)
        {
            // An intact room first.
            plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
            play (0.6, 0.0f);
            snap ("editor_pristine.png");

            // A dramatic preset, hit hard: cracks, holes, debris on the lawn.
            plugin->setCurrentProgram (findProgram (*plugin, "Glass Rain Collapse"));
            play (2.5, 1.0f);
            play (0.8, 0.0f);
            snap ("editor_snapshot.png");

            // Detonate a concrete house and catch the debris in the air.
            plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
            play (0.3, 0.0f);
            if (auto* d = findParam (*plugin, "detonate")) d->setValueNotifyingHost (1.0f);
            play (0.2, 0.0f);
            if (auto* d = findParam (*plugin, "detonate")) d->setValueNotifyingHost (0.0f);
            play (0.2, 0.0f);
            snap ("editor_detonate.png");

            // The same house from the back.
            ed->getHouse().setViewAngle (3.6f);
            play (1.5, 0.0f);
            snap ("editor_back.png");
            ed->getHouse().setViewAngle (0.7854f);

            // Gallery: the house alone, posed at set damage levels in each material and outside.
            // These are review images; the loop also times a repaint.
            {
                auto& house = ed->getHouse();
                auto& live = ed->getLive();
                const auto dir = outDir.getChildFile ("gallery");
                dir.createDirectory();
                auto pose = [&] (int material, int outside, float damage, float yaw)
                {
                    setParam (*plugin, "material", (float) material);
                    setParam (*plugin, "voidType", (float) outside);
                    const float spread[6] = { 0.0f, 0.06f, -0.05f, 0.03f, -0.02f, 0.04f };
                    for (size_t w = 0; w < 6; ++w)
                        live.frame.integrity[w] = juce::jlimit (0.0f, 1.0f, 1.0f - damage * (1.0f + spread[w]) - (damage > 0.0f ? spread[w] * 0.5f : 0.0f));
                    house.setViewAngle (yaw);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                };
                // Shots are tiled three across onto contact sheets, six to a sheet.
                auto save = [&] (const juce::Image& image, const juce::String& name)
                {
                    const auto png = dir.getChildFile (name + ".png");
                    png.deleteFile();
                    juce::FileOutputStream stream (png);
                    if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream (image, stream);
                };
                const int cellW = house.getWidth(), cellH = house.getHeight();
                juce::Image sheet (juce::Image::RGB, cellW * 3, cellH * 2, true);
                int onSheet = 0, sheetNumber = 0;
                auto flush = [&]
                {
                    if (onSheet == 0) return;
                    save (sheet, "sheet_" + juce::String (sheetNumber++));
                    sheet.clear (sheet.getBounds(), juce::Colours::black);
                    onSheet = 0;
                };
                auto shot = [&] (const juce::String& name)
                {
                    const auto image = house.createComponentSnapshot (house.getLocalBounds(), true, 1.0f);
                    juce::Graphics sg (sheet);
                    sg.drawImageAt (image, (onSheet % 3) * cellW, (onSheet / 3) * cellH);
                    sg.setColour (juce::Colours::black.withAlpha (0.6f));
                    sg.setFont (14.0f);
                    sg.drawText (name, (onSheet % 3) * cellW + 60, (onSheet / 3) * cellH + 6, 300, 18, juce::Justification::centredLeft);
                    if (++onSheet == 6) flush();
                };
                auto closeUp = [&] (const juce::String& name)
                {
                    save (house.createComponentSnapshot (house.getLocalBounds(), true, 2.0f), name);
                };
                // debris in flight: two frames of a detonation
                plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
                if (auto* r = findParam (*plugin, "reset")) { r->setValueNotifyingHost (1.0f); play (0.1, 0.0f); r->setValueNotifyingHost (0.0f); }
                play (0.5, 0.0f);
                if (auto* d = findParam (*plugin, "detonate")) d->setValueNotifyingHost (1.0f);
                play (0.15, 0.0f);
                if (auto* d = findParam (*plugin, "detonate")) d->setValueNotifyingHost (0.0f);
                play (0.12, 0.0f);
                closeUp ("close_blast_a");
                play (0.4, 0.0f);
                closeUp ("close_blast_b");
                play (1.5, 0.0f);
                closeUp ("close_blast_c");

                const float front = 0.7854f;
                const float levels[] = { 0.0f, 0.3f, 0.55f, 0.8f, 1.0f };
                for (int k = 0; k < 5; ++k)
                {
                    pose (0, 0, levels[k], front);
                    shot ("concrete_" + juce::String (k));
                }
                flush();
                static const char* materialNames[] = { "concrete", "ceramic", "silo", "hangar", "brutalist", "glass", "stone", "subway", "ice", "timber", "bulkhead" };
                for (int m = 1; m < 11; ++m)
                {
                    pose (m, m % 7, 0.0f, front);
                    shot (juce::String (materialNames[m]) + "_new");
                    pose (m, m % 7, 0.62f, front);
                    shot (juce::String (materialNames[m]) + "_mid");
                    pose (m, m % 7, 0.93f, front);
                    shot (juce::String (materialNames[m]) + "_end");
                }
                pose (0, 0, 0.7f, 2.4f);
                shot ("concrete_west");
                pose (0, 0, 0.7f, 3.9f);
                shot ("concrete_north");
                flush();
                pose (0, 0, 0.0f, front);
                closeUp ("close_new");
                pose (0, 0, 0.6f, front);
                closeUp ("close_mid");
                pose (0, 0, 0.9f, front);
                closeUp ("close_end");

                juce::Image canvas (juce::Image::ARGB, house.getWidth(), house.getHeight(), true);
                double worst = 0.0;
                for (const float level : { 0.0f, 0.4f, 0.75f, 1.0f })
                {
                    pose (0, 0, level, front);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int k = 0; k < 40; ++k)
                    {
                        juce::Graphics cg (canvas);
                        house.paintEntireComponent (cg, true);
                    }
                    const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 40.0;
                    worst = std::max (worst, ms);
                    std::printf ("    house repaint at %3.0f %% damage: %.2f ms\n", (double) level * 100.0, ms);
                }
                check (worst < 120.0, "house repaints in reasonable time (about 8 ms on Apple Silicon; CI machines draw in software)");

                pose (0, 0, 0.0f, front);
                live.frame.integrity.fill (1.0f);
            }

            ed->getInfoPanel().showHelpFor ("impact");
            snap ("editor_info.png");
            ed->getInfoPanel().showHelpFor ({});
            ed->setTab (1);
            snap ("editor_engine.png");
            ed->setTab (0);
        }
        plugin->editorBeingDeleted (editor.get());
    }

    plugin->releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
