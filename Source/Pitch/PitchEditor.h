#pragma once

#include <JuceHeader.h>
#include "PitchProcessor.h"
#include "../MenuButton.h"

//==============================================================================
// Horizontal tuning meter: +/-50 cents, live needle + recorded ghost needle.
//==============================================================================
class TuningMeter : public juce::Component
{
public:
    void update (bool voiced, float cents, bool ghost, float ghostCents)
    { voiced_ = voiced; cents_ = cents; ghost_ = ghost; ghostCents_ = ghostCents; repaint(); }
    void paint (juce::Graphics&) override;

private:
    bool  voiced_ { false }, ghost_ { false };
    float cents_ { 0.0f }, ghostCents_ { 0.0f };
};

//==============================================================================
class DejaVUPitchAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DejaVUPitchAudioProcessorEditor (DejaVUPitchAudioProcessor&);
    ~DejaVUPitchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshRecordingList();

    DejaVUPitchAudioProcessor& proc;
    juce::TooltipWindow tooltipWindow { this };

    TuningMeter meter;
    juce::Label noteLabel, centsLabel, recNoteLabel, deltaLabel;

    juce::TextButton autoButton  { "AUTO" };
    juce::TextButton armButton   { "ARM" };
    MenuButton       clearButton { "DEL" };

    juce::TextEditor nameField;
    juce::Label      nameLabel  { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel { {}, "TAKE" };
    juce::ComboBox   minLenBox;
    juce::Label      takeInfoLabel;
    juce::Label      positionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt, armAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> minLenAtt;

    bool blinkOn { false };
    int  blinkCounter { 0 };
    bool lastAutoOn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUPitchAudioProcessorEditor)
};
