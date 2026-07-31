#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU Spectrum — pass-through FFT spectrum analyser with timeline recording,
// ghost playback, peak tagging + Peaks Monitor, and vertical (dB) zoom.
//==============================================================================
class DejaVUSpectrumAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kFftOrder       = 11;
    static constexpr int kFftSize        = 1 << kFftOrder;    // 2048
    static constexpr int kFftHop         = kFftSize / 2;      // 50% overlap
    static constexpr int kBands          = 31;
    static constexpr int kSlotsPerSecond = 30;
    static constexpr int kMaxSeconds     = 1800;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;
    static constexpr int kPeakFifoSize   = 256;

    struct PeakMark
    {
        double seconds { 0.0 };
        double ppq     { 0.0 };
        float  db      { 0.0f };
        int    bar     { 1 };
        int    beat    { 1 };
        bool   monitor { false };
    };

    struct Recording
    {
        juce::String          name;
        std::vector<float>    frames;   // numSlots * kBands
        int                   numSlots { 0 };
        std::vector<PeakMark> peaks;
        double startSeconds { 0.0 }, endSeconds { 0.0 };
        int    startBar { 1 }, startBeat { 1 }, endBar { 1 }, endBeat { 1 };
    };

    struct TakeInfo
    {
        double startSeconds { 0.0 }, endSeconds { 0.0 };
        int    startBar { 1 }, startBeat { 1 }, endBar { 1 }, endBeat { 1 };
        bool   valid { false };
    };

    //==========================================================================
    DejaVUSpectrumAudioProcessor();
    ~DejaVUSpectrumAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU Spectrum"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::array<std::atomic<float>, kBands> liveBands;
    std::array<std::atomic<float>, kBands> recBands;

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    float getBandCentreHz (int b) const { return b >= 0 && b < kBands ? bandCentreHz[(size_t) b] : 0.0f; }

    //==========================================================================
    void drainPeaks();
    void finalizeCapture (const juce::String& name);
    void selectRecording (int index);
    void deleteActiveRecording();
    void deleteAllRecordings();

    int          getNumRecordings() const { return (int) history.size(); }
    int          getActiveRecording() const { return activeIndex; }
    juce::String getRecordingName (int i) const
    {
        return juce::isPositiveAndBelow (i, (int) history.size()) ? history[(size_t) i].name : juce::String();
    }
    TakeInfo getRecordingInfo (int i) const
    {
        TakeInfo t;
        if (juce::isPositiveAndBelow (i, (int) history.size()))
        {
            const auto& r = history[(size_t) i];
            t = { r.startSeconds, r.endSeconds, r.startBar, r.startBeat, r.endBar, r.endBeat, true };
        }
        return t;
    }

    int getNumPeaks() const
    {
        return juce::isPositiveAndBelow (activeIndex, (int) history.size())
                 ? (int) history[(size_t) activeIndex].peaks.size() : 0;
    }
    PeakMark getPeak (int i) const
    {
        if (juce::isPositiveAndBelow (activeIndex, (int) history.size()))
        {
            const auto& pk = history[(size_t) activeIndex].peaks;
            if (juce::isPositiveAndBelow (i, (int) pk.size())) return pk[(size_t) i];
        }
        return {};
    }
    int      getNumMonitorPeaks() const { return (int) monitorPeaks.size(); }
    PeakMark getMonitorPeak (int i) const
    {
        return juce::isPositiveAndBelow (i, (int) monitorPeaks.size()) ? monitorPeaks[(size_t) i] : PeakMark{};
    }
    int getMonitorRev() const { return monitorRev; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    double currentSampleRate { 44100.0 };

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize,
        juce::dsp::WindowingFunction<float>::hann };

    std::array<float, kFftSize>     fifo {};
    int                             writePos { 0 };
    int                             hopCounter { 0 };
    std::array<float, 2 * kFftSize> fftData {};
    std::array<float, kBands>       bandLevel {};
    std::array<float, kBands>       bandCentreHz {};
    std::array<int, kBands>         binLo {}, binHi {};

    std::vector<float> captureFrames, playbackFrames;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };

    std::vector<Recording> history;
    int                    activeIndex { -1 };

    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    // Peak tagging.
    std::vector<PeakMark>               capturePeaks, monitorPeaks;
    int                                 monitorRev { 0 };
    std::array<PeakMark, kPeakFifoSize> peakFifoBuf;
    juce::AbstractFifo                  peakFifo { kPeakFifoSize };
    std::atomic<bool>                   newCaptureStarted { false }, newMonitorStarted { false };
    bool     prevMonitorOn { false }, peakInEvent { false };
    float    peakEventMaxDb { -200.0f };
    PeakMark peakEventMark;

    void computeBands();
    void doFFT();
    void fillPlaybackFromActive();
    float peakThresholdDb() const;
    void  pushPeak (const PeakMark&) noexcept;

    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUSpectrumAudioProcessor)
};
