#pragma once
//==============================================================================
// RoomBreaker design system ("site office").
//
// A dark, matte console around one bright picture: the house. Everything that is
// not the house stays quiet, so the house and the numbers read first.
//
//   Colour   Six greys do all the structure. Exactly two colours carry meaning:
//            amber  the control you are touching, what is selected, caution
//            red    damage, clipping, Detonate
//            Nothing glows. The knob caps are the only gradient (2 stops, lit
//            from above): the one place where depth is allowed.
//   Type     Two embedded families, three sizes, nothing else:
//            Barlow Semi Condensed  11 caps (labels)  13 (text)  16 (preset name)
//            JetBrains Mono         11 (scales)       13 (values)
//   Grid     8 px unit at the 1200 x 750 design size, 16 px gutters. Hairlines
//            separate areas instead of boxes. Radius: 3 px on controls, 0 on areas.
//   Icons    16 x 16 grid, 1.5 px stroke, square caps, mitred joins, no fills.
//==============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace rb::ui
{
//==============================================================================
namespace C
{
    // Structure, darkest to lightest
    inline const juce::Colour bg0     { 0xff111315 };   // window, rails, wells (meters, fields)
    inline const juce::Colour bg1     { 0xff181b1e };   // panels: header, controls, footer
    inline const juce::Colour bg2     { 0xff202428 };   // raised: buttons, knob caps
    inline const juce::Colour bg3     { 0xff2a2f34 };   // raised under the mouse, selected rows
    inline const juce::Colour line    { 0xff2c3136 };   // hairlines between areas
    inline const juce::Colour edge    { 0xff3a4046 };   // control outlines, knob track
    inline const juce::Colour edgeHi  { 0xff4d555c };   // outline under the mouse

    // Text. Contrast on bg1: text0 13.8:1, text1 7.6:1, text2 5.2:1. All pass WCAG AA on bg0..bg2;
    // on bg3 (hover, selection) text2 is 4.1:1, so selected rows switch to text1 or brighter.
    inline const juce::Colour text0   { 0xffe9e5dd };   // values, names: anything you read
    inline const juce::Colour text1   { 0xffa7adb2 };   // labels
    inline const juce::Colour text2   { 0xff868e95 };   // units, hints, scales, disabled

    // Meaning
    inline const juce::Colour accent  { 0xfff0a43c };   // 8.3:1 on bg1
    inline const juce::Colour danger  { 0xffef5b4f };   // 5.2:1 on bg1
    inline const juce::Colour meter   { 0xff9fc2b6 };   // meter body below -6 dBFS, 8.9:1
    inline const juce::Colour shadow  { 0xff08090a };
}

/** Design size of the editor. Everything is laid out in these units and scaled as one. */
inline constexpr int kDesignW = 1200, kDesignH = 750;

//==============================================================================
namespace type
{
    inline constexpr float S = 11.0f, M = 13.0f, L = 16.0f;

    juce::Font caps (float h = S);     // Barlow SemiBold, tracked: give it upper-case text
    juce::Font text (float h = M);     // Barlow Medium
    juce::Font strong (float h = L);   // Barlow SemiBold
    juce::Font mono (float h = M);     // JetBrains Mono
    float width (const juce::Font&, const juce::String&);
}

//==============================================================================
enum class Icon { prev, next, caret, search, save, close, power, folder, trash };

/** The icon's path on a 16 x 16 grid. */
juce::Path iconPath (Icon);
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour, float stroke = 1.5f);

/** A section heading: tracked caps, then a hairline to the right edge. */
void drawSectionHeader (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text);

/** Help key for the footer's info line, looked up the parent chain. */
inline void setInfo (juce::Component& c, const juce::String& key) { c.getProperties().set ("info", key); }

/** Integrity in words, shared by the readouts and the help. */
inline const char* integrityState (float I) noexcept
{
    if (I >= 0.97f) return "SOUND";
    if (I >= 0.75f) return "CRACKED";
    if (I >= 0.45f) return "LOOSE";
    if (I >= 0.12f) return "BREACHED";
    return "DOWN";
}

//==============================================================================
class LnF final : public juce::LookAndFeel_V4
{
public:
    LnF();

    // knobs (the Knob class in Widgets.h draws its labels itself)
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    // buttons: property "style" = "danger" | "icon" | none
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return type::caps(); }

    // drop-down fields and menus
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return type::text(); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getPopupMenuFont() override { return type::text(); }
    void getIdealPopupMenuItemSize (const juce::String&, bool separator, int standardHeight, int& w, int& h) override;
    int getPopupMenuBorderSize() override { return 4; }

    // text fields
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;

    // the rest
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;
};
} // namespace rb::ui
