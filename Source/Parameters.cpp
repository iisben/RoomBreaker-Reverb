#include "Parameters.h"

namespace rb
{
using namespace juce;

namespace
{
    AudioParameterFloatAttributes percentAttributes()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; });
    }

    AudioParameterFloatAttributes secondsAttributes()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v < 10.0f ? String (v, 2) + " s" : String (v, 1) + " s"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    }

    StringArray materialNames()
    {
        StringArray names;
        for (const auto& m : dsp::kMaterialProfiles) names.add (m.name);
        return names;
    }

    StringArray voidNames()
    {
        StringArray names;
        for (const auto& v : dsp::kVoidProfiles) names.add (v.name);
        return names;
    }

    AudioParameterFloatAttributes dbAttributes()
    {
        return AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    }

    AudioParameterFloatAttributes hzAttributes()
    {
        return AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; })
            .withValueFromStringFunction ([] (const String& s)
            {
                const float v = s.getFloatValue();
                return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
            });
    }

    NormalisableRange<float> skewedRange (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto structure = std::make_unique<AudioProcessorParameterGroup> ("structure", "Structure", " | ");

    structure->addChild (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::material, 1 }, "Material Archetype", materialNames(), 0));

    // Stress level at which a pristine wall starts to yield (lower = weaker room).
    structure->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::yield, 1 }, "Yield Threshold",
        NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -12.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); })));

    // 0 % = brittle (transient peaks crack it) .. 100 % = ductile (sustained sub-bass fatigues it).
    structure->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::bias, 1 }, "Brittle / Ductile Bias",
        NormalisableRange<float> (0.0f, 1.0f), 0.35f, percentAttributes()));

    structure->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::asymmetry, 1 }, "Asymmetry Ratio",
        NormalisableRange<float> (0.0f, 1.0f), 0.3f, percentAttributes()));

    structure->addChild (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::voidType, 1 }, "Exterior Void Type", voidNames(), 0));

    // Continuous recovery time constant; the top of the range is permadeath.
    structure->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::heal, 1 }, "Tectonic Healing",
        skewedRange (0.25f, kPermadeathSeconds, 20.0f), 20.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                return v >= kPermadeathSeconds - 0.01f ? String ("Permadeath") : String (v, 1) + " s"; })
            .withValueFromStringFunction ([] (const String& s) {
                return s.trim().equalsIgnoreCase ("permadeath") ? kPermadeathSeconds : s.getFloatValue(); })));

    // Momentary: the audio thread acts on the rising edge.
    structure->addChild (std::make_unique<AudioParameterBool> (
        ParameterID { ids::reset, 1 }, "Manual Reset", false));

    auto space = std::make_unique<AudioProcessorParameterGroup> ("space", "Space", " | ");

    space->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::size, 1 }, "Room Size",
        skewedRange (0.4f, 2.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })));

    space->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::decay, 1 }, "Decay",
        skewedRange (0.3f, 12.0f, 2.5f), 2.8f, secondsAttributes()));

    space->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::debris, 1 }, "Debris Level",
        NormalisableRange<float> (0.0f, 1.0f), 0.6f, percentAttributes()));

    space->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::mix, 1 }, "Dry / Wet",
        NormalisableRange<float> (0.0f, 1.0f), 0.35f, percentAttributes()));

    // ---------------------------------------------------------------- v2
    // New parameters carry version hint 2 so hosts keep the v1 automation order intact.
    auto destruction = std::make_unique<AudioProcessorParameterGroup> ("destruction", "Destruction", " | ");
    destruction->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::impact, 2 }, "Impact", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percentAttributes()));
    destruction->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::rumble, 2 }, "Rumble", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percentAttributes()));
    destruction->addChild (std::make_unique<AudioParameterBool> (
        ParameterID { ids::detonate, 2 }, "Detonate", false));   // momentary, rising edge
    destruction->addChild (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::stressSource, 2 }, "Stress Source", StringArray { "Input", "Sidechain", "Input + Sidechain" }, 0));

    auto space2 = std::make_unique<AudioProcessorParameterGroup> ("space2", "Space+", " | ");
    space2->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::predelay, 2 }, "Pre-Delay", skewedRange (0.0f, 250.0f, 40.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); })));
    space2->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::density, 2 }, "Density", NormalisableRange<float> (0.0f, 1.0f), 0.7f, percentAttributes()));
    space2->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::motion, 2 }, "Motion", NormalisableRange<float> (0.0f, 1.0f), 0.25f, percentAttributes()));
    space2->addChild (std::make_unique<AudioParameterBool> (
        ParameterID { ids::freeze, 2 }, "Freeze", false));

    auto tone = std::make_unique<AudioProcessorParameterGroup> ("tone", "Tone", " | ");
    tone->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::drive, 2 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
    tone->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::lowCut, 2 }, "Low Cut", skewedRange (20.0f, 1000.0f, 120.0f), 20.0f, hzAttributes()));
    tone->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::highCut, 2 }, "High Cut", skewedRange (1000.0f, 20000.0f, 6000.0f), 20000.0f, hzAttributes()));
    tone->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::width, 2 }, "Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f, percentAttributes()));
    tone->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::duck, 2 }, "Ducking", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));

    auto engineGroup = std::make_unique<AudioProcessorParameterGroup> ("engine", "Engine", " | ");
    engineGroup->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::inputGain, 2 }, "Input Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttributes()));
    engineGroup->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::outputGain, 2 }, "Output Gain", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, dbAttributes()));
    engineGroup->addChild (std::make_unique<AudioParameterBool> (
        ParameterID { ids::limiter, 2 }, "Safety Limiter", true));
    engineGroup->addChild (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::oversampling, 2 }, "Oversampling", StringArray { "Off", "2x", "4x" }, 1));

    layout.add (std::move (structure), std::move (space), std::move (destruction), std::move (space2),
                std::move (tone), std::move (engineGroup));
    return layout;
}

//==============================================================================
ParameterCache::ParameterCache (AudioProcessorValueTreeState& s)
    : material  (s.getRawParameterValue (ids::material)),
      yield     (s.getRawParameterValue (ids::yield)),
      bias      (s.getRawParameterValue (ids::bias)),
      asymmetry (s.getRawParameterValue (ids::asymmetry)),
      voidType  (s.getRawParameterValue (ids::voidType)),
      heal      (s.getRawParameterValue (ids::heal)),
      size      (s.getRawParameterValue (ids::size)),
      decay     (s.getRawParameterValue (ids::decay)),
      debris    (s.getRawParameterValue (ids::debris)),
      mix       (s.getRawParameterValue (ids::mix)),
      reset     (s.getRawParameterValue (ids::reset)),
      impact    (s.getRawParameterValue (ids::impact)),
      rumble    (s.getRawParameterValue (ids::rumble)),
      predelay  (s.getRawParameterValue (ids::predelay)),
      density   (s.getRawParameterValue (ids::density)),
      motion    (s.getRawParameterValue (ids::motion)),
      freeze    (s.getRawParameterValue (ids::freeze)),
      detonate  (s.getRawParameterValue (ids::detonate)),
      drive     (s.getRawParameterValue (ids::drive)),
      lowCut    (s.getRawParameterValue (ids::lowCut)),
      highCut   (s.getRawParameterValue (ids::highCut)),
      width     (s.getRawParameterValue (ids::width)),
      duck      (s.getRawParameterValue (ids::duck)),
      inputGain (s.getRawParameterValue (ids::inputGain)),
      outputGain (s.getRawParameterValue (ids::outputGain)),
      limiter   (s.getRawParameterValue (ids::limiter)),
      oversampling (s.getRawParameterValue (ids::oversampling)),
      stressSource (s.getRawParameterValue (ids::stressSource))
{
    jassert (material && yield && bias && asymmetry && voidType && heal && size && decay && debris && mix && reset);
    jassert (impact && rumble && predelay && density && motion && freeze && detonate && drive && lowCut && highCut
             && width && duck && inputGain && outputGain && limiter && oversampling && stressSource);
}

dsp::EngineParams ParameterCache::snapshot() const noexcept
{
    constexpr auto relaxed = std::memory_order_relaxed;

    dsp::EngineParams p;
    p.material     = (int) std::lround (material->load (relaxed));
    p.yieldDb      = yield->load (relaxed);
    p.ductileBias  = bias->load (relaxed);
    p.asymmetry    = asymmetry->load (relaxed);
    p.voidType     = (int) std::lround (voidType->load (relaxed));
    p.healSeconds  = heal->load (relaxed);
    p.permadeath   = p.healSeconds >= kPermadeathSeconds - 0.01f;
    p.size         = size->load (relaxed);
    p.decaySeconds = decay->load (relaxed);
    p.debrisLevel  = debris->load (relaxed);
    p.mix          = mix->load (relaxed);
    p.impact       = impact->load (relaxed);
    p.rumble       = rumble->load (relaxed);
    p.predelayMs   = predelay->load (relaxed);
    p.density      = density->load (relaxed);
    p.motion       = motion->load (relaxed);
    p.freeze       = freeze->load (relaxed) > 0.5f;
    p.drive        = drive->load (relaxed);
    p.lowCutHz     = lowCut->load (relaxed);
    p.highCutHz    = highCut->load (relaxed);
    p.width        = width->load (relaxed);
    p.duck         = duck->load (relaxed);
    p.stressSource = (int) std::lround (stressSource->load (relaxed));
    return p;
}
} // namespace rb
