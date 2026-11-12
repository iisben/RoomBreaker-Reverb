#include "PluginEditor.h"

using namespace rb;
using namespace rb::ui;

namespace
{
    // Layout, in design units (1200 x 750). See the diagram in PluginEditor.h.
    constexpr int kHeaderH = 48, kFooterH = 32, kRailW = 88, kPanelW = 392;
    constexpr int kBodyY = kHeaderH, kBodyH = kDesignH - kHeaderH - kFooterH;
    constexpr int kPanelX = kDesignW - kRailW - kPanelW;
    const juce::Rectangle<int> kDisplayArea { kRailW + 8, kBodyY + 8, kPanelX - kRailW - 16, kBodyH - 16 };

    juce::String infoKeyFor (juce::Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (const auto* v = c->getProperties().getVarPointer ("info"))
                return v->toString();
        return {};
    }

    juce::Colour stateColour (float I)
    {
        return I >= 0.97f ? C::text1 : I >= 0.45f ? C::accent : C::danger;
    }

    const char* kSurfaceLetters[] = { "N", "S", "E", "W", "R", "F" };
    const char* kSurfaceNames[]   = { "North wall", "South wall", "East wall", "West wall", "Roof", "Floor" };
}

//==============================================================================
// Info line
//==============================================================================
InfoLine::InfoLine()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (5);
}

void InfoLine::mouseEnter (const juce::MouseEvent& e)
{
    const auto k = infoKeyFor (e.eventComponent);
    if (k.isNotEmpty())
    {
        clearAt = 0;
        if (k != key) { key = k; repaint(); }
    }
}

void InfoLine::mouseExit (const juce::MouseEvent&)
{
    // Moving from a control across a gap shouldn't flicker the line: clear a moment later.
    clearAt = juce::Time::getMillisecondCounter() + 400;
}

void InfoLine::timerCallback()
{
    if (clearAt != 0 && juce::Time::getMillisecondCounter() >= clearAt)
    {
        clearAt = 0;
        key.clear();
        repaint();
    }
}

void InfoLine::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (const auto* h = findHelp (key))
    {
        const juce::String title = juce::String (h->title).toUpperCase();
        g.setFont (type::caps());
        g.setColour (C::text0);
        g.drawText (title, r, juce::Justification::centredLeft, false);
        r.removeFromLeft (type::width (type::caps(), title) + 12.0f);

        juce::String text (h->text);
        if (const int dot = text.indexOf (". "); dot > 0)
            text = text.substring (0, dot + 1);   // the first sentence; the rest is in the manual
        g.setFont (type::text());
        g.setColour (C::text1);
        g.drawText (text, r, juce::Justification::centredLeft, true);
        return;
    }
    g.setFont (type::text());
    g.setColour (C::text2);
    g.drawText ("Drag a knob to set it.  Shift: fine.  Wheel: step.  Double-click: type a value.  Alt-click: default.  Right-click: more.",
                r, juce::Justification::centredLeft, true);
}

//==============================================================================
// The display
//==============================================================================
DisplayOverlay::DisplayOverlay (const LiveState& l, const HouseView& h, RoomBreakerAudioProcessor& p)
    : live (l), house (h), proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void DisplayOverlay::refresh()
{
    juce::uint64 sig = 1469598103934665603ull;
    auto mix = [&sig] (int v) { sig = (sig ^ (juce::uint64) (juce::int64) v) * 1099511628211ull; };
    for (size_t w = 0; w < 6; ++w)
    {
        mix (juce::roundToInt (live.frame.integrity[w] * 400.0f));
        mix (juce::roundToInt (std::min (live.frame.stressRatio[w], 1.5f) * 40.0f));
    }
    mix (house.hoveredWall());
    mix (house.isMouseOverOrDragging() ? 1 : 0);
    if (auto* b = proc.apvts.getRawParameterValue (ids::bypass)) mix (b->load() > 0.5f ? 1 : 0);
    if (sig != shown)
    {
        shown = sig;
        repaint();
    }
}

void DisplayOverlay::paint (juce::Graphics& g)
{
    const auto& f = live.frame;

    // Condition readout: overall, then each surface with its load against yield.
    {
        const auto r = readoutBounds();
        g.setColour (C::bg0.withAlpha (0.88f));
        g.fillRect (r);
        g.setColour (C::line);
        g.drawRect (r, 1.0f);

        auto in = r.reduced (12.0f, 9.0f);
        const float I = live.meanIntegrity();
        auto row = in.removeFromTop (16.0f);
        g.setFont (type::caps());
        g.setColour (C::text1);
        g.drawText ("CONDITION", row, juce::Justification::centredLeft, false);
        g.setColour (stateColour (I));
        g.drawText (integrityState (I), row, juce::Justification::centredRight, false);

        in.removeFromTop (4.0f);
        row = in.removeFromTop (18.0f);
        g.setFont (type::mono());
        g.setColour (C::text0);
        g.drawText (juce::String (juce::roundToInt (I * 100.0f)) + "%", row.removeFromLeft (44.0f), juce::Justification::centredLeft, false);
        const auto bar = row.withSizeKeepingCentre (row.getWidth(), 4.0f);
        g.setColour (C::edge);
        g.fillRect (bar);
        g.setColour (I < 0.45f ? C::danger : C::text1);
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, I)));

        in.removeFromTop (6.0f);
        const float colW = (in.getWidth() - 5.0f * 8.0f) / 6.0f;
        const int hovered = house.hoveredWall();
        for (int w = 0; w < 6; ++w)
        {
            const auto col = juce::Rectangle<float> (in.getX() + (float) w * (colW + 8.0f), in.getY(), colW, in.getHeight());
            const bool hot = w == hovered;
            const float wi = juce::jlimit (0.0f, 1.0f, f.integrity[(size_t) w]);
            g.setFont (type::caps());
            g.setColour (hot ? C::accent : C::text2);
            g.drawText (kSurfaceLetters[w], col.withHeight (13.0f), juce::Justification::centredLeft, false);
            const auto b = juce::Rectangle<float> (col.getX(), col.getY() + 18.0f, colW, 3.0f);
            g.setColour (C::edge);
            g.fillRect (b);
            g.setColour (hot ? C::accent : wi < 0.45f ? C::danger : C::text1);
            g.fillRect (b.withWidth (colW * wi));
            // load against yield: a tick that reaches the end of the bar at the yield threshold
            const float s = f.stressRatio[(size_t) w];
            if (s > 0.02f)
            {
                g.setColour (s >= 1.0f ? C::danger : C::text0);
                g.fillRect (b.getX() + colW * std::min (s, 1.0f) - 1.0f, b.getY() - 3.0f, 1.5f, 9.0f);
            }
        }
    }

    // The surface under the mouse, in words.
    if (const int w = house.hoveredWall(); w >= 0 && w < 6)
    {
        const auto r = readoutBounds().withY (readoutBounds().getBottom() + 6.0f).withHeight (26.0f);
        g.setColour (C::bg0.withAlpha (0.88f));
        g.fillRect (r);
        g.setColour (C::line);
        g.drawRect (r, 1.0f);
        const float I = f.integrity[(size_t) w];
        auto in = r.reduced (12.0f, 0.0f);
        g.setFont (type::caps());
        g.setColour (C::accent);
        g.drawText (juce::String (kSurfaceNames[w]).toUpperCase(), in, juce::Justification::centredLeft, false);
        g.setFont (type::mono (type::S));
        g.setColour (C::text0);
        g.drawText (juce::String (juce::roundToInt (I * 100.0f)) + "%  load " + juce::String (f.stressRatio[(size_t) w], 2) + "x",
                    in, juce::Justification::centredRight, false);
    }

    // How to move it, while the mouse is on it.
    if (house.isMouseOverOrDragging())
    {
        const juce::String hint = "DRAG TO TURN    DOUBLE-CLICK TO FACE FRONT";
        const float w = type::width (type::caps(), hint) + 24.0f;
        const auto r = juce::Rectangle<float> (12.0f, (float) getHeight() - 38.0f, w, 26.0f);
        g.setColour (C::bg0.withAlpha (0.8f));
        g.fillRect (r);
        g.setFont (type::caps());
        g.setColour (C::text1);
        g.drawText (hint, r, juce::Justification::centred, false);
    }

    // Bypassed: the picture steps back and says so.
    if (auto* b = proc.apvts.getRawParameterValue (ids::bypass); b != nullptr && b->load() > 0.5f)
    {
        g.setColour (C::bg0.withAlpha (0.6f));
        g.fillRect (getLocalBounds());
        const auto tag = getLocalBounds().toFloat().withSizeKeepingCentre (120.0f, 32.0f);
        g.setColour (C::bg0);
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (C::accent);
        g.drawRoundedRectangle (tag.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (type::caps());
        g.drawText ("BYPASSED", tag, juce::Justification::centred, false);
    }
}

Display::Display (LiveState& s, RoomBreakerAudioProcessor& p)
    : house (s, p.apvts), overlay (s, house, p)
{
    setInfo (house, "monitor");
    setInfo (readoutTarget, "condition");
    house.setBufferedToImage (true);   // the overlay repaints over it without redrawing the house
    addAndMakeVisible (house);
    addAndMakeVisible (overlay);
    addAndMakeVisible (readoutTarget);
}

void Display::resized()
{
    house.setBounds (getLocalBounds().reduced (1));
    overlay.setBounds (house.getBounds());
    readoutTarget.setBounds (overlay.readoutBounds().translated (1.0f, 1.0f).getSmallestIntegerContainer());
}

void Display::paintOverChildren (juce::Graphics& g)
{
    g.setColour (C::line);
    g.drawRect (getLocalBounds(), 1);
}

void Display::refresh (float dt)
{
    house.tick (dt);   // repaints itself when something changed
    overlay.refresh();
}

//==============================================================================
// Controls
//==============================================================================
namespace
{
    struct KnobSpec { const char* id; const char* name; double origin; };
    struct Section  { const char* title; KnobSpec knobs[4]; };

    // Four sections, four knobs each: what the room is, how it breaks, its size and time, its colour.
    const Section kSections[] =
    {
        { "Structure",   { { ids::yield, "Yield", 0.0 }, { ids::bias, "Ductility", 0.0 }, { ids::asymmetry, "Asymmetry", 0.0 }, { ids::heal, "Healing", 0.0 } } },
        { "Destruction", { { ids::impact, "Impact", 0.0 }, { ids::rumble, "Rumble", 0.0 }, { ids::debris, "Debris", 0.0 }, { ids::drive, "Drive", 0.0 } } },
        { "Space",       { { ids::size, "Size", 0.0 }, { ids::decay, "Decay", 0.0 }, { ids::predelay, "Pre-delay", 0.0 }, { ids::density, "Density", 0.0 } } },
        { "Tone",        { { ids::lowCut, "Low cut", 0.0 }, { ids::highCut, "High cut", 0.0 }, { ids::width, "Width", 0.5 }, { ids::motion, "Motion", 0.0 } } },
    };

    constexpr int kPad = 12, kKnobW = 92, kDial = 44;
    constexpr int kSectionY0 = 102, kSectionStep = 122;
}

ControlPanel::ControlPanel (RoomBreakerAudioProcessor& p)
    : material (p.apvts, ids::material), outside (p.apvts, ids::voidType),
      freeze (p.apvts, ids::freeze, "Freeze"),
      rebuild (p.apvts, ids::reset, "Rebuild", false),
      detonate (p.apvts, ids::detonate, "Detonate", true)
{
    addAndMakeVisible (material);
    addAndMakeVisible (outside);
    for (const auto& s : kSections)
        for (const auto& k : s.knobs)
        {
            auto* knob = knobs.add (new Knob (p.apvts, k.id, k.name, (float) kDial));
            knob->setArcOrigin (k.origin);
            addAndMakeVisible (knob);
        }
    addAndMakeVisible (freeze);
    addAndMakeVisible (rebuild);
    addAndMakeVisible (detonate);
}

Knob* ControlPanel::findKnob (const juce::String& id) const
{
    for (auto* k : knobs)
        if (k->getParameter().getParameterID() == id)
            return k;
    return nullptr;
}

void ControlPanel::resized()
{
    const int fieldW = (getWidth() - 2 * kPad - 12) / 2;
    material.setBounds (kPad, 52, fieldW, 32);
    outside.setBounds (kPad + fieldW + 12, 52, fieldW, 32);

    for (int s = 0; s < 4; ++s)
        for (int i = 0; i < 4; ++i)
            knobs[s * 4 + i]->setBounds (kPad + i * kKnobW, kSectionY0 + s * kSectionStep + 22, kKnobW, Knob::heightFor ((float) kDial));

    const int y = kSectionY0 + 4 * kSectionStep + 6, w = (getWidth() - 2 * kPad - 16) / 3;
    freeze.setBounds (kPad, y, w, 36);
    rebuild.setBounds (kPad + w + 8, y, w, 36);
    detonate.setBounds (getWidth() - kPad - w, y, w, 36);
}

void ControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (C::bg1);
    const float w = (float) getWidth() - 2.0f * kPad;
    drawSectionHeader (g, { (float) kPad, 12.0f, w, 16.0f }, "Room");
    g.setFont (type::caps());
    g.setColour (C::text2);
    g.drawText ("MATERIAL", material.getBounds().toFloat().withY (34.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
    g.drawText ("OUTSIDE", outside.getBounds().toFloat().withY (34.0f).withHeight (14.0f), juce::Justification::centredLeft, false);

    for (int s = 0; s < 4; ++s)
        drawSectionHeader (g, { (float) kPad, (float) (kSectionY0 + s * kSectionStep), w, 16.0f }, kSections[s].title);
}

//==============================================================================
Rail::Rail (RoomBreakerAudioProcessor& p, bool isInput)
    : input (isInput),
      meter (isInput ? StereoMeter::Source { { &p.inMeter[0].peak, &p.inMeter[1].peak }, { &p.inMeter[0].meanSquare, &p.inMeter[1].meanSquare } }
                     : StereoMeter::Source { { &p.outMeter[0].peak, &p.outMeter[1].peak }, { &p.outMeter[0].meanSquare, &p.outMeter[1].meanSquare } },
             isInput ? "In" : "Out")
{
    addAndMakeVisible (meter);
    auto add = [&] (const char* id, const char* name, double origin)
    {
        auto* k = knobs.add (new Knob (p.apvts, id, name, 40.0f));
        k->setArcOrigin (origin);
        addAndMakeVisible (k);
    };
    auto zeroDb = [&] (const char* id)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id));
        return param != nullptr ? (double) param->convertTo0to1 (0.0f) : 0.0;
    };
    if (input)
    {
        add (ids::inputGain, "Input", zeroDb (ids::inputGain));
        breakBy = std::make_unique<Segmented> (p.apvts, ids::stressSource, juce::StringArray { "Input", "Key", "Both" }, true);
        addAndMakeVisible (*breakBy);
    }
    else
    {
        add (ids::mix, "Mix", 0.0);
        add (ids::duck, "Ducking", 0.0);
        add (ids::outputGain, "Output", zeroDb (ids::outputGain));
    }
}

void Rail::resized()
{
    meter.setBounds (14, 14, getWidth() - 28, 336);
    int y = 366;
    for (auto* k : knobs)
    {
        k->setBounds (0, y, getWidth(), Knob::heightFor (40.0f));
        y += Knob::heightFor (40.0f) + 8;
    }
    if (breakBy != nullptr)
        breakBy->setBounds (12, y + 20, getWidth() - 24, 84);
}

void Rail::paint (juce::Graphics& g)
{
    if (breakBy != nullptr)
    {
        g.setFont (type::caps());
        g.setColour (C::text1);
        g.drawText ("BREAK BY", breakBy->getBounds().toFloat().translated (0.0f, -20.0f).withHeight (14.0f),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
// Header
//==============================================================================
class Header::PresetName : public juce::Button
{
public:
    explicit PresetName (RoomBreakerAudioProcessor& p) : juce::Button ("Preset"), proc (p)
    {
        setInfo (*this, "preset");
        setWantsKeyboardFocus (false);
        refresh();
    }

    void refresh()
    {
        auto& lib = proc.presets;
        const auto e = lib.entry (lib.current());
        const auto n = lib.currentName();
        const auto c = lib.current() >= 0 ? e.category : juce::String();
        const bool m = lib.isModified();
        if (n != name || c != category || m != modified)
        {
            name = n;
            category = c;
            modified = m;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (C::bg0);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (open || down ? C::accent : over ? C::edgeHi : C::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        auto in = r.reduced (12.0f, 0.0f);
        drawIcon (g, Icon::caret, in.removeFromRight (16.0f).withSizeKeepingCentre (16.0f, 16.0f), over || open ? C::text0 : C::text1);
        in.removeFromRight (10.0f);
        if (category.isNotEmpty())
        {
            const auto cat = category.toUpperCase();
            g.setFont (type::caps());
            g.setColour (C::text2);
            g.drawText (cat, in, juce::Justification::centredRight, false);
            in.removeFromRight (type::width (type::caps(), cat) + 14.0f);
        }
        if (modified)
        {
            g.setColour (C::accent);
            g.fillEllipse (in.getX(), in.getCentreY() - 3.0f, 6.0f, 6.0f);
            in.removeFromLeft (14.0f);
        }
        g.setFont (type::strong());
        g.setColour (C::text0);
        g.drawText (name, in, juce::Justification::centredLeft, true);
    }

    bool open = false;

private:
    RoomBreakerAudioProcessor& proc;
    juce::String name, category;
    bool modified = false;
};

Header::Header (RoomBreakerAudioProcessor& p)
    : proc (p),
      bypass (*p.apvts.getParameter (ids::bypass), [this] (float v) { bypassed = v > 0.5f; power.setLit (bypassed); repaint(); }, nullptr)
{
    name = std::make_unique<PresetName> (p);
    name->onClick = [this] { if (onBrowse) onBrowse(); };
    prev.onClick = [this] { step (-1); };
    next.onClick = [this] { step (+1); };
    save.onClick = [this] { if (onSave) onSave(); };
    power.onClick = [this] { bypass.setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f); };
    scale.onClick = [this] { showScaleMenu(); };
    for (auto* b : { (juce::Button*) &prev, (juce::Button*) &next, (juce::Button*) &save, (juce::Button*) &power,
                     (juce::Button*) name.get(), (juce::Button*) &scale })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    setInfo (prev, "preset");
    setInfo (next, "preset");
    setInfo (save, "preset");
    setInfo (power, "bypass");
    setInfo (scale, "scale");
    bypass.sendInitialUpdate();
}

void Header::resized()
{
    const int groupW = 32 + 4 + 32 + 8 + 360 + 8 + 32;
    int x = (getWidth() - groupW) / 2;
    prev.setBounds (x, 8, 32, 32);            x += 36;
    next.setBounds (x, 8, 32, 32);            x += 40;
    name->setBounds (x, 8, 360, 32);          x += 368;
    save.setBounds (x, 8, 32, 32);
    power.setBounds (getWidth() - 16 - 32, 8, 32, 32);
    scale.setBounds (power.getX() - 8 - 64, 10, 64, 28);
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (C::bg1);
    // wordmark: the name, and the version in the scale's mono
    const juce::String word = "ROOMBREAKER";
    const auto f = type::strong().withExtraKerningFactor (0.14f);
    g.setFont (f);
    g.setColour (C::text0);
    g.drawText (word, juce::Rectangle<float> (20.0f, 0.0f, 200.0f, (float) getHeight()), juce::Justification::centredLeft, false);
    g.setFont (type::mono (type::S));
    g.setColour (C::text2);
    g.drawText (JucePlugin_VersionString, juce::Rectangle<float> (26.0f + type::width (f, word), 1.0f, 60.0f, (float) getHeight()),
                juce::Justification::centredLeft, false);

    // status, next to the switch that sets it
    const auto status = juce::Rectangle<float> ((float) scale.getX() - 100.0f, 0.0f, 88.0f, (float) getHeight());
    g.setFont (type::caps());
    g.setColour (bypassed ? C::accent : C::text2);
    g.drawText (bypassed ? "BYPASSED" : "ACTIVE", status, juce::Justification::centredRight, false);
}

void Header::tick()
{
    name->refresh();
    const auto s = juce::String (juce::roundToInt (proc.editorScale.load() * 100.0f)) + "%";
    if (scale.getButtonText() != s)
        scale.setButtonText (s);
}

void Header::setBrowserOpen (bool isOpen)
{
    name->open = isOpen;
    name->repaint();
}

void Header::step (int delta)
{
    auto& lib = proc.presets;
    const int n = lib.size();
    if (n == 0)
        return;
    const int cur = lib.current();
    lib.load (cur < 0 ? (delta > 0 ? 0 : n - 1) : (cur + delta + n) % n);
    name->refresh();
}

void Header::showScaleMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Interface size");
    const float now = proc.editorScale.load();
    for (const int pc : { 75, 100, 125, 150, 175, 200 })
    {
        const float s = (float) pc / 100.0f;
        m.addItem (juce::String (pc) + " %", true, std::abs (s - now) < 0.02f, [this, s] { if (onScale) onScale (s); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scale).withStandardItemHeight (26));
}

//==============================================================================
// Footer
//==============================================================================
Footer::Footer (RoomBreakerAudioProcessor& p)
    : proc (p), oversampling (p.apvts, ids::oversampling, juce::StringArray { "1x", "2x", "4x" }),
      limiter (p.apvts, ids::limiter, "Limiter")
{
    addAndMakeVisible (info);
    addAndMakeVisible (oversampling);
    addAndMakeVisible (limiter);
    addAndMakeVisible (statusTarget);
    setInfo (statusTarget, "status");
}

void Footer::resized()
{
    const int h = getHeight();
    statusTarget.setBounds (getWidth() - 16 - 236, 0, 236, h);
    limiter.setBounds (statusTarget.getX() - 16 - 84, 5, 84, h - 10);
    oversampling.setBounds (limiter.getX() - 12 - 108, 5, 108, h - 10);
    info.setBounds (16, 0, oversampling.getX() - 16 - 110, h);
}

void Footer::tick()
{
    cpu += (proc.getCpuLoad() - cpu) * 0.4f;
    auto khz = [] (double hz) { return juce::String (hz / 1000.0, std::fmod (hz, 1000.0) == 0.0 ? 0 : 1); };
    const double rate = proc.getHostSampleRate();
    const int factor = proc.activeFactor.load();
    const auto s = khz (rate) + " kHz" + (factor > 1 ? "  room " + khz (rate * factor) + " kHz" : juce::String())
                 + "  cpu " + juce::String (cpu * 100.0f, 1) + " %";
    if (s != status)
    {
        status = s;
        repaint (statusTarget.getBounds());
    }
}

void Footer::paint (juce::Graphics& g)
{
    g.fillAll (C::bg1);
    g.setFont (type::caps());
    g.setColour (C::text2);
    g.drawText ("OVERSAMPLING", oversampling.getBounds().toFloat().translated (-104.0f, 0.0f).withWidth (96.0f),
                juce::Justification::centredRight, false);
    g.setFont (type::mono (type::S));
    g.drawText (status, statusTarget.getBounds().toFloat(), juce::Justification::centredRight, false);
}

//==============================================================================
void Canvas::paint (juce::Graphics& g)
{
    g.fillAll (C::bg0);
    g.setColour (C::line);
    g.fillRect (0, kHeaderH, kDesignW, 1);                       // under the header
    g.fillRect (0, kDesignH - kFooterH - 1, kDesignW, 1);        // over the footer
    g.fillRect (kRailW - 1, kBodyY, 1, kBodyH);                  // input rail
    g.fillRect (kDesignW - kRailW, kBodyY, 1, kBodyH);           // output rail
    g.fillRect (kPanelX - 1, kBodyY, 1, kBodyH);                 // controls
}

//==============================================================================
// Editor
//==============================================================================
RoomBreakerAudioProcessorEditor::RoomBreakerAudioProcessorEditor (RoomBreakerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      header (p), inRail (p, true), outRail (p, false), display (live, p), controls (p), footer (p), browser (p.presets)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (canvas);
    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &inRail, (juce::Component*) &outRail,
                                (juce::Component*) &display, (juce::Component*) &controls, (juce::Component*) &footer })
        canvas.addAndMakeVisible (c);
    canvas.addChildComponent (browser);
    canvas.addMouseListener (&footer.info, true);   // the info line follows the mouse everywhere

    header.setBounds (0, 0, kW, kHeaderH);
    inRail.setBounds (0, kBodyY, kRailW, kBodyH);
    outRail.setBounds (kW - kRailW, kBodyY, kRailW, kBodyH);
    display.setBounds (kDisplayArea);
    controls.setBounds (kPanelX, kBodyY, kPanelW, kBodyH);
    footer.setBounds (0, kH - kFooterH, kW, kFooterH);
    browser.setBounds (kRailW, kBodyY, kW - 2 * kRailW, kBodyH);

    header.onBrowse = [this] { if (browser.isVisible()) browser.close(); else { browser.open(); header.setBrowserOpen (true); } };
    header.onSave = [this] { if (! browser.isVisible()) { browser.open(); header.setBrowserOpen (true); } browser.startSave(); };
    header.onScale = [this] (float s) { setScale (s); };
    browser.onClose = [this] { header.setBrowserOpen (false); };
    browser.onPresetLoaded = [this] { header.tick(); };

    // Resizing: the corner handle and the host both go through the constrainer, which keeps the
    // design aspect ratio, so one scale factor fits both axes (see resized()). The saved size is
    // read first: setting the limits resizes the (still empty) window, and that must not be saved.
    const float s = juce::jlimit (kMinScale, kMaxScale, p.editorScale.load());
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (kW * kMinScale), juce::roundToInt (kH * kMinScale),
                     juce::roundToInt (kW * kMaxScale), juce::roundToInt (kH * kMaxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
    sizeReady = true;
    audioProcessor.editorScale.store (scale);
    startTimerHz (30);
}

RoomBreakerAudioProcessorEditor::~RoomBreakerAudioProcessorEditor()
{
    stopTimer();
    canvas.removeMouseListener (&footer.info);
    setLookAndFeel (nullptr);
}

void RoomBreakerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (rb::ui::C::bg0);   // only visible if a host forces a size off the aspect ratio
}

void RoomBreakerAudioProcessorEditor::resized()
{
    // Everything is laid out once, in design units, inside the canvas; resizing only changes the
    // canvas's transform. Text and paths are drawn as vectors at the final scale, and the house's
    // cached layers are rendered at the physical pixel density, so nothing is ever a stretched bitmap.
    // A host that ignores the aspect ratio gets the largest fit, centred.
    scale = std::min ((float) getWidth() / (float) kW, (float) getHeight() / (float) kH);
    const float dx = ((float) getWidth() - (float) kW * scale) * 0.5f;
    const float dy = ((float) getHeight() - (float) kH * scale) * 0.5f;
    canvas.setBounds (0, 0, kW, kH);
    canvas.setTransform (juce::AffineTransform::scale (scale).translated (dx, dy));
    if (sizeReady)
        audioProcessor.editorScale.store (scale);
}

void RoomBreakerAudioProcessorEditor::setScale (float s)
{
    s = juce::jlimit (kMinScale, kMaxScale, s);
    setSize (juce::roundToInt (kW * s), juce::roundToInt (kH * s));
}

void RoomBreakerAudioProcessorEditor::advance (float seconds)
{
    for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f)
        timerCallback();
}

void RoomBreakerAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 30.0f;
    rb::dsp::TelemetryFrame f;
    if (audioProcessor.telemetry.popLatest (f))
        live.push (f);
    live.tick();

    display.refresh (dt);
    inRail.tick (dt);
    outRail.tick (dt);
    ++frame;
    if (frame % 3 == 0)  header.tick();   // 10 Hz: preset name, modified mark, size
    if (frame % 15 == 0) footer.tick();   // 2 Hz: engine status
}
