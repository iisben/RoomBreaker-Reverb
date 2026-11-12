#pragma once
//==============================================================================
// The controls. Each one talks to its parameter through a JUCE attachment, so
// every change the mouse or keyboard makes arrives at the host as a proper
// begin / set / end gesture (automation write, undo, touch modes all work).
//
//   Knob           rotary, for every continuous parameter
//   ToggleBox      latching switch (Freeze, Limiter, Bypass)
//   ActionButton   momentary (Detonate, Rebuild): held = 1, released = 0
//   Segmented      a few mutually exclusive choices, all visible (Oversampling, Break by)
//   ChoiceBox      a long list of choices behind a drop-down (Material, Outside)
//   StereoMeter    peak + RMS bars with hold, clip latch and a numeric readout
//   IconButton     a 16 px icon on a 32 px target
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <optional>

namespace rb::ui
{
//==============================================================================
/** Rotary control for one parameter.

    Mouse      Drag up or right to raise it: 250 px of travel covers the whole range, measured
               in screen points so it feels the same at every UI scale. Hold Shift for 10x finer
               control; switching mid-drag never jumps, because the drag is incremental. The
               pointer hides while dragging and comes back where it went down.
    Wheel      2 % of the range per notch, Shift 0.2 %. Trackpads scroll continuously.
    Type       Double-click to type a value. Units are optional ("250", "250 ms", "4.5k", "-6").
               Return or clicking away applies it, Escape cancels.
    Reset      Alt-click or Cmd-click (Ctrl-click on Windows) returns to the default.
    Menu       Right-click: type a value, reset, plus the host's own items for this parameter
               (automation lanes, MIDI learn), where the host provides them.
    Display    Name above, value below. Under the mouse or while dragging, the arc and the value
               turn amber and the default is marked on the scale. */
class Knob : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name,
          float dialDiameter = 44.0f);
    ~Knob() override;

    /** Where the value arc starts, as a proportion of the travel: 0 = from the minimum, 0.5 = from the centre. */
    void setArcOrigin (double proportion);
    float getDialDiameter() const noexcept { return dial; }
    /** Height a knob needs for its dial plus the name and value lines. */
    static int heightFor (float dialDiameter) noexcept { return (int) dialDiameter + 38; }

    void resetToDefault();
    void openValueEditor();
    bool isEditingValue() const noexcept { return editor != nullptr; }
    juce::RangedAudioParameter& getParameter() const noexcept { return param; }
    juce::Rectangle<float> getDialBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr double kPixelsPerRange = 250.0, kFineFactor = 0.1, kWheelNotch = 0.02, kWheelSmooth = 0.5;

private:
    void closeValueEditor (bool apply);
    void showMenu();
    void setProportion (double p);
    juce::Rectangle<float> nameBounds() const;
    juce::Rectangle<float> valueBounds() const;

    juce::RangedAudioParameter& param;
    juce::String name;
    float dial;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    std::optional<juce::Slider::ScopedDragNotification> gesture;
    double dragProportion = 0.0, wheelProportion = -1.0;
    juce::Point<float> lastScreen, screenAtDown;
    bool pointerHidden = false;

    std::unique_ptr<juce::TextEditor> editor;
    bool closingEditor = false;
    std::unique_ptr<juce::HostProvidedContextMenu> lastHostMenu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
/** A latching switch. On: amber outline, amber text. */
class ToggleBox : public juce::TextButton
{
public:
    ToggleBox (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** A momentary button: the parameter is 1 while held, as one gesture. */
class ActionButton : public juce::TextButton
{
public:
    ActionButton (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text, bool danger);
    void buttonStateChanged() override;

private:
    juce::ParameterAttachment attachment;
    bool held = false;
};

//==============================================================================
/** Every choice visible as one strip. The selected one is lit, with an amber bar along its edge. */
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, bool vertical = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    int getSelected() const noexcept { return selected; }

private:
    int indexAt (juce::Point<float>) const;
    juce::Rectangle<float> segment (int i) const;

    juce::StringArray labels;
    bool vertical;
    int selected = 0, hovered = -1;
    juce::ParameterAttachment attachment;
};

/** A drop-down for a choice parameter with many options. The wheel steps through them. */
class ChoiceBox : public juce::ComboBox
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState&, const juce::String& paramId);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

//==============================================================================
/** A 16 px icon centred on a 32 px square target. */
class IconButton : public juce::Button
{
public:
    explicit IconButton (Icon i, const juce::String& name = {}) : juce::Button (name), icon (i)
    {
        getProperties().set ("style", "icon");
    }
    void setIcon (Icon i) { icon = i; repaint(); }
    void setLit (bool shouldBeLit) { lit = shouldBeLit; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
    bool lit = false;
};

//==============================================================================
/** Two vertical bars (left, right), -60 to +6 dBFS on a linear dB scale.

      solid       RMS (300 ms)
      light       peak, falling 24 dB/s
      white line  peak hold, 1.5 s
      readout     the highest peak since the last reset; red once anything reached 0 dBFS.
                  Click the meter to reset it.

    Colour by zone, never by gradient: sage below -6 dBFS, amber from -6 to 0, red above. */
class StereoMeter : public juce::Component
{
public:
    struct Source { std::atomic<float>* peak[2]; std::atomic<float>* meanSquare[2]; };

    StereoMeter (Source, juce::String caption);
    void tick (float dtSeconds);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { resetHold(); }
    void resetHold();

    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
    float getPeakDb (int ch) const noexcept { return peakDb[(size_t) ch]; }
    float getMaxDb() const noexcept { return maxDb; }

private:
    float yFor (float db, juce::Rectangle<float> bars) const noexcept;

    Source source;
    juce::String caption;
    std::array<float, 2> peakDb { -100.0f, -100.0f }, rmsDb { -100.0f, -100.0f }, holdDb { -100.0f, -100.0f }, holdAge {};
    float maxDb = -100.0f;
    bool clipped = false;
    juce::uint32 shown = 0;
};
} // namespace rb::ui
