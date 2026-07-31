#include "SpectrumProcessor.h"
#include "SpectrumEditor.h"
#include <cmath>

static constexpr int kMaxPeaks = 25;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
DejaVUSpectrumAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "autoMode", 1 }, "Auto Record", false));
    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "minLen", 1 }, "Min Take Length",
        StringArray { "Off", "0.5 s", "1 s", "2 s", "5 s" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "peakThresh", 1 }, "Peak Threshold",
        StringArray { "0 dBFS", "-1 dBFS", "-3 dBFS", "-6 dBFS" }, 1));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "peaksMonitor", 1 }, "Peaks Monitor", false));
    layout.add (std::make_unique<AudioParameterFloat>(
        ParameterID { "vZoom", 1 }, "Vertical Zoom",
        juce::NormalisableRange<float> (1.0f, 4.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "showDelta", 1 }, "Delta View", false));
    layout.add (std::make_unique<AudioParameterBool>(
        ParameterID { "recordArm", 1 }, "Record Arm", false));

    return layout;
}

//==============================================================================
DejaVUSpectrumAudioProcessor::DejaVUSpectrumAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureFrames.assign  ((size_t) kMaxSlots * kBands, 0.0f);
    playbackFrames.assign ((size_t) kMaxSlots * kBands, 0.0f);
    for (auto& a : liveBands) a.store (0.0f);
    for (auto& a : recBands)  a.store (0.0f);
    computeBands();
}

//==============================================================================
void DejaVUSpectrumAudioProcessor::computeBands()
{
    const double fLo = 20.0;
    const double fHi = juce::jmin (20000.0, currentSampleRate * 0.5 * 0.95);
    for (int b = 0; b < kBands; ++b)
    {
        const double e0 = fLo * std::pow (fHi / fLo, (double) b / kBands);
        const double e1 = fLo * std::pow (fHi / fLo, (double) (b + 1) / kBands);
        bandCentreHz[(size_t) b] = (float) std::sqrt (e0 * e1);
        int lo = (int) std::floor (e0 * kFftSize / currentSampleRate);
        int hi = (int) std::ceil  (e1 * kFftSize / currentSampleRate);
        lo = juce::jlimit (1, kFftSize / 2 - 1, lo);
        hi = juce::jlimit (lo, kFftSize / 2 - 1, hi);
        binLo[(size_t) b] = lo;
        binHi[(size_t) b] = hi;
    }
}

void DejaVUSpectrumAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    writePos = 0;
    hopCounter = 0;
    prevRecording = false;
    peakInEvent = false;
    std::fill (fifo.begin(), fifo.end(), 0.0f);
    std::fill (bandLevel.begin(), bandLevel.end(), 0.0f);
    computeBands();
}

bool DejaVUSpectrumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
float DejaVUSpectrumAudioProcessor::peakThresholdDb() const
{
    static const float table[] = { 0.0f, -1.0f, -3.0f, -6.0f };
    return table[juce::jlimit (0, 3, (int) *apvts.getRawParameterValue ("peakThresh"))];
}

void DejaVUSpectrumAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    const double beatLen = 4.0 / den;
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / beatLen) + 1;
}

void DejaVUSpectrumAudioProcessor::pushPeak (const PeakMark& m) noexcept
{
    int s1, sz1, s2, sz2;
    peakFifo.prepareToWrite (1, s1, sz1, s2, sz2);
    if (sz1 > 0)      peakFifoBuf[(size_t) s1] = m;
    else if (sz2 > 0) peakFifoBuf[(size_t) s2] = m;
    peakFifo.finishedWrite (sz1 + sz2 >= 1 ? 1 : 0);
}

void DejaVUSpectrumAudioProcessor::drainPeaks()
{
    if (newCaptureStarted.exchange (false)) capturePeaks.clear();
    if (newMonitorStarted.exchange (false)) { monitorPeaks.clear(); ++monitorRev; }

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
void DejaVUSpectrumAudioProcessor::doFFT()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        fftData[(size_t) i] = fifo[(size_t) ((writePos + i) % kFftSize)];   // oldest-first

    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / (float) kFftSize;
    for (int b = 0; b < kBands; ++b)
    {
        float sum = 0.0f;
        const int lo = binLo[(size_t) b], hi = binHi[(size_t) b];
        for (int k = lo; k <= hi; ++k) sum += fftData[(size_t) k];
        const float level = (sum / (float) (hi - lo + 1)) * norm;
        float& s = bandLevel[(size_t) b];
        s += (level - s) * 0.6f;
        liveBands[(size_t) b].store (s);
    }
}

void DejaVUSpectrumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float blockPeak = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer (ch)[n];
        mono /= (float) juce::jmax (1, numChannels);
        blockPeak = juce::jmax (blockPeak, std::abs (mono));

        fifo[(size_t) writePos] = mono;
        writePos = (writePos + 1) % kFftSize;
        if (++hopCounter >= kFftHop) { hopCounter = 0; doFFT(); }
    }

    const double blockSeconds = numSamples > 0 ? numSamples / currentSampleRate : 0.0;

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

    const int num = timeSigNum.load(), den = timeSigDen.load();
    transportPlaying.store (playing);
    playheadSeconds.store (posSecs);
    playheadPpq.store (ppq);

    const bool armed     = *apvts.getRawParameterValue ("recordArm") > 0.5f;
    const bool autoMode  = *apvts.getRawParameterValue ("autoMode")  > 0.5f;
    const bool recording = (armed || autoMode) && playing;
    recordingNow.store (recording);

    const bool monitorOn     = *apvts.getRawParameterValue ("peaksMonitor") > 0.5f;
    const bool monitorActive = monitorOn && playing && ! recording;
    const bool detectPeaks   = recording || monitorActive;
    if (monitorOn && ! prevMonitorOn) newMonitorStarted.store (true);
    prevMonitorOn = monitorOn;

    const int slot = posSamples >= 0
        ? (int) ((posSamples * (juce::int64) kSlotsPerSecond)
                    / (juce::int64) juce::jmax (1.0, currentSampleRate))
        : (int) (posSecs * kSlotsPerSecond);

    if (recording && ! prevRecording)
    {
        std::fill (captureFrames.begin(), captureFrames.end(), 0.0f);
        captureMaxSlot.store (-1);
        peakInEvent = false;
        newCaptureStarted.store (true);
        int sBar, sBeat; computeBarBeat (ppq, num, den, sBar, sBeat);
        capStartSecs.store (posSecs); capStartBar.store (sBar); capStartBeat.store (sBeat);
    }

    if (recording && slot >= 0 && slot < kMaxSlots)
    {
        float* dst = &captureFrames[(size_t) slot * kBands];
        for (int b = 0; b < kBands; ++b) dst[b] = bandLevel[(size_t) b];
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), slot));
        int eBar, eBeat; computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds); capEndBar.store (eBar); capEndBeat.store (eBeat);
    }

    // ---- Tag peaks (recording OR Peaks Monitor) ----------------------------
    if (detectPeaks)
    {
        const float thrDb = peakThresholdDb();
        const float blockPeakDb = juce::Decibels::gainToDecibels (blockPeak, -120.0f);
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
    else if (peakInEvent)
    {
        pushPeak (peakEventMark);
        peakInEvent = false;
    }

    if (! recording && prevRecording)
        captureFinished.store (true);
    prevRecording = recording;

    // ---- Ghost -------------------------------------------------------------
    if (slot >= 0 && slot < kMaxSlots)
    {
        const float* src = &playbackFrames[(size_t) slot * kBands];
        for (int b = 0; b < kBands; ++b) recBands[(size_t) b].store (src[b]);
    }
    else
        for (int b = 0; b < kBands; ++b) recBands[(size_t) b].store (0.0f);

    juce::ignoreUnused (buffer);
}

//==============================================================================
void DejaVUSpectrumAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackFrames.begin(), playbackFrames.end(), 0.0f);
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& r = history[(size_t) activeIndex];
        const int n = juce::jmin (r.numSlots * kBands, (int) playbackFrames.size());
        for (int i = 0; i < n && i < (int) r.frames.size(); ++i)
            playbackFrames[(size_t) i] = r.frames[(size_t) i];
        hasRecording.store (r.numSlots > 0);
    }
    else { hasRecording.store (false); }
}

void DejaVUSpectrumAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0) { capturePeaks.clear(); return; }

    static const float minTable[] = { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f };
    const float minLen = minTable[juce::jlimit (0, 4, (int) *apvts.getRawParameterValue ("minLen"))];
    if ((capEndSecs.load() - capStartSecs.load()) < (double) minLen) { capturePeaks.clear(); return; }

    Recording take;
    take.name = name.trim().isNotEmpty() ? makeUniqueName (name.trim()) : defaultTakeName();
    take.numSlots = len;
    take.frames.resize ((size_t) len * kBands);
    std::copy (captureFrames.begin(), captureFrames.begin() + (size_t) len * kBands, take.frames.begin());
    take.peaks = capturePeaks;
    capturePeaks.clear();
    take.startSeconds = capStartSecs.load(); take.endSeconds = capEndSecs.load();
    take.startBar = capStartBar.load(); take.startBeat = capStartBeat.load();
    take.endBar = capEndBar.load(); take.endBeat = capEndBeat.load();

    history.insert (history.begin(), std::move (take));
    if ((int) history.size() > kMaxRecordings) history.resize (kMaxRecordings);
    activeIndex = 0;
    fillPlaybackFromActive();
}

void DejaVUSpectrumAudioProcessor::selectRecording (int index)
{
    if (juce::isPositiveAndBelow (index, (int) history.size()))
    {
        activeIndex = index;
        fillPlaybackFromActive();
    }
}

void DejaVUSpectrumAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}

void DejaVUSpectrumAudioProcessor::deleteAllRecordings()
{
    history.clear();
    activeIndex = -1;
    fillPlaybackFromActive();
}

void DejaVUSpectrumAudioProcessor::renameActiveRecording (const juce::String& newName)
{
    if (! juce::isPositiveAndBelow (activeIndex, (int) history.size())) return;
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty()) return;
    auto existsOther = [this] (const juce::String& n)
    { for (int i = 0; i < (int) history.size(); ++i) if (i != activeIndex && history[(size_t) i].name == n) return true; return false; };
    juce::String candidate = trimmed;
    if (existsOther (candidate))
        for (int k = 2; ; ++k) { auto c = trimmed + " (" + juce::String (k) + ")"; if (! existsOther (c)) { candidate = c; break; } }
    history[(size_t) activeIndex].name = candidate;
}

//==============================================================================
bool DejaVUSpectrumAudioProcessor::nameExists (const juce::String& n) const
{
    for (const auto& r : history) if (r.name == n) return true;
    return false;
}

juce::String DejaVUSpectrumAudioProcessor::makeUniqueName (const juce::String& base) const
{
    if (! nameExists (base)) return base;
    for (int i = 2; ; ++i)
    {
        const juce::String c = base + " (" + juce::String (i) + ")";
        if (! nameExists (c)) return c;
    }
}

juce::String DejaVUSpectrumAudioProcessor::defaultTakeName() const
{
    for (int k = 1; ; ++k)
    {
        const juce::String c = "Take " + juce::String (k);
        if (! nameExists (c)) return c;
    }
}

//==============================================================================
juce::AudioProcessorEditor* DejaVUSpectrumAudioProcessor::createEditor()
{
    return new DejaVUSpectrumAudioProcessorEditor (*this);
}

//==============================================================================
void DejaVUSpectrumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);

    if (auto xml = apvts.copyState().createXml())
    {
        const auto s = xml->toString();
        mos.writeInt ((int) s.getNumBytesAsUTF8());
        mos.write (s.toRawUTF8(), s.getNumBytesAsUTF8());
    }
    else { mos.writeInt (0); }

    mos.writeInt (2);                    // format version
    mos.writeInt (kBands);
    mos.writeInt (activeIndex);
    mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name);
        mos.writeInt (r.numSlots);
        if (! r.frames.empty())
            mos.write (r.frames.data(), r.frames.size() * sizeof (float));
        mos.writeInt ((int) r.peaks.size());
        for (const auto& pk : r.peaks)
        {
            mos.writeDouble (pk.seconds); mos.writeDouble (pk.ppq); mos.writeFloat (pk.db);
            mos.writeInt (pk.bar); mos.writeInt (pk.beat);
        }
        mos.writeDouble (r.startSeconds); mos.writeDouble (r.endSeconds);
        mos.writeInt (r.startBar); mos.writeInt (r.startBeat);
        mos.writeInt (r.endBar);   mos.writeInt (r.endBeat);
    }
}

void DejaVUSpectrumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 2)
    {
        const int bands = mis.readInt();
        const int savedActive = mis.readInt();
        const int count = juce::jlimit (0, kMaxRecordings, mis.readInt());
        for (int i = 0; i < count; ++i)
        {
            Recording r;
            r.name = mis.readString();
            r.numSlots = juce::jlimit (0, kMaxSlots, mis.readInt());
            const int n = r.numSlots * bands;
            if (n > 0 && bands == kBands)
            {
                r.frames.resize ((size_t) n);
                mis.read (r.frames.data(), (int) (n * (int) sizeof (float)));
            }
            else if (n > 0)
            {
                mis.skipNextBytes ((juce::int64) n * (int) sizeof (float));
                r.numSlots = 0;
            }
            const int np = juce::jlimit (0, kMaxPeaks, mis.readInt());
            for (int p = 0; p < np; ++p)
            {
                PeakMark pk;
                pk.seconds = mis.readDouble(); pk.ppq = mis.readDouble(); pk.db = mis.readFloat();
                pk.bar = mis.readInt(); pk.beat = mis.readInt();
                r.peaks.push_back (pk);
            }
            r.startSeconds = mis.readDouble(); r.endSeconds = mis.readDouble();
            r.startBar = mis.readInt(); r.startBeat = mis.readInt();
            r.endBar = mis.readInt();   r.endBeat = mis.readInt();
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
    return new DejaVUSpectrumAudioProcessor();
}
