#include "PluginEditor.h"

//==============================================================================
// Vertical bar meter
//==============================================================================
void VUMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    auto inner = bounds.reduced (3.0f);
    const float h = inner.getHeight();
    const float bottom = inner.getBottom();

    g.setColour (juce::Colour (0x22ffffff));
    for (int dB : { 0, -6, -12, -20, -40 })
    {
        const float n = juce::jlimit (0.0f, 1.0f, ((float) dB - kMinDb) / (kMaxDb - kMinDb));
        g.drawHorizontalLine ((int) (bottom - n * h), inner.getX(), inner.getRight());
    }

    const float lvlN   = toNorm (level01);
    const float amberN = ((-6.0f) - kMinDb) / (kMaxDb - kMinDb);
    const float redN   = ((0.0f)  - kMinDb) / (kMaxDb - kMinDb);
    const float fillAlpha = isGhost ? 0.28f : 0.95f;

    auto fillSeg = [&] (float fromN, float toN, juce::Colour c)
    {
        if (toN <= fromN) return;
        const float y0 = bottom - toN   * h;
        const float y1 = bottom - fromN * h;
        g.setColour (c.withAlpha (fillAlpha));
        g.fillRect (juce::Rectangle<float> (inner.getX(), y0, inner.getWidth(), y1 - y0));
    };

    fillSeg (0.0f, juce::jmin (lvlN, amberN), juce::Colour (0xff36c46b));
    fillSeg (amberN, juce::jmin (lvlN, redN), juce::Colour (0xffe0a53a));
    fillSeg (redN, lvlN, juce::Colour (0xffe0483a));

    if (isGhost && lvlN > 0.001f)
    {
        const float y = bottom - lvlN * h;
        g.setColour (juce::Colour (0xffe0a53a));
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
    }

    const float peakN = toNorm (peak01);
    if (peakN > 0.001f)
    {
        const float y = bottom - peakN * h;
        g.setColour (isGhost ? juce::Colour (0xffe0a53a) : juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (inner.getX(), y - 1.5f, inner.getWidth(), 2.0f));
    }
}

//==============================================================================
// Analog VU face
//==============================================================================
float AnalogVUMeter::angleForVu (float vu)
{
    const float t = (juce::jlimit (kVuMin, kVuMax, vu) - kVuMin) / (kVuMax - kVuMin);
    return juce::degreesToRadians (-50.0f) + t * juce::degreesToRadians (100.0f);
}

void AnalogVUMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Bezel.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2c), b.getX(), b.getY(),
                                             juce::Colour (0xff050505), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 10.0f);

    auto face = b.reduced (12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f0dc), face.getX(), face.getY(),
                                             juce::Colour (0xffe6d6ad), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 5.0f);
    g.setColour (juce::Colour (0x33000000));
    g.drawRoundedRectangle (face, 5.0f, 1.0f);

    const float faceH  = face.getHeight();
    const float cx     = face.getCentreX();
    const float pivotY = face.getBottom() + faceH * 0.12f;
    const float R      = pivotY - (face.getY() + faceH * 0.22f);

    auto pt = [&] (float ang, float r)
    {
        return juce::Point<float> (cx + r * std::sin (ang), pivotY - r * std::cos (ang));
    };

    const float aMin = angleForVu (kVuMin);
    const float aMax = angleForVu (kVuMax);

    // Scale baseline + red zone.
    juce::Path arc;
    arc.addCentredArc (cx, pivotY, R, R, 0.0f, aMin, aMax, true);
    g.setColour (juce::Colour (0xff222018));
    g.strokePath (arc, juce::PathStrokeType (2.0f));

    juce::Path red;
    red.addCentredArc (cx, pivotY, R, R, 0.0f, angleForVu (0.0f), aMax, true);
    g.setColour (juce::Colour (0xffcc2b22));
    g.strokePath (red, juce::PathStrokeType (4.0f));

    // Major ticks + labels.
    struct Mark { float vu; const char* txt; };
    const Mark majors[] = { {-20,"20"},{-10,"10"},{-7,"7"},{-5,"5"},{-3,"3"},{0,"0"},{3,"3"} };
    g.setFont (juce::FontOptions (juce::jmax (9.0f, faceH * 0.075f), juce::Font::bold));
    for (auto& m : majors)
    {
        const float a = angleForVu (m.vu);
        g.setColour (m.vu >= 0.0f ? juce::Colour (0xffcc2b22) : juce::Colour (0xff20201a));
        auto o = pt (a, R);
        auto i = pt (a, R - faceH * 0.06f);
        g.drawLine (o.x, o.y, i.x, i.y, 2.0f);
        auto lp = pt (a, R + faceH * 0.085f);
        g.drawText (m.txt, juce::Rectangle<float> (0, 0, 26, 16).withCentre (lp),
                    juce::Justification::centred);
    }
    for (float vu : { -15.0f, -8.5f, -6.0f, -4.0f, -2.0f, -1.0f, 1.0f, 2.0f })
    {
        const float a = angleForVu (vu);
        auto o = pt (a, R);
        auto i = pt (a, R - faceH * 0.035f);
        g.setColour (vu >= 0.0f ? juce::Colour (0xffcc2b22) : juce::Colour (0x99201a10));
        g.drawLine (o.x, o.y, i.x, i.y, 1.0f);
    }

    // − / + end glyphs.
    g.setFont (juce::FontOptions (faceH * 0.09f, juce::Font::bold));
    g.setColour (juce::Colour (0xff20201a));
    g.drawText ("-", juce::Rectangle<float> (0, 0, 16, 16)
                    .withCentre (pt (aMin, R + faceH * 0.085f).translated (-faceH * 0.09f, 0)),
                juce::Justification::centred);
    g.setColour (juce::Colour (0xffcc2b22));
    g.drawText ("+", juce::Rectangle<float> (0, 0, 16, 16)
                    .withCentre (pt (aMax, R + faceH * 0.085f).translated (faceH * 0.06f, 0)),
                juce::Justification::centred);

    // "VU" wordmark.
    g.setColour (juce::Colour (0xcc20201a));
    g.setFont (juce::FontOptions (faceH * 0.18f, juce::Font::bold));
    g.drawText ("VU", juce::Rectangle<float> (cx - 40, face.getY() + faceH * 0.50f, 80, faceH * 0.22f),
                juce::Justification::centred);

    // PEAK lamps — one for Live, one for Recorded.
    {
        const float rr  = faceH * 0.045f;
        const float lx  = face.getRight() - faceH * 0.15f;
        const float lyL = face.getY() + faceH * 0.26f;
        const float lyR = lyL + faceH * 0.19f;

        g.setColour (juce::Colour (0xff20201a));
        g.setFont (juce::FontOptions (juce::jmax (8.0f, faceH * 0.06f), juce::Font::bold));
        g.drawText ("PEAK", juce::Rectangle<float> (lx - 66, lyL - faceH * 0.15f, 78, 12),
                    juce::Justification::centredRight);

        auto lamp = [&] (float y, bool on, juce::Colour colour, const char* label)
        {
            auto c = juce::Rectangle<float> (0, 0, rr * 2, rr * 2).withCentre ({ lx, y });
            if (on) { g.setColour (colour.withAlpha (0.35f)); g.fillEllipse (c.expanded (rr * 0.8f)); }
            g.setColour (on ? colour : colour.withMultipliedBrightness (0.28f));
            g.fillEllipse (c);
            g.setColour (juce::Colour (0xff20201a));
            g.setFont (juce::FontOptions (juce::jmax (8.0f, faceH * 0.055f), juce::Font::bold));
            g.drawText (label, juce::Rectangle<float> (lx - 66, y - 8, 56, 16),
                        juce::Justification::centredRight);
        };

        const bool clipLive = juce::Decibels::gainToDecibels (livePeak, -120.0f) >= -1.0f;
        const bool clipRec  = juce::Decibels::gainToDecibels (recPeak,  -120.0f) >= -1.0f;
        lamp (lyL, clipLive, juce::Colour (0xffff3b30), "LIVE");
        lamp (lyR, clipRec,  juce::Colour (0xffffb43a), "REC");
    }

    // Needles.
    auto drawNeedle = [&] (float vu, juce::Colour c, float thick, float alpha)
    {
        const float a = angleForVu (vu);
        auto tip  = pt (a, R * 0.98f);
        auto tail = pt (a + juce::MathConstants<float>::pi, R * 0.10f);
        g.setColour (c.withAlpha (alpha));
        g.drawLine (tail.x, tail.y, tip.x, tip.y, thick);
    };
    if (hasRec && rec > 0.0f)
        drawNeedle (vuFromLinear (rec), juce::Colour (0xffe0a53a), 2.5f, 0.85f);
    drawNeedle (vuFromLinear (live), juce::Colour (0xff1a1a1a), 2.5f, 1.0f);

    // Hub.
    auto hub = juce::Rectangle<float> (0, 0, faceH * 0.06f, faceH * 0.06f).withCentre ({ cx, pivotY });
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (hub);

    // Needle legend.
    g.setFont (juce::FontOptions (juce::jmax (8.0f, faceH * 0.06f), juce::Font::bold));
    const float ly = face.getBottom() - faceH * 0.11f;
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (face.getX() + 8, ly, 8, 8);
    g.drawText ("LIVE", juce::Rectangle<float> (face.getX() + 20, ly - 4, 44, 16), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffe0a53a));
    g.fillEllipse (face.getX() + 66, ly, 8, 8);
    g.drawText ("REC", juce::Rectangle<float> (face.getX() + 78, ly - 4, 40, 16), juce::Justification::centredLeft);
}

//==============================================================================
TimelineVUAudioProcessorEditor::TimelineVUAudioProcessorEditor (TimelineVUAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto styleTitle = [] (juce::Label& l)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colour (0xffb8b8b8));
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    };
    styleTitle (recordedLabel);
    styleTitle (liveLabel);

    addAndMakeVisible (recordedMeter);
    addAndMakeVisible (liveMeter);
    addAndMakeVisible (recordedLabel);
    addAndMakeVisible (liveLabel);
    addAndMakeVisible (analogMeter);

    modeBox.addItem ("VU", 1);
    modeBox.addItem ("Peak", 2);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "meterMode", modeBox);

    skinBox.addItem ("Bar", 1);
    skinBox.addItem ("Analog", 2);
    skinBox.onChange = [this] { applySkin(); };
    addAndMakeVisible (skinBox);
    skinAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "meterSkin", skinBox);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    autoButton.setTooltip ("Auto-record every playback pass (keeps the last 5)");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "recordArm", armButton);

    clearButton.onClick = [this]
    {
        proc.deleteActiveRecording();
        refreshRecordingList();
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

    takeInfoLabel.setJustificationType (juce::Justification::centred);
    takeInfoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    takeInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (takeInfoLabel);

    // Minimum-take-length filter (self-labeling items).
    minLenBox.addItem ("Min: Off", 1);
    minLenBox.addItem ("Min 0.5s", 2);
    minLenBox.addItem ("Min 1s",   3);
    minLenBox.addItem ("Min 2s",   4);
    minLenBox.addItem ("Min 5s",   5);
    minLenBox.setTooltip ("Discard recorded takes shorter than this");
    addAndMakeVisible (minLenBox);
    minLenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "minLen", minLenBox);

    // Peak threshold selector (parameter-backed) + peaks list.
    peaksLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    peaksLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (peaksLabel);

    threshBox.addItem ("0 dBFS", 1);
    threshBox.addItem ("-1 dBFS", 2);
    threshBox.addItem ("-3 dBFS", 3);
    threshBox.addItem ("-6 dBFS", 4);
    addAndMakeVisible (threshBox);
    threshAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "peakThresh", threshBox);

    peaksBox.setTextWhenNoChoicesAvailable ("no peaks tagged");
    peaksBox.onChange = [this]
    {
        const int id = peaksBox.getSelectedId();
        if (id > 0)
        {
            const auto pk = proc.getPeak (id - 1);
            const int mm = (int) (pk.seconds / 60.0);
            const double ss = pk.seconds - mm * 60.0;
            targetLabel.setText (
                juce::String::formatted ("GO TO   %d.%d      %d:%05.2f      %+.1f dB",
                                         pk.bar, pk.beat, mm, ss, pk.db),
                juce::dontSendNotification);
        }
    };
    addAndMakeVisible (peaksBox);

    targetLabel.setJustificationType (juce::Justification::centred);
    targetLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0a53a));
    targetLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff23200f));
    targetLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (targetLabel);

    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0b0));
    positionLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (positionLabel);

    refreshRecordingList();
    applySkin();               // sets size + visibility based on the loaded skin
    startTimerHz (30);
}

TimelineVUAudioProcessorEditor::~TimelineVUAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void TimelineVUAudioProcessorEditor::applySkin()
{
    analogSkin = (int) *proc.apvts.getRawParameterValue ("meterSkin") == 1;

    recordedMeter.setVisible (! analogSkin);
    liveMeter    .setVisible (! analogSkin);
    recordedLabel.setVisible (! analogSkin);
    liveLabel    .setVisible (! analogSkin);
    analogMeter  .setVisible (analogSkin);

    setSize (analogSkin ? 480 : 400, analogSkin ? 548 : 588);
    resized();
}

void TimelineVUAudioProcessorEditor::refreshRecordingList()
{
    takesBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumRecordings(); ++i)
        takesBox.addItem (proc.getRecordingName (i), i + 1);

    const int active = proc.getActiveRecording();
    if (active >= 0)
        takesBox.setSelectedId (active + 1, juce::dontSendNotification);

    // Peak markers for the active take.
    peaksBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPeaks(); ++i)
    {
        const auto pk = proc.getPeak (i);
        peaksBox.addItem (juce::String::formatted ("%d.%d    %+.1f dB", pk.bar, pk.beat, pk.db), i + 1);
    }
    targetLabel.setText ({}, juce::dontSendNotification);

    // Start / end / length of the active take.
    const auto info = proc.getRecordingInfo (proc.getActiveRecording());
    if (info.valid)
    {
        auto tc = [] (double s) { const int m = (int) (s / 60.0); return juce::String::formatted ("%d:%05.2f", m, s - m * 60.0); };
        const juce::String s0 = tc (info.startSeconds);
        const juce::String s1 = tc (info.endSeconds);
        const double len = juce::jmax (0.0, info.endSeconds - info.startSeconds);
        takeInfoLabel.setText (
            juce::String::formatted ("start %d.%d (%s)     end %d.%d (%s)     len %.1fs",
                                     info.startBar, info.startBeat, s0.toRawUTF8(),
                                     info.endBar, info.endBeat, s1.toRawUTF8(), len),
            juce::dontSendNotification);
    }
    else
    {
        takeInfoLabel.setText ({}, juce::dontSendNotification);
    }
}

//==============================================================================
void TimelineVUAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));

    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);

    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU", top, juce::Justification::centredLeft);

    // Version, far right.
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v" JucePlugin_VersionString, top, juce::Justification::centredRight);

    // REC indicator, left of the version, blinks while capturing.
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

void TimelineVUAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);                       // title band

    auto controls = r.removeFromBottom (214);
    r.removeFromBottom (8);

    // Meter area.
    if (analogSkin)
    {
        analogMeter.setBounds (r);
    }
    else
    {
        auto labels = r.removeFromBottom (20);
        recordedLabel.setBounds (labels.removeFromLeft (labels.getWidth() / 2).reduced (4, 0));
        liveLabel.setBounds (labels.reduced (4, 0));
        auto rec = r.removeFromLeft (r.getWidth() / 2);
        recordedMeter.setBounds (rec.reduced (14, 4));
        liveMeter.setBounds (r.reduced (14, 4));
    }

    // Controls stack.
    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 5;
    modeBox.setBounds     (buttons.removeFromLeft (bw).reduced (3, 0));
    skinBox.setBounds     (buttons.removeFromLeft (bw).reduced (3, 0));
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
    auto peaksRow = controls.removeFromTop (26);
    peaksLabel.setBounds (peaksRow.removeFromLeft (46));
    threshBox.setBounds (peaksRow.removeFromRight (84));
    peaksRow.removeFromRight (6);
    peaksBox.setBounds (peaksRow);

    controls.removeFromTop (8);
    targetLabel.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (4);
    positionLabel.setBounds (controls.removeFromTop (24));
}

//==============================================================================
void TimelineVUAudioProcessorEditor::timerCallback()
{
    proc.drainPeaks();   // move any tagged peaks from the audio thread

    // Meters.
    const float live = proc.liveLevel.load();
    liveDisplay = juce::jmax (live, liveDisplay * 0.80f);
    livePeak    = (live >= livePeak) ? live : livePeak * 0.97f;

    const float rec = proc.recordedLevel.load();
    recDisplay = juce::jmax (rec, recDisplay * 0.80f);
    recPeak    = (rec >= recPeak) ? rec : recPeak * 0.97f;

    liveMeter.setValues (liveDisplay, livePeak);
    recordedMeter.setValues (recDisplay, recPeak);
    analogMeter.setValues (liveDisplay, recDisplay, livePeak, recPeak, proc.hasRecording.load());

    const bool autoOn = *proc.apvts.getRawParameterValue ("autoMode") > 0.5f;
    armButton.setEnabled (! autoOn);

    // A take just finished: store it, refresh the list, and (manual) auto-disarm.
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
        nameField.setText (proc.getRecordingName (proc.getActiveRecording()),
                           juce::dontSendNotification);
    }

    // Blink the REC lamp ~2.5 Hz.
    if (++blinkCounter >= 12)
    {
        blinkCounter = 0;
        blinkOn = ! blinkOn;
        if (proc.recordingNow.load())
            repaint (getLocalBounds().removeFromTop (30));
    }

    // Playhead position readout.
    const double secs = proc.playheadSeconds.load();
    const double ppq  = proc.playheadPpq.load();
    const int    num  = proc.timeSigNum.load();
    const int    den  = juce::jmax (1, proc.timeSigDen.load());
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    const int    bar  = (int) std::floor (ppq / qPerBar) + 1;
    const double inBar = ppq - (bar - 1) * qPerBar;
    const int    beat = (int) std::floor (inBar / (4.0 / den)) + 1;
    const int    mm   = (int) (secs / 60.0);
    const double ss   = secs - mm * 60.0;

    positionLabel.setText (
        juce::String::formatted ("%d . %d      %d:%05.2f", bar, beat, mm, ss),
        juce::dontSendNotification);
    positionLabel.setColour (juce::Label::textColourId,
        proc.transportPlaying.load() ? juce::Colour (0xff36c46b) : juce::Colour (0xff888888));
}
