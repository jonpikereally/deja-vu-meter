#pragma once

#include <JuceHeader.h>
#include "SpectrumProcessor.h"

//==============================================================================
inline float specNorm (float linear, float minDb)
{
    const float dB = juce::Decibels::gainToDecibels (linear, -120.0f);
    return juce::jlimit (0.0f, 1.0f, (dB - minDb) / (0.0f - minDb));
}

//==============================================================================
class SpectrumCurve : public juce::Component
{
public:
    void setSources (const float* live, const float* rec, const float* centres, int n)
    { liveN = live; recN = rec; centres_ = centres; count = n; }
    void update (bool hasRecording, float minDb)
    { hasRec = hasRecording; minDb_ = minDb; repaint(); }
    void paint (juce::Graphics&) override;

private:
    const float* liveN { nullptr };
    const float* recN { nullptr };
    const float* centres_ { nullptr };
    int   count { 0 };
    bool  hasRec { false };
    float minDb_ { -90.0f };
};

//==============================================================================
class BandBars : public juce::Component
{
public:
    void setSources (const float* live, const float* rec, int n) { liveN = live; recN = rec; count = n; }
    void update (bool hasRecording, float minDb) { hasRec = hasRecording; minDb_ = minDb; repaint(); }
    void paint (juce::Graphics&) override;

private:
    const float* liveN { nullptr };
    const float* recN { nullptr };
    int   count { 0 };
    bool  hasRec { false };
    float minDb_ { -90.0f };
};

//==============================================================================
class DejaVUSpectrumAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit DejaVUSpectrumAudioProcessorEditor (DejaVUSpectrumAudioProcessor&);
    ~DejaVUSpectrumAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshRecordingList();
    void refreshPeaksList();

    DejaVUSpectrumAudioProcessor& proc;
    static constexpr int kBands = DejaVUSpectrumAudioProcessor::kBands;

    juce::TooltipWindow tooltipWindow { this };

    SpectrumCurve curve;
    BandBars      bars;
    juce::Label   numericLabel;

    juce::TextButton autoButton  { "AUTO" };
    juce::TextButton armButton   { "ARM" };
    juce::TextButton clearButton { "DEL" };

    juce::TextEditor nameField;
    juce::Label      nameLabel  { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel { {}, "TAKE" };
    juce::ComboBox   minLenBox;
    juce::Label      takeInfoLabel;

    juce::ComboBox   peaksBox;
    juce::Label      peaksLabel { {}, "PEAKS" };
    juce::ComboBox   threshBox;
    juce::TextButton monitorButton { "PEAKS MONITOR" };

    juce::Slider zoomSlider;
    juce::Label  zoomLabel { {}, "ZOOM" };

    juce::Label targetLabel;
    juce::Label positionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   armAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> minLenAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> threshAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   monitorAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   zoomAtt;

    std::array<float, kBands> liveDisp {};
    std::array<float, kBands> recDisp {};
    std::array<float, kBands> centres {};

    bool blinkOn { false };
    int  blinkCounter { 0 };
    bool lastAutoOn { false };
    bool lastPeaksMonitorOn { false };
    int  lastMonitorRev { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUSpectrumAudioProcessorEditor)
};
