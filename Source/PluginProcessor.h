#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU  — pass-through stereo metering effect. Measures L/R level (VU or
// Peak), records L/R level envelopes locked to the host timeline, plays them
// back as a "Recorded" ghost against "Live", and tags peak moments.
//==============================================================================
class TimelineVUAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 100;
    static constexpr int kMaxSeconds     = 3600;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;
    static constexpr int kPeakFifoSize   = 256;

    enum MeterMode { VU = 0, Peak = 1 };

    struct PeakMark
    {
        double seconds { 0.0 };
        double ppq     { 0.0 };
        float  db      { 0.0f };
        int    bar     { 1 };
        int    beat    { 1 };
    };

    struct Recording
    {
        juce::String          name;
        std::vector<float>    dataL, dataR;    // one entry per slot, per channel
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
    TimelineVUAudioProcessor();
    ~TimelineVUAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU"; }
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

    // Per-channel live state (linear, current metric).
    std::atomic<float>  liveL { 0.0f }, liveR { 0.0f };
    std::atomic<float>  livePeakL { 0.0f }, livePeakR { 0.0f };   // fast peak env
    std::atomic<float>  recordedL { 0.0f }, recordedR { 0.0f };   // ghost

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    //==========================================================================
    void drainPeaks();
    void finalizeCapture (const juce::String& name);
    void selectRecording (int index);
    void deleteActiveRecording();

    int          getNumRecordings() const { return (int) history.size(); }
    int          getActiveRecording() const { return activeIndex; }
    juce::String getRecordingName (int i) const
    {
        return juce::isPositiveAndBelow (i, (int) history.size()) ? history[(size_t) i].name
                                                                  : juce::String();
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
            if (juce::isPositiveAndBelow (i, (int) pk.size()))
                return pk[(size_t) i];
        }
        return {};
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    double currentSampleRate { 44100.0 };

    double smoothedMsL { 0.0 }, smoothedMsR { 0.0 };
    float  peakEnvL { 0.0f }, peakEnvR { 0.0f };

    std::vector<float> captureEnvL, captureEnvR;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording  { false };

    std::vector<float> playbackEnvL, playbackEnvR;

    std::vector<Recording> history;
    int                    activeIndex { -1 };

    // Peak tagging.
    std::vector<PeakMark>               capturePeaks;
    std::array<PeakMark, kPeakFifoSize> peakFifoBuf;
    juce::AbstractFifo                  peakFifo { kPeakFifoSize };
    std::atomic<bool>                   newCaptureStarted { false };
    bool     peakInEvent   { false };
    float    peakEventMaxDb { -200.0f };
    PeakMark peakEventMark;

    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    float peakThresholdDb() const;
    void  pushPeak (const PeakMark&) noexcept;
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);

    void fillPlaybackFromActive();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineVUAudioProcessor)
};
