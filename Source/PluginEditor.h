#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// A single vertical bar meter (green→amber→red) with a peak-hold cap. Ghost
// mode draws hollow/amber for the recorded take.
//==============================================================================
class VUMeter : public juce::Component
{
public:
    explicit VUMeter (bool ghost) : isGhost (ghost) {}

    void setValues (float level, float peak) { level01 = level; peak01 = peak; repaint(); }

    static float toNorm (float linear)
    {
        const float dB = juce::Decibels::gainToDecibels (linear, kMinDb);
        return juce::jlimit (0.0f, 1.0f, (dB - kMinDb) / (kMaxDb - kMinDb));
    }

    void paint (juce::Graphics& g) override;

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb =   6.0f;

private:
    bool  isGhost;
    float level01 { 0.0f };
    float peak01  { 0.0f };
};

//==============================================================================
// Classic analog VU face: cream dial, swept scale, red zone past 0 VU, a PEAK
// lamp, and two needles — black "Live" and amber "Recorded" ghost.
//==============================================================================
class AnalogVUMeter : public juce::Component
{
public:
    void setValues (float liveLin, float recLin, float livePeakLin, float recPeakLin, bool recActive)
    {
        live = liveLin; rec = recLin; livePeak = livePeakLin; recPeak = recPeakLin; hasRec = recActive;
        repaint();
    }

    void paint (juce::Graphics& g) override;

    // 0 VU reference and dial range.
    static constexpr float kRefDbfs = -18.0f;
    static constexpr float kVuMin   = -20.0f;
    static constexpr float kVuMax   =   3.0f;

private:
    static float vuFromLinear (float lin)
    {
        return juce::Decibels::gainToDecibels (lin, -120.0f) - kRefDbfs;
    }
    static float angleForVu (float vu);   // radians, clockwise from 12 o'clock

    float live { 0.0f }, rec { 0.0f }, livePeak { 0.0f }, recPeak { 0.0f };
    bool  hasRec { false };
};

//==============================================================================
class TimelineVUAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit TimelineVUAudioProcessorEditor (TimelineVUAudioProcessor&);
    ~TimelineVUAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applySkin();
    void refreshRecordingList();

    TimelineVUAudioProcessor& proc;

    // Meters.
    VUMeter       recordedMeter { true };
    VUMeter       liveMeter     { false };
    AnalogVUMeter analogMeter;
    juce::Label   recordedLabel { {}, "RECORDED" };
    juce::Label   liveLabel     { {}, "LIVE" };
    bool          analogSkin { true };

    // Controls.
    juce::ComboBox   modeBox;
    juce::ComboBox   skinBox;
    juce::TextButton autoButton  { "AUTO" };
    juce::TextButton armButton   { "ARM" };
    juce::TextButton clearButton { "DEL" };

    // Recording naming + history.
    juce::TextEditor nameField;
    juce::Label      nameLabel    { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel   { {}, "TAKE" };
    juce::Label      takeInfoLabel;

    // Peak tagging.
    juce::ComboBox peaksBox;
    juce::Label    peaksLabel  { {}, "PEAKS" };
    juce::ComboBox threshBox;

    // Status: playhead position + selected-peak target.
    juce::Label targetLabel;
    juce::Label positionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> skinAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> threshAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   armAtt;

    // Display smoothing / peak hold.
    float liveDisplay { 0.0f }, livePeak { 0.0f };
    float recDisplay  { 0.0f }, recPeak  { 0.0f };
    bool  blinkOn { false };
    int   blinkCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineVUAudioProcessorEditor)
};
