#pragma once

#include <JuceHeader.h>
#include "LUFSProcessor.h"
#include "../MenuButton.h"

//==============================================================================
// Horizontal short-term loudness bar with a recorded ghost marker + target line.
//==============================================================================
class LoudnessMeter : public juce::Component
{
public:
    void update (float shortT, float rec, float target, bool hasRec)
    { st = shortT; recV = rec; target_ = target; hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;

    static constexpr float kMin = -40.0f, kMax = 0.0f;
    static float norm (float lufs) { return juce::jlimit (0.0f, 1.0f, (lufs - kMin) / (kMax - kMin)); }

private:
    float st { -100.0f }, recV { -100.0f }, target_ { -1000.0f };
    bool  hasRec_ { false };
};

//==============================================================================
class DejaVULUFSAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DejaVULUFSAudioProcessorEditor (DejaVULUFSAudioProcessor&);
    ~DejaVULUFSAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshRecordingList();

    DejaVULUFSAudioProcessor& proc;
    juce::TooltipWindow tooltipWindow { this };

    LoudnessMeter meter;
    juce::Label   mLabel, sLabel, iLabel, tpLabel;

    juce::TextButton autoButton  { "AUTO" };
    juce::TextButton armButton   { "ARM" };
    MenuButton       clearButton { "DEL" };
    juce::ComboBox   targetBox;
    juce::Label      targetLabel { {}, "TARGET" };

    juce::TextEditor nameField;
    juce::Label      nameLabel  { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel { {}, "TAKE" };
    juce::ComboBox   minLenBox;
    juce::Label      takeInfoLabel;
    juce::Label      positionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt, armAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> minLenAtt, targetAtt;

    bool blinkOn { false };
    int  blinkCounter { 0 };
    bool lastAutoOn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVULUFSAudioProcessorEditor)
};
