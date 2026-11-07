#include "PluginEditor.h"
#include "Presets.h"

using namespace rb;
using namespace rb::ui;

namespace
{
    /** Word-wrapped paragraph; returns the height it used. */
    float drawParagraph (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                         const juce::Font& font, juce::Colour colour, float lineSpacing = 3.0f)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.setJustification (juce::Justification::topLeft);
        a.setLineSpacing (lineSpacing);
        a.append (text, font, colour);
        juce::TextLayout layout;
        layout.createLayout (a, r.getWidth());
        layout.draw (g, r.withHeight (std::min (layout.getHeight(), r.getHeight())));
        return layout.getHeight();
    }

    juce::String infoKeyFor (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (c->getProperties().contains ("info"))
                return c->getProperties()["info"].toString();
        return {};
    }

    const char* kWallNames[] = { "North", "South", "East", "West", "Roof", "Floor" };

    juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

//==============================================================================
// Under the house
//==============================================================================
void WallSurvey::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    g.setColour (C::line);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    const auto& f = live.frame;
    const float w = (float) (getWidth() - 40) / 6.0f;
    for (int i = 0; i < 6; ++i)
    {
        const float x = 20.0f + (float) i * w;
        const float I = f.integrity[(size_t) i], s = f.stressRatio[(size_t) i];
        const bool hl = highlighted == i;

        g.setColour (hl ? C::text : C::dim);
        g.setFont (uiFont (13.0f, true));
        g.drawText (kWallNames[i], (int) x, 12, (int) w - 8, 16, juce::Justification::centredLeft);

        g.setColour (I < 0.15f ? C::hot : C::text);
        g.setFont (monoFont (21.0f));
        g.drawText (percent (I), (int) x, 29, (int) w - 8, 24, juce::Justification::centredLeft);

        // load against yield: the tick is yield; past it the wall is taking damage now
        const auto bar = juce::Rectangle<float> (x, 59.0f, w - 22.0f, 3.0f);
        g.setColour (C::panel2);
        g.fillRect (bar);
        const float fill = juce::jlimit (0.0f, 1.0f, s / 2.0f);
        g.setColour (s >= 1.0f ? C::hot : C::dim);
        g.fillRect (bar.withWidth (bar.getWidth() * fill));
        g.setColour (C::text.withAlpha (0.7f));
        g.fillRect (bar.getX() + bar.getWidth() * 0.5f - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
    }
}

void HistoryGraph::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (C::bg);

    g.setColour (C::dim);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("Last 12 seconds", 20, 6, 200, 16, juce::Justification::centredLeft);
    g.setFont (uiFont (12.0f));
    g.setColour (C::text.withAlpha (0.85f));
    g.drawText ("standing", (int) r.getRight() - 190, 6, 70, 16, juce::Justification::centredRight);
    g.setColour (C::hot);
    g.drawText ("load", (int) r.getRight() - 112, 6, 40, 16, juce::Justification::centredRight);
    g.setColour (C::dim);
    g.drawText ("fractures", (int) r.getRight() - 72, 6, 52, 16, juce::Justification::centredRight);

    const auto plot = juce::Rectangle<float> (20.0f, 30.0f, r.getWidth() - 40.0f, r.getHeight() - 52.0f);
    g.setColour (C::line.withAlpha (0.6f));
    for (int sec = 0; sec <= 12; sec += 3)
    {
        const float x = plot.getRight() - plot.getWidth() * (float) sec / 12.0f;
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    constexpr float kStressTop = 3.0f;
    const float yieldY = plot.getBottom() - plot.getHeight() / kStressTop;
    {
        juce::Path dash, dashed;
        dash.startNewSubPath (plot.getX(), yieldY);
        dash.lineTo (plot.getRight(), yieldY);
        const float pattern[] = { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, dash, pattern, 2);
        g.setColour (C::hot.withAlpha (0.5f));
        g.fillPath (dashed);
    }

    const int N = LiveState::kHistory;
    auto xAt = [&] (int i) { return plot.getX() + plot.getWidth() * (float) i / (float) (N - 1); };
    juce::Path integ, stress;
    for (int i = 0; i < N; ++i)
    {
        const int idx = (live.histPos + 1 + i) % N;
        const float x = xAt (i);
        const float yi = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, live.histIntegrity[(size_t) idx]);
        const float ys = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, live.histStress[(size_t) idx] / kStressTop);
        if (i == 0) { integ.startNewSubPath (x, yi); stress.startNewSubPath (x, ys); }
        else        { integ.lineTo (x, yi); stress.lineTo (x, ys); }
        if (live.histFracture[(size_t) idx] > 0.0f)
        {
            g.setColour (C::dim);
            g.fillRect (x - 0.5f, plot.getBottom() + 4.0f, 1.0f, 6.0f);
        }
    }
    g.setColour (C::hot.withAlpha (0.9f));
    g.strokePath (stress, juce::PathStrokeType (1.3f));
    g.setColour (C::text.withAlpha (0.9f));
    g.strokePath (integ, juce::PathStrokeType (1.8f));
}

StructureView::StructureView (LiveState& s, juce::AudioProcessorValueTreeState& apvts)
    : house (s, apvts), survey (s), history (s)
{
    setInfo (house, "monitor");
    addAndMakeVisible (house);
    addAndMakeVisible (survey);
    addAndMakeVisible (history);
}

void StructureView::paint (juce::Graphics& g) { g.fillAll (C::bg); }

void StructureView::resized()
{
    house.setBounds (0, 0, getWidth(), 500);
    survey.setBounds (0, 500, getWidth(), 74);
    history.setBounds (0, 574, getWidth(), getHeight() - 574);
}

void StructureView::refresh (float dt)
{
    house.tick (dt);
    survey.highlighted = house.hoveredWall();
    house.repaint();
    survey.repaint();
    history.repaint();
}

//==============================================================================
// Room panel
//==============================================================================
namespace
{
    struct KnobSpec { const char* id; const char* label; };
    struct Row { const char* title; KnobSpec large; KnobSpec others[3]; };
    const Row kRows[] = {
        { "Structure",   { ids::yield, "Yield" },    { { ids::bias, "Brittle / ductile" }, { ids::asymmetry, "Asymmetry" }, { ids::heal, "Healing" } } },
        { "Destruction", { ids::impact, "Impact" },  { { ids::rumble, "Rumble" }, { ids::debris, "Debris" }, { ids::drive, "Drive" } } },
        { "Space",       { ids::decay, "Decay" },    { { ids::size, "Size" }, { ids::predelay, "Pre-delay" }, { ids::density, "Density" } } },
    };
    const KnobSpec kToneRow[4] = { { ids::lowCut, "Low cut" }, { ids::highCut, "High cut" }, { ids::width, "Width" }, { ids::motion, "Motion" } };
    constexpr int kRowY[4] = { 84, 222, 360, 498 };
}

RoomPanel::RoomPanel (RoomBreakerAudioProcessor& p)
    : proc (p),
      material (p.apvts, ids::material, "Material"),
      voidType (p.apvts, ids::voidType, "Outside")
{
    addAndMakeVisible (material);
    addAndMakeVisible (voidType);
    for (const auto& row : kRows)
    {
        knobs.push_back (std::make_unique<Knob> (p.apvts, row.large.id, row.large.label, Knob::Large));
        for (const auto& k : row.others)
            knobs.push_back (std::make_unique<Knob> (p.apvts, k.id, k.label, Knob::Regular));
    }
    for (const auto& k : kToneRow)
        knobs.push_back (std::make_unique<Knob> (p.apvts, k.id, k.label, Knob::Regular));
    for (auto& k : knobs)
        addAndMakeVisible (*k);
}

void RoomPanel::paint (juce::Graphics& g)
{
    g.fillAll (C::panel);
    g.setColour (C::line);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    for (int r = 0; r < 3; ++r)
        drawSection (g, 24, kRowY[r], getWidth() - 48, kRows[r].title);
    drawSection (g, 24, kRowY[3], getWidth() - 48, "Tone");

    // preset notes
    const auto& presets = getFactoryPresets();
    const int cp = proc.getCurrentProgram();
    if (cp >= 0 && cp < (int) presets.size())
    {
        const int y = 642;
        g.setColour (C::line);
        g.drawHorizontalLine (y, 24.0f, (float) getWidth() - 24.0f);
        drawParagraph (g, presets[(size_t) cp].description, { 24.0f, (float) y + 12.0f, (float) getWidth() - 48.0f, 60.0f },
                       uiFont (13.0f), C::dim);
    }
}

void RoomPanel::resized()
{
    material.setBounds (24, 16, 168, 48);
    voidType.setBounds (204, 16, getWidth() - 228, 48);

    size_t k = 0;
    for (int r = 0; r < 3; ++r)
    {
        const int y = kRowY[r] + 24;
        knobs[k++]->setBounds (18, y, 108, 108);
        for (int c = 0; c < 3; ++c)
            knobs[k++]->setBounds (132 + c * 84, y + 12, 84, 92);
    }
    for (int c = 0; c < 4; ++c)
        knobs[k++]->setBounds (24 + c * 88, kRowY[3] + 30, 88, 92);
}

//==============================================================================
// Engine page
//==============================================================================
namespace
{
    constexpr int kSheetX = 48, kControlX = 300, kNoteX = 620, kRowH = 128, kSheetTop = 40;
}

EnginePage::EnginePage (RoomBreakerAudioProcessor& p, const LiveState& s)
    : proc (p), live (s),
      oversampling (p.apvts, ids::oversampling, { "Off", "2x", "4x" }),
      stressSource (p.apvts, ids::stressSource, { "Input", "Sidechain", "Both" }),
      duck (p.apvts, ids::duck, "", Knob::Regular),
      limiter (p.apvts, ids::limiter, "On")
{
    for (juce::Component* c : { (juce::Component*) &oversampling, (juce::Component*) &stressSource,
                                (juce::Component*) &duck, (juce::Component*) &limiter })
        addAndMakeVisible (c);
}

void EnginePage::resized()
{
    oversampling.setBounds (kControlX, kSheetTop + 2, 240, 30);
    limiter.setBounds (kControlX, kSheetTop + kRowH + 4, 120, 26);
    duck.setBounds (kControlX - 6, kSheetTop + 2 * kRowH - 4, 92, 100);
    stressSource.setBounds (kControlX, kSheetTop + 3 * kRowH + 2, 280, 30);
}

void EnginePage::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
    const int w = getWidth() - 2 * kSheetX;

    auto row = [&] (int index, const juce::String& title, const juce::String& note)
    {
        const int y = kSheetTop + index * kRowH;
        if (index > 0)
        {
            g.setColour (C::line);
            g.drawHorizontalLine (y - 24, (float) kSheetX, (float) (kSheetX + w));
        }
        g.setColour (C::text);
        g.setFont (uiFont (17.0f, true));
        g.drawText (title, kSheetX, y + 4, kControlX - kSheetX - 20, 22, juce::Justification::centredLeft);
        drawParagraph (g, note, { (float) kNoteX, (float) y + 6.0f, (float) (kSheetX + w - kNoteX), 90.0f }, uiFont (14.0f), C::dim);
        return y;
    };

    // oversampling
    {
        const int factor = proc.activeFactor.load();
        const double sr = proc.getHostSampleRate();
        const juce::String note = factor == 1
            ? juce::String ("The room runs at the session rate, ") + juce::String (sr / 1000.0, 1) + " kHz. Lowest CPU; Drive can alias on bright material."
            : "The room runs at " + juce::String (sr * factor / 1000.0, 1) + " kHz. Cleaner Drive and debris, for about "
              + juce::String (factor) + "x the CPU. The dry signal is untouched, so there is no added latency.";
        const int y = row (0, "Oversampling", note);
        g.setColour (C::dim);
        g.setFont (monoFont (13.0f));
        g.drawText ("CPU " + juce::String (live.cpu * 100.0f, 1) + "%", kControlX, y + 42, 200, 16, juce::Justification::centredLeft);
    }

    row (1, "Safety limiter", "A soft ceiling from -1 dBFS to 0 dBFS on the output. It only touches overs, such as a detonation "
                             "into a full mix. Leave it on unless a limiter follows.");

    {
        const int y = row (2, "Ducking", "Pulls the room down while the dry signal plays and lets it bloom back in the gaps, "
                                         "so vocals and leads stay clear without a shorter tail.");
        const float duckDb = juce::Decibels::gainToDecibels (live.frame.duckGain, -60.0f);
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Room level now", kControlX + 110, y + 26, 160, 16, juce::Justification::centredLeft);
        g.setColour (C::text);
        g.setFont (monoFont (20.0f));
        g.drawText (juce::String (duckDb, 1) + " dB", kControlX + 110, y + 44, 160, 24, juce::Justification::centredLeft);
    }

    {
        const int y = row (3, "What breaks the walls",
                           "Input: the sound you are reverberating. Sidechain: another track, such as a kick or a riser, "
                           "breaks this room while you hear this track's reverb. Most hosts show a sidechain selector "
                           "in the plugin window's title bar.");
        const bool sc = proc.sidechainConnected.load();
        g.setColour (sc ? C::text : C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText (sc ? "Sidechain connected" : "No sidechain connected", kControlX, y + 42, 200, 16, juce::Justification::centredLeft);
        const auto bar = juce::Rectangle<float> ((float) kControlX, (float) y + 64.0f, 280.0f, 3.0f);
        g.setColour (C::panel2);
        g.fillRect (bar);
        const float keyDb = juce::Decibels::gainToDecibels (live.keyMeter, -60.0f);
        g.setColour (C::text.withAlpha (0.8f));
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (keyDb + 60.0f) / 60.0f)));
    }

    {
        const int y = kSheetTop + 4 * kRowH;
        g.setColour (C::line);
        g.drawHorizontalLine (y - 24, (float) kSheetX, (float) (kSheetX + w));
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText ("RoomBreaker " + juce::String (JucePlugin_VersionString) + ". Presets never change the settings on this page.",
                    kSheetX, y, w, 18, juce::Justification::centredLeft);
    }
}

//==============================================================================
// Info panel
//==============================================================================
InfoPanel::InfoPanel (RoomBreakerAudioProcessor& p, const LiveState& s) : proc (p), live (s)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}

void InfoPanel::mouseEnter (const juce::MouseEvent& e)
{
    const auto key = infoKeyFor (e.eventComponent);
    if (key.isNotEmpty())
    {
        clearAt = 0;
        if (key != hoverKey) { hoverKey = key; repaint(); }
    }
}

void InfoPanel::mouseExit (const juce::MouseEvent&)
{
    // Leaving a knob for its own label (or a gap) shouldn't flicker the panel: clear a moment later.
    clearAt = juce::Time::getMillisecondCounter() + 450;
}

void InfoPanel::timerCallback()
{
    if (clearAt != 0 && juce::Time::getMillisecondCounter() >= clearAt)
    {
        clearAt = 0;
        hoverKey.clear();
        repaint();
    }
}

void InfoPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.fillAll (C::bg2);
    g.setColour (C::line);
    g.drawVerticalLine (0, 0.0f, r.getHeight());

    const auto body = r.reduced (24.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (18.0f);
    if (hoverKey.isNotEmpty()) paintHelp (g, body);
    else                       paintStatus (g, body);
}

void InfoPanel::paintHelp (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto* help = findHelp (hoverKey);
    if (help == nullptr)
        return;

    g.setColour (C::text);
    g.setFont (logoFont (28.0f));
    g.drawFittedText (help->title, r.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredLeft, 1);

    if (auto* p = proc.apvts.getParameter (hoverKey))
    {
        g.setColour (C::text);
        g.setFont (monoFont (22.0f));
        g.drawFittedText (p->getCurrentValueAsText(), r.removeFromTop (28.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Default " + p->getText (p->getDefaultValue(), 32), r.removeFromTop (18.0f), juce::Justification::centredLeft);
    }
    r.removeFromTop (14.0f);
    g.setColour (C::line);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    r.removeFromTop (14.0f);

    const float used = drawParagraph (g, help->text, r, uiFont (14.5f), C::text.withAlpha (0.9f), 4.0f);
    r.removeFromTop (used + 16.0f);

    juce::String detail;
    if (hoverKey == ids::material)
    {
        const int m = (int) proc.apvts.getRawParameterValue (ids::material)->load();
        detail = juce::String (dsp::getMaterialProfile (m).name) + ": " + dsp::getMaterialVoicing (m).blurb;
    }
    else if (hoverKey == ids::voidType)
    {
        const int v = (int) proc.apvts.getRawParameterValue (ids::voidType)->load();
        detail = juce::String (dsp::getVoidProfile (v).name) + ": " + dsp::getVoidBlurb (v);
    }
    if (detail.isNotEmpty())
        drawParagraph (g, detail, r, uiFont (13.5f), C::dim, 3.0f);

    if (dynamic_cast<juce::AudioParameterFloat*> (proc.apvts.getParameter (hoverKey)) != nullptr)
    {
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Double-click the knob to reset it.", getLocalBounds().withTrimmedBottom (16).removeFromBottom (16).reduced (24, 0),
                    juce::Justification::centredLeft);
    }
}

void InfoPanel::paintStatus (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& presets = getFactoryPresets();
    const int cp = proc.getCurrentProgram();
    if (cp >= 0 && cp < (int) presets.size())
    {
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f));
        g.drawText (presets[(size_t) cp].category, r.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setColour (C::text);
        g.setFont (logoFont (28.0f));
        g.drawFittedText (presets[(size_t) cp].name, r.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    const int m = (int) proc.apvts.getRawParameterValue (ids::material)->load();
    const int v = (int) proc.apvts.getRawParameterValue (ids::voidType)->load();
    auto block = [&] (const juce::String& name, const juce::String& text)
    {
        r.removeFromTop (12.0f);
        g.setColour (C::text);
        g.setFont (uiFont (14.5f, true));
        g.drawText (name, r.removeFromTop (20.0f), juce::Justification::centredLeft);
        r.removeFromTop (drawParagraph (g, text, r.withHeight (72.0f), uiFont (13.5f), C::dim, 3.0f) + 2.0f);
    };
    block (dsp::getMaterialProfile (m).name, dsp::getMaterialVoicing (m).blurb);
    block ("Outside: " + juce::String (dsp::getVoidProfile (v).name), dsp::getVoidBlurb (v));

    r.removeFromTop (16.0f);
    g.setColour (C::line);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    r.removeFromTop (14.0f);

    const auto& f = live.frame;
    int weakest = 0;
    for (int w = 1; w < dsp::kNumWalls; ++w)
        if (f.integrity[(size_t) w] < f.integrity[(size_t) weakest]) weakest = w;
    const bool permadeath = proc.apvts.getRawParameterValue (ids::heal)->load() >= kPermadeathSeconds - 0.01f;
    const float meanI = live.meanIntegrity(), stress = live.maxStress();

    auto line = [&] (const juce::String& label, const juce::String& value, bool alarm = false)
    {
        auto row = r.removeFromTop (24.0f);
        g.setColour (C::dim);
        g.setFont (uiFont (14.0f));
        g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (alarm ? C::hot : C::text);
        g.setFont (monoFont (15.0f));
        g.drawText (value, row, juce::Justification::centredRight);
    };
    line ("Standing", percent (meanI));
    line ("Weakest", juce::String (kWallNames[weakest]) + " " + percent (f.integrity[(size_t) weakest]), f.integrity[(size_t) weakest] < 0.15f);
    line ("Peak load", juce::String (stress, 2) + "x yield", stress >= 1.0f);
    line ("Repairs", f.frozen ? juce::String ("frozen") : (permadeath ? juce::String ("never (permadeath)") : juce::String ("healing")));

    r.removeFromTop (16.0f);
    g.setColour (C::line);
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    r.removeFromTop (14.0f);

    g.setColour (C::text);
    g.setFont (uiFont (14.5f, true));
    g.drawText ("Reading the house", r.removeFromTop (20.0f), juce::Justification::centredLeft);
    r.removeFromTop (4.0f);
    drawParagraph (g, "Fine cracks: reflections start to scatter.\n"
                      "Bare brick or rebar: the wall is loose and rings at its own pitch.\n"
                      "Holes and missing tiles: sound leaks outside.\n"
                      "Flying debris: a fracture, heard as impact and rubble.",
                   r, uiFont (13.5f), C::dim, 5.0f);
}

//==============================================================================
// Header
//==============================================================================
Header::Header (RoomBreakerAudioProcessor& p, const LiveState& s)
    : proc (p), live (s),
      detonateB (p.apvts, ids::detonate, "Detonate", true),
      rebuildB (p.apvts, ids::reset, "Rebuild"),
      input (p.apvts, ids::inputGain, "Input", Knob::Small),
      mix (p.apvts, ids::mix, "Dry/wet", Knob::Small),
      output (p.apvts, ids::outputGain, "Output", Knob::Small),
      oversampling (p.apvts, ids::oversampling, { "Off", "2x", "4x" }),
      freeze (p.apvts, ids::freeze, "Freeze", C::ice)
{
    const auto& list = getFactoryPresets();
    juce::String lastCategory;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (lastCategory != list[i].category)
        {
            lastCategory = list[i].category;
            presets.addSectionHeading (lastCategory);
        }
        presets.addItem (list[i].name, (int) i + 1);
    }
    presets.setTextWhenNothingSelected ("Choose a preset");
    presets.onChange = [this]
    {
        const int index = presets.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentProgram())
            loadPreset (index);
    };
    setInfo (presets, "preset");
    setInfo (prevB, "preset");
    setInfo (nextB, "preset");
    const int count = (int) list.size();
    prevB.onClick = [this, count] { loadPreset ((proc.getCurrentProgram() + count - 1) % count); };
    nextB.onClick = [this, count] { loadPreset ((proc.getCurrentProgram() + 1) % count); };

    constexpr const char* tabNames[] = { "Room", "Engine" };
    for (int t = 0; t < 2; ++t)
    {
        auto& b = tabs[(size_t) t];
        b.setButtonText (tabNames[t]);
        b.getProperties().set ("tab", true);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (7171);
        b.onClick = [this, t] { if (tabs[(size_t) t].getToggleState() && onTab) onTab (t); };
        addAndMakeVisible (b);
    }

    setInfo (statusArea, "status");
    setInfo (meterArea, "meters");
    for (juce::Component* c : { (juce::Component*) &presets, (juce::Component*) &prevB, (juce::Component*) &nextB,
                                (juce::Component*) &detonateB, (juce::Component*) &rebuildB, (juce::Component*) &input,
                                (juce::Component*) &mix, (juce::Component*) &output, (juce::Component*) &freeze,
                                (juce::Component*) &oversampling, (juce::Component*) &statusArea, (juce::Component*) &meterArea })
        addAndMakeVisible (c);
    refreshPreset();
}

void Header::setTab (int t)
{
    tabs[(size_t) juce::jlimit (0, 1, t)].setToggleState (true, juce::dontSendNotification);
}

void Header::loadPreset (int index)
{
    proc.setCurrentProgram (index);
    proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
    refreshPreset();
    if (auto* parent = getParentComponent())
        parent->repaint();
}

void Header::refreshPreset()
{
    shownProgram = proc.getCurrentProgram();
    presets.setSelectedId (shownProgram + 1, juce::dontSendNotification);
}

void Header::resized()
{
    tabs[0].setBounds (22, 52, 58, 26);
    tabs[1].setBounds (88, 52, 66, 26);

    presets.setBounds (316, 12, 236, 30);
    prevB.setBounds (556, 12, 30, 30);
    nextB.setBounds (588, 12, 30, 30);
    detonateB.setBounds (316, 48, 150, 28);
    rebuildB.setBounds (472, 48, 146, 28);

    input.setBounds (646, 2, 72, 80);
    mix.setBounds (720, 2, 72, 80);
    output.setBounds (794, 2, 72, 80);

    oversampling.setBounds (896, 30, 186, 24);
    freeze.setBounds (896, 58, 120, 22);

    statusArea.setBounds (1104, 6, 106, 72);
    meterArea.setBounds (1214, 4, 60, 76);
}

void Header::tick()
{
    if (proc.getCurrentProgram() != shownProgram)   // host changed the program
        refreshPreset();
    if (const int d = proc.detonateCount.load(); d != lastDetonate) { lastDetonate = d; detonateGlow = 18; }
    if (detonateGlow > 0) --detonateGlow;
    repaint (0, 0, 300, getHeight());
    repaint (1090, 0, 190, getHeight());
}

void Header::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (C::bg2);
    g.setColour (detonateGlow > 0 ? C::hot.withAlpha (0.4f + 0.6f * (float) detonateGlow / 18.0f) : C::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());
    g.setColour (C::line);
    for (int x : { 296, 632, 878, 1090 })
        g.drawVerticalLine (x, 14.0f, r.getBottom() - 14.0f);

    g.setFont (logoFont (38.0f));
    g.setColour (C::text);
    g.drawText ("ROOMBREAKER", 22, 8, 270, 40, juce::Justification::centredLeft);

    g.setColour (C::dim);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("Oversampling", 896, 8, 186, 18, juce::Justification::centredLeft);

    // status
    const double sr = proc.getHostSampleRate();
    const int factor = proc.activeFactor.load();
    const bool sc = proc.sidechainConnected.load();
    g.setFont (monoFont (12.5f));
    g.setColour (C::dim);
    g.drawText (juce::String (sr / 1000.0, 1) + " kHz  " + (factor > 1 ? juce::String (factor) + "x" : juce::String ("1x")), 1104, 12, 106, 16, juce::Justification::centredLeft);
    g.drawText ("CPU " + juce::String (live.cpu * 100.0f, 1) + "%", 1104, 32, 106, 16, juce::Justification::centredLeft);
    g.setColour (sc ? C::text : C::dim);
    g.drawText (sc ? "Sidechain in" : "No sidechain", 1104, 52, 106, 16, juce::Justification::centredLeft);

    auto meter = [&] (int x, float v, const char* lbl)
    {
        const juce::Rectangle<float> bar ((float) x, 10.0f, 10.0f, 58.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (bar);
        const float db = juce::Decibels::gainToDecibels (v, -100.0f);
        constexpr int segs = 18;
        const float sh = (bar.getHeight() - 2.0f) / (float) segs;
        for (int k = 0; k < segs; ++k)
        {
            const float segDb = -54.0f + 54.0f * (float) (k + 1) / (float) segs;
            const bool lit = db >= segDb - 3.0f;
            const auto col = segDb > -3.0f ? C::hot : C::text.withAlpha (0.8f);
            g.setColour (lit ? col : col.withAlpha (0.08f));
            g.fillRect (bar.getX() + 1.0f, bar.getBottom() - 1.0f - (float) (k + 1) * sh + 1.0f, bar.getWidth() - 2.0f, sh - 1.0f);
        }
        g.setColour (C::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (lbl, x - 8, 70, 26, 12, juce::Justification::centred);
    };
    meter (1224, live.inMeter, "In");
    meter (1248, live.outMeter, "Out");
}

//==============================================================================
// Editor
//==============================================================================
RoomBreakerAudioProcessorEditor::RoomBreakerAudioProcessorEditor (RoomBreakerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      header (p, live), structure (live, p.apvts), roomPanel (p), enginePage (p, live), info (p, live)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (root);
    root.addAndMakeVisible (header);
    root.addAndMakeVisible (structure);
    root.addAndMakeVisible (roomPanel);
    root.addChildComponent (enginePage);
    root.addAndMakeVisible (info);
    root.addMouseListener (&info, true);   // INFO follows the mouse everywhere
    header.onTab = [this] (int t) { setTab (t); };
    setTab (0);

    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.6), (int) (kH * 0.6), (int) (kW * 1.6), (int) (kH * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (kW, kH);
    startTimerHz (30);
}

RoomBreakerAudioProcessorEditor::~RoomBreakerAudioProcessorEditor()
{
    stopTimer();
    root.removeMouseListener (&info);
    setLookAndFeel (nullptr);
}

void RoomBreakerAudioProcessorEditor::setTab (int t)
{
    tab = juce::jlimit (0, 1, t);
    structure.setVisible (tab == 0);
    roomPanel.setVisible (tab == 0);
    enginePage.setVisible (tab == 1);
    header.setTab (tab);
}

void RoomBreakerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (rb::ui::C::bg);
}

void RoomBreakerAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (s));
    root.setBounds (0, 0, kW, kH);
    header.setBounds (0, 0, kW, kHeaderH);
    structure.setBounds (0, kHeaderH, 600, kH - kHeaderH);
    roomPanel.setBounds (600, kHeaderH, 400, kH - kHeaderH);
    enginePage.setBounds (0, kHeaderH, 1000, kH - kHeaderH);
    info.setBounds (1000, kHeaderH, kW - 1000, kH - kHeaderH);
}

void RoomBreakerAudioProcessorEditor::advance (float seconds)
{
    for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f)
        timerCallback();
}

void RoomBreakerAudioProcessorEditor::timerCallback()
{
    rb::dsp::TelemetryFrame frame;
    if (audioProcessor.telemetry.popLatest (frame))
        live.push (frame);
    live.tick();

    auto take = [] (std::atomic<float>& a, float& meter)
    {
        meter = std::max (a.exchange (0.0f, std::memory_order_relaxed), meter * 0.86f);
    };
    take (audioProcessor.inPeak, live.inMeter);
    take (audioProcessor.outPeak, live.outMeter);
    take (audioProcessor.keyPeak, live.keyMeter);
    live.cpu += (audioProcessor.getCpuLoad() - live.cpu) * 0.2f;

    header.tick();
    if (tab == 0)
    {
        structure.refresh (1.0f / 30.0f);
        roomPanel.repaint (0, 636, roomPanel.getWidth(), 80);   // preset notes
    }
    else
        enginePage.repaint();
    info.repaint();
}
