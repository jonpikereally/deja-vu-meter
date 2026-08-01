#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU Width — pass-through stereo-field meter. Measures phase correlation,
// stereo width (side/mid energy) and L/R balance, feeds a goniometer, and
// records the width locked to the timeline for a ghost A/B against live.
//==============================================================================
class DejaVUWidthAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 30;
    static constexpr int kMaxSeconds     = 1800;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;
    static constexpr int kGonio          = 512;   // goniometer points

    struct Recording
    {
        juce::String       name;
        std::vector<float> data;     // width (0..1) per slot
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
    DejaVUWidthAudioProcessor();
    ~DejaVUWidthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU Width"; }
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

    std::atomic<float> liveCorr    { 1.0f };   // -1..+1
    std::atomic<float> liveWidth   { 0.0f };   // 0..1
    std::atomic<float> liveBalance { 0.0f };   // -1 (L) .. +1 (R)
    std::atomic<float> recordedWidth { 0.0f }; // ghost

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    // Goniometer points (x = side, y = mid), read by the editor. Benign race.
    std::array<float, 2 * kGonio> gonio {};
    std::atomic<int>              gonioWrite { 0 };

    //==========================================================================
    void finalizeCapture (const juce::String& name);
    void selectRecording (int index);
    void deleteActiveRecording();
    void deleteAllRecordings();
    void renameActiveRecording (const juce::String& newName);

    int          getNumRecordings() const { return (int) history.size(); }
    int          getActiveRecording() const { return activeIndex; }
    juce::String getRecordingName (int i) const
    { return juce::isPositiveAndBelow (i, (int) history.size()) ? history[(size_t) i].name : juce::String(); }
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
    double currentSampleRate { 48000.0 };
    double corrS { 1.0 }, widthS { 0.0 }, balS { 0.0 };
    int    gonioSub { 0 };

    std::vector<float> captureEnv, playbackEnv;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };
    std::vector<Recording> history;
    int                    activeIndex { -1 };
    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    void fillPlaybackFromActive();
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUWidthAudioProcessor)
};
