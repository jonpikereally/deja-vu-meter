#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static constexpr int kMaxPeaks = 25;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TimelineVUAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "meterMode", 1 }, "Meter Mode", StringArray { "VU", "Peak" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "meterSkin", 1 }, "Meter Skin", StringArray { "Bar", "Analog" }, 1));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "compactBars", 1 }, "Compact Bars", true));
    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "peakThresh", 1 }, "Peak Threshold",
        StringArray { "0 dBFS", "-1 dBFS", "-3 dBFS", "-6 dBFS" }, 1));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "peaksMonitor", 1 }, "Peaks Monitor", false));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "autoMode", 1 }, "Auto Record", false));
    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "minLen", 1 }, "Min Take Length",
        StringArray { "Off", "0.5 s", "1 s", "2 s", "5 s" }, 0));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "recordArm", 1 }, "Record Arm", false));

    return layout;
}

//==============================================================================
TimelineVUAudioProcessor::TimelineVUAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureEnvL.assign  (kMaxSlots, 0.0f);  captureEnvR.assign  (kMaxSlots, 0.0f);
    playbackEnvL.assign (kMaxSlots, 0.0f);  playbackEnvR.assign (kMaxSlots, 0.0f);
}

//==============================================================================
void TimelineVUAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedMsL = smoothedMsR = 0.0;
    peakEnvL = peakEnvR = 0.0f;
    prevRecording = false;
    peakInEvent   = false;
}

bool TimelineVUAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
float TimelineVUAudioProcessor::peakThresholdDb() const
{
    static const float table[] = { 0.0f, -1.0f, -3.0f, -6.0f };
    return table[juce::jlimit (0, 3, (int) *apvts.getRawParameterValue ("peakThresh"))];
}

void TimelineVUAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    const double beatLen = 4.0 / den;
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    const double inBar = ppq - (bar - 1) * qPerBar;
    beat = (int) std::floor (inBar / beatLen) + 1;
}

void TimelineVUAudioProcessor::pushPeak (const PeakMark& m) noexcept
{
    int s1, sz1, s2, sz2;
    peakFifo.prepareToWrite (1, s1, sz1, s2, sz2);
    if (sz1 > 0)      peakFifoBuf[(size_t) s1] = m;
    else if (sz2 > 0) peakFifoBuf[(size_t) s2] = m;
    peakFifo.finishedWrite (sz1 + sz2 >= 1 ? 1 : 0);
}

void TimelineVUAudioProcessor::drainPeaks()
{
    if (newCaptureStarted.exchange (false))
        capturePeaks.clear();
    if (newMonitorStarted.exchange (false))
    {
        monitorPeaks.clear();
        ++monitorRev;
    }

    bool monChanged = false;
    auto route = [&] (const PeakMark& m)
    {
        if (m.monitor) { monitorPeaks.push_back (m); monChanged = true; }
        else           { capturePeaks.push_back (m); }
    };

    int s1, sz1, s2, sz2;
    peakFifo.prepareToRead (peakFifo.getNumReady(), s1, sz1, s2, sz2);
    for (int i = 0; i < sz1; ++i) route (peakFifoBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < sz2; ++i) route (peakFifoBuf[(size_t) (s2 + i)]);
    peakFifo.finishedRead (sz1 + sz2);

    auto capList = [] (std::vector<PeakMark>& v)
    {
        if ((int) v.size() > kMaxPeaks)
            v.erase (v.begin(), v.begin() + ((int) v.size() - kMaxPeaks));
    };
    capList (capturePeaks);
    capList (monitorPeaks);

    if (monChanged) ++monitorRev;
}

//==============================================================================
void TimelineVUAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- Per-channel measurement (L, R; mono duplicates to R) --------------
    const float* chL = numChannels > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* chR = numChannels > 1 ? buffer.getReadPointer (1) : chL;

    double ssL = 0.0, ssR = 0.0;
    float  pkL = 0.0f, pkR = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        const float l = chL ? chL[n] : 0.0f;
        const float r = chR ? chR[n] : 0.0f;
        ssL += (double) l * l;  ssR += (double) r * r;
        pkL = juce::jmax (pkL, std::abs (l));
        pkR = juce::jmax (pkR, std::abs (r));
    }

    const double blockSeconds = numSamples > 0 ? numSamples / currentSampleRate : 0.0;
    const double msL = numSamples > 0 ? ssL / numSamples : 0.0;
    const double msR = numSamples > 0 ? ssR / numSamples : 0.0;

    const double alpha = 1.0 - std::exp (-blockSeconds / 0.300);
    smoothedMsL += (msL - smoothedMsL) * alpha;
    smoothedMsR += (msR - smoothedMsR) * alpha;
    const float vuL = (float) std::sqrt (juce::jmax (0.0, smoothedMsL));
    const float vuR = (float) std::sqrt (juce::jmax (0.0, smoothedMsR));

    const float rel = (float) std::exp (-blockSeconds / 0.500);
    peakEnvL = juce::jmax (pkL, peakEnvL * rel);
    peakEnvR = juce::jmax (pkR, peakEnvR * rel);

    const auto mode = (MeterMode) (int) *apvts.getRawParameterValue ("meterMode");
    const float levL = (mode == VU) ? vuL : peakEnvL;
    const float levR = (mode == VU) ? vuR : peakEnvR;
    liveL.store (levL);          liveR.store (levR);
    livePeakL.store (peakEnvL);  livePeakR.store (peakEnvR);

    // ---- Host timeline -----------------------------------------------------
    bool   playing = false;
    double posSecs = 0.0, ppq = 0.0;
    juce::int64 posSamples = -1;

    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto s = info->getTimeInSamples()) posSamples = *s;
            if (auto t = info->getTimeInSeconds()) posSecs = *t;
            else if (posSamples >= 0)              posSecs = (double) posSamples / currentSampleRate;
            if (auto q = info->getPpqPosition())   ppq = *q;
            if (auto ts = info->getTimeSignature())
            {
                timeSigNum.store (ts->numerator);
                timeSigDen.store (ts->denominator);
            }
        }

    const int num = timeSigNum.load();
    const int den = timeSigDen.load();

    transportPlaying.store (playing);
    playheadSeconds.store (posSecs);
    playheadPpq.store (ppq);

    const bool armed     = *apvts.getRawParameterValue ("recordArm") > 0.5f;
    const bool autoMode  = *apvts.getRawParameterValue ("autoMode")  > 0.5f;
    const bool recording = (armed || autoMode) && playing;
    recordingNow.store (recording);

    // Peaks Monitor tags peaks regardless of arm/auto (when not recording).
    const bool monitorOn     = *apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    const bool monitorActive = monitorOn && playing && ! recording;
    const bool detectPeaks   = recording || monitorActive;
    if (monitorOn && ! prevMonitorOn) newMonitorStarted.store (true);
    prevMonitorOn = monitorOn;

    const int slot = posSamples >= 0
        ? (int) ((posSamples * (juce::int64) kSlotsPerSecond)
                    / (juce::int64) juce::jmax (1.0, currentSampleRate))
        : (int) (posSecs * kSlotsPerSecond);

    // ---- Start edge --------------------------------------------------------
    if (recording && ! prevRecording)
    {
        std::fill (captureEnvL.begin(), captureEnvL.end(), 0.0f);
        std::fill (captureEnvR.begin(), captureEnvR.end(), 0.0f);
        captureMaxSlot.store (-1);
        peakInEvent = false;
        peakEventMaxDb = -200.0f;
        newCaptureStarted.store (true);

        int sBar, sBeat;
        computeBarBeat (ppq, num, den, sBar, sBeat);
        capStartSecs.store (posSecs);
        capStartBar.store (sBar);
        capStartBeat.store (sBeat);
    }

    // ---- Capture envelope + range (recording only) -------------------------
    if (recording)
    {
        const int endSlot = (int) ((posSecs + blockSeconds) * kSlotsPerSecond);
        const int from = juce::jlimit (0, kMaxSlots - 1, slot);
        const int to   = juce::jlimit (0, kMaxSlots - 1, juce::jmax (slot, endSlot));
        for (int s = from; s <= to; ++s)
        {
            captureEnvL[(size_t) s] = levL;
            captureEnvR[(size_t) s] = levR;
        }
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), to));

        int eBar, eBeat;
        computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds);
        capEndBar.store (eBar);
        capEndBeat.store (eBeat);
    }

    // ---- Tag peaks (recording OR Peaks Monitor) ----------------------------
    if (detectPeaks)
    {
        const float thrDb = peakThresholdDb();
        const float blockPeakDb = juce::Decibels::gainToDecibels (juce::jmax (pkL, pkR), -120.0f);
        if (blockPeakDb >= thrDb)
        {
            if (! peakInEvent) { peakInEvent = true; peakEventMaxDb = -200.0f; }
            if (blockPeakDb > peakEventMaxDb)
            {
                peakEventMaxDb = blockPeakDb;
                PeakMark m;
                m.seconds = posSecs; m.ppq = ppq; m.db = blockPeakDb; m.monitor = ! recording;
                computeBarBeat (ppq, num, den, m.bar, m.beat);
                peakEventMark = m;
            }
        }
        else if (peakInEvent && blockPeakDb < thrDb - 1.0f)
        {
            pushPeak (peakEventMark);
            peakInEvent = false;
        }
    }
    else if (peakInEvent)   // detection just stopped mid-event → flush
    {
        pushPeak (peakEventMark);
        peakInEvent = false;
    }

    // ---- Stop edge ---------------------------------------------------------
    if (! recording && prevRecording)
        captureFinished.store (true);
    prevRecording = recording;

    // ---- Ghost -------------------------------------------------------------
    if (slot >= 0 && slot < kMaxSlots)
    {
        recordedL.store (playbackEnvL[(size_t) slot]);
        recordedR.store (playbackEnvR[(size_t) slot]);
    }
    else { recordedL.store (0.0f); recordedR.store (0.0f); }

    juce::ignoreUnused (buffer);
}

//==============================================================================
void TimelineVUAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackEnvL.begin(), playbackEnvL.end(), 0.0f);
    std::fill (playbackEnvR.begin(), playbackEnvR.end(), 0.0f);

    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& r = history[(size_t) activeIndex];
        const int n = juce::jmin ((int) r.dataL.size(), (int) r.dataR.size(), kMaxSlots);
        for (int i = 0; i < n; ++i)
        {
            playbackEnvL[(size_t) i] = r.dataL[(size_t) i];
            playbackEnvR[(size_t) i] = r.dataR[(size_t) i];
        }
        hasRecording.store (n > 0);
    }
    else { hasRecording.store (false); }
}

void TimelineVUAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0) { capturePeaks.clear(); return; }

    static const float minTable[] = { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f };
    const float minLen = minTable[juce::jlimit (0, 4, (int) *apvts.getRawParameterValue ("minLen"))];
    const double durationSecs = juce::jmax (0.0, capEndSecs.load() - capStartSecs.load());
    if (durationSecs < (double) minLen) { capturePeaks.clear(); return; }

    Recording take;
    take.name = name.trim().isNotEmpty() ? makeUniqueName (name.trim())
                                         : defaultTakeName();
    take.dataL.resize ((size_t) len);
    take.dataR.resize ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        take.dataL[(size_t) i] = captureEnvL[(size_t) i];
        take.dataR[(size_t) i] = captureEnvR[(size_t) i];
    }
    take.peaks = capturePeaks;
    capturePeaks.clear();

    take.startSeconds = capStartSecs.load();
    take.endSeconds   = capEndSecs.load();
    take.startBar     = capStartBar.load();
    take.startBeat    = capStartBeat.load();
    take.endBar       = capEndBar.load();
    take.endBeat      = capEndBeat.load();

    history.insert (history.begin(), std::move (take));
    if ((int) history.size() > kMaxRecordings)
        history.resize (kMaxRecordings);

    activeIndex = 0;
    fillPlaybackFromActive();
}

void TimelineVUAudioProcessor::selectRecording (int index)
{
    if (juce::isPositiveAndBelow (index, (int) history.size()))
    {
        activeIndex = index;
        fillPlaybackFromActive();
    }
}

void TimelineVUAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}

void TimelineVUAudioProcessor::deleteAllRecordings()
{
    history.clear();
    activeIndex = -1;
    fillPlaybackFromActive();
}

void TimelineVUAudioProcessor::renameActiveRecording (const juce::String& newName)
{
    if (! juce::isPositiveAndBelow (activeIndex, (int) history.size())) return;
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty()) return;

    auto existsOther = [this] (const juce::String& n)
    {
        for (int i = 0; i < (int) history.size(); ++i)
            if (i != activeIndex && history[(size_t) i].name == n) return true;
        return false;
    };
    juce::String candidate = trimmed;
    if (existsOther (candidate))
        for (int k = 2; ; ++k) { auto c = trimmed + " (" + juce::String (k) + ")"; if (! existsOther (c)) { candidate = c; break; } }

    history[(size_t) activeIndex].name = candidate;
}

//==============================================================================
bool TimelineVUAudioProcessor::nameExists (const juce::String& n) const
{
    for (const auto& r : history)
        if (r.name == n)
            return true;
    return false;
}

juce::String TimelineVUAudioProcessor::makeUniqueName (const juce::String& base) const
{
    if (! nameExists (base))
        return base;
    for (int i = 2; ; ++i)
    {
        const juce::String candidate = base + " (" + juce::String (i) + ")";
        if (! nameExists (candidate))
            return candidate;
    }
}

juce::String TimelineVUAudioProcessor::defaultTakeName() const
{
    for (int k = 1; ; ++k)
    {
        const juce::String candidate = "Take " + juce::String (k);
        if (! nameExists (candidate))
            return candidate;
    }
}

//==============================================================================
juce::AudioProcessorEditor* TimelineVUAudioProcessor::createEditor()
{
    return new TimelineVUAudioProcessorEditor (*this);
}

//==============================================================================
void TimelineVUAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);

    if (auto xml = apvts.copyState().createXml())
    {
        const auto xmlStr = xml->toString();
        mos.writeInt ((int) xmlStr.getNumBytesAsUTF8());
        mos.write (xmlStr.toRawUTF8(), xmlStr.getNumBytesAsUTF8());
    }
    else { mos.writeInt (0); }

    mos.writeInt (5);                          // format version
    mos.writeInt (kSlotsPerSecond);
    mos.writeInt (activeIndex);
    mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name);
        const int len = (int) juce::jmin (r.dataL.size(), r.dataR.size());
        mos.writeInt (len);
        if (len > 0)
        {
            mos.write (r.dataL.data(), (size_t) len * sizeof (float));
            mos.write (r.dataR.data(), (size_t) len * sizeof (float));
        }

        mos.writeInt ((int) r.peaks.size());
        for (const auto& pk : r.peaks)
        {
            mos.writeDouble (pk.seconds);
            mos.writeDouble (pk.ppq);
            mos.writeFloat  (pk.db);
            mos.writeInt    (pk.bar);
            mos.writeInt    (pk.beat);
        }

        mos.writeDouble (r.startSeconds);
        mos.writeDouble (r.endSeconds);
        mos.writeInt (r.startBar);
        mos.writeInt (r.startBeat);
        mos.writeInt (r.endBar);
        mos.writeInt (r.endBeat);
    }
}

void TimelineVUAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream mis (data, (size_t) sizeInBytes, false);

    const int xmlLen = mis.readInt();
    if (xmlLen > 0)
    {
        juce::MemoryBlock xmlBytes;
        mis.readIntoMemoryBlock (xmlBytes, xmlLen);
        if (auto xml = juce::parseXML (xmlBytes.toString()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    history.clear();
    activeIndex = -1;

    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 5)
    {
        mis.readInt();                          // slotsPerSecond context
        const int savedActive = mis.readInt();
        const int count = juce::jlimit (0, kMaxRecordings, mis.readInt());
        for (int i = 0; i < count; ++i)
        {
            Recording r;
            r.name = mis.readString();
            const int len = juce::jlimit (0, kMaxSlots, mis.readInt());
            r.dataL.resize ((size_t) len);
            r.dataR.resize ((size_t) len);
            if (len > 0)
            {
                mis.read (r.dataL.data(), (int) (len * (int) sizeof (float)));
                mis.read (r.dataR.data(), (int) (len * (int) sizeof (float)));
            }

            const int np = juce::jlimit (0, kMaxPeaks, mis.readInt());
            for (int p = 0; p < np; ++p)
            {
                PeakMark pk;
                pk.seconds = mis.readDouble();
                pk.ppq     = mis.readDouble();
                pk.db      = mis.readFloat();
                pk.bar     = mis.readInt();
                pk.beat    = mis.readInt();
                r.peaks.push_back (pk);
            }

            r.startSeconds = mis.readDouble();
            r.endSeconds   = mis.readDouble();
            r.startBar     = mis.readInt();
            r.startBeat    = mis.readInt();
            r.endBar       = mis.readInt();
            r.endBeat      = mis.readInt();

            history.push_back (std::move (r));
        }
        activeIndex = juce::isPositiveAndBelow (savedActive, (int) history.size()) ? savedActive
                    : (history.empty() ? -1 : 0);
    }

    fillPlaybackFromActive();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TimelineVUAudioProcessor();
}
