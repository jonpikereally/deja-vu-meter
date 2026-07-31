#include "LUFSProcessor.h"
#include "LUFSEditor.h"
#include <cmath>

static inline double loudnessFromZ (double Z) { return Z > 0.0 ? -0.691 + 10.0 * std::log10 (Z) : -100.0; }

//==============================================================================
void DejaVULUFSAudioProcessor::Biquad::setHighPass (double fs, double f0, double Q)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 + c) / 2.0) / a0;
    b1 = (-(1.0 + c)) / a0;
    b2 = ((1.0 + c) / 2.0) / a0;
    a1 = (-2.0 * c) / a0;
    a2 = (1.0 - alpha) / a0;
    reset();
}

void DejaVULUFSAudioProcessor::Biquad::setHighShelf (double fs, double f0, double dBgain, double S)
{
    const double A = std::pow (10.0, dBgain / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    const double sqrtA = std::sqrt (A);
    const double a0 =        (A + 1.0) - (A - 1.0) * c + 2.0 * sqrtA * alpha;
    b0 =  A * ((A + 1.0) + (A - 1.0) * c + 2.0 * sqrtA * alpha) / a0;
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
    b2 =  A * ((A + 1.0) + (A - 1.0) * c - 2.0 * sqrtA * alpha) / a0;
    a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
    a2 =       ((A + 1.0) - (A - 1.0) * c - 2.0 * sqrtA * alpha) / a0;
    reset();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
DejaVULUFSAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "autoMode", 1 }, "Auto Record", false));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "minLen", 1 }, "Min Take Length",
        StringArray { "Off", "0.5 s", "1 s", "2 s", "5 s" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "target", 1 }, "Target",
        StringArray { "Off", "-14 (streaming)", "-16 (podcast)", "-23 (EBU R128)" }, 0));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "splitMeter", 1 }, "Split Meter", true));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "recordArm", 1 }, "Record Arm", false));
    return layout;
}

float DejaVULUFSAudioProcessor::targetLufs() const
{
    switch ((int) *apvts.getRawParameterValue ("target"))
    {
        case 1: return -14.0f;
        case 2: return -16.0f;
        case 3: return -23.0f;
        default: return -1000.0f;
    }
}

//==============================================================================
DejaVULUFSAudioProcessor::DejaVULUFSAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureEnv.assign  (kMaxSlots, -100.0f);
    playbackEnv.assign (kMaxSlots, -100.0f);
}

void DejaVULUFSAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    chunkLen = juce::jmax (1, (int) std::round (currentSampleRate * 0.1));
    chunkSum = 0.0; chunkSamples = 0; zCount = 0;
    zRing.fill (0.0);
    gatingBlocks.clear();
    prevRecording = false; prevPlaying = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        kShelf[(size_t) ch].setHighShelf (currentSampleRate, 1500.0, 4.0, 1.0);
        kHP[(size_t) ch].setHighPass (currentSampleRate, 38.0, 0.5);
    }

    osChannels = juce::jmax (1, getTotalNumInputChannels());
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) osChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
    oversampler->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
}

bool DejaVULUFSAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void DejaVULUFSAudioProcessor::pushChunk()
{
    const double Z = chunkSamples > 0 ? chunkSum / chunkSamples : 0.0;
    zRing[(size_t) (zCount % 30)] = Z;
    ++zCount;

    auto meanLast = [this] (int k) -> double
    {
        const int n = juce::jmin (k, zCount);
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += zRing[(size_t) ((zCount - 1 - i + 300) % 30)];
        return n > 0 ? sum / n : 0.0;
    };

    momentary.store ((float) loudnessFromZ (meanLast (4)));
    shortTerm.store ((float) loudnessFromZ (meanLast (30)));

    // Integrated (gated) — 400 ms block every 100 ms, accumulated while playing.
    if (transportPlaying.load())
    {
        const double blockZ = meanLast (4);
        if (loudnessFromZ (blockZ) > -70.0)
            gatingBlocks.push_back (blockZ);
        if ((int) gatingBlocks.size() > kMaxSlots) gatingBlocks.clear();

        double s1 = 0.0; int c1 = 0;
        for (double z : gatingBlocks) { s1 += z; ++c1; }
        if (c1 > 0)
        {
            const double relThresh = loudnessFromZ (s1 / c1) - 10.0;
            double s2 = 0.0; int c2 = 0;
            for (double z : gatingBlocks) if (loudnessFromZ (z) >= relThresh) { s2 += z; ++c2; }
            integrated.store ((float) (c2 > 0 ? loudnessFromZ (s2 / c2) : -100.0));
        }
    }

    chunkSum = 0.0; chunkSamples = 0;
}

void DejaVULUFSAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- True peak via oversampling ----------------------------------------
    float tp = 0.0f;
    if (oversampler != nullptr && numChannels == osChannels)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                tp = juce::jmax (tp, std::abs (up.getSample ((int) ch, (int) i)));
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            tp = juce::jmax (tp, buffer.getMagnitude (ch, 0, numSamples));
    }
    truePeakDb.store (juce::Decibels::gainToDecibels (tp, -100.0f));

    // ---- K-weighted loudness accumulation ----------------------------------
    for (int n = 0; n < numSamples; ++n)
    {
        double frameSum = 0.0;
        for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
        {
            const float x = buffer.getReadPointer (ch)[n];
            const float y = kHP[(size_t) ch].process (kShelf[(size_t) ch].process (x));
            frameSum += (double) y * y;
        }
        chunkSum += frameSum;
        if (++chunkSamples >= chunkLen)
            pushChunk();
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
            if (auto ts = info->getTimeSignature()) { timeSigNum.store (ts->numerator); timeSigDen.store (ts->denominator); }
        }

    if (playing && ! prevPlaying) { gatingBlocks.clear(); integrated.store (-100.0f); }
    prevPlaying = playing;

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
        std::fill (captureEnv.begin(), captureEnv.end(), -100.0f);
        captureMaxSlot.store (-1);
        int sBar, sBeat; computeBarBeat (ppq, num, den, sBar, sBeat);
        capStartSecs.store (posSecs); capStartBar.store (sBar); capStartBeat.store (sBeat);
    }
    if (recording && slot >= 0 && slot < kMaxSlots)
    {
        captureEnv[(size_t) slot] = shortTerm.load();
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), slot));
        int eBar, eBeat; computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds); capEndBar.store (eBar); capEndBeat.store (eBeat);
    }
    if (! recording && prevRecording) captureFinished.store (true);
    prevRecording = recording;

    recordedShortTerm.store ((slot >= 0 && slot < kMaxSlots) ? playbackEnv[(size_t) slot] : -100.0f);

    juce::ignoreUnused (buffer);
}

//==============================================================================
void DejaVULUFSAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
}

void DejaVULUFSAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackEnv.begin(), playbackEnv.end(), -100.0f);
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& d = history[(size_t) activeIndex].data;
        const int n = juce::jmin ((int) d.size(), kMaxSlots);
        for (int i = 0; i < n; ++i) playbackEnv[(size_t) i] = d[(size_t) i];
        hasRecording.store (n > 0);
    }
    else hasRecording.store (false);
}

void DejaVULUFSAudioProcessor::finalizeCapture (const juce::String& name)
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

void DejaVULUFSAudioProcessor::selectRecording (int index)
{
    if (juce::isPositiveAndBelow (index, (int) history.size())) { activeIndex = index; fillPlaybackFromActive(); }
}
void DejaVULUFSAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}
void DejaVULUFSAudioProcessor::deleteAllRecordings() { history.clear(); activeIndex = -1; fillPlaybackFromActive(); }

bool DejaVULUFSAudioProcessor::nameExists (const juce::String& n) const
{
    for (const auto& r : history) if (r.name == n) return true;
    return false;
}
juce::String DejaVULUFSAudioProcessor::makeUniqueName (const juce::String& base) const
{
    if (! nameExists (base)) return base;
    for (int i = 2; ; ++i) { auto c = base + " (" + juce::String (i) + ")"; if (! nameExists (c)) return c; }
}
juce::String DejaVULUFSAudioProcessor::defaultTakeName() const
{
    for (int k = 1; ; ++k) { auto c = "Take " + juce::String (k); if (! nameExists (c)) return c; }
}

//==============================================================================
juce::AudioProcessorEditor* DejaVULUFSAudioProcessor::createEditor() { return new DejaVULUFSAudioProcessorEditor (*this); }

void DejaVULUFSAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
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

void DejaVULUFSAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DejaVULUFSAudioProcessor(); }
