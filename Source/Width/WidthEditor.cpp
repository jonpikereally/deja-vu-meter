#include "WidthEditor.h"
#include <cmath>

//==============================================================================
void Goniometer::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);

    auto area = b.reduced (6.0f);
    const float s = juce::jmin (area.getWidth(), area.getHeight());
    auto sq = juce::Rectangle<float> (0, 0, s, s).withCentre (area.getCentre());
    const float cx = sq.getCentreX(), cy = sq.getCentreY(), scale = s * 0.45f;

    g.setColour (juce::Colour (0x18ffffff));
    g.drawVerticalLine ((int) cx, sq.getY(), sq.getBottom());
    g.drawLine (cx - scale, cy - scale, cx + scale, cy + scale, 1.0f);
    g.drawLine (cx - scale, cy + scale, cx + scale, cy - scale, 1.0f);

    g.setColour (juce::Colour (0xff666666));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("M", juce::Rectangle<float> (cx - 8, sq.getY() + 2, 16, 12), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (sq.getX() + 3, sq.getY() + 2, 14, 12), juce::Justification::left);
    g.drawText ("R", juce::Rectangle<float> (sq.getRight() - 17, sq.getY() + 2, 14, 12), juce::Justification::right);

    if (pts_ != nullptr && count_ > 0)
    {
        g.setColour (juce::Colour (0x9936c46b));
        for (int i = 0; i < count_; ++i)
        {
            const float side = pts_[2 * i], mid = pts_[2 * i + 1];
            const float px = cx - juce::jlimit (-1.5f, 1.5f, side) * scale;
            const float py = cy - juce::jlimit (-1.5f, 1.5f, mid) * scale;
            g.fillRect (px - 0.75f, py - 0.75f, 1.5f, 1.5f);
        }
    }
}

//==============================================================================
void FieldMeters::bipolar (juce::Graphics& g, juce::Rectangle<float> bar, float v, const char* label, const juce::String& value)
{
    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto row = bar;
    auto labelArea = row.removeFromLeft (56);
    auto valArea   = row.removeFromRight (64);
    g.drawText (label, labelArea, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffe8e8e8));
    g.drawText (value, valArea, juce::Justification::centredRight);

    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (row, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (row, 3.0f, 1.0f);
    auto in = row.reduced (3.0f);
    const float cx = in.getCentreX();
    g.setColour (juce::Colour (0x55ffffff));
    g.drawVerticalLine ((int) cx, in.getY(), in.getBottom());
    const float x = cx + juce::jlimit (-1.0f, 1.0f, v) * (in.getWidth() * 0.5f);
    g.setColour (v >= 0.0f ? juce::Colour (0xff36c46b) : juce::Colour (0xffe0483a));
    g.fillRect (juce::Rectangle<float> (juce::jmin (cx, x), in.getY(), std::abs (x - cx), in.getHeight()));
}

void FieldMeters::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float rowH = b.getHeight() / 3.0f;

    // Correlation (bipolar): +1 mono, 0 wide, -1 out of phase.
    bipolar (g, b.removeFromTop (rowH).reduced (0.0f, 3.0f), corr_, "CORR", juce::String (corr_, 2));

    // Width (0..1) with recorded ghost marker.
    {
        auto row = b.removeFromTop (rowH).reduced (0.0f, 3.0f);
        auto labelArea = row.removeFromLeft (56);
        auto valArea   = row.removeFromRight (64);
        g.setColour (juce::Colour (0xff888888)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("WIDTH", labelArea, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffe8e8e8));
        g.drawText (juce::String ((int) std::round (width_ * 100.0f)) + "%", valArea, juce::Justification::centredRight);

        g.setColour (juce::Colour (0xff141414)); g.fillRoundedRectangle (row, 3.0f);
        g.setColour (juce::Colour (0xff2a2a2a)); g.drawRoundedRectangle (row, 3.0f, 1.0f);
        auto in = row.reduced (3.0f);
        g.setColour (juce::Colour (0xff36a0c4));
        g.fillRect (juce::Rectangle<float> (in.getX(), in.getY(), juce::jlimit (0.0f, 1.0f, width_) * in.getWidth(), in.getHeight()));
        if (hasRec_)
        {
            const float x = in.getX() + juce::jlimit (0.0f, 1.0f, recWidth_) * in.getWidth();
            g.setColour (juce::Colour (0xffe0a53a));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, in.getY(), 3.0f, in.getHeight()));
        }
    }

    // Balance (bipolar): L .. R.
    juce::String balStr = std::abs (bal_) < 0.02f ? juce::String ("C")
                        : (bal_ > 0.0f ? "R " : "L ") + juce::String ((int) std::round (std::abs (bal_) * 100.0f)) + "%";
    bipolar (g, b.removeFromTop (rowH).reduced (0.0f, 3.0f), bal_, "BAL", balStr);
}

//==============================================================================
DejaVUWidthAudioProcessorEditor::DejaVUWidthAudioProcessorEditor (DejaVUWidthAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    gonio.setSource (gonioSnapshot.data(), DejaVUWidthAudioProcessor::kGonio);
    addAndMakeVisible (gonio);
    addAndMakeVisible (meters);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    autoButton.setTooltip ("Auto-record: automatically records the stereo width every time the transport plays. Keeps the last 5 takes.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records the stereo width while the transport plays, then disarms on stop.");
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
    nameField.setTooltip ("Name for the next recording. With a take selected, press Enter to rename it.");
    nameField.onReturnKey = [this]
    {
        if (proc.getActiveRecording() >= 0)
        {
            proc.renameActiveRecording (nameField.getText());
            refreshRecordingList();
            nameField.setText (proc.getRecordingName (proc.getActiveRecording()), juce::dontSendNotification);
        }
    };
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
    setSize (460, 540);
    startTimerHz (30);
}

DejaVUWidthAudioProcessorEditor::~DejaVUWidthAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void DejaVUWidthAudioProcessorEditor::refreshRecordingList()
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
void DejaVUWidthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);
    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU WIDTH", top, juce::Justification::centredLeft);
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

void DejaVUWidthAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);

    auto controls = r.removeFromBottom (150);
    r.removeFromBottom (8);
    meters.setBounds (r.removeFromBottom (100));
    r.removeFromBottom (6);
    gonio.setBounds (r);

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
void DejaVUWidthAudioProcessorEditor::timerCallback()
{
    // Snapshot goniometer points (benign race with the audio writer).
    for (int i = 0; i < 2 * DejaVUWidthAudioProcessor::kGonio; ++i)
        gonioSnapshot[(size_t) i] = proc.gonio[(size_t) i];
    gonio.refresh();

    meters.update (proc.liveCorr.load(), proc.liveWidth.load(), proc.recordedWidth.load(),
                   proc.liveBalance.load(), proc.hasRecording.load());

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
