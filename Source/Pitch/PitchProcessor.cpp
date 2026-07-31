#include "PitchProcessor.h"
#include "PitchEditor.h"
#include <cmath>

//==============================================================================
juce::String DejaVUPitchAudioProcessor::noteName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0) midi = 0;
    return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout
DejaVUPitchAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "autoMode", 1 }, "Auto Record", false));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "minLen", 1 }, "Min Take Length",
        StringArray { "Off", "0.5 s", "1 s", "2 s", "5 s" }, 0));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "recordArm", 1 }, "Record Arm", false));
    return layout;
}

//==============================================================================
DejaVUPitchAudioProcessor::DejaVUPitchAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureEnv.assign  (kMaxSlots, 0.0f);
    playbackEnv.assign (kMaxSlots, 0.0f);
}

void DejaVUPitchAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    writePos = 0; hopCounter = 0; prevRecording = false;
    curMidi = 0.0f; curVoiced = false;
    ring.fill (0.0f);
    minLag = juce::jmax (2, (int) (currentSampleRate / 1000.0));   // up to 1000 Hz
    maxLag = juce::jmin (kWindow / 2, (int) (currentSampleRate / 50.0)); // down to 50 Hz
}

bool DejaVUPitchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void DejaVUPitchAudioProcessor::detect()
{
    // Copy ring into work buffer, oldest-first.
    for (int i = 0; i < kWindow; ++i)
        work[(size_t) i] = ring[(size_t) ((writePos + i) % kWindow)];

    const int N = kWindow - maxLag;
    if (N <= 0) { curVoiced = false; return; }

    double e0 = 0.0;
    for (int i = 0; i < N; ++i) e0 += (double) work[(size_t) i] * work[(size_t) i];

    // Silence gate.
    if (e0 / N < 1.0e-6)
    {
        curVoiced = false;
        liveVoiced.store (false);
        return;
    }

    double bestR = 0.0; int bestLag = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0, eLag = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double a = work[(size_t) i], b = work[(size_t) (i + lag)];
            ac += a * b; eLag += b * b;
        }
        const double denom = std::sqrt (e0 * eLag);
        const double r = denom > 0.0 ? ac / denom : 0.0;
        if (r > bestR) { bestR = r; bestLag = lag; }
    }

    if (bestLag < 1 || bestR < 0.6)
    {
        curVoiced = false;
        liveVoiced.store (false);
        return;
    }

    // Parabolic interpolation around the best lag.
    double refined = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        auto rAt = [&] (int lag)
        {
            double ac = 0.0, eLag = 0.0;
            for (int i = 0; i < N; ++i) { const double a = work[(size_t) i], b = work[(size_t) (i + lag)]; ac += a * b; eLag += b * b; }
            const double d = std::sqrt (e0 * eLag);
            return d > 0.0 ? ac / d : 0.0;
        };
        const double rm = rAt (bestLag - 1), r0 = bestR, rp = rAt (bestLag + 1);
        const double denom = (rm - 2.0 * r0 + rp);
        if (std::abs (denom) > 1.0e-9) refined = bestLag + 0.5 * (rm - rp) / denom;
    }

    const double freq = currentSampleRate / refined;
    const float midi = 69.0f + 12.0f * std::log2 ((float) freq / 440.0f);
    curMidi = midi; curVoiced = true;
    liveVoiced.store (true);
    liveMidi.store (midi);
    liveFreq.store ((float) freq);
}

void DejaVUPitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int n = 0; n < numSamples; ++n)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) mono += buffer.getReadPointer (ch)[n];
        mono /= (float) juce::jmax (1, numChannels);
        ring[(size_t) writePos] = mono;
        writePos = (writePos + 1) % kWindow;
        if (++hopCounter >= kHop) { hopCounter = 0; detect(); }
    }

    const double blockSeconds = numSamples > 0 ? numSamples / currentSampleRate : 0.0;

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
            if (auto ts = info->getTimeSignature()) { timeSigNum.store (ts->numerator); timeSigDen.store (ts->denominator); }
        }

    const int num = timeSigNum.load(), den = timeSigDen.load();
    transportPlaying.store (playing);
    playheadSeconds.store (posSecs);
    playheadPpq.store (ppq);

    const bool armed     = *apvts.getRawParameterValue ("recordArm") > 0.5f;
    const bool autoMode  = *apvts.getRawParameterValue ("autoMode")  > 0.5f;
    const bool recording = (armed || autoMode) && playing;
    recordingNow.store (recording);

    const int slot = posSamples >= 0
        ? (int) ((posSamples * (juce::int64) kSlotsPerSecond) / (juce::int64) juce::jmax (1.0, currentSampleRate))
        : (int) (posSecs * kSlotsPerSecond);

    if (recording && ! prevRecording)
    {
        std::fill (captureEnv.begin(), captureEnv.end(), 0.0f);
        captureMaxSlot.store (-1);
        int sBar, sBeat; computeBarBeat (ppq, num, den, sBar, sBeat);
        capStartSecs.store (posSecs); capStartBar.store (sBar); capStartBeat.store (sBeat);
    }
    if (recording && slot >= 0 && slot < kMaxSlots)
    {
        captureEnv[(size_t) slot] = curVoiced ? curMidi : 0.0f;
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), slot));
        int eBar, eBeat; computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds); capEndBar.store (eBar); capEndBeat.store (eBeat);
    }
    if (! recording && prevRecording) captureFinished.store (true);
    prevRecording = recording;

    recordedMidi.store ((slot >= 0 && slot < kMaxSlots) ? playbackEnv[(size_t) slot] : 0.0f);

    juce::ignoreUnused (buffer);
}

//==============================================================================
void DejaVUPitchAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
}

void DejaVUPitchAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackEnv.begin(), playbackEnv.end(), 0.0f);
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& d = history[(size_t) activeIndex].data;
        const int n = juce::jmin ((int) d.size(), kMaxSlots);
        for (int i = 0; i < n; ++i) playbackEnv[(size_t) i] = d[(size_t) i];
        hasRecording.store (n > 0);
    }
    else hasRecording.store (false);
}

void DejaVUPitchAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0) return;
    static const float minTable[] = { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f };
    const float minLen = minTable[juce::jlimit (0, 4, (int) *apvts.getRawParameterValue ("minLen"))];
    if ((capEndSecs.load() - capStartSecs.load()) < (double) minLen) return;

    Recording take;
    take.name = name.trim().isNotEmpty() ? makeUniqueName (name.trim()) : defaultTakeName();
    take.data.assign (captureEnv.begin(), captureEnv.begin() + len);
    take.startSeconds = capStartSecs.load(); take.endSeconds = capEndSecs.load();
    take.startBar = capStartBar.load(); take.startBeat = capStartBeat.load();
    take.endBar = capEndBar.load(); take.endBeat = capEndBeat.load();
    history.insert (history.begin(), std::move (take));
    if ((int) history.size() > kMaxRecordings) history.resize (kMaxRecordings);
    activeIndex = 0;
    fillPlaybackFromActive();
}

void DejaVUPitchAudioProcessor::selectRecording (int index)
{ if (juce::isPositiveAndBelow (index, (int) history.size())) { activeIndex = index; fillPlaybackFromActive(); } }
void DejaVUPitchAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}
void DejaVUPitchAudioProcessor::deleteAllRecordings() { history.clear(); activeIndex = -1; fillPlaybackFromActive(); }

bool DejaVUPitchAudioProcessor::nameExists (const juce::String& n) const
{ for (const auto& r : history) if (r.name == n) return true; return false; }
juce::String DejaVUPitchAudioProcessor::makeUniqueName (const juce::String& base) const
{ if (! nameExists (base)) return base; for (int i = 2; ; ++i) { auto c = base + " (" + juce::String (i) + ")"; if (! nameExists (c)) return c; } }
juce::String DejaVUPitchAudioProcessor::defaultTakeName() const
{ for (int k = 1; ; ++k) { auto c = "Take " + juce::String (k); if (! nameExists (c)) return c; } }

//==============================================================================
juce::AudioProcessorEditor* DejaVUPitchAudioProcessor::createEditor() { return new DejaVUPitchAudioProcessorEditor (*this); }

void DejaVUPitchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    if (auto xml = apvts.copyState().createXml())
    {
        const auto s = xml->toString();
        mos.writeInt ((int) s.getNumBytesAsUTF8());
        mos.write (s.toRawUTF8(), s.getNumBytesAsUTF8());
    }
    else mos.writeInt (0);
    mos.writeInt (1);
    mos.writeInt (activeIndex);
    mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name);
        mos.writeInt ((int) r.data.size());
        if (! r.data.empty()) mos.write (r.data.data(), r.data.size() * sizeof (float));
        mos.writeDouble (r.startSeconds); mos.writeDouble (r.endSeconds);
        mos.writeInt (r.startBar); mos.writeInt (r.startBeat); mos.writeInt (r.endBar); mos.writeInt (r.endBeat);
    }
}

void DejaVUPitchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream mis (data, (size_t) sizeInBytes, false);
    const int xmlLen = mis.readInt();
    if (xmlLen > 0)
    {
        juce::MemoryBlock xmlBytes;
        mis.readIntoMemoryBlock (xmlBytes, xmlLen);
        if (auto xml = juce::parseXML (xmlBytes.toString())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
    history.clear(); activeIndex = -1;
    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 1)
    {
        const int savedActive = mis.readInt();
        const int count = juce::jlimit (0, kMaxRecordings, mis.readInt());
        for (int i = 0; i < count; ++i)
        {
            Recording r;
            r.name = mis.readString();
            const int len = juce::jlimit (0, kMaxSlots, mis.readInt());
            r.data.resize ((size_t) len);
            if (len > 0) mis.read (r.data.data(), (int) (len * (int) sizeof (float)));
            r.startSeconds = mis.readDouble(); r.endSeconds = mis.readDouble();
            r.startBar = mis.readInt(); r.startBeat = mis.readInt(); r.endBar = mis.readInt(); r.endBeat = mis.readInt();
            history.push_back (std::move (r));
        }
        activeIndex = juce::isPositiveAndBelow (savedActive, (int) history.size()) ? savedActive : (history.empty() ? -1 : 0);
    }
    fillPlaybackFromActive();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DejaVUPitchAudioProcessor(); }
