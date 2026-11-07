#pragma once
//==============================================================================
// Parameter widgets. Each one tags itself with an "info" key (its parameter ID)
// so the INFO panel can explain whatever the mouse is over.
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace rb::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

inline void setInfo (juce::Component& c, const juce::String& key) { c.getProperties().set ("info", key); }

//==============================================================================
class Knob : public juce::Component
{
public:
    enum Size { Small, Regular, Large };

    Knob (APVTS& s, const juce::String& paramId, const juce::String& label, Size size = Regular)
        : name (label), knobSize (size)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 16);
        slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        slider.setMouseDragSensitivity (size == Large ? 260 : 220);
        addAndMakeVisible (slider);
        att = std::make_unique<APVTS::SliderAttachment> (s, paramId, slider);
        if (auto* p = s.getParameter (paramId))
        {
            const auto range = p->getNormalisableRange();
            slider.getProperties().set ("bipolar", range.start < 0.0f && range.end > 0.0f);
            slider.setDoubleClickReturnValue (true, range.convertFrom0to1 (p->getDefaultValue()));
        }
        setInfo (*this, paramId);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (slider.isMouseOverOrDragging() ? C::text : C::dim);
        g.setFont (uiFont (knobSize == Large ? 14.0f : (knobSize == Small ? 12.0f : 13.0f), true));
        g.drawFittedText (name, 0, 0, getWidth(), 16, juce::Justification::centred, 1, 0.85f);
    }

    void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (16)); }
    void parentHierarchyChanged() override { slider.sendLookAndFeelChange(); }

    juce::Slider slider;

private:
    juce::String name;
    Size knobSize;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

//==============================================================================
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (APVTS& s, const juce::String& paramId, const juce::String& label) : name (label)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (paramId)))
            box.addItemList (p->choices, 1);   // must be populated before the attachment syncs
        box.setColour (juce::ComboBox::focusedOutlineColourId, C::dim);
        addAndMakeVisible (box);
        att = std::make_unique<APVTS::ComboBoxAttachment> (s, paramId, box);
        setInfo (*this, paramId);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (C::dim);
        g.setFont (uiFont (13.0f, true));
        g.drawText (name, 1, 0, getWidth(), 16, juce::Justification::centredLeft);
    }
    void resized() override { box.setBounds (getLocalBounds().withTrimmedTop (19)); }
    void parentHierarchyChanged() override { box.sendLookAndFeelChange(); }

    juce::ComboBox box;

private:
    juce::String name;
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
};

//==============================================================================
/** Row of buttons for a choice parameter: every option visible, one click to change. */
class Segmented : public juce::Component
{
public:
    Segmented (APVTS& s, const juce::String& paramId, const juce::StringArray& labels)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (labels[i]));
            b->getProperties().set ("segment", i == 0 ? 0 : (i == labels.size() - 1 ? 2 : 1));
            b->onClick = [this, i] { if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        if (auto* p = s.getParameter (paramId))
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { select (juce::roundToInt (v)); }, nullptr);
            attachment->sendInitialUpdate();
        }
        setInfo (*this, paramId);
    }

    void resized() override
    {
        const int n = buttons.size();
        for (int i = 0; i < n; ++i)
        {
            const int x0 = getWidth() * i / n, x1 = getWidth() * (i + 1) / n;
            buttons[i]->setBounds (x0, 0, x1 - x0 + (i < n - 1 ? 1 : 0), getHeight());   // shared 1 px borders
        }
    }

private:
    void select (int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
/** Switch bound to a bool parameter. */
class Chip : public juce::Component
{
public:
    Chip (APVTS& s, const juce::String& paramId, const juce::String& label, juce::Colour onColour = C::text)
    {
        button.setButtonText (label);
        button.setColour (juce::ToggleButton::tickColourId, onColour);
        addAndMakeVisible (button);
        att = std::make_unique<APVTS::ButtonAttachment> (s, paramId, button);
        setInfo (*this, paramId);
    }
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> att;
};

//==============================================================================
/** Momentary button for a bool parameter: press = 1, release = 0, wrapped in one host gesture. */
class MomentaryButton : public juce::TextButton
{
public:
    MomentaryButton (APVTS& s, const juce::String& paramId, const juce::String& label, bool solid = false)
        : juce::TextButton (label), param (s.getParameter (paramId))
    {
        setColour (juce::TextButton::buttonColourId, solid ? C::hot : C::panel2);
        if (solid) getProperties().set ("solid", true);
        setInfo (*this, paramId);
        onStateChange = [this]
        {
            const bool down = isDown();
            if (down == held || param == nullptr)
                return;
            held = down;
            if (down) { param->beginChangeGesture(); param->setValueNotifyingHost (1.0f); }
            else      { param->setValueNotifyingHost (0.0f); param->endChangeGesture(); }
        };
    }

private:
    juce::RangedAudioParameter* param = nullptr;
    bool held = false;
};
} // namespace rb::ui
