#pragma once

#include <JuceHeader.h>
#include "WidthProcessor.h"
#include "../MenuButton.h"

//==============================================================================
// Goniometer (Lissajous): mono = vertical, L = upper-left, R = upper-right.
//==============================================================================
class Goniometer : public juce::Component
{
public:
    void setSource (const float* pts, int count) { pts_ = pts; count_ = count; }
    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;

private:
    const float* pts_ { nullptr };
    int count_ { 0 };
};

//==============================================================================
// Correlation / Width / Balance horizontal meters.
//==============================================================================
class FieldMeters : public juce::Component
{
public:
    void update (float corr, float width, float recWidth, float balance, bool hasRec)
    { corr_ = corr; width_ = width; recWidth_ = recWidth; bal_ = balance; hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;

private:
    void bipolar (juce::Graphics&, juce::Rectangle<float> bar, float v, const char* label, const juce::String& value);
    float corr_ { 1.0f }, width_ { 0.0f }, recWidth_ { 0.0f }, bal_ { 0.0f };
    bool  hasRec_ { false };
};

//==============================================================================
class DejaVUWidthAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DejaVUWidthAudioProcessorEditor (DejaVUWidthAudioProcessor&);
    ~DejaVUWidthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshRecordingList();

    DejaVUWidthAudioProcessor& proc;
    juce::TooltipWindow tooltipWindow { this };

    Goniometer  gonio;
    FieldMeters meters;
    std::array<float, 2 * DejaVUWidthAudioProcessor::kGonio> gonioSnapshot {};

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUWidthAudioProcessorEditor)
};
