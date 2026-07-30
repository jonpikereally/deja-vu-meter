#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static constexpr int kMaxPeaks = 25;   // remember only the most recent 25

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

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "peakThresh", 1 }, "Peak Threshold",
        StringArray { "0 dBFS", "-1 dBFS", "-3 dBFS", "-6 dBFS" }, 1));

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
    captureEnvelope.assign  (kMaxSlots, 0.0f);
    playbackEnvelope.assign (kMaxSlots, 0.0f);
}

//==============================================================================
void TimelineVUAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedMeanSquare = 0.0;
    peakEnvelope       = 0.0f;
    prevRecording      = false;
    peakInEvent        = false;
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
    const int idx = juce::jlimit (0, 3, (int) *apvts.getRawParameterValue ("peakThresh"));
    return table[idx];
}

void TimelineVUAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar  = juce::jmax (0.25, num * 4.0 / den);
    const double beatLen  = 4.0 / den;
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

    int s1, sz1, s2, sz2;
    peakFifo.prepareToRead (peakFifo.getNumReady(), s1, sz1, s2, sz2);
    for (int i = 0; i < sz1; ++i) capturePeaks.push_back (peakFifoBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < sz2; ++i) capturePeaks.push_back (peakFifoBuf[(size_t) (s2 + i)]);
    peakFifo.finishedRead (sz1 + sz2);

    // Keep only the most recent 25.
    if ((int) capturePeaks.size() > kMaxPeaks)
        capturePeaks.erase (capturePeaks.begin(),
                            capturePeaks.begin() + ((int) capturePeaks.size() - kMaxPeaks));
}

//==============================================================================
void TimelineVUAudioProcessor::writeSlots (int fromSlot, int toSlot, float value) noexcept
{
    fromSlot = juce::jlimit (0, kMaxSlots - 1, fromSlot);
    toSlot   = juce::jlimit (0, kMaxSlots - 1, toSlot);
    for (int s = fromSlot; s <= toSlot; ++s)
        captureEnvelope[(size_t) s] = value;
}

void TimelineVUAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- Measure this block ------------------------------------------------
    double sumSquares = 0.0;
    float  blockPeak  = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer (ch)[n];
        mono /= (float) juce::jmax (1, numChannels);
        sumSquares += (double) mono * mono;
        blockPeak = juce::jmax (blockPeak, std::abs (mono));
    }

    const double blockMeanSquare = numSamples > 0 ? sumSquares / (double) numSamples : 0.0;
    const double blockSeconds    = numSamples > 0 ? numSamples / currentSampleRate : 0.0;

    const double alpha = 1.0 - std::exp (-blockSeconds / 0.300);
    smoothedMeanSquare += (blockMeanSquare - smoothedMeanSquare) * alpha;
    const float vuValue = (float) std::sqrt (juce::jmax (0.0, smoothedMeanSquare));

    const float peakRelease = (float) std::exp (-blockSeconds / 0.500);
    peakEnvelope = juce::jmax (blockPeak, peakEnvelope * peakRelease);

    const auto mode = (MeterMode) (int) *apvts.getRawParameterValue ("meterMode");
    const float currentLevel = (mode == VU) ? vuValue : peakEnvelope;
    liveLevel.store (currentLevel);

    // ---- Host timeline -----------------------------------------------------
    bool   playing = false;
    double posSecs = 0.0;
    double ppq     = 0.0;
    juce::int64 posSamples = -1;

    if (auto* ph = getPlayHead())
    {
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
    }

    const int num = timeSigNum.load();
    const int den = timeSigDen.load();

    transportPlaying.store (playing);
    playheadSeconds.store (posSecs);
    playheadPpq.store (ppq);

    const bool armed     = *apvts.getRawParameterValue ("recordArm") > 0.5f;
    const bool recording = armed && playing;
    recordingNow.store (recording);

    const int slot = posSamples >= 0
        ? (int) ((posSamples * (juce::int64) kSlotsPerSecond)
                    / (juce::int64) juce::jmax (1.0, currentSampleRate))
        : (int) (posSecs * kSlotsPerSecond);

    // ---- Start edge --------------------------------------------------------
    if (recording && ! prevRecording)
    {
        std::fill (captureEnvelope.begin(), captureEnvelope.end(), 0.0f);
        captureMaxSlot.store (-1);
        peakInEvent = false;
        peakEventMaxDb = -200.0f;
        newCaptureStarted.store (true);
    }

    // ---- Capture envelope + tag peaks --------------------------------------
    if (recording)
    {
        if (slot >= 0 && slot < kMaxSlots)
        {
            const int endSlot = (int) ((posSecs + blockSeconds) * kSlotsPerSecond);
            writeSlots (slot, juce::jmax (slot, endSlot), currentLevel);
            captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), juce::jmin (endSlot, kMaxSlots - 1)));
        }

        const float thrDb = peakThresholdDb();
        const float blockPeakDb = juce::Decibels::gainToDecibels (blockPeak, -120.0f);
        if (blockPeakDb >= thrDb)
        {
            if (! peakInEvent) { peakInEvent = true; peakEventMaxDb = -200.0f; }
            if (blockPeakDb > peakEventMaxDb)
            {
                peakEventMaxDb = blockPeakDb;
                PeakMark m;
                m.seconds = posSecs; m.ppq = ppq; m.db = blockPeakDb;
                computeBarBeat (ppq, num, den, m.bar, m.beat);
                peakEventMark = m;
            }
        }
        else if (peakInEvent && blockPeakDb < thrDb - 1.0f)   // 1 dB hysteresis
        {
            pushPeak (peakEventMark);
            peakInEvent = false;
        }
    }

    // ---- Stop edge ---------------------------------------------------------
    if (! recording && prevRecording)
    {
        if (peakInEvent) { pushPeak (peakEventMark); peakInEvent = false; }
        captureFinished.store (true);
    }
    prevRecording = recording;

    // ---- Ghost value -------------------------------------------------------
    recordedLevel.store ((slot >= 0 && slot < kMaxSlots) ? playbackEnvelope[(size_t) slot] : 0.0f);

    juce::ignoreUnused (buffer);   // pass-through
}

//==============================================================================
void TimelineVUAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackEnvelope.begin(), playbackEnvelope.end(), 0.0f);

    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& d = history[(size_t) activeIndex].data;
        const int n = juce::jmin ((int) d.size(), kMaxSlots);
        for (int i = 0; i < n; ++i)
            playbackEnvelope[(size_t) i] = d[(size_t) i];
        hasRecording.store (n > 0);
    }
    else { hasRecording.store (false); }
}

void TimelineVUAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0) { capturePeaks.clear(); return; }

    Recording take;
    take.name = name.trim().isNotEmpty() ? name.trim()
              : juce::String ("Take ") + juce::String (history.size() + 1);
    take.data.resize ((size_t) len);
    for (int i = 0; i < len; ++i)
        take.data[(size_t) i] = captureEnvelope[(size_t) i];
    take.peaks = capturePeaks;
    capturePeaks.clear();

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

    mos.writeInt (3);                          // format version
    mos.writeInt (kSlotsPerSecond);
    mos.writeInt (activeIndex);
    mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name);
        mos.writeInt ((int) r.data.size());
        if (! r.data.empty())
            mos.write (r.data.data(), r.data.size() * sizeof (float));

        mos.writeInt ((int) r.peaks.size());
        for (const auto& pk : r.peaks)
        {
            mos.writeDouble (pk.seconds);
            mos.writeDouble (pk.ppq);
            mos.writeFloat  (pk.db);
            mos.writeInt    (pk.bar);
            mos.writeInt    (pk.beat);
        }
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

    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 3)
    {
        mis.readInt();                          // slotsPerSecond context
        const int savedActive = mis.readInt();
        const int count = juce::jlimit (0, kMaxRecordings, mis.readInt());
        for (int i = 0; i < count; ++i)
        {
            Recording r;
            r.name = mis.readString();
            const int len = juce::jlimit (0, kMaxSlots, mis.readInt());
            r.data.resize ((size_t) len);
            if (len > 0)
                mis.read (r.data.data(), (int) (len * (int) sizeof (float)));

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
