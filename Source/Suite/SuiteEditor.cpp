#include "SuiteEditor.h"
#include <cmath>
#include <vector>

//==============================================================================
static juce::Rectangle<float> panelFrame (juce::Graphics& g, juce::Rectangle<int> bounds, const char* title)
{
    auto b = bounds.toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff121212)); g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (0xff2a2a2a)); g.drawRoundedRectangle (b, 4.0f, 1.0f);
    auto inner = b.reduced (6.0f);
    auto titleArea = inner.removeFromTop (14.0f);
    g.setColour (juce::Colour (0xff8a8a8a)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (title, titleArea, juce::Justification::centredLeft);
    inner.removeFromTop (2.0f);
    return inner;
}

//==============================================================================
void PanelMeter::paint (juce::Graphics& g)
{
    auto in = panelFrame (g, getLocalBounds(), "METER");
    const float bw = in.getWidth() * 0.5f;
    const float amberN = 54.0f / 66.0f, redN = 60.0f / 66.0f;

    auto drawV = [&] (juce::Rectangle<float> a, float lvl, float pk, float rec)
    {
        a = a.reduced (8.0f, 2.0f);
        const float h = a.getHeight(), bottom = a.getBottom();
        g.setColour (juce::Colour (0xff0d0d0d)); g.fillRect (a);
        auto nrm = [] (float lin) { return dbNorm (lin, -60.0f, 6.0f); };
        const float lvlN = nrm (lvl);
        auto seg = [&] (float f, float t, juce::Colour c) { if (t <= f) return; g.setColour (c); g.fillRect (juce::Rectangle<float> (a.getX(), bottom - t * h, a.getWidth(), (t - f) * h)); };
        seg (0.0f, juce::jmin (lvlN, amberN), juce::Colour (0xff36c46b));
        seg (amberN, juce::jmin (lvlN, redN), juce::Colour (0xffe0a53a));
        seg (redN, lvlN, juce::Colour (0xffe0483a));
        const float pkN = nrm (pk);
        if (pkN > 0.001f) { g.setColour (juce::Colours::white); g.fillRect (juce::Rectangle<float> (a.getX(), bottom - pkN * h - 1.5f, a.getWidth(), 2.0f)); }
        if (hasRec_ && rec > 0.0005f) { const float y = bottom - nrm (rec) * h; g.setColour (juce::Colour (0xffe0a53a)); g.fillRect (juce::Rectangle<float> (a.getX(), y - 1.25f, a.getWidth(), 2.5f)); }
    };
    drawV (in.removeFromLeft (bw), lL_, pL_, rL_);
    drawV (in, lR_, pR_, rR_);
}

//==============================================================================
void PanelLUFS::paint (juce::Graphics& g)
{
    auto in = panelFrame (g, getLocalBounds(), "LUFS");
    auto ls = [] (float v) { return v > -70.0f ? juce::String (v, 1) : juce::String ("-"); };

    auto top = in.removeFromTop (24.0f);
    g.setColour (juce::Colour (0xffe8e8e8)); g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("S " + ls (st_), top.removeFromLeft (top.getWidth() * 0.62f), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff9fd0b0)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("LUFS", top, juce::Justification::centredLeft);

    auto line = in.removeFromTop (15.0f);
    g.setColour (juce::Colour (0xffb0b0b0)); g.setFont (juce::FontOptions (10.5f));
    g.drawText ("M " + ls (mom_) + "    I " + ls (integ_) + "    TP " + juce::String (tp_, 1), line, juce::Justification::centredLeft);

    in.removeFromTop (4.0f);
    auto bar = in.removeFromTop (16.0f);
    auto n = [] (float l) { return juce::jlimit (0.0f, 1.0f, (l + 40.0f) / 40.0f); };
    const float w = bar.getWidth(), left = bar.getX();
    g.setColour (juce::Colour (0xff0d0d0d)); g.fillRoundedRectangle (bar, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36c46b), left, 0.0f, juce::Colour (0xffe0483a), left + w, 0.0f, false));
    g.fillRect (juce::Rectangle<float> (left, bar.getY(), n (st_) * w, bar.getHeight()));
    if (target_ > -100.0f) { const float x = left + n (target_) * w; g.setColour (juce::Colours::white.withAlpha (0.9f)); g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, bar.getHeight())); }
    if (hasRec_ && recSt_ > -70.0f) { const float x = left + n (recSt_) * w; g.setColour (juce::Colour (0xffe0a53a)); g.fillRect (juce::Rectangle<float> (x - 1.5f, bar.getY(), 3.0f, bar.getHeight())); }
}

//==============================================================================
void PanelSpectrum::paint (juce::Graphics& g)
{
    auto in = panelFrame (g, getLocalBounds(), "SPECTRUM");
    if (n_ <= 1 || live_ == nullptr) return;
    const float w = in.getWidth(), h = in.getHeight(), left = in.getX(), bottom = in.getBottom();
    auto nrm = [] (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin, -120.0f) + 90.0f) / 90.0f); };
    auto xOf = [&] (int i) { return left + (float) i / (n_ - 1) * w; };

    juce::Path fill; fill.startNewSubPath (left, bottom);
    for (int i = 0; i < n_; ++i) fill.lineTo (xOf (i), bottom - nrm (live_[i]) * h);
    fill.lineTo (in.getRight(), bottom); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x8836c46b), left, in.getY(), juce::Colour (0x1136c46b), left, bottom, false));
    g.fillPath (fill);

    juce::Path s;
    for (int i = 0; i < n_; ++i) { const float x = xOf (i), y = bottom - nrm (live_[i]) * h; if (i == 0) s.startNewSubPath (x, y); else s.lineTo (x, y); }
    g.setColour (juce::Colour (0xff5fe08a)); g.strokePath (s, juce::PathStrokeType (1.4f));

    if (hasRec_ && rec_ != nullptr)
    {
        juce::Path r;
        for (int i = 0; i < n_; ++i) { const float x = xOf (i), y = bottom - nrm (rec_[i]) * h; if (i == 0) r.startNewSubPath (x, y); else r.lineTo (x, y); }
        g.setColour (juce::Colour (0xffe0a53a)); g.strokePath (r, juce::PathStrokeType (1.6f));
    }
}

//==============================================================================
void PanelWidth::paint (juce::Graphics& g)
{
    auto in = panelFrame (g, getLocalBounds(), "WIDTH");
    auto metersArea = in.removeFromBottom (38.0f);

    // Goniometer.
    const float s = juce::jmin (in.getWidth(), in.getHeight());
    auto sq = juce::Rectangle<float> (0, 0, s, s).withCentre (in.getCentre());
    const float cx = sq.getCentreX(), cy = sq.getCentreY(), scale = s * 0.45f;
    g.setColour (juce::Colour (0x18ffffff));
    g.drawVerticalLine ((int) cx, sq.getY(), sq.getBottom());
    g.drawLine (cx - scale, cy - scale, cx + scale, cy + scale, 1.0f);
    g.drawLine (cx - scale, cy + scale, cx + scale, cy - scale, 1.0f);
    if (pts_ != nullptr && count_ > 0)
    {
        g.setColour (juce::Colour (0x9936c46b));
        for (int i = 0; i < count_; ++i)
        {
            const float side = pts_[2 * i], mid = pts_[2 * i + 1];
            g.fillRect (cx - juce::jlimit (-1.5f, 1.5f, side) * scale - 0.75f, cy - juce::jlimit (-1.5f, 1.5f, mid) * scale - 0.75f, 1.5f, 1.5f);
        }
    }

    // Corr / Width / Balance line.
    auto row = metersArea.removeFromTop (16.0f);
    g.setColour (juce::Colour (0xffb0b0b0)); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    juce::String balS = std::abs (bal_) < 0.02f ? juce::String ("C") : (bal_ > 0.0f ? "R" : "L") + juce::String ((int) std::round (std::abs (bal_) * 100.0f));
    g.drawText ("C " + juce::String (corr_, 2) + "   W " + juce::String ((int) std::round (width_ * 100.0f)) + "%   B " + balS, row, juce::Justification::centredLeft);

    // Width bar + ghost.
    auto bar = metersArea;
    const float w = bar.getWidth(), left = bar.getX();
    g.setColour (juce::Colour (0xff0d0d0d)); g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (juce::Colour (0xff36a0c4)); g.fillRect (juce::Rectangle<float> (left, bar.getY(), juce::jlimit (0.0f, 1.0f, width_) * w, bar.getHeight()));
    if (hasRec_) { const float x = left + juce::jlimit (0.0f, 1.0f, recWidth_) * w; g.setColour (juce::Colour (0xffe0a53a)); g.fillRect (juce::Rectangle<float> (x - 1.5f, bar.getY(), 3.0f, bar.getHeight())); }
}

//==============================================================================
DejaVUSuiteAudioProcessorEditor::DejaVUSuiteAudioProcessorEditor (DejaVUSuiteAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    for (int b = 0; b < kBands; ++b) centres[(size_t) b] = proc.getBandCentreHz (b);
    panelSpectrum.setSources (liveBands.data(), recBands.data(), centres.data(), kBands);
    panelWidth.setGonio (gonioSnapshot.data(), DejaVUSuiteAudioProcessor::kGonio);
    addAndMakeVisible (panelMeter); addAndMakeVisible (panelLUFS);
    addAndMakeVisible (panelSpectrum); addAndMakeVisible (panelWidth);

    auto setupToggle = [this] (juce::TextButton& btn, const char* param, std::unique_ptr<BAtt>& att)
    {
        btn.setClickingTogglesState (true);
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
        btn.onClick = [this] { applyPanels(); };
        addAndMakeVisible (btn);
        att = std::make_unique<BAtt> (proc.apvts, param, btn);
    };
    setupToggle (showMeterBtn, "showMeter", showMeterAtt);
    setupToggle (showLufsBtn,  "showLUFS", showLufsAtt);
    setupToggle (showSpecBtn,  "showSpectrum", showSpecAtt);
    setupToggle (showWidthBtn, "showWidth", showWidthAtt);

    modeBox.addItem ("VU", 1); modeBox.addItem ("Peak", 2);
    modeBox.setTooltip ("Meter panel: VU (300 ms RMS) or Peak.");
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<CAtt> (proc.apvts, "meterMode", modeBox);

    targetBox.addItem ("Target Off", 1); targetBox.addItem ("-14", 2); targetBox.addItem ("-16", 3); targetBox.addItem ("-23", 4);
    targetBox.setTooltip ("LUFS panel: reference loudness target line.");
    addAndMakeVisible (targetBox);
    targetAtt = std::make_unique<CAtt> (proc.apvts, "target", targetBox);

    autoButton.setClickingTogglesState (true);
    autoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff36a0c4));
    autoButton.setTooltip ("Auto-record: records all four metrics every time the transport plays. Keeps the last 5 takes.");
    addAndMakeVisible (autoButton);
    autoAtt = std::make_unique<BAtt> (proc.apvts, "autoMode", autoButton);

    armButton.setClickingTogglesState (true);
    armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a));
    armButton.setTooltip ("Arm record: records all four metrics while the transport plays, then disarms on stop.");
    addAndMakeVisible (armButton);
    armAtt = std::make_unique<BAtt> (proc.apvts, "recordArm", armButton);

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

    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888)); nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (nameLabel);
    nameField.setTextToShowWhenEmpty ("recording name", juce::Colour (0xff666666));
    nameField.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1a1a));
    nameField.setTooltip ("Name for the next recording. With a take selected, press Enter to rename it.");
    nameField.onReturnKey = [this] { if (proc.getActiveRecording() >= 0) { proc.renameActiveRecording (nameField.getText()); refreshRecordingList(); nameField.setText (proc.getRecordingName (proc.getActiveRecording()), juce::dontSendNotification); } };
    addAndMakeVisible (nameField);

    takesLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888)); takesLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (takesLabel);
    takesBox.setTextWhenNoChoicesAvailable ("no takes yet");
    takesBox.onChange = [this] { const int id = takesBox.getSelectedId(); if (id > 0) { proc.selectRecording (id - 1); nameField.setText (proc.getRecordingName (id - 1), juce::dontSendNotification); refreshRecordingList(); } };
    addAndMakeVisible (takesBox);

    minLenBox.addItem ("Min: Off", 1); minLenBox.addItem ("Min 0.5s", 2); minLenBox.addItem ("Min 1s", 3); minLenBox.addItem ("Min 2s", 4); minLenBox.addItem ("Min 5s", 5);
    addAndMakeVisible (minLenBox);
    minLenAtt = std::make_unique<CAtt> (proc.apvts, "minLen", minLenBox);

    takeInfoLabel.setJustificationType (juce::Justification::centred);
    takeInfoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a)); takeInfoLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (takeInfoLabel);
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b0b0)); positionLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (positionLabel);

    refreshRecordingList();
    setSize (640, 620);
    applyPanels();
    startTimerHz (30);
}

DejaVUSuiteAudioProcessorEditor::~DejaVUSuiteAudioProcessorEditor() { stopTimer(); }

//==============================================================================
void DejaVUSuiteAudioProcessorEditor::applyPanels()
{
    panelMeter.setVisible (showMeterBtn.getToggleState());
    panelLUFS.setVisible (showLufsBtn.getToggleState());
    panelSpectrum.setVisible (showSpecBtn.getToggleState());
    panelWidth.setVisible (showWidthBtn.getToggleState());
    resized();
}

void DejaVUSuiteAudioProcessorEditor::refreshRecordingList()
{
    takesBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumRecordings(); ++i) takesBox.addItem (proc.getRecordingName (i), i + 1);
    if (proc.getActiveRecording() >= 0) takesBox.setSelectedId (proc.getActiveRecording() + 1, juce::dontSendNotification);
    const auto info = proc.getRecordingInfo (proc.getActiveRecording());
    if (info.valid)
    {
        auto tc = [] (double s) { const int m = (int) (s / 60.0); return juce::String::formatted ("%d:%05.2f", m, s - m * 60.0); };
        const juce::String s0 = tc (info.startSeconds), s1 = tc (info.endSeconds);
        takeInfoLabel.setText (juce::String::formatted ("start %d.%d (%s)   end %d.%d (%s)   len %.1fs", info.startBar, info.startBeat, s0.toRawUTF8(), info.endBar, info.endBeat, s1.toRawUTF8(), juce::jmax (0.0, info.endSeconds - info.startSeconds)), juce::dontSendNotification);
    }
    else takeInfoLabel.setText ({}, juce::dontSendNotification);
}

//==============================================================================
void DejaVUSuiteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0e0e));
    auto top = getLocalBounds().removeFromTop (28).reduced (12, 4);
    g.setColour (juce::Colour (0xffe8e8e8)); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("DEJA VU SUITE", top, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff6a6a6a)); g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v" JucePlugin_VersionString, top, juce::Justification::centredRight);
    if (proc.recordingNow.load())
    {
        auto dot = juce::Rectangle<float> (top.getRight() - 118.0f, top.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (blinkOn ? juce::Colour (0xffff3b30) : juce::Colour (0xff661512)); g.fillEllipse (dot);
        g.setColour (blinkOn ? juce::Colour (0xffff6b60) : juce::Colour (0xff884440)); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("REC", juce::Rectangle<int> ((int) dot.getRight() + 4, top.getY(), 44, top.getHeight()), juce::Justification::centredLeft);
    }
}

void DejaVUSuiteAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (28);   // title

    auto toggles = r.removeFromTop (28);
    const int tw = (toggles.getWidth() - 136) / 4;
    showMeterBtn.setBounds (toggles.removeFromLeft (tw).reduced (2, 0));
    showLufsBtn.setBounds  (toggles.removeFromLeft (tw).reduced (2, 0));
    showSpecBtn.setBounds  (toggles.removeFromLeft (tw).reduced (2, 0));
    showWidthBtn.setBounds (toggles.removeFromLeft (tw).reduced (2, 0));
    targetBox.setBounds (toggles.removeFromRight (68).reduced (2, 0));
    modeBox.setBounds   (toggles.removeFromRight (64).reduced (2, 0));

    r.removeFromTop (6);
    auto controls = r.removeFromBottom (128);
    r.removeFromBottom (6);

    // Panel grid.
    std::vector<juce::Component*> vis;
    if (showMeterBtn.getToggleState()) vis.push_back (&panelMeter);
    if (showLufsBtn.getToggleState())  vis.push_back (&panelLUFS);
    if (showSpecBtn.getToggleState())  vis.push_back (&panelSpectrum);
    if (showWidthBtn.getToggleState()) vis.push_back (&panelWidth);
    const int n = (int) vis.size();
    if (n > 0)
    {
        const int cols = n <= 1 ? 1 : 2;
        const int rows = (n + cols - 1) / cols;
        const int cw = r.getWidth() / cols, ch = r.getHeight() / rows;
        for (int i = 0; i < n; ++i)
        {
            const int cxi = i % cols, cyi = i / cols;
            vis[(size_t) i]->setBounds (juce::Rectangle<int> (r.getX() + cxi * cw, r.getY() + cyi * ch, cw, ch).reduced (3));
        }
    }

    auto buttons = controls.removeFromTop (30);
    const int bw = buttons.getWidth() / 3;
    autoButton.setBounds  (buttons.removeFromLeft (bw).reduced (3, 0));
    armButton.setBounds   (buttons.removeFromLeft (bw).reduced (3, 0));
    clearButton.setBounds (buttons.reduced (3, 0));
    controls.removeFromTop (6);
    auto nameRow = controls.removeFromTop (26);
    nameLabel.setBounds (nameRow.removeFromLeft (46)); nameField.setBounds (nameRow);
    controls.removeFromTop (6);
    auto takeRow = controls.removeFromTop (26);
    takesLabel.setBounds (takeRow.removeFromLeft (46));
    minLenBox.setBounds (takeRow.removeFromRight (92)); takeRow.removeFromRight (6);
    takesBox.setBounds (takeRow);
    controls.removeFromTop (4);
    takeInfoLabel.setBounds (controls.removeFromTop (16));
    positionLabel.setBounds (controls.removeFromTop (22));
}

//==============================================================================
void DejaVUSuiteAudioProcessorEditor::timerCallback()
{
    for (int b = 0; b < kBands; ++b) { liveBands[(size_t) b] = proc.bands[(size_t) b].load(); recBands[(size_t) b] = proc.recBands[(size_t) b].load(); }
    for (int i = 0; i < 2 * DejaVUSuiteAudioProcessor::kGonio; ++i) gonioSnapshot[(size_t) i] = proc.gonio[(size_t) i];

    const bool hasRec = proc.hasRecording.load();
    panelMeter.update (proc.vuL.load(), proc.vuPeakL.load(), proc.vuR.load(), proc.vuPeakR.load(), proc.recVuL.load(), proc.recVuR.load(), hasRec);
    panelLUFS.update (proc.lufsMom.load(), proc.lufsShort.load(), proc.lufsInteg.load(), proc.lufsTP.load(), proc.recLufs.load(), proc.targetLufs(), hasRec);
    panelSpectrum.update (hasRec);
    panelWidth.update (proc.corr.load(), proc.width.load(), proc.recWidth.load(), proc.balance.load(), hasRec);

    const bool autoOn = *proc.apvts.getRawParameterValue ("autoMode") > 0.5f;
    if (autoOn != lastAutoOn)
    {
        if (! autoOn) { armButton.setColour (juce::TextButton::buttonColourId, getLookAndFeel().findColour (juce::TextButton::buttonColourId)); armButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483a)); }
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
        blinkCounter = 0; blinkOn = ! blinkOn;
        if (proc.recordingNow.load()) repaint (getLocalBounds().removeFromTop (28));
        if (autoOn) { const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff4a3a12); armButton.setColour (juce::TextButton::buttonColourId, c); armButton.setColour (juce::TextButton::buttonOnColourId, c); }
        if (proc.transportPlaying.load() && proc.hasRecording.load()) { const auto c = blinkOn ? juce::Colour (0xffe0a53a) : juce::Colour (0xff5c4410); takesBox.setColour (juce::ComboBox::textColourId, c); takesBox.setColour (juce::ComboBox::outlineColourId, c); }
        else { takesBox.setColour (juce::ComboBox::textColourId, getLookAndFeel().findColour (juce::ComboBox::textColourId)); takesBox.setColour (juce::ComboBox::outlineColourId, getLookAndFeel().findColour (juce::ComboBox::outlineColourId)); }
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
