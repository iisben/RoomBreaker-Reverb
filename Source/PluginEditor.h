#pragma once
//==============================================================================
// The editor: one console, laid out at 1200 x 750 design units on an 8 px grid and
// scaled as a whole (75 % to 200 %, aspect ratio locked).
//
//   +--------------------------------------------------------------------------+
//   | ROOMBREAKER       [<][>]  Preset name ......... CATEGORY  [save]  100% [o] |  header   48
//   +-----+------------------------------------+-------------------------+-----+
//   | IN  |                                    | ROOM  Material  Outside | OUT |
//   | ||  |   the house                        | STRUCTURE   o  o  o  o  | ||  |
//   | ||  |   (condition readout top-left)     | DESTRUCTION o  o  o  o  | ||  |
//   | ||  |                                    | SPACE       o  o  o  o  | MIX |
//   | IN  |                                    | TONE        o  o  o  o  | DUCK|
//   | BRK |                                    | [FREEZE][REBUILD][DET.] | OUT |
//   +-----+------------------------------------+-------------------------+-----+
//   | info line (what the mouse is over)        OVERSAMPLING [1x 2x 4x] LIMITER |  footer   32
//   +--------------------------------------------------------------------------+
//      88                 624                            392              88
//
// Level flows left to right: input rail, the room, the controls, output rail.
//==============================================================================
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "UI/Help.h"
#include "UI/LiveState.h"
#include "UI/HouseView.h"
#include "UI/PresetBrowser.h"

namespace rb::ui
{
//==============================================================================
/** The footer's info line: help for whatever the mouse is over, otherwise how the knobs work. */
class InfoLine : public juce::Component, private juce::Timer
{
public:
    InfoLine();
    void paint (juce::Graphics&) override;

    // Registered on the canvas for all nested children.
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String currentKey() const { return key; }
    void showHelpFor (const juce::String& k) { key = k; clearAt = 0; repaint(); }

private:
    void timerCallback() override;
    juce::String key;
    juce::uint32 clearAt = 0;
};

//==============================================================================
/** Drawn over the house: the condition readout, the hovered surface, the turn hint, bypass. */
class DisplayOverlay : public juce::Component
{
public:
    DisplayOverlay (const LiveState&, const HouseView&, RoomBreakerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void refresh();
    juce::Rectangle<float> readoutBounds() const { return { 12.0f, 12.0f, 212.0f, 84.0f }; }

private:
    const LiveState& live;
    const HouseView& house;
    RoomBreakerAudioProcessor& proc;
    juce::uint64 shown = 0;
};

/** The house in its frame. */
class Display : public juce::Component
{
public:
    Display (LiveState&, RoomBreakerAudioProcessor&);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh (float dt);

    HouseView house;

private:
    DisplayOverlay overlay;
    juce::Component readoutTarget;   // gives the readout its own help text
};

//==============================================================================
/** The sound controls: room, four sections of four knobs, and the three actions. */
class ControlPanel : public juce::Component
{
public:
    explicit ControlPanel (RoomBreakerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    Knob* findKnob (const juce::String& paramId) const;

private:
    ChoiceBox material, outside;
    juce::OwnedArray<Knob> knobs;   // 4 x 4, in section order
    ToggleBox freeze;
    ActionButton rebuild, detonate;
};

/** An edge column: a stereo meter and the controls that belong at that end of the signal. */
class Rail : public juce::Component
{
public:
    Rail (RoomBreakerAudioProcessor&, bool isInput);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (float dt) { meter.tick (dt); }
    StereoMeter& getMeter() noexcept { return meter; }

private:
    bool input;
    StereoMeter meter;
    juce::OwnedArray<Knob> knobs;
    std::unique_ptr<Segmented> breakBy;
};

//==============================================================================
/** Wordmark, the preset bar, interface size and bypass. */
class Header : public juce::Component
{
public:
    Header (RoomBreakerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    std::function<void()> onBrowse, onSave;
    std::function<void (float)> onScale;
    void setBrowserOpen (bool);

private:
    class PresetName;
    void step (int delta);
    void showScaleMenu();

    RoomBreakerAudioProcessor& proc;
    IconButton prev { Icon::prev, "Previous preset" }, next { Icon::next, "Next preset" },
               save { Icon::save, "Save preset" }, power { Icon::power, "Bypass" };
    std::unique_ptr<PresetName> name;
    juce::TextButton scale;
    juce::ParameterAttachment bypass;
    bool bypassed = false;
};

/** Info line, oversampling, limiter, engine status. */
class Footer : public juce::Component
{
public:
    explicit Footer (RoomBreakerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    InfoLine info;

private:
    RoomBreakerAudioProcessor& proc;
    Segmented oversampling;
    ToggleBox limiter;
    juce::Component statusTarget;
    juce::String status;
    float cpu = 0.0f;
};

/** Everything, at design size. The editor scales this one component. */
class Canvas : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};
} // namespace rb::ui

//==============================================================================
class RoomBreakerAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RoomBreakerAudioProcessorEditor (RoomBreakerAudioProcessor&);
    ~RoomBreakerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = rb::ui::kDesignW, kH = rb::ui::kDesignH;
    static constexpr float kMinScale = 0.75f, kMaxScale = 2.0f;
    /** Resizes the window to a multiple of the design size. */
    void setScale (float);
    float getScale() const noexcept { return scale; }

    // tests
    rb::ui::InfoLine& getInfoPanel() { return footer.info; }
    rb::ui::HouseView& getHouse() { return display.house; }
    rb::ui::LiveState& getLive() { return live; }
    rb::ui::PresetBrowser& getBrowser() { return browser; }
    rb::ui::ControlPanel& getControls() { return controls; }
    juce::Component& getCanvas() { return canvas; }
    void advance (float seconds);   // run the UI clock without a message loop timer

private:
    void timerCallback() override;

    RoomBreakerAudioProcessor& audioProcessor;
    rb::ui::LnF lnf;
    rb::ui::LiveState live;
    rb::ui::Canvas canvas;
    rb::ui::Header header;
    rb::ui::Rail inRail, outRail;
    rb::ui::Display display;
    rb::ui::ControlPanel controls;
    rb::ui::Footer footer;
    rb::ui::PresetBrowser browser;
    float scale = 1.0f;
    bool sizeReady = false;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomBreakerAudioProcessorEditor)
};
