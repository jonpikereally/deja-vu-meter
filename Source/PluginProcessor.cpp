#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
TimelineVUAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "meterMode", 1 }, "Meter Mode",
        StringArray { "VU", "Peak" }, 0));

    layout.add (std::make_unique<AudioParameterChoice>(
        ParameterID { "meterSkin", 1 }, "Meter Skin",
        StringArray { "Bar", "Analog" }, 1));

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
}

bool TimelineVUAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void TimelineVUAudioProcessor::writeSlots (int fromSlot, int toSlot, float value) noexcept
{
    fromSlot = juce::jlimit (0, kMaxSlots - 1, fromSlot);
    toSlot   = juce::jlimit (0, kMaxSlots - 1, toSlot);
    for (int s = fromSlot; s <= toSlot; ++s)
        captureEnvelope[(size_t) s] = value;
}

void TimelineVUAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- Measure this block: mono sum of channels ----------------------------
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

    // VU: ~300 ms RMS ballistics.
    const double alpha = 1.0 - std::exp (-blockSeconds / 0.300);
    smoothedMeanSquare += (blockMeanSquare - smoothedMeanSquare) * alpha;
    const float vuValue = (float) std::sqrt (juce::jmax (0.0, smoothedMeanSquare));

    // Peak: instant attack, ~500 ms release.
    const float peakRelease = (float) std::exp (-blockSeconds / 0.500);
    peakEnvelope = juce::jmax (blockPeak, peakEnvelope * peakRelease);

    const auto mode = (MeterMode) (int) *apvts.getRawParameterValue ("meterMode");
    const float currentLevel = (mode == VU) ? vuValue : peakEnvelope;
    liveLevel.store (currentLevel);

    // ---- Timeline position from the host ------------------------------------
    bool   playing    = false;
    double posSecs    = 0.0;
    double ppq        = 0.0;
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

    // ---- Capture, with start/stop edge detection ----------------------------
    if (recording && ! prevRecording)
    {
        // Start of a new take: clear the capture buffer.
        std::fill (captureEnvelope.begin(), captureEnvelope.end(), 0.0f);
        captureMaxSlot.store (-1);
    }

    if (recording && slot >= 0 && slot < kMaxSlots)
    {
        const int endSlot = (int) ((posSecs + blockSeconds) * kSlotsPerSecond);
        writeSlots (slot, juce::jmax (slot, endSlot), currentLevel);
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), juce::jmin (endSlot, kMaxSlots - 1)));
    }

    if (! recording && prevRecording)
        captureFinished.store (true);   // editor will finalise + disarm

    prevRecording = recording;

    // ---- Ghost value for the GUI (active stored take) -----------------------
    if (slot >= 0 && slot < kMaxSlots)
        recordedLevel.store (playbackEnvelope[(size_t) slot]);
    else
        recordedLevel.store (0.0f);

    juce::ignoreUnused (buffer);   // pass-through
}

//==============================================================================
void TimelineVUAudioProcessor::fillPlaybackFromActive()
{
    // Message thread. Benign float tearing vs the audio reader is acceptable
    // for a meter (fixed size, no reallocation).
    std::fill (playbackEnvelope.begin(), playbackEnvelope.end(), 0.0f);

    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& d = history[(size_t) activeIndex].data;
        const int n = juce::jmin ((int) d.size(), kMaxSlots);
        for (int i = 0; i < n; ++i)
            playbackEnvelope[(size_t) i] = d[(size_t) i];
        hasRecording.store (n > 0);
    }
    else
    {
        hasRecording.store (false);
    }
}

void TimelineVUAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0)
        return;   // nothing was captured

    Recording take;
    take.name = name.trim().isNotEmpty() ? name.trim()
              : juce::String ("Take ") + juce::String (history.size() + 1);
    take.data.resize ((size_t) len);
    for (int i = 0; i < len; ++i)
        take.data[(size_t) i] = captureEnvelope[(size_t) i];

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

    mos.writeInt (2);                          // format version
    mos.writeInt (kSlotsPerSecond);            // context
    mos.writeInt (activeIndex);
    mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name);
        mos.writeInt ((int) r.data.size());
        if (! r.data.empty())
            mos.write (r.data.data(), r.data.size() * sizeof (float));
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

    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 2)
    {
        mis.readInt();                         // slotsPerSecond (context)
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
