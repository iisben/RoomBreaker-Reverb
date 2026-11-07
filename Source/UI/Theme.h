#pragma once
//==============================================================================
// RoomBreaker look: warm matte panels, DIN lettering, one signal orange.
//
// Rules this file enforces, so the panel reads as designed rather than generated:
//   * one accent colour, used only for destruction (DETONATE, fractures, overs)
//   * no coloured side-bars, no cards inside cards, no rainbow colour coding
//   * hierarchy from size and weight, not from boxes; sentence-case headings
//   * text never below ~4.5:1 contrast on its panel
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace rb::ui
{
namespace C
{
    inline const juce::Colour bg     { 0xff151413 };
    inline const juce::Colour bg2    { 0xff1a1917 };
    inline const juce::Colour panel  { 0xff1f1d1b };
    inline const juce::Colour panel2 { 0xff2a2724 };
    inline const juce::Colour line   { 0xff3a3631 };
    inline const juce::Colour text   { 0xffeae4d6 };
    inline const juce::Colour dim    { 0xffa79f90 };   // secondary text: 6:1 on panel
    inline const juce::Colour faint  { 0xff6b645b };   // rules and scale marks only, never text
    inline const juce::Colour hot    { 0xffe2552d };   // the one accent: destruction
    inline const juce::Colour ice    { 0xff9cbfd0 };   // Freeze state only
    inline const juce::Colour knob   { 0xff312e2a };
    inline const juce::Colour arc    { 0xffd9cfb8 };   // knob value arcs: one ink for all

    // kept for older call sites
    inline const juce::Colour cream  { 0xffd9cfb8 };
    inline const juce::Colour rust   { 0xffc8673e };
    inline const juce::Colour violet { 0xffd1a54a };
    inline const juce::Colour sage   { 0xff8fae9b };
}

//==============================================================================
// Fonts: DIN on macOS, Bahnschrift (Microsoft's DIN 1451) on Windows.
namespace F
{
#if JUCE_WINDOWS
    inline constexpr const char* label   = "Bahnschrift";
    inline constexpr const char* display = "Bahnschrift Condensed";
    inline constexpr const char* body    = "Segoe UI";
    inline constexpr float labelScale = 0.98f, displayScale = 1.0f, bodyScale = 0.96f;
#elif JUCE_MAC
    inline constexpr const char* label   = "DIN Alternate";
    inline constexpr const char* display = "DIN Condensed";
    inline constexpr const char* body    = "Helvetica Neue";
    inline constexpr float labelScale = 1.04f, displayScale = 1.08f, bodyScale = 0.96f;
#else
    inline constexpr const char* label   = "DejaVu Sans";
    inline constexpr const char* display = "DejaVu Sans Condensed";
    inline constexpr const char* body    = "DejaVu Sans";
    inline constexpr float labelScale = 0.92f, displayScale = 0.95f, bodyScale = 0.9f;
#endif
}

/** Labels and headings (bold = DIN), body copy (plain). */
inline juce::Font uiFont (float h, bool bold = false)
{
    if (bold)
        return juce::Font (juce::FontOptions (F::label, h * F::labelScale, juce::Font::bold));
    return juce::Font (juce::FontOptions (F::body, h * F::bodyScale, juce::Font::plain));
}
/** Figures: DIN's numerals are tabular, so values don't jitter. */
inline juce::Font monoFont (float h, bool = false)
{
    return juce::Font (juce::FontOptions (F::label, h * F::labelScale * 1.04f, juce::Font::bold));
}
inline juce::Font logoFont (float h)
{
    return juce::Font (juce::FontOptions (F::display, h * F::displayScale, juce::Font::bold)).withExtraKerningFactor (0.03f);
}

inline float textWidth (const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (f, s);
}

//==============================================================================
class LnF : public juce::LookAndFeel_V4
{
public:
    LnF()
    {
        setColour (juce::PopupMenu::backgroundColourId, C::bg2);
        setColour (juce::PopupMenu::textColourId, C::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::panel2);
        setColour (juce::PopupMenu::highlightedTextColourId, C::text);
        setColour (juce::PopupMenu::headerTextColourId, C::dim);
        setColour (juce::ComboBox::backgroundColourId, C::panel2);
        setColour (juce::ComboBox::textColourId, C::text);
        setColour (juce::ComboBox::outlineColourId, C::line);
        setColour (juce::ComboBox::arrowColourId, C::dim);
        setColour (juce::Slider::textBoxTextColourId, C::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, C::hot.withAlpha (0.35f));
        setColour (juce::Label::textColourId, C::text);
        setColour (juce::Label::textWhenEditingColourId, C::text);
        setColour (juce::TextEditor::backgroundColourId, C::bg2);
        setColour (juce::TextEditor::textColourId, C::text);
        setColour (juce::TextEditor::highlightColourId, C::hot.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId, C::line);
        setColour (juce::TextEditor::focusedOutlineColourId, C::dim);
        setColour (juce::CaretComponent::caretColourId, C::text);
        setColour (juce::TooltipWindow::backgroundColourId, C::bg2);
        setColour (juce::TooltipWindow::textColourId, C::text);
        setColour (juce::TooltipWindow::outlineColourId, C::line);
        setColour (juce::TextButton::textColourOffId, C::text);
        setColour (juce::TextButton::textColourOnId, C::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAng, float endAng, juce::Slider& s) override
    {
        const float r = (float) std::min (w, h) * 0.5f - 6.0f;
        const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float toAng = startAng + pos * (endAng - startAng);
        const float fromAng = bipolar ? (startAng + endAng) * 0.5f : startAng;
        const bool hover = s.isMouseOverOrDragging();
        auto pt = [&] (float ang, float rad) { return juce::Point<float> (cx + rad * std::sin (ang), cy - rad * std::cos (ang)); };

        // printed scale: dots, with ticks at the ends and the centre
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAng + (endAng - startAng) * (float) i / 10.0f;
            const bool major = i == 0 || i == 10 || i == 5;
            g.setColour (major ? C::dim.withAlpha (0.6f) : C::faint);
            if (major) g.drawLine ({ pt (a, r + 2.5f), pt (a, r + 5.5f) }, 1.1f);
            else { const auto d = pt (a, r + 4.0f); g.fillEllipse (d.x - 0.8f, d.y - 0.8f, 1.6f, 1.6f); }
        }

        juce::Path track;
        track.addCentredArc (cx, cy, r - 1.5f, r - 1.5f, 0.0f, startAng, endAng, true);
        g.setColour (C::bg);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        if (std::abs (toAng - fromAng) > 0.001f)
        {
            juce::Path val;
            val.addCentredArc (cx, cy, r - 1.5f, r - 1.5f, 0.0f, std::min (fromAng, toAng), std::max (fromAng, toAng), true);
            g.setColour (C::arc.withAlpha (s.isEnabled() ? (hover ? 1.0f : 0.8f) : 0.35f));
            g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        const float br = r * 0.7f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (cx - br, cy - br + 1.5f, br * 2.0f, br * 2.0f);
        g.setColour (hover ? C::knob.brighter (0.1f) : C::knob);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.setColour (C::bg.withAlpha (0.9f));
        g.drawEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f, 1.0f);

        g.setColour (C::text.withAlpha (s.isEnabled() ? 0.95f : 0.4f));
        g.drawLine ({ pt (toAng, br * 0.25f), pt (toAng, br * 0.88f) }, 2.0f);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (monoFont (11.0f));
        l->setColour (juce::Label::textColourId, C::text.withAlpha (0.85f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (down ? C::panel2.brighter (0.06f) : C::panel2);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (box.isMouseOver (true) ? C::dim : C::line);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (C::dim);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (14.0f, true); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return uiFont (15.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (C::bg2);
        g.setColour (C::line);
        g.drawRect (0, 0, w, h);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool) override
    {
        // A switch: small track with a sliding knob; label to the right.
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const auto accent = b.findColour (juce::ToggleButton::tickColourId);
        const auto track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - 8.0f, 30.0f, 16.0f);
        g.setColour (on ? accent.withAlpha (0.9f) : C::bg);
        g.fillRoundedRectangle (track, 8.0f);
        g.setColour (hl ? C::dim : C::line);
        g.drawRoundedRectangle (track, 8.0f, 1.0f);
        const float kx = on ? track.getRight() - 14.0f : track.getX() + 2.0f;
        g.setColour (on ? C::bg : C::dim);
        g.fillEllipse (kx, track.getY() + 2.0f, 12.0f, 12.0f);
        g.setColour (on ? C::text : C::dim);
        g.setFont (uiFont (14.0f, true));
        g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (40.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& col, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto& props = b.getProperties();

        if (props.contains ("tab"))
        {
            if (b.getToggleState())
            {
                g.setColour (C::text);
                g.fillRect (r.getX(), r.getBottom() - 2.0f, r.getWidth(), 2.0f);
            }
            else if (hl)
            {
                g.setColour (C::faint);
                g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
            }
            return;
        }

        if (props.contains ("segment"))
        {
            const int pos = (int) props["segment"];   // 0 first, 1 middle, 2 last
            g.setColour (b.getToggleState() ? C::text : (hl ? C::panel2.brighter (0.05f) : C::panel2));
            g.fillRect (r);
            g.setColour (C::line);
            g.drawRect (r, 1.0f);
            juce::ignoreUnused (pos);
            return;
        }

        if (props.contains ("solid"))   // DETONATE: the one filled button
        {
            g.setColour (down ? col.darker (0.2f) : (hl ? col.brighter (0.08f) : col));
            g.fillRoundedRectangle (r, 2.0f);
            return;
        }

        g.setColour (down ? C::panel2.brighter (0.1f) : (hl ? C::panel2.brighter (0.05f) : C::panel2));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (hl ? C::dim : C::line);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto& props = b.getProperties();
        const bool on = b.getToggleState();
        juce::Colour c = b.isEnabled() ? C::text : C::faint;
        if (props.contains ("tab"))          c = on ? C::text : C::dim;
        else if (props.contains ("segment")) c = on ? C::bg : C::text;
        else if (props.contains ("solid"))   c = C::bg;
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if (b.getProperties().contains ("tab")) return uiFont (15.0f, true);
        return uiFont (std::min (14.5f, (float) h * 0.5f), true);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.fillAll (C::panel2);
        g.setColour (C::line);
        g.drawRect (0, 0, w, h);
        g.setColour (C::text);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (text, 8, 4, w - 16, h - 8, juce::Justification::centredLeft, 4);
    }
};

//==============================================================================
inline const char* integrityState (float I)
{
    return I > 0.7f ? "Standing" : I > 0.4f ? "Cracked" : I > 0.15f ? "Loose" : I > 0.0f ? "Breaking through" : "Breached";
}

/** Section heading: sentence case, then a hairline to the right edge. */
inline void drawSection (juce::Graphics& g, int x, int y, int w, const juce::String& title)
{
    const auto f = uiFont (14.0f, true);
    g.setFont (f);
    g.setColour (C::text);
    g.drawText (title, x, y, w, 18, juce::Justification::centredLeft);
    const float tx = (float) x + 10.0f + textWidth (f, title);
    g.setColour (C::line);
    g.drawHorizontalLine (y + 10, tx, (float) (x + w));
}
} // namespace rb::ui
