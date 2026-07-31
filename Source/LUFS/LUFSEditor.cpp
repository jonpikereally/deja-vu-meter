#include "LUFSEditor.h"
#include <cmath>

//==============================================================================
void LoudnessMeter::drawScaleAndTarget (juce::Graphics& g, juce::Rectangle<float> barsArea, juce::Rectangle<float> labels)
{
    const float w = barsArea.getWidth(), left = barsArea.getX();
    g.setFont (juce::FontOptions (9.0f));
    for (int l : { -40, -30, -23, -16, -14, -9, 0 })
    {
        const float x = left + norm ((float) l) * w;
        g.setColour (juce::Colour (0x22ffffff));
        g.drawVerticalLine ((int) x, barsArea.getY(), barsArea.getBottom());
        g.setColour (juce::Colour (0xff777777));
        g.drawText (juce::String (l), juce::Rectangle<float> (x - 12, labels.getY(), 24, 12), juce::Justification::centred);
    }
    if (target_ > -100.0f)
    {
        const float x = left + norm (target_) * w;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, barsArea.getY(), 2.0f, barsArea.getHeight()));
    }
}

void LoudnessMeter::paintBar (juce::Graphics& g, juce::Rectangle<float> bar, float lufs, juce::Colour c, const char* label)
{
    const float w = bar.getWidth(), left = bar.getX();
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillRect (bar);
    if (lufs > -70.0f)
    {
        g.setColour (c);
        g.fillRect (juce::Rectangle<float> (left, bar.getY(), norm (lufs) * w, bar.getHeight()));
    }
    g.setColour (juce::Colour (0xffcfcfcf));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (label, bar.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
}

void LoudnessMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto in = b.reduced (6.0f);
    auto labels = in.removeFromBottom (14.0f);
    const float w = in.getWidth(), left = in.getX();

    if (split_)
    {
        // Two stacked bars: LIVE on top, RECORDED below.
        auto full = in;
        auto liveBar = in.removeFromTop (in.getHeight() * 0.5f).reduced (0.0f, 1.5f);
        auto recBar  = in.reduced (0.0f, 1.5f);

        g.setColour (juce::Colour (0xff0d0d0d)); g.fillRect (liveBar);
        if (st > -70.0f)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36c46b), left, 0.0f,
                                                     juce::Colour (0xffe0483a), left + w, 0.0f, false));
            g.fillRect (juce::Rectangle<float> (left, liveBar.getY(), norm (st) * w, liveBar.getHeight()));
        }
        g.setColour (juce::Colour (0xffcfcfcf)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("LIVE", liveBar.reduced (4.0f, 0.0f), juce::Justification::centredLeft);

        paintBar (g, recBar, hasRec_ ? recV : -100.0f, juce::Colour (0xffe0a53a), "REC");

        drawScaleAndTarget (g, full, labels);
        return;
    }

    // Overlapping: single live bar + recorded ghost marker.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36c46b), left, 0.0f,
                                             juce::Colour (0xffe0483a), left + w, 0.0f, false));
    g.fillRect (juce::Rectangle<float> (left, in.getY(), norm (st) * w, in.getHeight()));

    drawScaleAndTarget (g, in, labels);

    if (hasRec_ && recV > -70.0f)
    {
        const float x = left + norm (recV) * w;
        g.setColour (juce::Colour (0xffe0a53a));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, in.getY(), 3.0f, in.getHeight()));
    }
}

//==============================================================================
DejaVULUFSAudioProcessorEditor::DejaVULUFSAudioProcessorEditor (DejaVULUFSAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (meter);

    auto styleStat = [this] (juce::Label& l)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colour (0xffe8e8e8));
        l.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    styleStat (mLabel); styleStat (sLabel); styleStat (iLabel); styleStat (tpLabel);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    autoButton.setTooltip ("Auto-record: automatically records every time the transport plays. Keeps the last 5 takes.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records the short-term loudness while the transport plays, then disarms on stop.");
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "recordArm", armButton);

    clearButton.setTooltip ("Delete takes - opens a menu (delete this take or all takes).");
    clearButton.onClick = [this]
    {
        const bool has = proc.getNumRecordings() > 0;
        juce::PopupMenu m;
        m.addItem (1, "Delete take: " + proc.getRecordingName (proc.getActiveRecording()), has, false);
        m.addItem (2, "Delete ALL takes", has, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (clearButton),
            [this] (int r) { if (r == 1) proc.deleteActiveRecording(); else if (r == 2) proc.deleteAllRecordings(); if (r != 0) refreshRecordingList(); });
    };
    addAndMakeVisible (clearButton);

    targetLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    targetLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (targetLabel);
    targetBox.addItem ("Off", 1);
    targetBox.addItem ("-14 (streaming)", 2);
    targetBox.addItem ("-16 (podcast)", 3);
    targetBox.addItem ("-23 (EBU R128)", 4);
    targetBox.setTooltip ("Reference loudness target line on the meter.");
    addAndMakeVisible (targetBox);
    targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "target", targetBox);

    splitButton.setClickingTogglesState (true);
    splitButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    splitButton.setTooltip ("Show live and recorded loudness as two separate bars instead of one bar with a ghost marker.");
    addAndMakeVisible (splitButton);
    splitAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "splitMeter", splitButton);

    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (nameLabel);
    nameField.setTextToShowWhenEmpty ("recording name", juce::Colour (0xff666666));
    nameField.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1a1a));
    addAndMakeVisible (nameField);

    takesLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    takesLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (takesLabel);
    takesBox.setTextWhenNoChoicesAvailable ("no takes yet");
    takesBox.onChange = [this]
    {
        const int id = takesBox.getSelectedId();
        if (id > 0) { proc.selectRecording (id - 1); nameField.setText (proc.getRecordingName (id - 1), juce::dontSendNotification); refreshRecordingList(); }
    };
    addAndMakeVisible (takesBox);

    minLenBox.addItem ("Min: Off", 1); minLenBox.addItem ("Min 0.5s", 2); minLenBox.addItem ("Min 1s", 3);
    minLenBox.addItem ("Min 2s", 4); minLenBox.addItem ("Min 5s", 5);
    addAndMakeVisible (minLenBox);
    minLenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "minLen", minLenBox);

    takeInfoLabel.setJustificationType (juce::Justification::centred);
    takeInfoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    takeInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (takeInfoLabel);

    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0b0));
    positionLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (positionLabel);

    refreshRecordingList();
    setSize (520, 440);
    startTimerHz (30);
}

DejaVULUFSAudioProcessorEditor::~DejaVULUFSAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void DejaVULUFSAudioProcessorEditor::refreshRecordingList()
{
    takesBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumRecordings(); ++i) takesBox.addItem (proc.getRecordingName (i), i + 1);
    if (proc.getActiveRecording() >= 0) takesBox.setSelectedId (proc.getActiveRecording() + 1, juce::dontSendNotification);

    const auto info = proc.getRecordingInfo (proc.getActiveRecording());
    if (info.valid)
    {
        auto tc = [] (double s) { const int m = (int) (s / 60.0); return juce::String::formatted ("%d:%05.2f", m, s - m * 60.0); };
        const juce::String s0 = tc (info.startSeconds), s1 = tc (info.endSeconds);
        takeInfoLabel.setText (juce::String::formatted ("start %d.%d (%s)     end %d.%d (%s)     len %.1fs",
            info.startBar, info.startBeat, s0.toRawUTF8(), info.endBar, info.endBeat, s1.toRawUTF8(),
            juce::jmax (0.0, info.endSeconds - info.startSeconds)), juce::dontSendNotification);
    }
    else takeInfoLabel.setText ({}, juce::dontSendNotification);
}

//==============================================================================
void DejaVULUFSAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);
    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU LUFS", top, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v" JucePlugin_VersionString, top, juce::Justification::centredRight);

    if (proc.recordingNow.load())
    {
        auto dot = juce::Rectangle<float> (top.getRight() - 118.0f, top.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (blinkOn ? juce::Colour (0xffff3b30) : juce::Colour (0xff661512));
        g.fillEllipse (dot);
        g.setColour (blinkOn ? juce::Colour (0xffff6b60) : juce::Colour (0xff884440));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("REC", juce::Rectangle<int> ((int) dot.getRight() + 4, top.getY(), 44, top.getHeight()), juce::Justification::centredLeft);
    }

    // Stat header labels.
    g.setColour (juce::Colour (0xff777777));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    auto r = getLocalBounds().reduced (14); r.removeFromTop (30);
    auto stats = r.removeFromTop (48); const int sw = stats.getWidth() / 4;
    const char* hdr[] = { "MOMENTARY", "SHORT-TERM", "INTEGRATED", "TRUE PEAK" };
    for (int i = 0; i < 4; ++i)
        g.drawText (hdr[i], stats.removeFromLeft (sw).removeFromTop (14), juce::Justification::centred);
}

void DejaVULUFSAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);

    auto stats = r.removeFromTop (48);
    stats.removeFromTop (14);
    const int sw = stats.getWidth() / 4;
    mLabel.setBounds  (stats.removeFromLeft (sw));
    sLabel.setBounds  (stats.removeFromLeft (sw));
    iLabel.setBounds  (stats.removeFromLeft (sw));
    tpLabel.setBounds (stats);

    auto controls = r.removeFromBottom (180);
    r.removeFromBottom (8);
    meter.setBounds (r);

    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 3;
    autoButton.setBounds  (buttons.removeFromLeft (bw).reduced (3, 0));
    armButton.setBounds   (buttons.removeFromLeft (bw).reduced (3, 0));
    clearButton.setBounds (buttons.reduced (3, 0));

    controls.removeFromTop (6);
    auto tRow = controls.removeFromTop (26);
    targetLabel.setBounds (tRow.removeFromLeft (56));
    splitButton.setBounds (tRow.removeFromRight (84));
    tRow.removeFromRight (6);
    targetBox.setBounds (tRow);

    controls.removeFromTop (6);
    auto nameRow = controls.removeFromTop (26);
    nameLabel.setBounds (nameRow.removeFromLeft (46));
    nameField.setBounds (nameRow);

    controls.removeFromTop (6);
    auto takeRow = controls.removeFromTop (26);
    takesLabel.setBounds (takeRow.removeFromLeft (46));
    minLenBox.setBounds (takeRow.removeFromRight (92));
    takeRow.removeFromRight (6);
    takesBox.setBounds (takeRow);

    controls.removeFromTop (4);
    takeInfoLabel.setBounds (controls.removeFromTop (18));
    controls.removeFromTop (4);
    positionLabel.setBounds (controls.removeFromTop (24));
}

//==============================================================================
void DejaVULUFSAudioProcessorEditor::timerCallback()
{
    auto ls = [] (float v) { return v > -70.0f ? juce::String (v, 1) : juce::String ("  -  "); };

    const float mom = proc.momentary.load(), st = proc.shortTerm.load();
    const float integ = proc.integrated.load(), tp = proc.truePeakDb.load();
    mLabel.setText (ls (mom), juce::dontSendNotification);
    sLabel.setText (ls (st), juce::dontSendNotification);
    iLabel.setText (ls (integ) + " LUFS", juce::dontSendNotification);
    tpLabel.setText (juce::String (tp, 1) + " dBTP", juce::dontSendNotification);
    tpLabel.setColour (juce::Label::textColourId, tp > -1.0f ? juce::Colour (0xffe0483a) : juce::Colour (0xffe8e8e8));

    meter.update (st, proc.recordedShortTerm.load(), proc.targetLufs(), proc.hasRecording.load(),
                  *proc.apvts.getRawParameterValue ("splitMeter") > 0.5f);

    const bool autoOn = *proc.apvts.getRawParameterValue ("autoMode") > 0.5f;
    if (autoOn != lastAutoOn)
    {
        if (! autoOn)
        {
            armButton.setColour (juce::TextButton::buttonColourId, getLookAndFeel().findColour (juce::TextButton::buttonColourId));
            armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
        }
        lastAutoOn = autoOn;
    }

    if (proc.captureFinished.exchange (false))
    {
        const juce::String nm = autoOn ? "Auto " + juce::Time::getCurrentTime().formatted ("%H:%M:%S") : nameField.getText();
        proc.finalizeCapture (nm);
        refreshRecordingList();
        if (! autoOn) if (auto* prm = proc.apvts.getParameter ("recordArm")) prm->setValueNotifyingHost (0.0f);
        nameField.setText (proc.getRecordingName (proc.getActiveRecording()), juce::dontSendNotification);
    }

    if (++blinkCounter >= 12)
    {
        blinkCounter = 0;
        blinkOn = ! blinkOn;
        if (proc.recordingNow.load()) repaint (getLocalBounds().removeFromTop (30));
        if (autoOn)
        {
            const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff4a3a12);
            armButton.setColour (juce::TextButton::buttonColourId, c);
            armButton.setColour (juce::TextButton::buttonOnColourId, c);
        }
        if (proc.transportPlaying.load() && proc.hasRecording.load())
        {
            const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff5c4410);
            takesBox.setColour (juce::ComboBox::textColourId, c);
            takesBox.setColour (juce::ComboBox::outlineColourId, c);
        }
        else
        {
            takesBox.setColour (juce::ComboBox::textColourId, getLookAndFeel().findColour (juce::ComboBox::textColourId));
            takesBox.setColour (juce::ComboBox::outlineColourId, getLookAndFeel().findColour (juce::ComboBox::outlineColourId));
        }
    }

    const double secs = proc.playheadSeconds.load(), ppq = proc.playheadPpq.load();
    const int num = proc.timeSigNum.load(), den = juce::jmax (1, proc.timeSigDen.load());
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    const int bar = (int) std::floor (ppq / qPerBar) + 1;
    const int beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
    const int mm = (int) (secs / 60.0);
    positionLabel.setText (juce::String::formatted ("%d . %d      %d:%05.2f", bar, beat, mm, secs - mm * 60.0), juce::dontSendNotification);
    positionLabel.setColour (juce::Label::textColourId, proc.transportPlaying.load() ? juce::Colour (0xff36c46b) : juce::Colour (0xff888888));
}
