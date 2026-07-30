#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU  — pass-through metering effect. Measures level (VU or Peak), records
// a level envelope locked to the host timeline, plays it back as a "Recorded"
// ghost against "Live", and tags peak moments during recording.
//==============================================================================
class TimelineVUAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 100;                 // 10 ms hop
    static constexpr int kMaxSeconds     = 3600;                // 60 min cap
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;
    static constexpr int kPeakFifoSize   = 256;

    enum MeterMode { VU = 0, Peak = 1 };

    // A tagged peak moment on the timeline.
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
        std::vector<float>    data;    // one entry per slot
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
    // Live state read by the editor (GUI thread). All lock-free.
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>  liveLevel        { 0.0f };
    std::atomic<float>  recordedLevel    { 0.0f };
    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    //==========================================================================
    // Message-thread API for the editor.
    void drainPeaks();                                 // move fifo → capturePeaks
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

    double smoothedMeanSquare { 0.0 };
    float  peakEnvelope       { 0.0f };

    std::vector<float> captureEnvelope;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording  { false };

    std::vector<float> playbackEnvelope;

    std::vector<Recording> history;
    int                    activeIndex { -1 };

    // Peak tagging.
    std::vector<PeakMark>            capturePeaks;         // message thread
    std::array<PeakMark, kPeakFifoSize> peakFifoBuf;
    juce::AbstractFifo               peakFifo { kPeakFifoSize };
    std::atomic<bool>                newCaptureStarted { false };
    bool     peakInEvent   { false };                     // audio thread
    float    peakEventMaxDb { -200.0f };
    PeakMark peakEventMark;

    // Capture range (start/end) for the take in progress.
    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    float peakThresholdDb() const;
    void  pushPeak (const PeakMark&) noexcept;
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);

    void fillPlaybackFromActive();
    void writeSlots (int fromSlot, int toSlot, float value) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineVUAudioProcessor)
};
