#include "SpectrumEditor.h"

//==============================================================================
void SpectrumCurve::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff121212));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto in = b.reduced (6.0f);
    const float w = in.getWidth(), h = in.getHeight(), left = in.getX(), bottom = in.getBottom();

    g.setFont (juce::FontOptions (9.0f));
    for (int dB : { 0, -20, -40, -60, -80 })
    {
        const float y = bottom - ((float) dB + 90.0f) / 90.0f * h;
        g.setColour (juce::Colour (0x18ffffff));
        g.drawHorizontalLine ((int) y, left, in.getRight());
        g.setColour (juce::Colour (0xff555555));
        g.drawText (juce::String (dB), juce::Rectangle<float> (left + 2, y - 10, 30, 12), juce::Justification::left);
    }

    if (count <= 1 || liveN == nullptr)
        return;

    if (centres_ != nullptr)
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            int bi = 0; float best = 1.0e9f;
            for (int i = 0; i < count; ++i)
            {
                const float d = std::abs (centres_[i] - f);
                if (d < best) { best = d; bi = i; }
            }
            const float x = left + (float) bi / (count - 1) * w;
            g.setColour (juce::Colour (0x18ffffff));
            g.drawVerticalLine ((int) x, in.getY(), bottom);
            g.setColour (juce::Colour (0xff555555));
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x - 14, bottom - 12, 28, 12), juce::Justification::centred);
        }

    auto xOf = [&] (int i) { return left + (float) i / (count - 1) * w; };

    juce::Path fill;
    fill.startNewSubPath (left, bottom);
    for (int i = 0; i < count; ++i) fill.lineTo (xOf (i), bottom - specNorm (liveN[i]) * h);
    fill.lineTo (in.getRight(), bottom);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x8836c46b), left, in.getY(),
                                             juce::Colour (0x1136c46b), left, bottom, false));
    g.fillPath (fill);

    juce::Path live;
    for (int i = 0; i < count; ++i)
    {
        const float x = xOf (i), y = bottom - specNorm (liveN[i]) * h;
        if (i == 0) live.startNewSubPath (x, y); else live.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff5fe08a));
    g.strokePath (live, juce::PathStrokeType (1.5f));

    if (hasRec && recN != nullptr)
    {
        juce::Path ghost;
        for (int i = 0; i < count; ++i)
        {
            const float x = xOf (i), y = bottom - specNorm (recN[i]) * h;
            if (i == 0) ghost.startNewSubPath (x, y); else ghost.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffe0a53a));
        g.strokePath (ghost, juce::PathStrokeType (1.8f));
    }
}

//==============================================================================
void BandBars::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff121212));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    if (count <= 0 || liveN == nullptr)
        return;

    auto in = b.reduced (4.0f);
    const float w = in.getWidth(), h = in.getHeight(), bottom = in.getBottom();
    const float bw = w / (float) count;

    for (int i = 0; i < count; ++i)
    {
        const float x = in.getX() + i * bw;
        const float nrm = specNorm (liveN[i]);
        const float y = bottom - nrm * h;
        const float dB = nrm * 90.0f - 90.0f;

        juce::Colour col = juce::Colour (0xff36c46b);
        if (dB > -3.0f)       col = juce::Colour (0xffe0483a);
        else if (dB > -12.0f) col = juce::Colour (0xffe0a53a);
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (x + 0.5f, y, bw - 1.0f, bottom - y));

        if (hasRec && recN != nullptr)
        {
            const float rn = specNorm (recN[i]);
            if (rn > 0.001f)
            {
                g.setColour (juce::Colour (0xffe0a53a));
                g.fillRect (juce::Rectangle<float> (x + 0.5f, bottom - rn * h - 1.0f, bw - 1.0f, 2.0f));
            }
        }
    }
}

//==============================================================================
DejaVUSpectrumAudioProcessorEditor::DejaVUSpectrumAudioProcessorEditor (DejaVUSpectrumAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    for (int b = 0; b < kBands; ++b) centres[(size_t) b] = proc.getBandCentreHz (b);

    curve.setSources (liveDisp.data(), recDisp.data(), centres.data(), kBands);
    bars.setSources  (liveDisp.data(), recDisp.data(), kBands);
    addAndMakeVisible (curve);
    addAndMakeVisible (bars);

    numericLabel.setJustificationType (juce::Justification::centred);
    numericLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9fd0b0));
    numericLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (numericLabel);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "recordArm", armButton);

    clearButton.onClick = [this]
    {
        const bool has = proc.getNumRecordings() > 0;
        juce::PopupMenu m;
        m.addItem (1, "Delete take: " + proc.getRecordingName (proc.getActiveRecording()), has, false);
        m.addItem (2, "Delete ALL takes", has, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (clearButton),
            [this] (int r)
            {
                if      (r == 1) proc.deleteActiveRecording();
                else if (r == 2) proc.deleteAllRecordings();
                if (r != 0) refreshRecordingList();
            });
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
        if (id > 0)
        {
            proc.selectRecording (id - 1);
            nameField.setText (proc.getRecordingName (id - 1), juce::dontSendNotification);
            refreshRecordingList();
        }
    };
    addAndMakeVisible (takesBox);

    minLenBox.addItem ("Min: Off", 1);
    minLenBox.addItem ("Min 0.5s", 2);
    minLenBox.addItem ("Min 1s",   3);
    minLenBox.addItem ("Min 2s",   4);
    minLenBox.addItem ("Min 5s",   5);
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
    setSize (560, 640);
    startTimerHz (30);
}

DejaVUSpectrumAudioProcessorEditor::~DejaVUSpectrumAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void DejaVUSpectrumAudioProcessorEditor::refreshRecordingList()
{
    takesBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumRecordings(); ++i)
        takesBox.addItem (proc.getRecordingName (i), i + 1);
    const int active = proc.getActiveRecording();
    if (active >= 0)
        takesBox.setSelectedId (active + 1, juce::dontSendNotification);

    const auto info = proc.getRecordingInfo (proc.getActiveRecording());
    if (info.valid)
    {
        auto tc = [] (double s) { const int m = (int) (s / 60.0); return juce::String::formatted ("%d:%05.2f", m, s - m * 60.0); };
        const juce::String s0 = tc (info.startSeconds), s1 = tc (info.endSeconds);
        const double len = juce::jmax (0.0, info.endSeconds - info.startSeconds);
        takeInfoLabel.setText (
            juce::String::formatted ("start %d.%d (%s)     end %d.%d (%s)     len %.1fs",
                                     info.startBar, info.startBeat, s0.toRawUTF8(),
                                     info.endBar, info.endBeat, s1.toRawUTF8(), len),
            juce::dontSendNotification);
    }
    else { takeInfoLabel.setText ({}, juce::dontSendNotification); }
}

//==============================================================================
void DejaVUSpectrumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);

    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU SPECTRUM", top, juce::Justification::centredLeft);

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
        g.drawText ("REC", juce::Rectangle<int> ((int) dot.getRight() + 4, top.getY(), 44, top.getHeight()),
                    juce::Justification::centredLeft);
    }
}

void DejaVUSpectrumAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);

    auto controls = r.removeFromBottom (170);
    r.removeFromBottom (8);
    numericLabel.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);

    auto barsArea = r.removeFromBottom ((int) (r.getHeight() * 0.42f));
    r.removeFromBottom (6);
    curve.setBounds (r);
    bars.setBounds (barsArea);

    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 3;
    autoButton.setBounds  (buttons.removeFromLeft (bw).reduced (3, 0));
    armButton.setBounds   (buttons.removeFromLeft (bw).reduced (3, 0));
    clearButton.setBounds (buttons.reduced (3, 0));

    controls.removeFromTop (8);
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
    controls.removeFromTop (6);
    positionLabel.setBounds (controls.removeFromTop (24));
}

//==============================================================================
void DejaVUSpectrumAudioProcessorEditor::timerCallback()
{
    for (int b = 0; b < kBands; ++b)
    {
        centres[(size_t) b] = proc.getBandCentreHz (b);

        const float lv = proc.liveBands[(size_t) b].load();
        float& ld = liveDisp[(size_t) b];
        ld = juce::jmax (lv, ld * 0.82f);

        const float rv = proc.recBands[(size_t) b].load();
        float& rd = recDisp[(size_t) b];
        rd = juce::jmax (rv, rd * 0.82f);
    }

    const bool hasRec = proc.hasRecording.load();
    curve.update (hasRec);
    bars.update (hasRec);

    // Numeric: loudest live band.
    int maxb = 0;
    for (int b = 1; b < kBands; ++b) if (liveDisp[(size_t) b] > liveDisp[(size_t) maxb]) maxb = b;
    const float f = centres[(size_t) maxb];
    const float dB = juce::Decibels::gainToDecibels (liveDisp[(size_t) maxb], -120.0f);
    numericLabel.setText (
        "PEAK  " + (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String ((int) f) + " Hz")
        + juce::String::formatted ("    %.1f dB", dB),
        juce::dontSendNotification);

    // Auto-mode arm flash.
    const bool autoOn = *proc.apvts.getRawParameterValue ("autoMode") > 0.5f;
    if (autoOn != lastAutoOn)
    {
        if (! autoOn)
        {
            armButton.setColour (juce::TextButton::buttonColourId,
                                 getLookAndFeel().findColour (juce::TextButton::buttonColourId));
            armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
        }
        lastAutoOn = autoOn;
    }

    if (proc.captureFinished.exchange (false))
    {
        const juce::String nm = autoOn
            ? "Auto " + juce::Time::getCurrentTime().formatted ("%H:%M:%S")
            : nameField.getText();
        proc.finalizeCapture (nm);
        refreshRecordingList();
        if (! autoOn)
            if (auto* prm = proc.apvts.getParameter ("recordArm"))
                prm->setValueNotifyingHost (0.0f);
        nameField.setText (proc.getRecordingName (proc.getActiveRecording()), juce::dontSendNotification);
    }

    if (++blinkCounter >= 12)
    {
        blinkCounter = 0;
        blinkOn = ! blinkOn;
        if (proc.recordingNow.load())
            repaint (getLocalBounds().removeFromTop (30));
        if (autoOn)
        {
            const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff4a3a12);
            armButton.setColour (juce::TextButton::buttonColourId, c);
            armButton.setColour (juce::TextButton::buttonOnColourId, c);
        }
    }

    const double secs = proc.playheadSeconds.load();
    const double ppq  = proc.playheadPpq.load();
    const int    num  = proc.timeSigNum.load();
    const int    den  = juce::jmax (1, proc.timeSigDen.load());
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    const int    bar  = (int) std::floor (ppq / qPerBar) + 1;
    const int    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
    const int    mm   = (int) (secs / 60.0);
    positionLabel.setText (juce::String::formatted ("%d . %d      %d:%05.2f", bar, beat, mm, secs - mm * 60.0),
                           juce::dontSendNotification);
    positionLabel.setColour (juce::Label::textColourId,
        proc.transportPlaying.load() ? juce::Colour (0xff36c46b) : juce::Colour (0xff888888));
}
