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
    if (delta_ && hasRec && liveN != nullptr && recN != nullptr && count > 1)
        paintDelta (g, in);
    else
        paintNormal (g, in);
}

void SpectrumCurve::paintNormal (juce::Graphics& g, juce::Rectangle<float> in)
{
    const float w = in.getWidth(), h = in.getHeight(), left = in.getX(), bottom = in.getBottom();

    g.setFont (juce::FontOptions (9.0f));
    for (int dB : { 0, -3, -6, -12, -24, -48, -72 })
    {
        if ((float) dB < minDb_) continue;
        const float y = bottom - (((float) dB - minDb_) / (0.0f - minDb_)) * h;
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
    for (int i = 0; i < count; ++i) fill.lineTo (xOf (i), bottom - specNorm (liveN[i], minDb_) * h);
    fill.lineTo (in.getRight(), bottom);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x8836c46b), left, in.getY(),
                                             juce::Colour (0x1136c46b), left, bottom, false));
    g.fillPath (fill);

    juce::Path live;
    for (int i = 0; i < count; ++i)
    {
        const float x = xOf (i), y = bottom - specNorm (liveN[i], minDb_) * h;
        if (i == 0) live.startNewSubPath (x, y); else live.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff5fe08a));
    g.strokePath (live, juce::PathStrokeType (1.5f));

    if (hasRec && recN != nullptr)
    {
        juce::Path ghost;
        for (int i = 0; i < count; ++i)
        {
            const float x = xOf (i), y = bottom - specNorm (recN[i], minDb_) * h;
            if (i == 0) ghost.startNewSubPath (x, y); else ghost.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffe0a53a));
        g.strokePath (ghost, juce::PathStrokeType (1.8f));
    }
}

void SpectrumCurve::paintDelta (juce::Graphics& g, juce::Rectangle<float> in)
{
    const float w = in.getWidth(), left = in.getX(), right = in.getRight();
    const float centre = in.getCentreY(), half = in.getHeight() * 0.5f;
    const float range = juce::jmax (1.0f, deltaRange_);

    auto xOf = [&] (int i) { return left + (float) i / (count - 1) * w; };
    auto yOf = [&] (float dDb) { return centre - juce::jlimit (-range, range, dDb) / range * half; };
    auto deltaAt = [&] (int i)
    {
        const float lv = juce::jmax (juce::Decibels::gainToDecibels (liveN[i], -120.0f), -90.0f);
        const float rv = juce::jmax (juce::Decibels::gainToDecibels (recN[i],  -120.0f), -90.0f);
        return lv - rv;
    };

    // dB grid (relative), centre line emphasised.
    g.setFont (juce::FontOptions (9.0f));
    for (float v : { range, range * 0.5f, 0.0f, -range * 0.5f, -range })
    {
        const float y = yOf (v);
        g.setColour (v == 0.0f ? juce::Colour (0x55ffffff) : juce::Colour (0x18ffffff));
        g.drawHorizontalLine ((int) y, left, right);
        g.setColour (juce::Colour (0xff666666));
        g.drawText (juce::String (v > 0.0f ? "+" : "") + juce::String (v, 0),
                    juce::Rectangle<float> (left + 2, y - 10, 34, 12), juce::Justification::left);
    }

    // Filled area between the delta curve and the centre: green above, red below.
    juce::Path poly;
    poly.startNewSubPath (left, centre);
    for (int i = 0; i < count; ++i) poly.lineTo (xOf (i), yOf (deltaAt (i)));
    poly.lineTo (right, centre);
    poly.closeSubPath();

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) left, (int) in.getY(), (int) w, (int) (centre - in.getY())));
    g.setColour (juce::Colour (0x8836c46b));
    g.fillPath (poly);
    g.restoreState();

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) left, (int) centre, (int) w, (int) (in.getBottom() - centre)));
    g.setColour (juce::Colour (0x88e0483a));
    g.fillPath (poly);
    g.restoreState();

    // Delta curve line.
    juce::Path line;
    for (int i = 0; i < count; ++i)
    {
        const float x = xOf (i), y = yOf (deltaAt (i));
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xffe8e8e8));
    g.strokePath (line, juce::PathStrokeType (1.5f));

    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("LIVE - REC  (dB)", in.reduced (4.0f).removeFromTop (14.0f), juce::Justification::centredRight);
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

    // Delta mode: bars grow up/down from a centre line by (live - rec) dB.
    if (delta_ && hasRec && recN != nullptr)
    {
        const float centre = in.getCentreY(), half = h * 0.5f;
        const float range = juce::jmax (1.0f, deltaRange_);
        g.setColour (juce::Colour (0x55ffffff));
        g.drawHorizontalLine ((int) centre, in.getX(), in.getRight());
        for (int i = 0; i < count; ++i)
        {
            const float x = in.getX() + i * bw;
            const float lv = juce::jmax (juce::Decibels::gainToDecibels (liveN[i], -120.0f), -90.0f);
            const float rv = juce::jmax (juce::Decibels::gainToDecibels (recN[i],  -120.0f), -90.0f);
            const float d  = juce::jlimit (-range, range, lv - rv);
            const float y  = centre - d / range * half;
            g.setColour (d >= 0.0f ? juce::Colour (0xff36c46b) : juce::Colour (0xffe0483a));
            g.fillRect (juce::Rectangle<float> (x + 0.5f, juce::jmin (centre, y), bw - 1.0f, std::abs (y - centre)));
        }
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const float x = in.getX() + i * bw;
        const float nrm = specNorm (liveN[i], minDb_);
        const float y = bottom - nrm * h;
        const float dB = juce::Decibels::gainToDecibels (liveN[i], -120.0f);

        juce::Colour col = juce::Colour (0xff36c46b);
        if (dB > -3.0f)       col = juce::Colour (0xffe0483a);
        else if (dB > -12.0f) col = juce::Colour (0xffe0a53a);
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (x + 0.5f, y, bw - 1.0f, bottom - y));

        if (hasRec && recN != nullptr)
        {
            const float rn = specNorm (recN[i], minDb_);
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
    autoButton.setTooltip ("Auto-record: automatically records every time the transport plays. "
                           "Keeps the last 5 takes; no need to arm.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records the incoming signal while the transport plays, "
                          "then disarms itself when it stops.");
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "recordArm", armButton);

    deltaButton.setClickingTogglesState (true);
    deltaButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0a53a));
    deltaButton.setTooltip ("Delta view: show only the difference (live - recorded) per band, "
                            "centred at 0. Needs a recorded take playing back.");
    addAndMakeVisible (deltaButton);
    deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "showDelta", deltaButton);

    clearButton.setTooltip ("Delete takes - opens a menu (delete this take or all takes).");
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

    peaksLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    peaksLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (peaksLabel);

    threshBox.addItem ("0 dBFS", 1);
    threshBox.addItem ("-1 dBFS", 2);
    threshBox.addItem ("-3 dBFS", 3);
    threshBox.addItem ("-6 dBFS", 4);
    addAndMakeVisible (threshBox);
    threshAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "peakThresh", threshBox);

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
                              "- no need to arm or auto-record. Keeps the last 25.");
    monitorButton.onClick = [this] { refreshPeaksList(); };
    addAndMakeVisible (monitorButton);
    monitorAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "peaksMonitor", monitorButton);

    zoomLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    zoomLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    zoomLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (zoomLabel);
    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
    zoomSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff36a0c4));
    zoomSlider.setTooltip ("Vertical zoom of the dB axis to magnify live-vs-recorded differences");
    addAndMakeVisible (zoomSlider);
    zoomAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "vZoom", zoomSlider);

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
    setSize (560, 730);
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

void DejaVUSpectrumAudioProcessorEditor::refreshPeaksList()
{
    const bool mon = *proc.apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    peaksBox.setTextWhenNoChoicesAvailable (mon ? "monitoring..." : "no peaks tagged");
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

    auto controls = r.removeFromBottom (250);
    r.removeFromBottom (8);
    numericLabel.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);

    auto barsArea = r.removeFromBottom ((int) (r.getHeight() * 0.42f));
    r.removeFromBottom (6);
    curve.setBounds (r);
    bars.setBounds (barsArea);

    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 4;
    autoButton.setBounds  (buttons.removeFromLeft (bw).reduced (3, 0));
    armButton.setBounds   (buttons.removeFromLeft (bw).reduced (3, 0));
    deltaButton.setBounds (buttons.removeFromLeft (bw).reduced (3, 0));
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
    auto mzRow = controls.removeFromTop (26);
    monitorButton.setBounds (mzRow.removeFromLeft (150));
    mzRow.removeFromLeft (10);
    zoomLabel.setBounds (mzRow.removeFromLeft (40));
    zoomSlider.setBounds (mzRow);

    controls.removeFromTop (8);
    targetLabel.setBounds (controls.removeFromTop (24));
    controls.removeFromTop (6);
    positionLabel.setBounds (controls.removeFromTop (24));
}

//==============================================================================
void DejaVUSpectrumAudioProcessorEditor::timerCallback()
{
    proc.drainPeaks();

    const bool monOn = *proc.apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    if (monOn != lastPeaksMonitorOn || (monOn && proc.getMonitorRev() != lastMonitorRev))
        refreshPeaksList();

    for (int b = 0; b < kBands; ++b)
    {
        centres[(size_t) b] = proc.getBandCentreHz (b);
        float& ld = liveDisp[(size_t) b]; ld = juce::jmax (proc.liveBands[(size_t) b].load(), ld * 0.82f);
        float& rd = recDisp[(size_t) b];  rd = juce::jmax (proc.recBands[(size_t) b].load(),  rd * 0.82f);
    }

    const float zoom = *proc.apvts.getRawParameterValue ("vZoom");
    const float minDb = -90.0f / juce::jmax (1.0f, zoom);
    const bool  hasRec = proc.hasRecording.load();
    const bool  deltaMode = *proc.apvts.getRawParameterValue ("showDelta") > 0.5f && hasRec;
    const float deltaRange = 24.0f / juce::jmax (1.0f, zoom);
    curve.update (hasRec, minDb, deltaMode, deltaRange);
    bars.update (hasRec, minDb, deltaMode, deltaRange);

    auto floorDb = [] (float lin) { return juce::jmax (juce::Decibels::gainToDecibels (lin, -120.0f), -90.0f); };
    if (deltaMode)
    {
        int mb = 0; float best = -1.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float d = std::abs (floorDb (liveDisp[(size_t) b]) - floorDb (recDisp[(size_t) b]));
            if (d > best) { best = d; mb = b; }
        }
        const float f = centres[(size_t) mb];
        numericLabel.setText (
            "MAX DELTA  " + (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String ((int) f) + " Hz")
            + juce::String::formatted ("    %+.1f dB", floorDb (liveDisp[(size_t) mb]) - floorDb (recDisp[(size_t) mb])),
            juce::dontSendNotification);
    }
    else
    {
        int maxb = 0;
        for (int b = 1; b < kBands; ++b) if (liveDisp[(size_t) b] > liveDisp[(size_t) maxb]) maxb = b;
        const float f = centres[(size_t) maxb];
        numericLabel.setText (
            "PEAK  " + (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String ((int) f) + " Hz")
            + juce::String::formatted ("    %.1f dB", juce::Decibels::gainToDecibels (liveDisp[(size_t) maxb], -120.0f)),
            juce::dontSendNotification);
    }

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
