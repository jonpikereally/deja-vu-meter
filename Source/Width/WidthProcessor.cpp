#include "WidthProcessor.h"
#include "WidthEditor.h"
#include <cmath>

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
DejaVUWidthAudioProcessor::createLayout()
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
DejaVUWidthAudioProcessor::DejaVUWidthAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureEnv.assign  (kMaxSlots, 0.0f);
    playbackEnv.assign (kMaxSlots, 0.0f);
}

void DejaVUWidthAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    corrS = 1.0; widthS = 0.0; balS = 0.0;
    prevRecording = false; gonioSub = 0;
}

bool DejaVUWidthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void DejaVUWidthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float* L = numChannels > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* R = numChannels > 1 ? buffer.getReadPointer (1) : L;

    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0, sumMM = 0.0, sumSS = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const float l = L ? L[n] : 0.0f;
        const float r = R ? R[n] : 0.0f;
        const float mid  = 0.70710678f * (l + r);
        const float side = 0.70710678f * (l - r);
        sumLL += (double) l * l; sumRR += (double) r * r; sumLR += (double) l * r;
        sumMM += (double) mid * mid; sumSS += (double) side * side;

        if (++gonioSub >= 2)
        {
            gonioSub = 0;
            const int w = gonioWrite.load();
            gonio[(size_t) (2 * w)]     = side;
            gonio[(size_t) (2 * w + 1)] = mid;
            gonioWrite.store ((w + 1) % kGonio);
        }
    }

    const int N = juce::jmax (1, numSamples);
    const double rmsL = std::sqrt (sumLL / N), rmsR = std::sqrt (sumRR / N);
    const double rmsM = std::sqrt (sumMM / N), rmsS = std::sqrt (sumSS / N);

    const double denomCorr = std::sqrt (sumLL * sumRR);
    const double blockCorr = denomCorr > 1.0e-12 ? sumLR / denomCorr : 1.0;
    const double blockWidth = (rmsM + rmsS) > 1.0e-9 ? rmsS / (rmsM + rmsS) : 0.0;
    const double blockBal   = (rmsL + rmsR) > 1.0e-9 ? (rmsR - rmsL) / (rmsR + rmsL) : 0.0;

    const double blockSeconds = numSamples / currentSampleRate;
    const double alpha = 1.0 - std::exp (-blockSeconds / 0.150);
    corrS  += (blockCorr  - corrS)  * alpha;
    widthS += (blockWidth - widthS) * alpha;
    balS   += (blockBal   - balS)   * alpha;

    liveCorr.store ((float) corrS);
    liveWidth.store ((float) widthS);
    liveBalance.store ((float) balS);

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
        captureEnv[(size_t) slot] = (float) widthS;
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), slot));
        int eBar, eBeat; computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds); capEndBar.store (eBar); capEndBeat.store (eBeat);
    }
    if (! recording && prevRecording) captureFinished.store (true);
    prevRecording = recording;

    recordedWidth.store ((slot >= 0 && slot < kMaxSlots) ? playbackEnv[(size_t) slot] : 0.0f);

    juce::ignoreUnused (buffer);
}

//==============================================================================
void DejaVUWidthAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
}

void DejaVUWidthAudioProcessor::fillPlaybackFromActive()
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

void DejaVUWidthAudioProcessor::finalizeCapture (const juce::String& name)
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

void DejaVUWidthAudioProcessor::selectRecording (int index)
{ if (juce::isPositiveAndBelow (index, (int) history.size())) { activeIndex = index; fillPlaybackFromActive(); } }
void DejaVUWidthAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}
void DejaVUWidthAudioProcessor::deleteAllRecordings() { history.clear(); activeIndex = -1; fillPlaybackFromActive(); }

void DejaVUWidthAudioProcessor::renameActiveRecording (const juce::String& newName)
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

bool DejaVUWidthAudioProcessor::nameExists (const juce::String& n) const
{ for (const auto& r : history) if (r.name == n) return true; return false; }
juce::String DejaVUWidthAudioProcessor::makeUniqueName (const juce::String& base) const
{ if (! nameExists (base)) return base; for (int i = 2; ; ++i) { auto c = base + " (" + juce::String (i) + ")"; if (! nameExists (c)) return c; } }
juce::String DejaVUWidthAudioProcessor::defaultTakeName() const
{ for (int k = 1; ; ++k) { auto c = "Take " + juce::String (k); if (! nameExists (c)) return c; } }

//==============================================================================
juce::AudioProcessorEditor* DejaVUWidthAudioProcessor::createEditor() { return new DejaVUWidthAudioProcessorEditor (*this); }

void DejaVUWidthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
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

void DejaVUWidthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DejaVUWidthAudioProcessor(); }
