#include "SuiteProcessor.h"
#include "SuiteEditor.h"
#include <cmath>

static inline double loudnessFromZ (double Z) { return Z > 0.0 ? -0.691 + 10.0 * std::log10 (Z) : -100.0; }

//==============================================================================
void DejaVUSuiteAudioProcessor::Biquad::setHighPass (double fs, double f0, double Q)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * Q), a0 = 1.0 + alpha;
    b0 = ((1.0 + c) / 2.0) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) / 2.0) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0; reset();
}
void DejaVUSuiteAudioProcessor::Biquad::setHighShelf (double fs, double f0, double dBgain, double S)
{
    const double A = std::pow (10.0, dBgain / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0), sq = std::sqrt (A);
    const double a0 = (A + 1.0) - (A - 1.0) * c + 2.0 * sq * alpha;
    b0 =  A * ((A + 1.0) + (A - 1.0) * c + 2.0 * sq * alpha) / a0;
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
    b2 =  A * ((A + 1.0) + (A - 1.0) * c - 2.0 * sq * alpha) / a0;
    a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
    a2 =       ((A + 1.0) - (A - 1.0) * c - 2.0 * sq * alpha) / a0; reset();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
DejaVUSuiteAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "autoMode", 1 }, "Auto Record", false));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "minLen", 1 }, "Min Take Length",
        StringArray { "Off", "0.5 s", "1 s", "2 s", "5 s" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "meterMode", 1 }, "Meter Mode",
        StringArray { "VU", "Peak" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "target", 1 }, "Target",
        StringArray { "Off", "-14", "-16", "-23" }, 0));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "showMeter", 1 }, "Show Meter", true));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "showLUFS", 1 }, "Show LUFS", true));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "showSpectrum", 1 }, "Show Spectrum", true));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "showWidth", 1 }, "Show Width", true));
    layout.add (std::make_unique<AudioParameterBool>(ParameterID { "recordArm", 1 }, "Record Arm", false));
    return layout;
}

float DejaVUSuiteAudioProcessor::targetLufs() const
{
    switch ((int) *apvts.getRawParameterValue ("target")) { case 1: return -14.0f; case 2: return -16.0f; case 3: return -23.0f; default: return -1000.0f; }
}

//==============================================================================
DejaVUSuiteAudioProcessor::DejaVUSuiteAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    captureFrames.assign  ((size_t) kMaxSlots * kStride, 0.0f);
    playbackFrames.assign ((size_t) kMaxSlots * kStride, 0.0f);
    for (auto& a : bands)    a.store (0.0f);
    for (auto& a : recBands) a.store (0.0f);
    computeBands();
}

void DejaVUSuiteAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    smMsL = smMsR = 0.0; pkEnvL = pkEnvR = 0.0f;
    chunkLen = juce::jmax (1, (int) std::round (currentSampleRate * 0.1));
    chunkSum = 0.0; chunkSamples = 0; zCount = 0; zRing.fill (0.0); gatingBlocks.clear();
    prevPlaying = false; prevRecording = false;
    for (int ch = 0; ch < 2; ++ch) { kShelf[(size_t) ch].setHighShelf (currentSampleRate, 1500.0, 4.0, 1.0); kHP[(size_t) ch].setHighPass (currentSampleRate, 38.0, 0.5); }
    osChannels = juce::jmax (1, getTotalNumInputChannels());
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>((size_t) osChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
    oversampler->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
    writePos = 0; hopCounter = 0; fifo.fill (0.0f); bandLevel.fill (0.0f);
    corrS = 1.0; widthS = 0.0; balS = 0.0; gonioSub = 0;
    computeBands();
}

bool DejaVUSuiteAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void DejaVUSuiteAudioProcessor::computeBands()
{
    const double fLo = 20.0, fHi = juce::jmin (20000.0, currentSampleRate * 0.5 * 0.95);
    for (int b = 0; b < kBands; ++b)
    {
        const double e0 = fLo * std::pow (fHi / fLo, (double) b / kBands);
        const double e1 = fLo * std::pow (fHi / fLo, (double) (b + 1) / kBands);
        bandCentreHz[(size_t) b] = (float) std::sqrt (e0 * e1);
        int lo = juce::jlimit (1, kFftSize / 2 - 1, (int) std::floor (e0 * kFftSize / currentSampleRate));
        int hi = juce::jlimit (lo, kFftSize / 2 - 1, (int) std::ceil (e1 * kFftSize / currentSampleRate));
        binLo[(size_t) b] = lo; binHi[(size_t) b] = hi;
    }
}

void DejaVUSuiteAudioProcessor::doFFT()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < kFftSize; ++i) fftData[(size_t) i] = fifo[(size_t) ((writePos + i) % kFftSize)];
    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const float norm = 4.0f / (float) kFftSize;
    for (int b = 0; b < kBands; ++b)
    {
        float sum = 0.0f; const int lo = binLo[(size_t) b], hi = binHi[(size_t) b];
        for (int k = lo; k <= hi; ++k) sum += fftData[(size_t) k];
        float& s = bandLevel[(size_t) b];
        s += ((sum / (float) (hi - lo + 1)) * norm - s) * 0.6f;
        bands[(size_t) b].store (s);
    }
}

void DejaVUSuiteAudioProcessor::pushChunk()
{
    const double Z = chunkSamples > 0 ? chunkSum / chunkSamples : 0.0;
    zRing[(size_t) (zCount % 30)] = Z; ++zCount;
    auto meanLast = [this] (int k) -> double { const int n = juce::jmin (k, zCount); double s = 0.0; for (int i = 0; i < n; ++i) s += zRing[(size_t) ((zCount - 1 - i + 300) % 30)]; return n > 0 ? s / n : 0.0; };
    lufsMom.store ((float) loudnessFromZ (meanLast (4)));
    lufsShort.store ((float) loudnessFromZ (meanLast (30)));
    if (transportPlaying.load())
    {
        const double blockZ = meanLast (4);
        if (loudnessFromZ (blockZ) > -70.0) gatingBlocks.push_back (blockZ);
        if ((int) gatingBlocks.size() > kMaxSlots) gatingBlocks.clear();
        double s1 = 0.0; int c1 = 0; for (double z : gatingBlocks) { s1 += z; ++c1; }
        if (c1 > 0)
        {
            const double rel = loudnessFromZ (s1 / c1) - 10.0;
            double s2 = 0.0; int c2 = 0; for (double z : gatingBlocks) if (loudnessFromZ (z) >= rel) { s2 += z; ++c2; }
            lufsInteg.store ((float) (c2 > 0 ? loudnessFromZ (s2 / c2) : -100.0));
        }
    }
    chunkSum = 0.0; chunkSamples = 0;
}

void DejaVUSuiteAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float* L = numChannels > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* R = numChannels > 1 ? buffer.getReadPointer (1) : L;

    // True peak.
    float tp = 0.0f;
    if (oversampler != nullptr && numChannels == osChannels)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                tp = juce::jmax (tp, std::abs (up.getSample ((int) ch, (int) i)));
    }
    else for (int ch = 0; ch < numChannels; ++ch) tp = juce::jmax (tp, buffer.getMagnitude (ch, 0, numSamples));
    lufsTP.store (juce::Decibels::gainToDecibels (tp, -100.0f));

    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0, sumMM = 0.0, sumSS = 0.0;
    float  pkBlkL = 0.0f, pkBlkR = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        const float l = L ? L[n] : 0.0f, r = R ? R[n] : 0.0f;
        sumLL += (double) l * l; sumRR += (double) r * r; sumLR += (double) l * r;
        pkBlkL = juce::jmax (pkBlkL, std::abs (l)); pkBlkR = juce::jmax (pkBlkR, std::abs (r));

        const float yl = kHP[0].process (kShelf[0].process (l)), yr = kHP[1].process (kShelf[1].process (r));
        chunkSum += (double) yl * yl + (double) yr * yr;
        if (++chunkSamples >= chunkLen) pushChunk();

        const float mid = 0.70710678f * (l + r), side = 0.70710678f * (l - r);
        sumMM += (double) mid * mid; sumSS += (double) side * side;
        if (++gonioSub >= 2) { gonioSub = 0; const int w = gonioWrite.load(); gonio[(size_t) (2 * w)] = side; gonio[(size_t) (2 * w + 1)] = mid; gonioWrite.store ((w + 1) % kGonio); }

        fifo[(size_t) writePos] = 0.5f * (l + r);
        writePos = (writePos + 1) % kFftSize;
        if (++hopCounter >= kFftHop) { hopCounter = 0; doFFT(); }
    }

    const int N = juce::jmax (1, numSamples);
    const double blockSeconds = numSamples / currentSampleRate;

    // Meter.
    const double aVu = 1.0 - std::exp (-blockSeconds / 0.300);
    smMsL += (sumLL / N - smMsL) * aVu; smMsR += (sumRR / N - smMsR) * aVu;
    const float relP = (float) std::exp (-blockSeconds / 0.500);
    pkEnvL = juce::jmax (pkBlkL, pkEnvL * relP); pkEnvR = juce::jmax (pkBlkR, pkEnvR * relP);
    const bool peakMode = (int) *apvts.getRawParameterValue ("meterMode") == 1;
    vuL.store (peakMode ? pkEnvL : (float) std::sqrt (juce::jmax (0.0, smMsL)));
    vuR.store (peakMode ? pkEnvR : (float) std::sqrt (juce::jmax (0.0, smMsR)));
    vuPeakL.store (pkEnvL); vuPeakR.store (pkEnvR);

    // Width.
    const double rmsL = std::sqrt (sumLL / N), rmsR = std::sqrt (sumRR / N), rmsM = std::sqrt (sumMM / N), rmsS = std::sqrt (sumSS / N);
    const double bCorr = std::sqrt (sumLL * sumRR) > 1.0e-12 ? sumLR / std::sqrt (sumLL * sumRR) : 1.0;
    const double bWidth = (rmsM + rmsS) > 1.0e-9 ? rmsS / (rmsM + rmsS) : 0.0;
    const double bBal = (rmsL + rmsR) > 1.0e-9 ? (rmsR - rmsL) / (rmsR + rmsL) : 0.0;
    const double aW = 1.0 - std::exp (-blockSeconds / 0.150);
    corrS += (bCorr - corrS) * aW; widthS += (bWidth - widthS) * aW; balS += (bBal - balS) * aW;
    corr.store ((float) corrS); width.store ((float) widthS); balance.store ((float) balS);

    // ---- Host timeline -----------------------------------------------------
    bool   playing = false; double posSecs = 0.0, ppq = 0.0; juce::int64 posSamples = -1;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto s = info->getTimeInSamples()) posSamples = *s;
            if (auto t = info->getTimeInSeconds()) posSecs = *t; else if (posSamples >= 0) posSecs = (double) posSamples / currentSampleRate;
            if (auto q = info->getPpqPosition()) ppq = *q;
            if (auto ts = info->getTimeSignature()) { timeSigNum.store (ts->numerator); timeSigDen.store (ts->denominator); }
        }
    if (playing && ! prevPlaying) { gatingBlocks.clear(); lufsInteg.store (-100.0f); }
    prevPlaying = playing;

    const int num = timeSigNum.load(), den = timeSigDen.load();
    transportPlaying.store (playing); playheadSeconds.store (posSecs); playheadPpq.store (ppq);

    const bool armed = *apvts.getRawParameterValue ("recordArm") > 0.5f;
    const bool autoMode = *apvts.getRawParameterValue ("autoMode") > 0.5f;
    const bool recording = (armed || autoMode) && playing;
    recordingNow.store (recording);

    const int slot = posSamples >= 0
        ? (int) ((posSamples * (juce::int64) kSlotsPerSecond) / (juce::int64) juce::jmax (1.0, currentSampleRate))
        : (int) (posSecs * kSlotsPerSecond);

    if (recording && ! prevRecording)
    {
        std::fill (captureFrames.begin(), captureFrames.end(), 0.0f);
        captureMaxSlot.store (-1);
        int sBar, sBeat; computeBarBeat (ppq, num, den, sBar, sBeat);
        capStartSecs.store (posSecs); capStartBar.store (sBar); capStartBeat.store (sBeat);
    }
    if (recording && slot >= 0 && slot < kMaxSlots)
    {
        float* dst = &captureFrames[(size_t) slot * kStride];
        dst[OFF_VUL] = vuL.load(); dst[OFF_VUR] = vuR.load();
        dst[OFF_LUFS] = lufsShort.load(); dst[OFF_WIDTH] = (float) widthS;
        for (int b = 0; b < kBands; ++b) dst[OFF_BANDS + b] = bandLevel[(size_t) b];
        captureMaxSlot.store (juce::jmax (captureMaxSlot.load(), slot));
        int eBar, eBeat; computeBarBeat (ppq, num, den, eBar, eBeat);
        capEndSecs.store (posSecs + blockSeconds); capEndBar.store (eBar); capEndBeat.store (eBeat);
    }
    if (! recording && prevRecording) captureFinished.store (true);
    prevRecording = recording;

    // Ghost.
    if (slot >= 0 && slot < kMaxSlots)
    {
        const float* src = &playbackFrames[(size_t) slot * kStride];
        recVuL.store (src[OFF_VUL]); recVuR.store (src[OFF_VUR]);
        recLufs.store (src[OFF_LUFS]); recWidth.store (src[OFF_WIDTH]);
        for (int b = 0; b < kBands; ++b) recBands[(size_t) b].store (src[OFF_BANDS + b]);
    }
    else
    {
        recVuL.store (0.0f); recVuR.store (0.0f); recLufs.store (-100.0f); recWidth.store (0.0f);
        for (int b = 0; b < kBands; ++b) recBands[(size_t) b].store (0.0f);
    }

    juce::ignoreUnused (buffer);
}

//==============================================================================
void DejaVUSuiteAudioProcessor::computeBarBeat (double ppq, int num, int den, int& bar, int& beat)
{
    den = juce::jmax (1, den);
    const double qPerBar = juce::jmax (0.25, num * 4.0 / den);
    bar  = (int) std::floor (ppq / qPerBar) + 1;
    beat = (int) std::floor ((ppq - (bar - 1) * qPerBar) / (4.0 / den)) + 1;
}

void DejaVUSuiteAudioProcessor::fillPlaybackFromActive()
{
    std::fill (playbackFrames.begin(), playbackFrames.end(), 0.0f);
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        const auto& r = history[(size_t) activeIndex];
        const int n = juce::jmin (r.numSlots * kStride, (int) playbackFrames.size());
        for (int i = 0; i < n && i < (int) r.frames.size(); ++i) playbackFrames[(size_t) i] = r.frames[(size_t) i];
        hasRecording.store (r.numSlots > 0);
    }
    else hasRecording.store (false);
}

void DejaVUSuiteAudioProcessor::finalizeCapture (const juce::String& name)
{
    const int len = captureMaxSlot.load() + 1;
    if (len <= 0) return;
    static const float minTable[] = { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f };
    const float minLen = minTable[juce::jlimit (0, 4, (int) *apvts.getRawParameterValue ("minLen"))];
    if ((capEndSecs.load() - capStartSecs.load()) < (double) minLen) return;

    Recording take;
    take.name = name.trim().isNotEmpty() ? makeUniqueName (name.trim()) : defaultTakeName();
    take.numSlots = len;
    take.frames.resize ((size_t) len * kStride);
    std::copy (captureFrames.begin(), captureFrames.begin() + (size_t) len * kStride, take.frames.begin());
    take.startSeconds = capStartSecs.load(); take.endSeconds = capEndSecs.load();
    take.startBar = capStartBar.load(); take.startBeat = capStartBeat.load();
    take.endBar = capEndBar.load(); take.endBeat = capEndBeat.load();
    history.insert (history.begin(), std::move (take));
    if ((int) history.size() > kMaxRecordings) history.resize (kMaxRecordings);
    activeIndex = 0; fillPlaybackFromActive();
}

void DejaVUSuiteAudioProcessor::selectRecording (int index)
{ if (juce::isPositiveAndBelow (index, (int) history.size())) { activeIndex = index; fillPlaybackFromActive(); } }
void DejaVUSuiteAudioProcessor::deleteActiveRecording()
{
    if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
    {
        history.erase (history.begin() + activeIndex);
        activeIndex = history.empty() ? -1 : juce::jmin (activeIndex, (int) history.size() - 1);
        fillPlaybackFromActive();
    }
}
void DejaVUSuiteAudioProcessor::deleteAllRecordings() { history.clear(); activeIndex = -1; fillPlaybackFromActive(); }
void DejaVUSuiteAudioProcessor::renameActiveRecording (const juce::String& newName)
{
    if (! juce::isPositiveAndBelow (activeIndex, (int) history.size())) return;
    const auto trimmed = newName.trim(); if (trimmed.isEmpty()) return;
    auto existsOther = [this] (const juce::String& n) { for (int i = 0; i < (int) history.size(); ++i) if (i != activeIndex && history[(size_t) i].name == n) return true; return false; };
    juce::String candidate = trimmed;
    if (existsOther (candidate)) for (int k = 2; ; ++k) { auto c = trimmed + " (" + juce::String (k) + ")"; if (! existsOther (c)) { candidate = c; break; } }
    history[(size_t) activeIndex].name = candidate;
}

bool DejaVUSuiteAudioProcessor::nameExists (const juce::String& n) const { for (const auto& r : history) if (r.name == n) return true; return false; }
juce::String DejaVUSuiteAudioProcessor::makeUniqueName (const juce::String& base) const { if (! nameExists (base)) return base; for (int i = 2; ; ++i) { auto c = base + " (" + juce::String (i) + ")"; if (! nameExists (c)) return c; } }
juce::String DejaVUSuiteAudioProcessor::defaultTakeName() const { for (int k = 1; ; ++k) { auto c = "Take " + juce::String (k); if (! nameExists (c)) return c; } }

//==============================================================================
juce::AudioProcessorEditor* DejaVUSuiteAudioProcessor::createEditor() { return new DejaVUSuiteAudioProcessorEditor (*this); }

void DejaVUSuiteAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    if (auto xml = apvts.copyState().createXml()) { const auto s = xml->toString(); mos.writeInt ((int) s.getNumBytesAsUTF8()); mos.write (s.toRawUTF8(), s.getNumBytesAsUTF8()); }
    else mos.writeInt (0);
    mos.writeInt (1); mos.writeInt (kStride); mos.writeInt (activeIndex); mos.writeInt ((int) history.size());
    for (const auto& r : history)
    {
        mos.writeString (r.name); mos.writeInt (r.numSlots);
        if (! r.frames.empty()) mos.write (r.frames.data(), r.frames.size() * sizeof (float));
        mos.writeDouble (r.startSeconds); mos.writeDouble (r.endSeconds);
        mos.writeInt (r.startBar); mos.writeInt (r.startBeat); mos.writeInt (r.endBar); mos.writeInt (r.endBeat);
    }
}

void DejaVUSuiteAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream mis (data, (size_t) sizeInBytes, false);
    const int xmlLen = mis.readInt();
    if (xmlLen > 0) { juce::MemoryBlock xmlBytes; mis.readIntoMemoryBlock (xmlBytes, xmlLen); if (auto xml = juce::parseXML (xmlBytes.toString())) apvts.replaceState (juce::ValueTree::fromXml (*xml)); }
    history.clear(); activeIndex = -1;
    if (mis.getNumBytesRemaining() >= 4 && mis.readInt() == 1)
    {
        const int stride = mis.readInt();
        const int savedActive = mis.readInt();
        const int count = juce::jlimit (0, kMaxRecordings, mis.readInt());
        for (int i = 0; i < count; ++i)
        {
            Recording r; r.name = mis.readString(); r.numSlots = juce::jlimit (0, kMaxSlots, mis.readInt());
            const int n = r.numSlots * stride;
            if (n > 0 && stride == kStride) { r.frames.resize ((size_t) n); mis.read (r.frames.data(), (int) (n * (int) sizeof (float))); }
            else if (n > 0) { mis.skipNextBytes ((juce::int64) n * (int) sizeof (float)); r.numSlots = 0; }
            r.startSeconds = mis.readDouble(); r.endSeconds = mis.readDouble();
            r.startBar = mis.readInt(); r.startBeat = mis.readInt(); r.endBar = mis.readInt(); r.endBeat = mis.readInt();
            history.push_back (std::move (r));
        }
        activeIndex = juce::isPositiveAndBelow (savedActive, (int) history.size()) ? savedActive : (history.empty() ? -1 : 0);
    }
    fillPlaybackFromActive();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DejaVUSuiteAudioProcessor(); }
