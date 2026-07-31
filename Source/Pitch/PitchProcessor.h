#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU Pitch — pass-through pitch / intonation meter. Detects the fundamental
// (autocorrelation), records the pitch (MIDI note) locked to the timeline, and
// plays it back as a ghost against live so takes can be compared for tuning.
//==============================================================================
class DejaVUPitchAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kWindow         = 2048;
    static constexpr int kHop            = 1024;
    static constexpr int kSlotsPerSecond = 30;
    static constexpr int kMaxSeconds     = 1800;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;

    struct Recording
    {
        juce::String       name;
        std::vector<float> data;     // MIDI note per slot (0 = unvoiced)
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
    DejaVUPitchAudioProcessor();
    ~DejaVUPitchAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU Pitch"; }
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

    std::atomic<bool>  liveVoiced { false };
    std::atomic<float> liveMidi   { 0.0f };
    std::atomic<float> liveFreq   { 0.0f };
    std::atomic<float> recordedMidi { 0.0f };   // ghost (0 = unvoiced)

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    static juce::String noteName (int midi);
    static int    nearestNote (float midi) { return (int) std::lround (midi); }
    static float  centsOf     (float midi) { return (midi - (float) nearestNote (midi)) * 100.0f; }

    //==========================================================================
    void finalizeCapture (const juce::String& name);
    void selectRecording (int index);
    void deleteActiveRecording();
    void deleteAllRecordings();

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

    std::array<float, kWindow> ring {};
    int   writePos { 0 }, hopCounter { 0 };
    std::array<float, kWindow> work {};
    int   minLag { 48 }, maxLag { 960 };
    float curMidi { 0.0f };
    bool  curVoiced { false };

    std::vector<float> captureEnv, playbackEnv;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };
    std::vector<Recording> history;
    int                    activeIndex { -1 };
    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    void detect();
    void fillPlaybackFromActive();
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUPitchAudioProcessor)
};
