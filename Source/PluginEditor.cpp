#include "PluginEditor.h"

//==============================================================================
// Stereo bar meter
//==============================================================================
void VUMeter::drawBar (juce::Graphics& g, juce::Rectangle<float> area,
                       float level, float peak, const char* label)
{
    auto labelArea = area.removeFromBottom (12.0f);
    const float h = area.getHeight();
    const float bottom = area.getBottom();

    g.setColour (juce::Colour (0x22ffffff));
    for (int dB : { 0, -6, -12, -20, -40 })
    {
        const float n = juce::jlimit (0.0f, 1.0f, ((float) dB - kMinDb) / (kMaxDb - kMinDb));
        g.drawHorizontalLine ((int) (bottom - n * h), area.getX(), area.getRight());
    }

    const float lvlN   = toNorm (level);
    const float amberN = ((-6.0f) - kMinDb) / (kMaxDb - kMinDb);
    const float redN   = ((0.0f)  - kMinDb) / (kMaxDb - kMinDb);
    const float a = isGhost ? 0.28f : 0.95f;

    auto seg = [&] (float fromN, float toN, juce::Colour c)
    {
        if (toN <= fromN) return;
        const float y0 = bottom - toN * h, y1 = bottom - fromN * h;
        g.setColour (c.withAlpha (a));
        g.fillRect (juce::Rectangle<float> (area.getX(), y0, area.getWidth(), y1 - y0));
    };
    seg (0.0f, juce::jmin (lvlN, amberN), juce::Colour (0xff36c46b));
    seg (amberN, juce::jmin (lvlN, redN), juce::Colour (0xffe0a53a));
    seg (redN, lvlN, juce::Colour (0xffe0483a));

    if (isGhost && lvlN > 0.001f)
    {
        g.setColour (juce::Colour (0xffe0a53a));
        g.drawHorizontalLine ((int) (bottom - lvlN * h), area.getX(), area.getRight());
    }

    const float peakN = toNorm (peak);
    if (peakN > 0.001f)
    {
        g.setColour (isGhost ? juce::Colour (0xffe0a53a) : juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (area.getX(), bottom - peakN * h - 1.5f, area.getWidth(), 2.0f));
    }

    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (label, labelArea, juce::Justification::centred);
}

void VUMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto inner = b.reduced (4.0f);
    auto left = inner.removeFromLeft (inner.getWidth() * 0.5f);
    drawBar (g, left.reduced (4.0f, 2.0f),  levL, pkL, "L");
    drawBar (g, inner.reduced (4.0f, 2.0f), levR, pkR, "R");
}

//==============================================================================
// Analog VU face with needle physics
//==============================================================================
float AnalogVUMeter::angleForVu (float vu)
{
    const float t = (juce::jlimit (kVuMin, kVuMax, vu) - kVuMin) / (kVuMax - kVuMin);
    return juce::degreesToRadians (-50.0f) + t * juce::degreesToRadians (100.0f);
}

void AnalogVUMeter::stepNeedle (float& pos, float& vel, float target, bool isVu)
{
    // Spring-damper: VU mode swings with a slight overshoot (~300 ms); Peak
    // mode is fast and critically damped. Sub-stepped for stability at 30 Hz.
    const float wn   = isVu ? 18.0f : 46.0f;
    const float zeta = isVu ? 0.80f : 1.0f;
    const float k = wn * wn, c = 2.0f * zeta * wn;
    const float dt = (1.0f / 30.0f) / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const float acc = k * (target - pos) - c * vel;
        vel += acc * dt;
        pos += vel * dt;
    }
}

void AnalogVUMeter::setValues (float liveLin, float recLin, float livePeakLin, float recPeakLin,
                               bool recActive, bool isVu)
{
    const float tLive = juce::jlimit (kVuMin - 3.0f, kVuMax + 2.0f, vuFromLinear (liveLin));
    const float tRec  = recActive ? juce::jlimit (kVuMin - 3.0f, kVuMax + 2.0f, vuFromLinear (recLin))
                                  : kVuMin;
    stepNeedle (posLive, velLive, tLive, isVu);
    stepNeedle (posRec,  velRec,  tRec,  isVu);
    livePeak = livePeakLin; recPeak = recPeakLin; hasRec = recActive;
    repaint();
}

void AnalogVUMeter::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // Peak indicator lamps ABOVE the dial (LIVE + RECORDED for this channel).
    {
        auto s = full.removeFromTop (24.0f).reduced (4.0f, 3.0f);
        const float y = s.getCentreY();
        const bool liveClip = juce::Decibels::gainToDecibels (livePeak, -120.0f) >= -1.0f;
        const bool recClip  = juce::Decibels::gainToDecibels (recPeak,  -120.0f) >= -1.0f;

        float x = s.getCentreX() - 70.0f;
        g.setColour (juce::Colour (0xff8a8a8a));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("PEAK", juce::Rectangle<float> (x, s.getY(), 34, s.getHeight()), juce::Justification::centredLeft);
        x += 36.0f;

        auto lamp = [&] (bool on, juce::Colour col, const char* label)
        {
            auto c = juce::Rectangle<float> (x, y - 4.0f, 8.0f, 8.0f);
            if (on) { g.setColour (col.withAlpha (0.40f)); g.fillEllipse (c.expanded (3.0f)); }
            g.setColour (on ? col : col.withMultipliedBrightness (0.30f));
            g.fillEllipse (c);
            x += 12.0f;
            g.setColour (juce::Colour (0xffb0b0b0));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (label, juce::Rectangle<float> (x, s.getY(), 40, s.getHeight()), juce::Justification::centredLeft);
            x += 40.0f;
        };
        lamp (liveClip, juce::Colour (0xffff3b30), "LIVE");
        lamp (recClip,  juce::Colour (0xffffb43a), "REC");
    }

    auto b = full;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2c), b.getX(), b.getY(),
                                             juce::Colour (0xff050505), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 9.0f);

    auto face = b.reduced (10.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f0dc), face.getX(), face.getY(),
                                             juce::Colour (0xffe6d6ad), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 4.0f);
    g.setColour (juce::Colour (0x33000000));
    g.drawRoundedRectangle (face, 4.0f, 1.0f);

    const float faceH  = face.getHeight();
    const float cx     = face.getCentreX();
    const float pivotY = face.getBottom() + faceH * 0.12f;
    const float R      = pivotY - (face.getY() + faceH * 0.22f);

    auto pt = [&] (float ang, float r)
    { return juce::Point<float> (cx + r * std::sin (ang), pivotY - r * std::cos (ang)); };

    const float aMin = angleForVu (kVuMin);
    const float aMax = angleForVu (kVuMax);

    juce::Path arc;
    arc.addCentredArc (cx, pivotY, R, R, 0.0f, aMin, aMax, true);
    g.setColour (juce::Colour (0xff222018));
    g.strokePath (arc, juce::PathStrokeType (2.0f));

    juce::Path red;
    red.addCentredArc (cx, pivotY, R, R, 0.0f, angleForVu (0.0f), aMax, true);
    g.setColour (juce::Colour (0xffcc2b22));
    g.strokePath (red, juce::PathStrokeType (4.0f));

    struct Mark { float vu; const char* txt; };
    const Mark majors[] = { {-20,"20"},{-10,"10"},{-7,"7"},{-5,"5"},{-3,"3"},{0,"0"},{3,"3"} };
    g.setFont (juce::FontOptions (juce::jmax (8.5f, faceH * 0.072f), juce::Font::bold));
    for (auto& m : majors)
    {
        const float ang = angleForVu (m.vu);
        g.setColour (m.vu >= 0.0f ? juce::Colour (0xffcc2b22) : juce::Colour (0xff20201a));
        auto o = pt (ang, R), i = pt (ang, R - faceH * 0.06f);
        g.drawLine (o.x, o.y, i.x, i.y, 2.0f);
        auto lp = pt (ang, R + faceH * 0.085f);
        g.drawText (m.txt, juce::Rectangle<float> (0, 0, 24, 15).withCentre (lp), juce::Justification::centred);
    }
    for (float vu : { -15.0f, -8.5f, -6.0f, -4.0f, -2.0f, -1.0f, 1.0f, 2.0f })
    {
        const float ang = angleForVu (vu);
        auto o = pt (ang, R), i = pt (ang, R - faceH * 0.035f);
        g.setColour (vu >= 0.0f ? juce::Colour (0xffcc2b22) : juce::Colour (0x99201a10));
        g.drawLine (o.x, o.y, i.x, i.y, 1.0f);
    }

    // VU wordmark + channel letter.
    g.setColour (juce::Colour (0xcc20201a));
    g.setFont (juce::FontOptions (faceH * 0.17f, juce::Font::bold));
    g.drawText ("VU", juce::Rectangle<float> (cx - 40, face.getY() + faceH * 0.50f, 80, faceH * 0.22f),
                juce::Justification::centred);
    g.setColour (juce::Colour (0x9920201a));
    g.setFont (juce::FontOptions (faceH * 0.11f, juce::Font::bold));
    g.drawText (channel, juce::Rectangle<float> (face.getX() + 6, face.getY() + 4, 22, 18),
                juce::Justification::centredLeft);

    // Needles (positions are already in VU units from the physics step).
    auto drawNeedle = [&] (float vu, juce::Colour c, float thick, float alpha)
    {
        const float ang = angleForVu (vu);
        auto tip  = pt (ang, R * 0.98f);
        auto tail = pt (ang + juce::MathConstants<float>::pi, R * 0.10f);
        g.setColour (c.withAlpha (alpha));
        g.drawLine (tail.x, tail.y, tip.x, tip.y, thick);
    };
    if (hasRec) drawNeedle (posRec, juce::Colour (0xffe0a53a), 2.2f, 0.85f);
    drawNeedle (posLive, juce::Colour (0xff1a1a1a), 2.2f, 1.0f);

    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (juce::Rectangle<float> (0, 0, faceH * 0.055f, faceH * 0.055f).withCentre ({ cx, pivotY }));
}

//==============================================================================
TimelineVUAudioProcessorEditor::TimelineVUAudioProcessorEditor (TimelineVUAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    auto styleGroup = [] (juce::Label& l)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colour (0xffb8b8b8));
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    };
    styleGroup (recordedLabel);
    styleGroup (liveLabel);

    addAndMakeVisible (recordedMeter);
    addAndMakeVisible (liveMeter);
    addAndMakeVisible (recordedLabel);
    addAndMakeVisible (liveLabel);
    addAndMakeVisible (dialL);
    addAndMakeVisible (dialR);

    numericLabel.setJustificationType (juce::Justification::centred);
    numericLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9fd0b0));
    numericLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (numericLabel);

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
    autoButton.setTooltip ("Auto-record: automatically records every time the transport plays. "
                           "Keeps the last 5 takes; no need to arm.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records the incoming signal while the transport plays, "
                          "then disarms itself when it stops.");
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "recordArm", armButton);

    clearButton.setButtonText ("DEL  \xE2\x96\xBE");
    clearButton.setTooltip ("Delete takes — opens a menu (delete this take or all takes).");
    clearButton.onClick = [this]
    {
        const bool has = proc.getNumRecordings() > 0;
        const juce::String sel = proc.getRecordingName (proc.getActiveRecording());

        juce::PopupMenu m;
        m.addItem (1, has && sel.isNotEmpty() ? "Delete take: " + sel : "Delete selected take", has, false);
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

    takeInfoLabel.setJustificationType (juce::Justification::centred);
    takeInfoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    takeInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (takeInfoLabel);

    minLenBox.addItem ("Min: Off", 1);
    minLenBox.addItem ("Min 0.5s", 2);
    minLenBox.addItem ("Min 1s",   3);
    minLenBox.addItem ("Min 2s",   4);
    minLenBox.addItem ("Min 5s",   5);
    minLenBox.setTooltip ("Discard recorded takes shorter than this");
    addAndMakeVisible (minLenBox);
    minLenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, "minLen", minLenBox);

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
            const bool mon = *proc.apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
            const auto pk = mon ? proc.getMonitorPeak (id - 1) : proc.getPeak (id - 1);
            const int mm = (int) (pk.seconds / 60.0);
            targetLabel.setText (
                juce::String::formatted ("GO TO   %d.%d      %d:%05.2f      %+.1f dB",
                                         pk.bar, pk.beat, mm, pk.seconds - mm * 60.0, pk.db),
                juce::dontSendNotification);
        }
    };
    addAndMakeVisible (peaksBox);

    monitorButton.setClickingTogglesState (true);
    monitorButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    monitorButton.setTooltip ("Peaks Monitor: continuously tags level peaks while the transport plays "
                              "\xe2\x80\x94 no need to arm or auto-record. Keeps the last 25.");
    monitorButton.onClick = [this] { refreshPeaksList(); };
    addAndMakeVisible (monitorButton);
    monitorAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "peaksMonitor", monitorButton);

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
    applySkin();
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
    dialL.setVisible (analogSkin);
    dialR.setVisible (analogSkin);

    setSize (analogSkin ? 520 : 440, analogSkin ? 632 : 672);
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

    refreshPeaksList();

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

void TimelineVUAudioProcessorEditor::refreshPeaksList()
{
    const bool mon = *proc.apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    peaksBox.setTextWhenNoChoicesAvailable (mon ? "monitoring\xe2\x80\xa6" : "no peaks tagged");
    peaksBox.clear (juce::dontSendNotification);
    const int n = mon ? proc.getNumMonitorPeaks() : proc.getNumPeaks();
    for (int i = 0; i < n; ++i)
    {
        const auto pk = mon ? proc.getMonitorPeak (i) : proc.getPeak (i);
        peaksBox.addItem (juce::String::formatted ("%d.%d    %+.1f dB", pk.bar, pk.beat, pk.db), i + 1);
    }
    targetLabel.setText ({}, juce::dontSendNotification);
    lastPeaksMonitorOn = mon;
    lastMonitorRev = proc.getMonitorRev();
}

//==============================================================================
void TimelineVUAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (30).reduced (14, 4);

    g.setColour (juce::Colour (0xffe8e8e8));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU", top, juce::Justification::centredLeft);

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

void TimelineVUAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (30);

    auto controls = r.removeFromBottom (246);
    r.removeFromBottom (8);
    numericLabel.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);

    if (analogSkin)
    {
        auto lArea = r.removeFromLeft (r.getWidth() / 2);
        dialL.setBounds (lArea.reduced (6, 2));
        dialR.setBounds (r.reduced (6, 2));
    }
    else
    {
        auto labels = r.removeFromBottom (20);
        recordedLabel.setBounds (labels.removeFromLeft (labels.getWidth() / 2).reduced (4, 0));
        liveLabel.setBounds (labels.reduced (4, 0));
        auto rec = r.removeFromLeft (r.getWidth() / 2);
        recordedMeter.setBounds (rec.reduced (8, 4));
        liveMeter.setBounds (r.reduced (8, 4));
    }

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

    controls.removeFromTop (6);
    auto monRow = controls.removeFromTop (26);
    monRow.removeFromLeft (46);
    monitorButton.setBounds (monRow.removeFromLeft (170));

    controls.removeFromTop (8);
    targetLabel.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (4);
    positionLabel.setBounds (controls.removeFromTop (24));
}

//==============================================================================
void TimelineVUAudioProcessorEditor::timerCallback()
{
    proc.drainPeaks();

    // Peaks Monitor: keep the list in sync while it updates live.
    const bool monOn = *proc.apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    if (monOn != lastPeaksMonitorOn || (monOn && proc.getMonitorRev() != lastMonitorRev))
        refreshPeaksList();

    auto upd = [] (float v, float& disp, float& pk)
    {
        disp = juce::jmax (v, disp * 0.80f);
        pk   = (v >= pk) ? v : pk * 0.97f;
    };
    upd (proc.liveL.load(),     liveDispL, livePkL);
    upd (proc.liveR.load(),     liveDispR, livePkR);
    upd (proc.recordedL.load(), recDispL,  recPkL);
    upd (proc.recordedR.load(), recDispR,  recPkR);

    liveMeter.setValues     (liveDispL, livePkL, liveDispR, livePkR);
    recordedMeter.setValues (recDispL,  recPkL,  recDispR,  recPkR);

    const bool isVu = (int) *proc.apvts.getRawParameterValue ("meterMode") == 0;
    const bool recActive = proc.hasRecording.load();
    dialL.setValues (proc.liveL.load(), proc.recordedL.load(), proc.livePeakL.load(), recPkL, recActive, isVu);
    dialR.setValues (proc.liveR.load(), proc.recordedR.load(), proc.livePeakR.load(), recPkR, recActive, isVu);

    auto dbStr = [] (float lin)
    {
        return lin <= 0.00002f ? juce::String ("-\xe2\x88\x9e")
                               : juce::String (juce::Decibels::gainToDecibels (lin, -120.0f), 1);
    };
    numericLabel.setText (
        "LIVE  L " + dbStr (liveDispL) + "  R " + dbStr (liveDispR)
        + " dB      REC  L " + dbStr (recDispL) + "  R " + dbStr (recDispR) + " dB",
        juce::dontSendNotification);

    const bool autoOn = *proc.apvts.getRawParameterValue ("autoMode") > 0.5f;
    if (autoOn != lastAutoOn)
    {
        armButton.setEnabled (true);
        if (! autoOn)   // leaving auto: restore normal button colours
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

        if (autoOn)   // flash the ARM button amber while auto-record is active
        {
            const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff4a3a12);
            armButton.setColour (juce::TextButton::buttonColourId, c);
            armButton.setColour (juce::TextButton::buttonOnColourId, c);
        }

        // Flash the TAKE selector amber while the selected take is playing back.
        if (proc.transportPlaying.load() && proc.hasRecording.load())
        {
            const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff5c4410);
            takesBox.setColour (juce::ComboBox::textColourId, c);
            takesBox.setColour (juce::ComboBox::outlineColourId, c);
        }
        else
        {
            takesBox.setColour (juce::ComboBox::textColourId,
                                getLookAndFeel().findColour (juce::ComboBox::textColourId));
            takesBox.setColour (juce::ComboBox::outlineColourId,
                                getLookAndFeel().findColour (juce::ComboBox::outlineColourId));
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
