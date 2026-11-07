#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Widgets.h"
#include "UI/Help.h"
#include "UI/LiveState.h"
#include "UI/HouseView.h"

namespace rb::ui
{
//==============================================================================
/** Six columns under the house: how much of each wall stands, and its load against yield. */
class WallSurvey : public juce::Component
{
public:
    explicit WallSurvey (const LiveState& s) : live (s) { setInfo (*this, "survey"); }
    void paint (juce::Graphics&) override;
    int highlighted = -1;

private:
    const LiveState& live;
};

class HistoryGraph : public juce::Component
{
public:
    explicit HistoryGraph (const LiveState& s) : live (s) { setInfo (*this, "history"); }
    void paint (juce::Graphics&) override;

private:
    const LiveState& live;
};

/** Left column of the ROOM page. */
class StructureView : public juce::Component
{
public:
    StructureView (LiveState&, juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh (float dt);

    HouseView house;

private:
    WallSurvey survey;
    HistoryGraph history;
};

//==============================================================================
/** Middle column of the ROOM page: material, outside, and the 16 sound controls. */
class RoomPanel : public juce::Component
{
public:
    explicit RoomPanel (RoomBreakerAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RoomBreakerAudioProcessor& proc;
    ChoiceBox material, voidType;
    std::vector<std::unique_ptr<Knob>> knobs;
};

//==============================================================================
/** ENGINE page: a plain settings sheet. */
class EnginePage : public juce::Component
{
public:
    EnginePage (RoomBreakerAudioProcessor&, const LiveState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RoomBreakerAudioProcessor& proc;
    const LiveState& live;
    Segmented oversampling, stressSource;
    Knob duck;
    Chip limiter;
};

//==============================================================================
/** Right column, on every page: explains the control under the mouse, or shows the room's status. */
class InfoPanel : public juce::Component, private juce::Timer
{
public:
    InfoPanel (RoomBreakerAudioProcessor&, const LiveState&);
    void paint (juce::Graphics&) override;

    // Registered on the editor's root for all nested children.
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String currentKey() const { return hoverKey; }
    void showHelpFor (const juce::String& key) { hoverKey = key; clearAt = 0; repaint(); }

private:
    void timerCallback() override;
    void paintHelp (juce::Graphics&, juce::Rectangle<float>);
    void paintStatus (juce::Graphics&, juce::Rectangle<float>);

    RoomBreakerAudioProcessor& proc;
    const LiveState& live;
    juce::String hoverKey;
    juce::uint32 clearAt = 0;
};

//==============================================================================
/** Top bar: name + pages, presets + DETONATE / Rebuild, gains, oversampling + freeze, status and meters. */
class Header : public juce::Component
{
public:
    Header (RoomBreakerAudioProcessor&, const LiveState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPreset();
    void tick();
    std::function<void (int)> onTab;
    void setTab (int t);

private:
    void loadPreset (int index);

    RoomBreakerAudioProcessor& proc;
    const LiveState& live;
    juce::ComboBox presets;
    juce::TextButton prevB { juce::String::fromUTF8 ("\xe2\x80\xb9") }, nextB { juce::String::fromUTF8 ("\xe2\x80\xba") };
    MomentaryButton detonateB, rebuildB;
    Knob input, mix, output;
    Segmented oversampling;
    Chip freeze;
    std::array<juce::TextButton, 2> tabs;
    juce::Component statusArea, meterArea;
    int shownProgram = -2, lastDetonate = 0, detonateGlow = 0;
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

    static constexpr int kW = 1280, kH = 800, kHeaderH = 84;
    void setTab (int t);
    rb::ui::InfoPanel& getInfoPanel() { return info; }
    rb::ui::HouseView& getHouse() { return structure.house; }
    void advance (float seconds);   // tests: run the UI clock without a message loop timer

private:
    void timerCallback() override;

    RoomBreakerAudioProcessor& audioProcessor;
    rb::ui::LnF lnf;
    rb::ui::LiveState live;
    juce::Component root;
    rb::ui::Header header;
    rb::ui::StructureView structure;
    rb::ui::RoomPanel roomPanel;
    rb::ui::EnginePage enginePage;
    rb::ui::InfoPanel info;
    juce::TooltipWindow tooltips { this, 700 };
    int tab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomBreakerAudioProcessorEditor)
};
