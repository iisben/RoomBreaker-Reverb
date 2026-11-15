//==============================================================================
// Headless smoke test of the real RoomBreaker AudioProcessor — exercised the way
// a host would, without installing anything.
//
//   rb_plugin_smoke [outputDir]
//
// Checks: parameter layout, processBlock with random host block sizes, state
// save/recall including structural damage, Manual Reset edge trigger, editor
// construction + telemetry, bypass, meters, the preset library, the knobs' mouse
// and text entry, editor scaling, and writes PNG snapshots of the editor.
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
    check (plugin->getParameters().size() == 29, "29 parameters (11 v1 + 17 v2 + bypass)");
    check (plugin->getBypassParameter() == findParam (*plugin, "bypass"), "bypass parameter is exposed to the host");
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
        check (count == 48, juce::String (count) + " presets");
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
    // Each Signature preset is built around one behaviour; its description promises it.
    // These scenes play the material the preset is for and check the structure does that.
    std::printf ("\n-- Signature presets do what they say --\n");
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        juce::MidiBuffer midi;
        juce::Random noise (77);
        auto nz = [&] { return (float) (noise.nextDouble() * 2.0 - 1.0); };
        auto db = [] (float d) { return std::pow (10.0f, d / 20.0f); };

        using Gen = std::function<void (double t, float& l, float& r, float& kl, float& kr)>;
        struct Rig
        {
            std::unique_ptr<juce::AudioProcessor> p;
            double t = 0.0;
            float lowest = 1.0f;   // lowest integrity seen during the last feed (every 50 ms)
            std::function<void (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out)> tap;
        };
        auto open = [&] (const char* preset)
        {
            Rig rig;
            rig.p.reset (createPluginFilter());
            auto layout = rig.p->getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            rig.p->setBusesLayout (layout);
            rig.p->setRateAndBufferSizeDetails (48000.0, 480);
            rig.p->prepareToPlay (48000.0, 480);
            rig.p->setCurrentProgram (findProgram (*rig.p, preset));
            return rig;
        };
        auto feed = [&] (Rig& rig, double seconds, const Gen& gen)
        {
            const int chans = rig.p->getTotalNumInputChannels();
            juce::AudioBuffer<float> buf (std::max (chans, 2), 480), in;
            rig.lowest = 1.0f;
            for (int b = 0; b < (int) (seconds * 100.0); ++b)
            {
                buf.clear();
                for (int i = 0; i < 480; ++i)
                {
                    float l = 0, r = 0, kl = 0, kr = 0;
                    gen (rig.t + (double) i / 48000.0, l, r, kl, kr);
                    buf.setSample (0, i, l); buf.setSample (1, i, r);
                    if (chans >= 4) { buf.setSample (2, i, kl); buf.setSample (3, i, kr); }
                }
                if (rig.tap) in.makeCopyOf (buf);
                rig.p->processBlock (buf, midi);
                if (rig.tap) rig.tap (in, buf);
                rig.t += 0.01;
                if (b % 5 == 4) rig.lowest = std::min (rig.lowest, minOf (savedIntegrity (*rig.p)));
            }
            return savedIntegrity (*rig.p);
        };
        auto silence = [] (double, float&, float&, float&, float&) {};
        // A hit every `period` s with a peak of `level` dBFS (kick body + noise snap).
        auto hits = [&] (float level, double period) -> Gen
        {
            return [&, level, period] (double t, float& l, float& r, float&, float&)
            {
                const double tk = std::fmod (t, period);
                const float x = db (level) * (float) (0.6 * std::sin (twoPi * 55.0 * tk) * std::exp (-10.0 * tk))
                              + db (level) * 0.4f * nz() * (float) std::exp (-40.0 * tk);
                l += x; r += x;
            };
        };
        // A sustained chord with a soft 300 ms attack, peaking near `level` dBFS.
        auto pad = [&] (float level, double from) -> Gen
        {
            return [&, level, from] (double t, float& l, float& r, float&, float&)
            {
                const double fade = std::min (1.0, (t - from) / 0.3);
                const float x = db (level) * (float) (fade * (std::sin (twoPi * 220.0 * t) + std::sin (twoPi * 277.2 * t) + std::sin (twoPi * 329.6 * t)) / 3.0);
                l += x; r += x;
            };
        };
        auto mix = [] (Gen a, Gen b) -> Gen
        {
            return [a, b] (double t, float& l, float& r, float& kl, float& kr) { a (t, l, r, kl, kr); b (t, l, r, kl, kr); };
        };
        auto sub = [&] (float level, double from) -> Gen
        {
            return [&, level, from] (double t, float& l, float& r, float&, float&)
            {
                const float x = db (level) * (float) (std::min (1.0, (t - from) / 0.2) * std::sin (twoPi * 45.0 * t));
                l += x; r += x;
            };
        };
        const auto n = [] (int w, const std::array<float, 6>& I) { return I[(size_t) w]; };

        {   // Consonant Shards: sung vowels at -9 dBFS leave the glass alone; hard consonants break it.
            auto rig = open ("Consonant Shards");
            const auto sung = feed (rig, 4.0, pad (-9.0f, rig.t));
            const double t0 = rig.t;
            const auto spat = feed (rig, 2.0, [&, t0] (double t, float& l, float& r, float&, float&)
            {
                const double tk = std::fmod (t - t0, 0.5);
                const float x = 0.8f * nz() * (float) std::exp (-150.0 * tk);   // a 't' at about -2 dBFS
                l += x; r += x;
            });
            std::printf ("    Consonant Shards: after vowels %.3f, after consonants %.3f\n", minOf (sung), minOf (spat));
            check (minOf (sung) > 0.999f && minOf (spat) < 0.97f, "Consonant Shards: vowels don't break it, consonants do");
        }
        {   // Kick-Torn Cathedral: the pad alone never breaks it; with Break by = Key the kick tears it, and it seals again.
            auto rig = open ("Kick-Torn Cathedral");
            const auto padOnly = feed (rig, 3.0, pad (-8.0f, rig.t));
            setParam (*rig.p, "stresssrc", 1.0f);
            const auto torn = feed (rig, 3.0, mix (pad (-8.0f, -1.0), [&] (double t, float&, float&, float& kl, float& kr)
            {
                const double tk = std::fmod (t, 0.5);
                kl = kr = 0.8f * (float) (std::sin (twoPi * 50.0 * tk) * std::exp (-8.0 * tk));
            }));
            const auto sealed = feed (rig, 4.0, pad (-8.0f, -1.0));
            std::printf ("    Kick-Torn Cathedral: pad %.3f, keyed kicks %.3f, 4 s later %.3f\n", minOf (padOnly), minOf (torn), meanOf (sealed));
            check (minOf (padOnly) > 0.999f && minOf (torn) < 0.95f && meanOf (sealed) > 0.97f,
                   "Kick-Torn Cathedral: pad alone leaves it whole, keyed kicks tear it, it seals again");
        }
        {   // Chorus Memory keeps the damage far longer than Default Bunker.
            auto recovered = [&] (const char* preset)
            {
                auto rig = open (preset);
                const auto hit = feed (rig, 4.0, hits (-1.0f, 0.25));
                const auto later = feed (rig, 15.0, pad (-30.0f, rig.t));
                return std::make_pair (meanOf (hit), meanOf (later));
            };
            const auto memory = recovered ("Chorus Memory");
            const auto bunker = recovered ("Default Bunker");
            auto share = [] (std::pair<float, float> r) { return (r.second - r.first) / (1.0f - r.first); };   // of the damage, how much healed
            std::printf ("    Chorus Memory: %.2f -> %.2f after 15 s (%.0f %% healed); Default Bunker: %.2f -> %.2f (%.0f %%)\n",
                         memory.first, memory.second, 100.0f * share (memory), bunker.first, bunker.second, 100.0f * share (bunker));
            check (memory.first < 0.9f && share (memory) < 0.5f * share (bunker),
                   "Chorus Memory: wrecked by the loud part, heals less than half as much as Default Bunker in 15 s");
        }
        {   // Growing Canyon: every loud passage opens it further; nothing heals.
            auto rig = open ("Growing Canyon");
            const auto a = feed (rig, 3.0, hits (-2.0f, 0.3));
            const auto rest = feed (rig, 3.0, silence);
            const auto b = feed (rig, 3.0, hits (-2.0f, 0.3));
            std::printf ("    Growing Canyon: %.3f -> rest %.3f -> %.3f\n", meanOf (a), meanOf (rest), meanOf (b));
            check (meanOf (a) < 0.93f && std::abs (meanOf (rest) - meanOf (a)) < 1.0e-5f && meanOf (b) < meanOf (a) - 0.02f,
                   "Growing Canyon: erodes with every loud passage and never heals");
        }
        {   // Riser Tension: a swell to -8 dBFS never breaks it; the drop's first hit does.
            auto rig = open ("Riser Tension");
            const double t0 = rig.t;
            const auto swell = feed (rig, 6.0, [&, t0] (double t, float& l, float& r, float&, float&)
            {
                const float x = db (-30.0f + 22.0f * (float) ((t - t0) / 6.0)) * nz();
                l += x; r += x;
            });
            const auto drop = feed (rig, 1.0, hits (0.0f, 2.0));
            std::printf ("    Riser Tension: after the swell %.3f, after the drop %.3f\n", minOf (swell), minOf (drop));
            check (minOf (swell) > 0.999f && minOf (drop) < 0.97f, "Riser Tension: the swell never breaks it, the drop does");
        }
        {   // Controlled Demolition: full-scale drums can't touch it; Detonate does, for good.
            auto rig = open ("Controlled Demolition");
            const auto drums = feed (rig, 4.0, hits (0.0f, 0.25));
            setParam (*rig.p, "detonate", 1.0f);
            const auto blown = feed (rig, 2.0, silence);
            setParam (*rig.p, "detonate", 0.0f);
            const auto after = feed (rig, 3.0, hits (0.0f, 0.25));
            std::printf ("    Controlled Demolition: drums %.3f, detonated %.3f, drums again %.3f\n", minOf (drums), meanOf (blown), meanOf (after));
            check (minOf (drums) > 0.999f && meanOf (blown) < 0.9f && std::abs (meanOf (after) - meanOf (blown)) < 1.0e-5f,
                   "Controlled Demolition: only Detonate breaks it, and it stays broken");
        }
        {   // Rising From Rubble: starts as rubble and rebuilds under a quiet intro.
            auto rig = open ("Rising From Rubble");
            const auto start = feed (rig, 0.1, silence);
            const auto built = feed (rig, 30.0, pad (-24.0f, rig.t));
            std::printf ("    Rising From Rubble: %.2f -> %.2f after a 30 s quiet intro\n", meanOf (start), meanOf (built));
            check (meanOf (start) < 0.06f && meanOf (built) > 0.6f, "Rising From Rubble: rebuilds itself while the music is quiet");
        }
        {   // Booth to Stadium: verse-level hits stay in the booth, chorus hits breach it.
            auto rig = open ("Booth to Stadium");
            const auto verse = feed (rig, 3.0, hits (-10.0f, 0.25));
            const auto chorus = feed (rig, 3.0, hits (-1.0f, 0.25));
            std::printf ("    Booth to Stadium: verse %.3f, chorus %.3f\n", minOf (verse), minOf (chorus));
            check (minOf (verse) > 0.999f && minOf (chorus) < 0.9f, "Booth to Stadium: verse hits hold, chorus hits breach");
        }
        {   // Glacier Groan: drums can't strain it; a held low note does.
            auto rig = open ("Glacier Groan");
            const auto drums = feed (rig, 3.0, hits (-6.0f, 0.25));
            const auto held = feed (rig, 6.0, sub (-6.0f, rig.t));
            std::printf ("    Glacier Groan: drums %.3f, held 45 Hz %.3f\n", minOf (drums), minOf (held));
            check (minOf (drums) > 0.999f && minOf (held) < 0.9f, "Glacier Groan: only long low notes strain it");
        }
        {   // Tile Snare Exciter: ghost notes don't crack it, accents do, and it is whole again within a second.
            auto rig = open ("Tile Snare Exciter");
            feed (rig, 2.0, hits (-22.0f, 0.2));
            const float ghosts = rig.lowest;
            feed (rig, 2.0, hits (-3.0f, 0.5));
            const float accents = rig.lowest;
            const auto healed = feed (rig, 1.0, silence);
            std::printf ("    Tile Snare Exciter: lowest under ghosts %.3f, under accents %.3f, 1 s later %.3f\n", ghosts, accents, minOf (healed));
            check (ghosts > 0.999f && accents < 0.97f && minOf (healed) > 0.99f,
                   "Tile Snare Exciter: accents crack it, ghost notes don't, and it heals at once");
        }
        {   // Breath Abyss: ducked all the way, so the wet signal is all but gone under the words and
            // rises in the gaps. Compare with the same preset with no ducking.
            auto measure = [&] (float duck)
            {
                auto rig = open ("Breath Abyss");
                setParam (*rig.p, "duck", duck);
                const float dry = std::cos (0.4f * juce::MathConstants<float>::halfPi);   // the preset's dry gain (Mix 40 %)
                double word = 0, gap = 0; int nw = 0, ng = 0;
                bool inWord = false, inGap = false;
                rig.tap = [&] (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out)
                {
                    for (int i = 0; i < in.getNumSamples(); ++i)
                    {
                        const float res = out.getSample (0, i) - dry * in.getSample (0, i);   // everything that isn't the dry voice
                        if (inWord) { word += res * res; ++nw; }
                        if (inGap)  { gap += res * res; ++ng; }
                    }
                };
                for (int k = 0; k < 3; ++k)
                {
                    inWord = false; feed (rig, 0.4, pad (-6.0f, rig.t));                 // the word's attack
                    inWord = true;  feed (rig, 0.8, pad (-6.0f, rig.t - 1.0));
                    inWord = false; feed (rig, 0.6, silence);                            // the duck lets go...
                    inGap = true;   feed (rig, 1.0, silence);                            // ...and the abyss is back
                    inGap = false;
                }
                return std::make_pair (std::sqrt (word / std::max (nw, 1)), std::sqrt (gap / std::max (ng, 1)));
            };
            const auto ducked = measure (1.0f), unducked = measure (0.0f);
            std::printf ("    Breath Abyss: wet under the words %.4f (unducked %.4f), in the gaps %.4f (unducked %.4f)\n",
                         ducked.first, unducked.first, ducked.second, unducked.second);
            check (ducked.first < 0.25f * unducked.first && ducked.second > 2.0f * ducked.first && ducked.second > 0.4f * unducked.second,
                   "Breath Abyss: the reverb all but vanishes under the words and rises in the gaps");
        }
        {   // Sinkhole: the floor is gone for good; a held sub grinds the other walls down.
            auto rig = open ("Sinkhole");
            const auto held = feed (rig, 6.0, sub (-3.0f, rig.t));
            std::printf ("    Sinkhole: floor %.3f, walls min %.3f after a held sub\n", n (5, held), std::min ({ n (0, held), n (1, held), n (2, held), n (3, held) }));
            check (n (5, held) < 0.05f && std::min ({ n (0, held), n (1, held), n (2, held), n (3, held) }) < 0.9f,
                   "Sinkhole: no floor, and a held sub grinds the walls");
        }
        {   // Hearth Crackle: every note splinters it, and it re-sets between phrases.
            auto rig = open ("Hearth Crackle");
            const auto playing = feed (rig, 2.0, hits (-10.0f, 0.3));
            const auto pause = feed (rig, 1.5, silence);
            std::printf ("    Hearth Crackle: playing %.3f, after a pause %.3f\n", minOf (playing), minOf (pause));
            check (minOf (playing) < 0.97f && minOf (pause) > 0.98f, "Hearth Crackle: notes splinter it, a pause re-sets it");
        }
        {   // The hit-driven ones: ordinary hits at -6 dBFS set them off.
            for (const char* preset : { "Thunder Sheet", "Singing Lake Ice", "Porcelain Dollhouse", "Garage Amp Shed", "High Vault Aftermath",
                                        "Breakage Foley Printer", "Distant Front Line" })
            {
                auto rig = open (preset);
                const auto I = feed (rig, 2.0, hits (-6.0f, 0.4));
                std::printf ("    %s: %.3f after hits at -6 dBFS\n", preset, minOf (I));
                check (minOf (I) < 0.97f, juce::String (preset) + ": hits at -6 dBFS break it");
            }
        }
    }

    //--------------------------------------------------------------------------
    // Silence in, silence out. A fresh room must output digital zero, and once a passage that
    // breaks the room has stopped, everything (reverb, aftershocks, rubble) must be over by the
    // tail length the plugin reports: hosts stop calling it then, and analyzers flag whatever is
    // left as noise. Rubble used to trickle on for 10-25 s after a break, well past the tail.
    std::printf ("\n-- Silence in, silence out --\n");
    {
        juce::MidiBuffer midi;
        bool freshSilent = true, settled = true;
        double worstAfter = -600.0;
        juce::String worstName;
        for (int prog = 0; prog < plugin->getNumPrograms(); ++prog)
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            p->setRateAndBufferSizeDetails (48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            p->setCurrentProgram (prog);
            juce::AudioBuffer<float> buf (2, 512);
            juce::Random noiseGen (11);

            auto runFor = [&] (double fromSec, double toSec, bool audition, float& peak)
            {
                for (int64_t pos = (int64_t) (fromSec * 48000.0); pos < (int64_t) (toSec * 48000.0); pos += 512)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        float l = 0.0f, r = 0.0f;
                        if (audition) auditionSample ((double) (pos + i) / 48000.0, noiseGen, l, r);
                        buf.setSample (0, i, l); buf.setSample (1, i, r);
                    }
                    p->processBlock (buf, midi);
                    peak = std::max (peak, buf.getMagnitude (0, 512));
                }
            };

            float fresh = 0.0f, loud = 0.0f, after = 0.0f;
            runFor (0.0, 2.0, false, fresh);                         // nothing in: exactly nothing out
            runFor (0.0, 7.0, true, loud);                           // drums, sub bass and a 0 dBFS hit
            const double tail = p->getTailLengthSeconds();
            runFor (0.0, tail, false, loud);                         // let it ring out for the reported tail
            runFor (0.0, 1.0, false, after);                         // then: what a host would cut off
            freshSilent = freshSilent && fresh == 0.0f;
            const double afterDb = juce::Decibels::gainToDecibels ((double) after, -600.0);
            if (afterDb > worstAfter) { worstAfter = afterDb; worstName = p->getProgramName (prog); }
            settled = settled && afterDb < -80.0;
        }
        check (freshSilent, "every preset: digital silence in gives digital silence out");
        std::printf ("    loudest output after the reported tail: %.1f dBFS (%s)\n", worstAfter, worstName.toRawUTF8());
        check (settled, "every preset: after a passage that breaks the room, quiet (< -80 dBFS) by the reported tail length");
    }

    //--------------------------------------------------------------------------
    // Tone purity. A held tone through the default room may come back detuned by Motion (a few Hz
    // of chorus around it) but nothing else: no jitter skirt, no crackle from a steady load.
    // Analyzers read both as aliasing and intermodulation. Before the fixes: 800 Hz -57 dBc,
    // 3 kHz -49 dBc; a 700 + 900 Hz pair -54 dBc.
    std::printf ("\n-- Tone purity (default room, -6 dBFS) --\n");
    {
        constexpr int order = 16, N = 1 << order;
        constexpr double fs = 48000.0, hz = fs / N;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) N, juce::dsp::WindowingFunction<float>::blackmanHarris, false);
        juce::MidiBuffer midi;

        // Spurious power, relative to the first tone, outside +-20 Hz of each tone and +-4.4 Hz of
        // each harmonic and low-order IM product.
        auto spurious = [&] (std::vector<double> tones, double amp, const char* preset = nullptr)
        {
            std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
            p->setRateAndBufferSizeDetails (fs, 512);
            p->prepareToPlay (fs, 512);
            if (preset != nullptr) p->setCurrentProgram (findProgram (*p, preset));
            juce::AudioBuffer<float> buf (2, 512);
            std::vector<float> out;
            const int64_t total = (int64_t) (2.0 * fs) + N;
            for (int64_t pos = 0; pos < total; pos += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    double x = 0.0;
                    for (double f : tones) x += amp * std::sin (juce::MathConstants<double>::twoPi * f * (double) (pos + i) / fs);
                    buf.setSample (0, i, (float) x); buf.setSample (1, i, (float) x);
                }
                p->processBlock (buf, midi);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            }
            std::vector<float> spec (2 * N, 0.0f);
            std::copy (out.end() - N, out.end(), spec.begin());
            window.multiplyWithWindowingTable (spec.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (spec.data());

            std::vector<char> keep (N / 2, 1);
            auto drop = [&] (double f, double w)
            {
                for (int k = std::max (0, (int) ((f - w) / hz)); k <= std::min (N / 2 - 1, (int) ((f + w) / hz)); ++k) keep[(size_t) k] = 0;
            };
            drop (0.0, 20.0);
            double ref = 0.0;
            for (int k = (int) ((tones[0] - 4.4) / hz); k <= (int) ((tones[0] + 4.4) / hz); ++k) ref += (double) spec[(size_t) k] * spec[(size_t) k];
            for (double f : tones)
            {
                drop (f, 20.0);
                for (int h = 2; h * f < fs / 2; ++h) drop (h * f, 4.4);
            }
            if (tones.size() == 2)
                for (double f : { tones[1] - tones[0], tones[0] + tones[1], 2 * tones[0] - tones[1], 2 * tones[1] - tones[0] })
                    if (f > 0.0) drop (f, 4.4);
            double other = 0.0;
            for (int k = 0; k < N / 2; ++k)
                if (keep[(size_t) k]) other += (double) spec[(size_t) k] * spec[(size_t) k];
            return 10.0 * std::log10 (other / ref + 1e-30);
        };

        const double s800 = spurious ({ 800.0 }, 0.5), s3k = spurious ({ 3000.0 }, 0.5), pair = spurious ({ 700.0, 900.0 }, 0.25);
        std::printf ("    spurious: 800 Hz %.1f dBc, 3 kHz %.1f dBc, 700 + 900 Hz %.1f dBc\n", s800, s3k, pair);
        check (s800 < -62.0 && s3k < -55.0 && pair < -62.0,
               "held tones come back clean: no motion jitter, no crackle under a steady load");

        // A ruin: cracked walls scatter their reflections, but slowly. The crack jitter used to
        // warble them by 1.5-3.5 % (Bombed Cathedral: 800 Hz -23 dBc, 3 kHz -15 dBc).
        const double r800 = spurious ({ 800.0 }, 0.5, "Bombed Cathedral"), r3k = spurious ({ 3000.0 }, 0.5, "Bombed Cathedral");
        std::printf ("    Bombed Cathedral (starts ruined): 800 Hz %.1f dBc, 3 kHz %.1f dBc\n", r800, r3k);
        check (r800 < -28.0 && r3k < -32.0, "a ruined room scatters its reflections without warbling held tones");
    }


    //--------------------------------------------------------------------------
    std::printf ("\n-- Bypass and meters --\n");
    {
        auto& rbp = dynamic_cast<RoomBreakerAudioProcessor&> (*plugin);
        plugin->setCurrentProgram (findProgram (*plugin, "Default Bunker"));
        setParam (*plugin, "mix", 0.6f);
        juce::MidiBuffer midi;
        auto sine = [] (juce::AudioBuffer<float>& b, int start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) (start + i) / 48000.0);
                b.setSample (0, i, x);
                b.setSample (1, i, -x);
            }
        };
        int pos = 0;
        for (int k = 0; k < 40; ++k) { juce::AudioBuffer<float> b (2, 480); sine (b, pos); plugin->processBlock (b, midi); pos += 480; }
        check (rbp.inMeter[0].peak.load() > 0.2f && rbp.inMeter[1].peak.load() > 0.2f, "input meters see both channels");
        check (rbp.outMeter[0].meanSquare.load() > 1.0e-4f, "output RMS follows the signal");

        if (auto* b = findParam (*plugin, "bypass")) b->setValueNotifyingHost (1.0f);
        float worstWhileFading = 0.0f, worstAfter = 0.0f;
        for (int k = 0; k < 40; ++k)
        {
            juce::AudioBuffer<float> b (2, 480), dry (2, 480);
            sine (b, pos);
            dry.makeCopyOf (b);
            plugin->processBlockBypassed (b, midi);   // what a host calls while its bypass is on
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 480; ++i)
                {
                    const float d = std::abs (b.getSample (ch, i) - dry.getSample (ch, i));
                    (k < 2 ? worstWhileFading : worstAfter) = std::max (k < 2 ? worstWhileFading : worstAfter, d);
                }
            pos += 480;
        }
        std::printf ("    bypassed: largest difference from the input %.2g (after the 20 ms fade)\n", (double) worstAfter);
        check (worstAfter < 1.0e-6f, "bypassed output is the untouched input");
        if (auto* b = findParam (*plugin, "bypass")) b->setValueNotifyingHost (0.0f);
        bool fl = true;
        pump (*plugin, 0.3, 0.2f, rng, fl);
        check (fl, "un-bypassing fades back without non-finite output");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n-- Preset library --\n");
    const auto userPresets = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rb_smoke_presets");
    {
        auto& rbp = dynamic_cast<RoomBreakerAudioProcessor&> (*plugin);
        auto& lib = rbp.presets;
        userPresets.deleteRecursively();
        lib.setUserFolder (userPresets);   // never the user's real folder
        check (lib.size() == 48, "48 factory presets listed");

        auto names = [&] (const std::vector<int>& idx)
        {
            juce::StringArray n;
            for (int i : idx) n.add (lib.entry (i).name);
            return n;
        };
        const auto glass = names (lib.search ("glass", {}, {}));
        check (glass.size() >= 1 && glass[0] == "Glass Rain Collapse", "search finds by name, name matches first");
        const auto perm = names (lib.search ({}, { "Permadeath" }, {}));
        check (perm.size() == 7, "tag filter: 7 presets tagged Permadeath (" + perm.joinIntoString (", ") + ")");
        check (lib.search ("dark tail", {}, {}).size() >= 2, "every word must match (name, tags, category or description)");
        check (lib.search ({}, { "Drums", "Metallic" }, {}).size() == 3, "tags combine (Drums AND Metallic)");
        check (lib.search ({}, {}, "Ambient").size() == 3, "category narrows to Ambient (3)");
        bool everyTagKnown = true;
        for (int i = 0; i < lib.size(); ++i)
            for (const auto& t : lib.entry (i).tags)
                everyTagKnown = everyTagKnown && rb::presetTagVocabulary().contains (t);
        check (everyTagKnown, "every factory tag is in the vocabulary");

        lib.load (lib.search ("Drowned Signal", {}, {})[0]);
        check (lib.currentName() == "Drowned Signal" && plugin->getCurrentProgram() == findProgram (*plugin, "Drowned Signal"),
               "loading a factory preset sets the host program");
        check (! lib.isModified(), "a freshly loaded preset is not modified");
        setParam (*plugin, "decay", 9.0f);
        check (lib.isModified(), "moving a control marks it modified");
        setParam (*plugin, "input", 3.0f);   // a global setting: never part of a preset

        check (lib.saveUser ("Smoke Test Room", {}, { "Dark", "Long tail" }), "user preset saved");
        check (userPresets.getChildFile ("Smoke Test Room.rbpreset").existsAsFile(), "file written as Smoke Test Room.rbpreset");
        check (lib.size() == 49 && lib.currentName() == "Smoke Test Room" && ! lib.isModified(), "saved preset is listed and current");
        check (lib.search ({}, {}, rb::PresetLibrary::kUserCategory).size() == 1
               && lib.categories().contains (rb::PresetLibrary::kUserCategory), "user presets have their own category");

        juce::MemoryBlock withUser;
        plugin->getStateInformation (withUser);
        lib.load (0);
        check (std::abs (dynamic_cast<juce::RangedAudioParameter*> (findParam (*plugin, "decay"))->convertFrom0to1 (findParam (*plugin, "decay")->getValue()) - 2.8f) < 0.01f,
               "factory preset replaced the decay");
        lib.load (lib.search ("Smoke Test", {}, {})[0]);
        auto* decay = dynamic_cast<juce::RangedAudioParameter*> (findParam (*plugin, "decay"));
        check (std::abs (decay->convertFrom0to1 (decay->getValue()) - 9.0f) < 0.05f, "user preset brings its decay back");
        auto* input = dynamic_cast<juce::RangedAudioParameter*> (findParam (*plugin, "input"));
        check (std::abs (input->convertFrom0to1 (input->getValue()) - 3.0f) < 0.05f, "presets leave Input gain alone");

        lib.load (0);
        plugin->setStateInformation (withUser.getData(), (int) withUser.getSize());
        check (lib.currentName() == "Smoke Test Room", "a session remembers it was using a user preset");
        setParam (*plugin, "input", 0.0f);
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

            // Close-ups at 2x: as a high-density screen shows them.
            auto detail = [&] (juce::Rectangle<int> area, const juce::String& fileName)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                const auto image = editor->createComponentSnapshot (area, true, 2.0f);
                const auto png = outDir.getChildFile (fileName);
                png.deleteFile();
                juce::FileOutputStream stream (png);
                if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream (image, stream);
            };
            plugin->setCurrentProgram (findProgram (*plugin, "Glass Rain Collapse"));
            play (1.0, 0.8f);
            detail ({ 720, 48, 480, 670 }, "detail_controls.png");
            detail ({ 300, 0, 900, 48 }, "detail_header.png");
            detail ({ 0, 48, 96, 670 }, "detail_meter.png");

            //------------------------------------------------------------------
            std::printf ("\n-- Knobs --\n");
            {
                auto& controls = ed->getControls();
                auto* decayKnob = controls.findKnob ("decay");
                auto* hiCut = controls.findKnob ("highcut");
                auto* pre = controls.findKnob ("predelay");
                check (decayKnob != nullptr && hiCut != nullptr && pre != nullptr, "the knobs are where the layout says");

                struct Gestures : juce::AudioProcessorListener
                {
                    int begun = 0, ended = 0;
                    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
                    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
                    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begun; }
                    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ended; }
                } gestures;
                plugin->addListener (&gestures);

                auto plain = [] (juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); };
                auto type = [&] (rb::ui::Knob& k, const juce::String& text)
                {
                    k.openValueEditor();
                    juce::TextEditor* te = nullptr;
                    for (auto* c : k.getChildren())
                        if (auto* t = dynamic_cast<juce::TextEditor*> (c)) te = t;
                    if (te == nullptr) return false;
                    te->setText (text, false);
                    te->onReturnKey();
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                    return ! k.isEditingValue();
                };
                if (decayKnob != nullptr && hiCut != nullptr && pre != nullptr)
                {
                    check (type (*decayKnob, "4.5"), "double-click editor opens and closes on Return");
                    check (std::abs (plain (decayKnob->getParameter()) - 4.5f) < 0.01f, "typed 4.5 -> Decay 4.50 s");
                    type (*pre, "120 ms");
                    check (std::abs (plain (pre->getParameter()) - 120.0f) < 0.5f, "typed \"120 ms\" -> Pre-delay 120 ms (units optional)");
                    type (*hiCut, "4.5k");
                    check (std::abs (plain (hiCut->getParameter()) - 4500.0f) < 5.0f, "typed \"4.5k\" -> High cut 4.50 kHz");
                    type (*decayKnob, "loud");
                    check (std::abs (plain (decayKnob->getParameter()) - 4.5f) < 0.01f, "nonsense text leaves the value alone");
                    type (*decayKnob, "99");
                    check (std::abs (plain (decayKnob->getParameter()) - 12.0f) < 0.01f, "typed values clamp to the range");

                    auto wheel = [&] (rb::ui::Knob& k, float dy, bool smooth, juce::ModifierKeys mods)
                    {
                        juce::MouseWheelDetails w {};
                        w.deltaY = dy; w.isSmooth = smooth;
                        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), { 40.0f, 40.0f }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                  &k, &k, juce::Time::getCurrentTime(), { 40.0f, 40.0f }, juce::Time::getCurrentTime(), 1, false);
                        k.mouseWheelMove (e, w);
                    };
                    decayKnob->resetToDefault();
                    const double p0 = decayKnob->valueToProportionOfLength (decayKnob->getValue());
                    wheel (*decayKnob, 0.1f, false, {});
                    const double p1 = decayKnob->valueToProportionOfLength (decayKnob->getValue());
                    wheel (*decayKnob, -0.1f, false, juce::ModifierKeys::shiftModifier);
                    const double p2 = decayKnob->valueToProportionOfLength (decayKnob->getValue());
                    std::printf ("    wheel notch %+.4f of the range, with Shift %+.4f\n", p1 - p0, p2 - p1);
                    check (std::abs ((p1 - p0) - 0.02) < 1.0e-3 && std::abs ((p2 - p1) + 0.002) < 1.0e-3, "wheel: 2 % per notch, Shift 0.2 %");

                    auto* yieldKnob = controls.findKnob ("yield");   // a 0.1 dB stepped parameter
                    yieldKnob->resetToDefault();
                    const double y0 = yieldKnob->getValue();
                    for (int i = 0; i < 10; ++i) wheel (*yieldKnob, 0.1f, false, juce::ModifierKeys::shiftModifier);
                    check (yieldKnob->getValue() > y0 + 0.5, "fine wheel steps add up on a stepped parameter");

                    decayKnob->resetToDefault();
                    check (std::abs (plain (decayKnob->getParameter()) - 2.8f) < 0.01f, "reset returns to the default");
                    std::printf ("    host gestures: %d begun, %d ended\n", gestures.begun, gestures.ended);
                    check (gestures.begun >= 18 && gestures.begun == gestures.ended, "every edit is one complete host gesture");
                }
                plugin->removeListener (&gestures);
            }

            //------------------------------------------------------------------
            std::printf ("\n-- Scaling --\n");
            {
                auto& rbp = dynamic_cast<RoomBreakerAudioProcessor&> (*plugin);
                ed->setScale (1.5f);
                check (editor->getWidth() == 1800 && editor->getHeight() == 1125, "150 %: 1800 x 1125");
                check (std::abs (ed->getCanvas().getTransform().mat00 - 1.5f) < 1.0e-4f && ed->getCanvas().getWidth() == 1200,
                       "the canvas keeps its design size and is scaled as one");
                juce::MemoryBlock st;
                plugin->getStateInformation (st);
                std::unique_ptr<juce::AudioProcessor> other (createPluginFilter());
                other->setStateInformation (st.getData(), (int) st.getSize());
                check (std::abs (dynamic_cast<RoomBreakerAudioProcessor&> (*other).editorScale.load() - 1.5f) < 1.0e-3f, "the size is saved with the session");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                const auto big = editor->createComponentSnapshot ({ 1080, 72, 720, 480 }, true, 1.0f);
                juce::FileOutputStream stream (outDir.getChildFile ("detail_scale150.png").getFullPathName());
                if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream (big, stream);
                ed->setScale (0.5f);
                check (editor->getWidth() == 900, "sizes clamp at 75 % (" + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) + ")");
                ed->setScale (3.0f);
                check (editor->getWidth() == 2400, "sizes clamp at 200 %");
                ed->setScale (1.0f);
                check (std::abs (rbp.editorScale.load() - 1.0f) < 1.0e-4f, "back to 100 %");
            }

            //------------------------------------------------------------------
            std::printf ("\n-- Preset browser --\n");
            {
                auto& browser = ed->getBrowser();
                plugin->setCurrentProgram (findProgram (*plugin, "Ash Cathedral"));
                browser.open();
                check (browser.isVisible(), "browser opens");
                browser.setSearchText ("cathedral");
                check (browser.getNumResults() == 3, "search \"cathedral\": 3 results");
                browser.setSearchText ({});
                browser.setTagSelected ("Dark", true);
                const int dark = browser.getNumResults();
                browser.setTagSelected ("Bass", true);
                check (browser.getNumResults() < dark && browser.getNumResults() > 0, "tags narrow the list");
                browser.setTagSelected ("Bass", false);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                snap ("editor_browser.png");
                browser.setTagSelected ("Dark", false);
                browser.close();
                check (! browser.isVisible(), "browser closes");
            }

            ed->getInfoPanel().showHelpFor ("decay");
            snap ("editor_info.png");
            ed->getInfoPanel().showHelpFor ({});
        }
        plugin->editorBeingDeleted (editor.get());
    }

    userPresets.deleteRecursively();
    plugin->releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
