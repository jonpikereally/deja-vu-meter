#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MenuButton.h"

//==============================================================================
// Stereo vertical bar meter: two bars (L, R). Ghost mode draws hollow/amber.
//==============================================================================
class VUMeter : public juce::Component
{
public:
    explicit VUMeter (bool ghost) : isGhost (ghost) {}

    void setValues (float lL, float pL, float lR, float pR,
                    float rL = -100.0f, float rR = -100.0f, bool showRec = false)
    {
        levL = lL; pkL = pL; levR = lR; pkR = pR;
        recL = rL; recR = rR; showRec_ = showRec;
        repaint();
    }

    static float toNorm (float linear)
    {
        const float dB = juce::Decibels::gainToDecibels (linear, kMinDb);
        return juce::jlimit (0.0f, 1.0f, (dB - kMinDb) / (kMaxDb - kMinDb));
    }

    void paint (juce::Graphics& g) override;

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb =   6.0f;

private:
    void drawBar (juce::Graphics&, juce::Rectangle<float>, float level, float peak,
                  float recMark, bool showRec, const char* label);

    bool  isGhost;
    float levL { 0.0f }, pkL { 0.0f }, levR { 0.0f }, pkR { 0.0f };
    float recL { -100.0f }, recR { -100.0f };
    bool  showRec_ { false };
};

//==============================================================================
// Single-channel analog VU face with spring-damped needle ballistics: a black
// "Live" needle and an amber "Recorded" ghost needle, plus two PEAK lamps.
//==============================================================================
class AnalogVUMeter : public juce::Component
{
public:
    explicit AnalogVUMeter (juce::String chan) : channel (std::move (chan)) {}

    // Advances the needle physics one display frame toward the new targets.
    void setValues (float liveLin, float recLin, float livePeakLin, float recPeakLin,
                    bool recActive, bool isVu);

    void paint (juce::Graphics& g) override;

    static constexpr float kRefDbfs = -18.0f;   // 0 VU
    static constexpr float kVuMin   = -20.0f;
    static constexpr float kVuMax   =   3.0f;

private:
    static float vuFromLinear (float lin) { return juce::Decibels::gainToDecibels (lin, -120.0f) - kRefDbfs; }
    static float angleForVu (float vu);
    static void  stepNeedle (float& pos, float& vel, float target, bool isVu);

    juce::String channel;
    float posLive { kVuMin }, velLive { 0.0f };
    float posRec  { kVuMin }, velRec  { 0.0f };
    float livePeak { 0.0f }, recPeak { 0.0f };
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
    void refreshPeaksList();

    TimelineVUAudioProcessor& proc;

    juce::TooltipWindow tooltipWindow { this };

    // Meters.
    VUMeter       recordedMeter { true };
    VUMeter       liveMeter     { false };
    AnalogVUMeter dialL { "L" };
    AnalogVUMeter dialR { "R" };
    juce::Label   recordedLabel { {}, "RECORDED" };
    juce::Label   liveLabel     { {}, "LIVE" };
    juce::Label   numericLabel;
    bool          analogSkin { true };

    // Controls.
    juce::ComboBox   modeBox;
    juce::ComboBox   skinBox;
    juce::TextButton compactButton { "2 BARS" };
    juce::TextButton autoButton  { "AUTO" };
    juce::TextButton armButton   { "ARM" };
    MenuButton       clearButton { "DEL" };

    // Recording naming + history.
    juce::TextEditor nameField;
    juce::Label      nameLabel  { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel { {}, "TAKE" };
    juce::Label      takeInfoLabel;
    juce::ComboBox   minLenBox;

    // Peak tagging.
    juce::ComboBox   peaksBox;
    juce::Label      peaksLabel { {}, "PEAKS" };
    juce::ComboBox   threshBox;
    juce::TextButton monitorButton { "PEAKS MONITOR" };

    juce::Label targetLabel;
    juce::Label positionLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> skinAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   compactAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> threshAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> minLenAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   armAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   monitorAtt;

    // Per-channel display smoothing / peak hold.
    float liveDispL { 0.0f }, liveDispR { 0.0f }, livePkL { 0.0f }, livePkR { 0.0f };
    float recDispL  { 0.0f }, recDispR  { 0.0f }, recPkL  { 0.0f }, recPkR  { 0.0f };
    bool  blinkOn { false };
    int   blinkCounter { 0 };
    bool  lastAutoOn { false };
    bool  lastPeaksMonitorOn { false };
    int   lastMonitorRev { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineVUAudioProcessorEditor)
};
