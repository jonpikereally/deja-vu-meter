#include "PitchEditor.h"
#include <cmath>

//==============================================================================
void TuningMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto in = b.reduced (8.0f);
    const float cx = in.getCentreX(), halfW = in.getWidth() * 0.5f;
    const float top = in.getY(), barBottom = in.getBottom() - 16.0f;
    auto xOfCents = [&] (float c) { return cx + juce::jlimit (-50.0f, 50.0f, c) / 50.0f * halfW; };

    // In-tune zone (+/-5 cents).
    const float zoneW = 5.0f / 50.0f * halfW;
    g.setColour (juce::Colour (0x1536c46b));
    g.fillRect (juce::Rectangle<float> (cx - zoneW, top, zoneW * 2.0f, barBottom - top));

    g.setFont (juce::FontOptions (9.0f));
    for (int c : { -50, -25, 0, 25, 50 })
    {
        const float x = xOfCents ((float) c);
        g.setColour (c == 0 ? juce::Colour (0x66ffffff) : juce::Colour (0x18ffffff));
        g.drawVerticalLine ((int) x, top, barBottom);
        g.setColour (juce::Colour (0xff777777));
        g.drawText ((c > 0 ? "+" : "") + juce::String (c),
                    juce::Rectangle<float> (x - 14.0f, barBottom + 1.0f, 28.0f, 12.0f), juce::Justification::centred);
    }

    if (ghost_)
    {
        const float x = xOfCents (ghostCents_);
        g.setColour (juce::Colour (0xffe0a53a));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, top, 3.0f, barBottom - top));
    }
    if (voiced_)
    {
        const float cc = juce::jlimit (-50.0f, 50.0f, cents_);
        const float x = xOfCents (cc);
        const juce::Colour col = std::abs (cc) < 5.0f ? juce::Colour (0xff36c46b)
                               : std::abs (cc) < 15.0f ? juce::Colour (0xffe0a53a)
                                                       : juce::Colour (0xffe0483a);
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (x - 2.0f, top, 4.0f, barBottom - top));
    }
}

//==============================================================================
DejaVUPitchAudioProcessorEditor::DejaVUPitchAudioProcessorEditor (DejaVUPitchAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (meter);

    noteLabel.setJustificationType (juce::Justification::centred);
    noteLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8e8e8));
    noteLabel.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    addAndMakeVisible (noteLabel);

    centsLabel.setJustificationType (juce::Justification::centred);
    centsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0b0));
    centsLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (centsLabel);

    recNoteLabel.setJustificationType (juce::Justification::centred);
    recNoteLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0a53a));
    recNoteLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (recNoteLabel);

    deltaLabel.setJustificationType (juce::Justification::centred);
    deltaLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9fd0b0));
    deltaLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (deltaLabel);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    autoButton.setTooltip ("Auto-record: automatically records the pitch every time the transport plays. Keeps the last 5 takes.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records the detected pitch while the transport plays, then disarms on stop.");
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
    setSize (480, 470);
    startTimerHz (30);
}

DejaVUPitchAudioProcessorEditor::~DejaVUPitchAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void DejaVUPitchAudioProcessorEditor::refreshRecordingList()
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
void DejaVUPitchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);
    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU PITCH", top, juce::Justification::centredLeft);
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
}

void DejaVUPitchAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);

    noteLabel.setBounds (r.removeFromTop (52));
    centsLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);
    meter.setBounds (r.removeFromTop (66));
    auto info = r.removeFromTop (20);
    recNoteLabel.setBounds (info.removeFromLeft (info.getWidth() / 2));
    deltaLabel.setBounds (info);

    auto controls = r.removeFromBottom (152);

    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 3;
    autoButton.setBounds  (buttons.removeFromLeft (bw).reduced (3, 0));
    armButton.setBounds   (buttons.removeFromLeft (bw).reduced (3, 0));
    clearButton.setBounds (buttons.reduced (3, 0));

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
void DejaVUPitchAudioProcessorEditor::timerCallback()
{
    const bool voiced = proc.liveVoiced.load();
    const float midi = proc.liveMidi.load();
    const float recMidi = proc.recordedMidi.load();
    const bool recVoiced = proc.hasRecording.load() && recMidi > 0.0f;

    const float liveCents = voiced ? DejaVUPitchAudioProcessor::centsOf (midi) : 0.0f;
    const float recCents  = recVoiced ? DejaVUPitchAudioProcessor::centsOf (recMidi) : 0.0f;
    meter.update (voiced, liveCents, recVoiced, recCents);

    if (voiced)
    {
        noteLabel.setText (DejaVUPitchAudioProcessor::noteName (DejaVUPitchAudioProcessor::nearestNote (midi)), juce::dontSendNotification);
        const int c = (int) std::lround (liveCents);
        centsLabel.setText (juce::String (c > 0 ? "+" : "") + juce::String (c) + " cents   ("
                            + juce::String (proc.liveFreq.load(), 1) + " Hz)", juce::dontSendNotification);
    }
    else { noteLabel.setText ("--", juce::dontSendNotification); centsLabel.setText ({}, juce::dontSendNotification); }

    recNoteLabel.setText (recVoiced ? "REC " + DejaVUPitchAudioProcessor::noteName (DejaVUPitchAudioProcessor::nearestNote (recMidi))
                                    : juce::String ("REC --"), juce::dontSendNotification);

    if (voiced && recVoiced)
    {
        const int d = (int) std::lround ((midi - recMidi) * 100.0f);
        deltaLabel.setText (juce::String (d > 0 ? "+" : "") + juce::String (d) + " c vs take", juce::dontSendNotification);
    }
    else deltaLabel.setText ({}, juce::dontSendNotification);

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
