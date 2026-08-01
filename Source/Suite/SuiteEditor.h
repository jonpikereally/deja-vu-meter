#pragma once

#include <JuceHeader.h>
#include "SuiteProcessor.h"
#include "../MenuButton.h"

//==============================================================================
inline float dbNorm (float lin, float minDb, float maxDb)
{
    const float d = juce::Decibels::gainToDecibels (lin, -120.0f);
    return juce::jlimit (0.0f, 1.0f, (d - minDb) / (maxDb - minDb));
}

//==============================================================================
class PanelMeter : public juce::Component
{
public:
    void update (float lL, float pL, float lR, float pR, float rL, float rR, bool hasRec)
    { lL_ = lL; pL_ = pL; lR_ = lR; pR_ = pR; rL_ = rL; rR_ = rR; hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float lL_ {}, pL_ {}, lR_ {}, pR_ {}, rL_ {}, rR_ {}; bool hasRec_ { false };
};

class PanelLUFS : public juce::Component
{
public:
    void update (float mom, float st, float integ, float tp, float recSt, float target, bool hasRec)
    { mom_ = mom; st_ = st; integ_ = integ; tp_ = tp; recSt_ = recSt; target_ = target; hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float mom_ { -100 }, st_ { -100 }, integ_ { -100 }, tp_ { -100 }, recSt_ { -100 }, target_ { -1000 }; bool hasRec_ { false };
};

class PanelSpectrum : public juce::Component
{
public:
    void setSources (const float* live, const float* rec, const float* centres, int n) { live_ = live; rec_ = rec; centres_ = centres; n_ = n; }
    void update (bool hasRec) { hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;
private:
    const float* live_ { nullptr }; const float* rec_ { nullptr }; const float* centres_ { nullptr };
    int n_ { 0 }; bool hasRec_ { false };
};

class PanelWidth : public juce::Component
{
public:
    void setGonio (const float* pts, int count) { pts_ = pts; count_ = count; }
    void update (float corr, float width, float recWidth, float bal, bool hasRec)
    { corr_ = corr; width_ = width; recWidth_ = recWidth; bal_ = bal; hasRec_ = hasRec; repaint(); }
    void paint (juce::Graphics&) override;
private:
    const float* pts_ { nullptr }; int count_ { 0 };
    float corr_ { 1 }, width_ { 0 }, recWidth_ { 0 }, bal_ { 0 }; bool hasRec_ { false };
};

//==============================================================================
class DejaVUSuiteAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DejaVUSuiteAudioProcessorEditor (DejaVUSuiteAudioProcessor&);
    ~DejaVUSuiteAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshRecordingList();
    void applyPanels();

    DejaVUSuiteAudioProcessor& proc;
    juce::TooltipWindow tooltipWindow { this };
    static constexpr int kBands = DejaVUSuiteAudioProcessor::kBands;

    PanelMeter    panelMeter;
    PanelLUFS     panelLUFS;
    PanelSpectrum panelSpectrum;
    PanelWidth    panelWidth;

    std::array<float, kBands> liveBands {}, recBands {}, centres {};
    std::array<float, 2 * DejaVUSuiteAudioProcessor::kGonio> gonioSnapshot {};

    juce::TextButton showMeterBtn { "METER" }, showLufsBtn { "LUFS" }, showSpecBtn { "SPECTRUM" }, showWidthBtn { "WIDTH" };
    juce::ComboBox   modeBox, targetBox;

    juce::TextButton autoButton { "AUTO" }, armButton { "ARM" };
    MenuButton       clearButton { "DEL" };
    juce::TextEditor nameField;
    juce::Label      nameLabel { {}, "NAME" };
    juce::ComboBox   takesBox;
    juce::Label      takesLabel { {}, "TAKE" };
    juce::ComboBox   minLenBox;
    juce::Label      takeInfoLabel, positionLabel;

    using BAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<BAtt> showMeterAtt, showLufsAtt, showSpecAtt, showWidthAtt, autoAtt, armAtt;
    std::unique_ptr<CAtt> modeAtt, targetAtt, minLenAtt;

    bool blinkOn { false }; int blinkCounter { 0 }; bool lastAutoOn { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUSuiteAudioProcessorEditor)
};
