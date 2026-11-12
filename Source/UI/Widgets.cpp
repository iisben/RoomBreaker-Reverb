#include "Widgets.h"
#include <BinaryData.h>

namespace rb::ui
{
//==============================================================================
// Type
//==============================================================================
namespace
{
    struct Faces
    {
        juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (
            RoomBreakerFonts::BarlowSemiCondensedMedium_ttf, (size_t) RoomBreakerFonts::BarlowSemiCondensedMedium_ttfSize);
        juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (
            RoomBreakerFonts::BarlowSemiCondensedSemiBold_ttf, (size_t) RoomBreakerFonts::BarlowSemiCondensedSemiBold_ttfSize);
        juce::Typeface::Ptr mono = juce::Typeface::createSystemTypefaceFor (
            RoomBreakerFonts::JetBrainsMono_ttf, (size_t) RoomBreakerFonts::JetBrainsMono_ttfSize);
    };

    const Faces& faces()
    {
        static const Faces f;
        return f;
    }

    // Sizes are point sizes (the em), so 11 / 13 / 16 mean the same thing in both families.
    juce::Font make (const juce::Typeface::Ptr& face, float pointHeight, float tracking = 0.0f)
    {
        return juce::Font (juce::FontOptions (face).withPointHeight (pointHeight).withKerningFactor (tracking));
    }
}

namespace type
{
    juce::Font caps (float h)   { return make (faces().semibold, h, 0.07f); }
    juce::Font text (float h)   { return make (faces().medium, h); }
    juce::Font strong (float h) { return make (faces().semibold, h, 0.01f); }
    juce::Font mono (float h)   { return make (faces().mono, h); }
    float width (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }
}

//==============================================================================
// Icons
//==============================================================================
juce::Path iconPath (Icon icon)
{
    juce::Path p;
    switch (icon)
    {
        case Icon::prev:   p.startNewSubPath (10.0f, 3.0f); p.lineTo (5.0f, 8.0f); p.lineTo (10.0f, 13.0f); break;
        case Icon::next:   p.startNewSubPath (6.0f, 3.0f); p.lineTo (11.0f, 8.0f); p.lineTo (6.0f, 13.0f); break;
        case Icon::caret:  p.startNewSubPath (4.0f, 6.0f); p.lineTo (8.0f, 10.0f); p.lineTo (12.0f, 6.0f); break;
        case Icon::search:
            p.addEllipse (2.5f, 2.5f, 9.0f, 9.0f);
            p.startNewSubPath (10.2f, 10.2f); p.lineTo (14.0f, 14.0f);
            break;
        case Icon::save:
            p.startNewSubPath (8.0f, 2.0f); p.lineTo (8.0f, 10.0f);
            p.startNewSubPath (4.5f, 6.5f); p.lineTo (8.0f, 10.0f); p.lineTo (11.5f, 6.5f);
            p.startNewSubPath (2.0f, 10.0f); p.lineTo (2.0f, 14.0f); p.lineTo (14.0f, 14.0f); p.lineTo (14.0f, 10.0f);
            break;
        case Icon::close:
            p.startNewSubPath (3.5f, 3.5f); p.lineTo (12.5f, 12.5f);
            p.startNewSubPath (12.5f, 3.5f); p.lineTo (3.5f, 12.5f);
            break;
        case Icon::power:
            p.addCentredArc (8.0f, 8.8f, 5.6f, 5.6f, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (8.0f, 1.5f); p.lineTo (8.0f, 8.0f);
            break;
        case Icon::folder:
            p.startNewSubPath (1.5f, 3.5f); p.lineTo (6.0f, 3.5f); p.lineTo (7.5f, 5.0f); p.lineTo (14.5f, 5.0f);
            p.lineTo (14.5f, 13.5f); p.lineTo (1.5f, 13.5f); p.closeSubPath();
            break;
        case Icon::trash:
            p.startNewSubPath (2.5f, 4.0f); p.lineTo (13.5f, 4.0f);
            p.startNewSubPath (6.0f, 4.0f); p.lineTo (6.0f, 2.0f); p.lineTo (10.0f, 2.0f); p.lineTo (10.0f, 4.0f);
            p.startNewSubPath (4.0f, 4.0f); p.lineTo (4.8f, 14.0f); p.lineTo (11.2f, 14.0f); p.lineTo (12.0f, 4.0f);
            break;
    }
    return p;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float stroke)
{
    const float s = std::min (area.getWidth(), area.getHeight()) / 16.0f;
    auto p = iconPath (icon);
    p.applyTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - 8.0f * s, area.getCentreY() - 8.0f * s));
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

void drawSectionHeader (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    const auto f = type::caps();
    const auto t = text.toUpperCase();
    g.setFont (f);
    g.setColour (C::text1);
    g.drawText (t, r, juce::Justification::centredLeft, false);
    const float x = r.getX() + type::width (f, t) + 10.0f;
    g.setColour (C::line);
    g.fillRect (juce::Rectangle<float> (x, std::round (r.getCentreY()), r.getRight() - x, 1.0f));
}

//==============================================================================
// LookAndFeel
//==============================================================================
LnF::LnF()
{
    setDefaultSansSerifTypeface (faces().medium);

    setColour (juce::ResizableWindow::backgroundColourId, C::bg0);
    setColour (juce::Label::textColourId, C::text1);
    setColour (juce::ComboBox::textColourId, C::text0);
    setColour (juce::PopupMenu::backgroundColourId, C::bg1);
    setColour (juce::PopupMenu::textColourId, C::text0);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::bg3);
    setColour (juce::PopupMenu::highlightedTextColourId, C::text0);
    setColour (juce::TextEditor::backgroundColourId, C::bg0);
    setColour (juce::TextEditor::textColourId, C::text0);
    setColour (juce::TextEditor::highlightColourId, C::accent.withAlpha (0.32f));
    setColour (juce::TextEditor::highlightedTextColourId, C::text0);
    setColour (juce::TextEditor::outlineColourId, C::edge);
    setColour (juce::TextEditor::focusedOutlineColourId, C::accent);
    setColour (juce::CaretComponent::caretColourId, C::accent);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, C::edge);
}

juce::Slider::SliderLayout LnF::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout layout;
    if (auto* knob = dynamic_cast<Knob*> (&s))
        layout.sliderBounds = knob->getDialBounds().getSmallestIntegerContainer();
    else
        layout.sliderBounds = s.getLocalBounds();
    return layout;
}

void LnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                            float a0, float a1, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float d = std::min (area.getWidth(), area.getHeight());
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float rt = d * 0.5f - 2.0f;          // scale track
    const float rc = d * 0.5f - 7.5f;          // cap
    const bool hot = s.isEnabled() && s.isMouseOverOrDragging();
    const float origin = (float) (double) s.getProperties().getWithDefault ("arcOrigin", 0.0);
    auto angle = [&] (float p) { return a0 + p * (a1 - a0); };

    // Scale: the whole travel, then the value from its origin.
    {
        juce::Path track;
        track.addCentredArc (cx, cy, rt, rt, 0.0f, a0, a1, true);
        g.setColour (C::edge);
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        const float from = angle (origin), to = angle (pos);
        if (std::abs (to - from) > 0.002f)
        {
            juce::Path value;
            value.addCentredArc (cx, cy, rt, rt, 0.0f, std::min (from, to), std::max (from, to), true);
            g.setColour (! s.isEnabled() ? C::text2 : hot ? C::accent : C::text1);
            g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        // The default, marked outside the scale while the knob is under the mouse.
        if (hot && s.getProperties().contains ("defaultPos"))
        {
            const float a = angle ((float) (double) s.getProperties()["defaultPos"]);
            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            g.setColour (C::text2);
            g.drawLine ({ juce::Point<float> (cx, cy) + dir * (rt + 2.5f), juce::Point<float> (cx, cy) + dir * (rt + 5.5f) }, 1.2f);
        }
    }

    // Cap: a machined, matte top lit from above. Two soft contact shadows, one 2-stop fill,
    // a dark rim and a faint highlight on the upper edge. No glow.
    g.setColour (C::shadow.withAlpha (0.45f));
    g.fillEllipse (cx - rc - 1.0f, cy - rc + 1.0f, (rc + 1.0f) * 2.0f, (rc + 1.0f) * 2.0f);
    g.setColour (C::shadow.withAlpha (0.18f));
    g.fillEllipse (cx - rc - 2.0f, cy - rc + 2.5f, (rc + 2.0f) * 2.0f, (rc + 2.0f) * 2.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff343a40 : 0xff2e3338), cx, cy - rc,
                                             juce::Colour (0xff1a1d20), cx, cy + rc, false));
    g.fillEllipse (cx - rc, cy - rc, rc * 2.0f, rc * 2.0f);
    g.setColour (juce::Colour (0xff0c0d0f));
    g.drawEllipse (cx - rc, cy - rc, rc * 2.0f, rc * 2.0f, 1.0f);
    {
        juce::Path rim;
        rim.addCentredArc (cx, cy, rc - 1.0f, rc - 1.0f, 0.0f, -1.1f, 1.1f, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.strokePath (rim, juce::PathStrokeType (1.0f));
    }

    // Pointer
    const float a = angle (pos);
    const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
    g.setColour (s.isEnabled() ? C::text0 : C::text2);
    g.drawLine ({ juce::Point<float> (cx, cy) + dir * (rc * 0.32f), juce::Point<float> (cx, cy) + dir * (rc - 2.5f) }, 2.0f);
}

void LnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto style = b.getProperties()["style"].toString();
    const bool on = b.getToggleState();
    over = over && b.isEnabled();

    if (style == "icon")
    {
        if (over || down)
        {
            g.setColour (down ? C::edge : C::bg3);
            g.fillRoundedRectangle (r, 3.0f);
        }
        return;
    }

    juce::Colour fill, outline;
    if (style == "switch")
    {
        // A state switch: the LED says on or off, the body stays neutral (Limiter on is normal,
        // so it must not shout).
        fill = down ? C::edge : over ? C::bg3 : C::bg2;
        outline = over ? C::edgeHi : C::edge;
        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const auto led = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (on ? C::accent : C::bg0);
        g.fillRect (led);
        g.setColour (on ? C::accent : C::edgeHi);
        g.drawRect (led, 1.0f);
        return;
    }
    if (style == "danger")
    {
        fill = down ? C::danger.withAlpha (0.30f) : over ? C::danger.withAlpha (0.14f) : C::danger.withAlpha (0.04f);
        outline = b.isEnabled() ? C::danger : C::edge;
    }
    else
    {
        fill = on ? C::accent.withAlpha (down ? 0.24f : 0.14f) : down ? C::edge : over ? C::bg3 : C::bg2;
        outline = on ? C::accent : over ? C::edgeHi : C::edge;
    }
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void LnF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    juce::Colour c = ! b.isEnabled() ? C::text2
                   : style == "danger" ? (down ? C::text0 : C::danger)
                   : style == "switch" ? (b.getToggleState() || over ? C::text0 : C::text1)
                   : b.getToggleState() ? C::accent
                   : over ? C::text0 : C::text1;
    g.setColour (c);
    g.setFont (type::caps());
    auto area = b.getLocalBounds();
    if (style == "switch")
        area.removeFromLeft (14);   // room for the LED
    g.drawText (b.getButtonText().toUpperCase(), area, juce::Justification::centred, false);
}

void LnF::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (C::bg0);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (down || box.isPopupActive() ? C::accent : box.isMouseOver (true) ? C::edgeHi : C::edge);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    drawIcon (g, Icon::caret, { (float) w - 26.0f, ((float) h - 16.0f) * 0.5f, 16.0f, 16.0f },
              box.isMouseOver (true) ? C::text0 : C::text1);
}

void LnF::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 32, box.getHeight());
    label.setFont (type::text());
    label.setColour (juce::Label::textColourId, C::text0);
}

juce::PopupMenu::Options LnF::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (26);
}

void LnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (C::bg1);
    g.setColour (C::edge);
    g.drawRect (0, 0, w, h, 1);
}

void LnF::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active,
                             bool highlighted, bool ticked, bool hasSubMenu, const juce::String& text,
                             const juce::String& shortcut, const juce::Drawable*, const juce::Colour*)
{
    if (separator)
    {
        g.setColour (C::line);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.toFloat().reduced (2.0f, 1.0f);
    if (highlighted && active)
    {
        g.setColour (C::bg3);
        g.fillRoundedRectangle (r, 3.0f);
    }
    if (ticked)
    {
        g.setColour (C::accent);
        g.fillRect (r.withWidth (2.0f).reduced (0.0f, 5.0f));
    }
    g.setFont (type::text());
    g.setColour (! active ? C::text2 : ticked ? C::accent : C::text0);
    g.drawText (text, r.withTrimmedLeft (12.0f).withTrimmedRight (24.0f), juce::Justification::centredLeft, true);
    if (shortcut.isNotEmpty())
    {
        g.setColour (C::text2);
        g.drawText (shortcut, r.withTrimmedRight (10.0f), juce::Justification::centredRight, false);
    }
    if (hasSubMenu)
        drawIcon (g, Icon::next, r.removeFromRight (22.0f).withSizeKeepingCentre (12.0f, 12.0f), C::text1, 1.3f);
}

void LnF::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text)
{
    g.setFont (type::caps());
    g.setColour (C::text2);
    g.drawText (text.toUpperCase(), area.toFloat().withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
}

void LnF::getIdealPopupMenuItemSize (const juce::String& text, bool separator, int, int& w, int& h)
{
    h = separator ? 9 : 26;
    w = juce::roundToInt (type::width (type::text(), text)) + 48;
}

void LnF::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (C::bg0);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 3.0f);
}

void LnF::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    if (! te.isEnabled())
        return;
    const bool focused = te.hasKeyboardFocus (true) && ! te.isReadOnly();
    g.setColour (focused ? C::accent : te.isMouseOver (true) ? C::edgeHi : C::edge);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), 3.0f, 1.0f);
}

void LnF::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                         int thumbStart, int thumbSize, bool over, bool down)
{
    const auto thumb = vertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStart, (float) w - 4.0f, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) h - 4.0f);
    g.setColour (over || down ? C::edgeHi : C::edge);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void LnF::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (over || dragging ? C::text1 : C::text2);
    for (float k : { 0.35f, 0.6f, 0.85f })
        g.drawLine ((float) w * k, (float) h - 2.0f, (float) w - 2.0f, (float) h * k, 1.0f);
}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& displayName, float dialDiameter)
    : param (*dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (paramId))),
      name (displayName), dial (dialDiameter)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setRepaintsOnMouseActivity (true);
    setPopupMenuEnabled (false);
    // Knobs never take keyboard focus: a plug-in that grabs focus on a click steals the host's
    // transport keys (space bar) until the user clicks back into the host.
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    // The attachment gives the slider the parameter's own range, skew and text conversion, so a
    // proportion of the slider's travel is exactly the parameter's normalised value.
    getProperties().set ("defaultPos", (double) param.getDefaultValue());
    setTitle (displayName);
    setInfo (*this, paramId);
}

Knob::~Knob()
{
    gesture.reset();
}

void Knob::setArcOrigin (double proportion)
{
    getProperties().set ("arcOrigin", proportion);
    repaint();
}

juce::Rectangle<float> Knob::getDialBounds() const
{
    return { ((float) getWidth() - dial) * 0.5f, 17.0f, dial, dial };
}

juce::Rectangle<float> Knob::nameBounds() const  { return { 0.0f, 0.0f, (float) getWidth(), 14.0f }; }
juce::Rectangle<float> Knob::valueBounds() const { return { 0.0f, 17.0f + dial + 3.0f, (float) getWidth(), 18.0f }; }

void Knob::resized()
{
    juce::Slider::resized();
    if (editor != nullptr)
        editor->setBounds (valueBounds().reduced (2.0f, 0.0f).getSmallestIntegerContainer());
}

void Knob::paint (juce::Graphics& g)
{
    juce::Slider::paint (g);   // the dial, through LnF::drawRotarySlider

    const bool hot = isEnabled() && (isMouseOverOrDragging() || gesture.has_value());
    g.setFont (type::caps());
    g.setColour (hot ? C::text0 : C::text1);
    g.drawText (name.toUpperCase(), nameBounds(), juce::Justification::centred, false);

    if (editor == nullptr)
    {
        g.setFont (type::mono());
        g.setColour (! isEnabled() ? C::text2 : hot ? C::accent : C::text0);
        g.drawText (getTextFromValue (getValue()), valueBounds(), juce::Justification::centred, false);
    }
}

void Knob::setProportion (double p)
{
    setValue (proportionOfLengthToValue (juce::jlimit (0.0, 1.0, p)), juce::sendNotificationSync);
}

void Knob::resetToDefault()
{
    const juce::Slider::ScopedDragNotification g (*this);
    setValue (param.convertFrom0to1 (param.getDefaultValue()), juce::sendNotificationSync);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        resetToDefault();
        return;
    }
    if (editor != nullptr)
        closeValueEditor (true);

    gesture.emplace (*this);                       // host: begin gesture (touch automation starts here)
    dragProportion = valueToProportionOfLength (getValue());
    lastScreen = screenAtDown = e.source.getScreenPosition();
    pointerHidden = false;
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! gesture.has_value())
        return;

    if (! pointerHidden && e.getDistanceFromDragStart() > 2)
    {
        // Hide the pointer and stop it at the screen edge, so a long drag never runs out of room.
        e.source.enableUnboundedMouseMovement (true, false);
        pointerHidden = true;
    }

    // Incremental, in screen points: the feel does not change with the UI scale, and pressing or
    // releasing Shift mid-drag changes the rate without a jump.
    const auto now = e.source.getScreenPosition();
    const double travel = (double) ((now.x - lastScreen.x) - (now.y - lastScreen.y));
    lastScreen = now;
    const double rate = e.mods.isShiftDown() ? kFineFactor : 1.0;
    dragProportion = juce::jlimit (0.0, 1.0, dragProportion + travel * rate / kPixelsPerRange);
    setProportion (dragProportion);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (pointerHidden)
    {
        auto source = e.source;                       // a handle; the event's copy is const
        source.enableUnboundedMouseMovement (false);
        source.setScreenPosition (screenAtDown);      // the pointer comes back where it went down
        pointerHidden = false;
    }
    gesture.reset();                                  // host: end gesture
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEnabled() && ! e.mods.isPopupMenu() && ! e.mods.isAltDown() && ! e.mods.isCommandDown())
        openValueEditor();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || editor != nullptr)
        return;

    // macOS turns Shift + vertical wheel into horizontal scrolling, so take whichever axis moved.
    float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    if (wheel.isReversed)
        delta = -delta;
    if (delta == 0.0f)
        return;

    double step = wheel.isSmooth ? (double) delta * kWheelSmooth : (delta > 0.0f ? kWheelNotch : -kWheelNotch);
    if (e.mods.isShiftDown())
        step *= kFineFactor;

    // Keep the fractional position between events, so fine steps on a stepped parameter
    // (0.1 dB, say) still add up instead of snapping back each time.
    const double now = valueToProportionOfLength (getValue());
    const double base = (wheelProportion >= 0.0 && std::abs (wheelProportion - now) < 0.01) ? wheelProportion : now;
    wheelProportion = juce::jlimit (0.0, 1.0, base + step);

    const juce::Slider::ScopedDragNotification g (*this);
    setProportion (wheelProportion);
}

void Knob::openValueEditor()
{
    if (editor != nullptr)
        return;
    editor = std::make_unique<juce::TextEditor> (name);
    editor->setFont (type::mono());
    editor->setJustification (juce::Justification::centred);
    editor->setIndents (2, 1);
    editor->setSelectAllWhenFocused (true);
    editor->setInputRestrictions (24);
    editor->setText (getTextFromValue (getValue()), false);
    editor->onReturnKey = [this] { closeValueEditor (true); };
    editor->onEscapeKey = [this] { closeValueEditor (false); };
    editor->onFocusLost = [this] { closeValueEditor (true); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
    repaint();
}

void Knob::closeValueEditor (bool apply)
{
    if (editor == nullptr || closingEditor)
        return;
    closingEditor = true;

    if (apply)
    {
        const auto text = editor->getText().trim();
        const double v = getValueFromText (text);   // the parameter's own parser: units optional
        // Text without a digit only counts when it names a value ("Permadeath"); anything else is a typo.
        const bool meaningful = text.containsAnyOf ("0123456789") || getTextFromValue (v).equalsIgnoreCase (text);
        if (text.isNotEmpty() && meaningful && std::isfinite (v))
        {
            const juce::Slider::ScopedDragNotification g (*this);
            setValue (juce::jlimit (getMinimum(), getMaximum(), v), juce::sendNotificationSync);
        }
    }

    // The editor is still inside its own callback here, so it is removed on the next message.
    editor->setVisible (false);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
    {
        if (safe != nullptr)
        {
            safe->editor.reset();
            safe->closingEditor = false;
            safe->repaint();
        }
    });
}

void Knob::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (name);
    menu.addItem ("Type a value...", [safe = juce::Component::SafePointer<Knob> (this)] { if (safe != nullptr) safe->openValueEditor(); });
    menu.addItem ("Reset to " + param.getText (param.getDefaultValue(), 0),
                  [safe = juce::Component::SafePointer<Knob> (this)] { if (safe != nullptr) safe->resetToDefault(); });

    // The host's own items for this parameter (automation lane, MIDI learn, ...), where it offers them.
    if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* context = ed->getHostContext())
            if (auto hostMenu = context->getContextMenuForParameter (&param))
            {
                const auto items = hostMenu->getEquivalentPopupMenu();
                if (items.getNumItems() > 0)
                {
                    menu.addSeparator();
                    menu.addSectionHeader ("Host");
                    for (juce::PopupMenu::MenuItemIterator it (items); it.next();)
                        menu.addItem (it.getItem());
                }
                // The items act through this object, so it lives as long as the menu can be open.
                lastHostMenu = std::move (hostMenu);
            }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (26));
}

//==============================================================================
// Buttons, choices
//==============================================================================
ToggleBox::ToggleBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
    : juce::TextButton (text)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
    getProperties().set ("style", "switch");
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
    setInfo (*this, paramId);
}

ActionButton::ActionButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text, bool danger)
    : juce::TextButton (text), attachment (*state.getParameter (paramId), [] (float) {}, nullptr)
{
    if (danger)
        getProperties().set ("style", "danger");
    setWantsKeyboardFocus (false);
    setInfo (*this, paramId == "reset" ? juce::String ("reset") : paramId);
}

void ActionButton::buttonStateChanged()
{
    juce::TextButton::buttonStateChanged();
    const bool down = isDown();
    if (down == held)
        return;
    held = down;
    if (down)
    {
        attachment.beginGesture();
        attachment.setValueAsPartOfGesture (1.0f);
    }
    else
    {
        attachment.setValueAsPartOfGesture (0.0f);
        attachment.endGesture();
    }
}

Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::StringArray l, bool v)
    : labels (std::move (l)), vertical (v),
      attachment (*state.getParameter (paramId), [this] (float x) { selected = juce::roundToInt (x); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setInfo (*this, paramId);
    setRepaintsOnMouseActivity (true);
}

juce::Rectangle<float> Segmented::segment (int i) const
{
    const auto r = getLocalBounds().toFloat();
    const float n = (float) labels.size();
    return vertical ? juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * (float) i / n, r.getWidth(), r.getHeight() / n)
                    : juce::Rectangle<float> (r.getX() + r.getWidth() * (float) i / n, r.getY(), r.getWidth() / n, r.getHeight());
}

int Segmented::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (segment (i).contains (p))
            return i;
    return -1;
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (C::bg0);
    g.fillRoundedRectangle (r, 3.0f);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto s = segment (i);
        if (i == selected)
        {
            g.setColour (C::bg3);
            g.fillRect (s.reduced (1.0f));
            g.setColour (C::accent);
            g.fillRect (vertical ? s.reduced (1.0f).withWidth (2.0f) : s.reduced (1.0f).withTrimmedTop (s.getHeight() - 3.0f));
        }
        else if (i > 0)
        {
            g.setColour (C::line);
            if (vertical) g.fillRect (s.getX() + 1.0f, s.getY(), s.getWidth() - 2.0f, 1.0f);
            else          g.fillRect (s.getX(), s.getY() + 4.0f, 1.0f, s.getHeight() - 8.0f);
        }
        g.setFont (type::caps());
        g.setColour (i == selected ? C::text0 : i == hovered ? C::text0 : C::text1);
        g.drawText (labels[i].toUpperCase(), s, juce::Justification::centred, false);
    }
    g.setColour (isMouseOver (true) ? C::edgeHi : C::edge);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hovered) { hovered = i; repaint(); }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

ChoiceBox::ChoiceBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        addItemList (p->choices, 1);
    setScrollWheelEnabled (true);
    setWantsKeyboardFocus (false);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, *this);
    setInfo (*this, paramId);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, over, down);
    const auto c = ! isEnabled() ? C::text2 : lit ? C::accent : over ? C::text0 : C::text1;
    drawIcon (g, icon, getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f), c);
}

//==============================================================================
// Meter
//==============================================================================
StereoMeter::StereoMeter (Source s, juce::String c) : source (s), caption (std::move (c))
{
    setInfo (*this, "meters");
}

void StereoMeter::tick (float dt)
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float pk = source.peak[ch]->exchange (0.0f, std::memory_order_relaxed);
        const float db = juce::Decibels::gainToDecibels (pk, -100.0f);
        peakDb[ch] = std::max (db, peakDb[ch] - 24.0f * dt);
        rmsDb[ch] = juce::Decibels::gainToDecibels (std::sqrt (source.meanSquare[ch]->load (std::memory_order_relaxed)), -100.0f);
        if (db >= holdDb[ch]) { holdDb[ch] = db; holdAge[ch] = 0.0f; }
        else if ((holdAge[ch] += dt) > 1.5f) holdDb[ch] = std::max (peakDb[ch], holdDb[ch] - 24.0f * dt);
        maxDb = std::max (maxDb, db);
        clipped = clipped || pk > 1.0f;   // over full scale; the limiter's ceiling (just under) is not a clip
    }

    // Repaint only when something visible moved (a quarter dB is under a pixel).
    juce::uint32 sig = clipped ? 1u : 0u;
    for (size_t ch = 0; ch < 2; ++ch)
        for (const float v : { peakDb[ch], rmsDb[ch], holdDb[ch] })
            sig = sig * 31u + (juce::uint32) juce::roundToInt (std::max (v, kMinDb - 1.0f) * 4.0f);
    sig = sig * 31u + (juce::uint32) juce::roundToInt (std::max (maxDb, -100.0f) * 10.0f);
    if (sig != shown)
    {
        shown = sig;
        repaint();
    }
}

void StereoMeter::resetHold()
{
    maxDb = -100.0f;
    clipped = false;
    holdDb = { -100.0f, -100.0f };
    repaint();
}

float StereoMeter::yFor (float db, juce::Rectangle<float> bars) const noexcept
{
    const float t = (juce::jlimit (kMinDb, kMaxDb, db) - kMinDb) / (kMaxDb - kMinDb);
    return bars.getBottom() - t * bars.getHeight();
}

void StereoMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // caption, then the numeric peak: the one number you check before a bounce
    g.setFont (type::caps());
    g.setColour (C::text1);
    g.drawText (caption.toUpperCase(), r.removeFromTop (14.0f), juce::Justification::centred, false);
    r.removeFromTop (4.0f);
    {
        const auto box = r.removeFromTop (20.0f);
        g.setColour (clipped ? C::danger.withAlpha (0.16f) : C::bg1);
        g.fillRoundedRectangle (box.reduced (0.5f), 3.0f);
        g.setColour (clipped ? C::danger : C::edge);
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (type::mono (type::S));
        g.setColour (clipped ? C::danger : maxDb > -99.0f ? C::text0 : C::text2);
        g.drawText (maxDb > -99.0f ? juce::String (maxDb, 1) : juce::String ("-inf"), box, juce::Justification::centred, false);
    }
    r.removeFromTop (10.0f);

    // scale on the left, bars on the right
    const auto scale = r.removeFromLeft (r.getWidth() - 20.0f);
    const auto bars = r.withTrimmedTop (6.0f).withTrimmedBottom (6.0f).withSizeKeepingCentre (18.0f, r.getHeight() - 12.0f);
    g.setFont (type::mono (type::S));
    for (const float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = yFor (db, bars);
        g.setColour (db == 0.0f ? C::text1 : C::text2);
        g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db),
                    juce::Rectangle<float> (scale.getX(), y - 7.0f, scale.getWidth() - 6.0f, 14.0f),
                    juce::Justification::centredRight, false);
        g.setColour (db == 0.0f ? C::text2 : C::line);
        g.fillRect (scale.getRight() - 3.0f, std::round (y), 3.0f, 1.0f);
    }

    const float zoneWarn = yFor (-6.0f, bars), zoneClip = yFor (0.0f, bars);
    auto fillLevel = [&] (juce::Rectangle<float> bar, float top, bool peakOnly)
    {
        // Colour by zone, clipped, so the colour says where the level is. The peak above the RMS
        // is a dimmer sage in the normal zone; near and over full scale both are full strength,
        // because there the peak is what matters.
        auto lvl = bar.withTop (top);
        auto paintZone = [&] (float y0, float y1, juce::Colour c)
        {
            auto z = lvl.getIntersection (juce::Rectangle<float> (bar.getX(), y0, bar.getWidth(), y1 - y0));
            if (! z.isEmpty()) { g.setColour (c); g.fillRect (z); }
        };
        paintZone (zoneWarn, bar.getBottom(), peakOnly ? C::meter.withAlpha (0.4f) : C::meter);
        paintZone (zoneClip, zoneWarn, C::accent);
        paintZone (bar.getY(), zoneClip, C::danger);
    };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = bars.withWidth (8.0f).translated (ch == 0 ? 0.0f : 10.0f, 0.0f);
        g.setColour (juce::Colour (0xff0b0c0e));
        g.fillRect (bar);
        const float yr = yFor (rmsDb[(size_t) ch], bar), yp = yFor (peakDb[(size_t) ch], bar);
        if (peakDb[(size_t) ch] > kMinDb) fillLevel (bar.withBottom (yr), yp, true);
        if (rmsDb[(size_t) ch] > kMinDb)  fillLevel (bar, yr, false);
        if (holdDb[(size_t) ch] > kMinDb)
        {
            g.setColour (holdDb[(size_t) ch] >= 0.0f ? C::danger : C::text0);
            g.fillRect (bar.getX(), std::round (yFor (holdDb[(size_t) ch], bar)), bar.getWidth(), 1.5f);
        }
    }
}
} // namespace rb::ui
