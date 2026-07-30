#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

//==============================================================================
// Deja VU
//
// A pass-through metering effect. It measures the incoming level (VU or Peak),
// and when "Record" is armed and the host transport is rolling it captures that
// level into an envelope indexed by the timeline position. When the transport
// stops, the take is finalised into a named history (last 5 kept) and record is
// auto-disarmed. The active take is played back as a "Recorded" ghost against
// the "Live" meter.
//==============================================================================
class TimelineVUAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 100;                 // 10 ms hop
    static constexpr int kMaxSeconds     = 3600;                // 60 min cap
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;

    enum MeterMode { VU = 0, Peak = 1 };

    struct Recording
    {
        juce::String       name;
        std::vector<float> data;   // length entries, one per slot
    };

    //==========================================================================
    TimelineVUAudioProcessor();
    ~TimelineVUAudioProcessor() override = default;

    //==========================================================================
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

    // Raised by the audio thread when a take just finished; the editor consumes
    // it, finalises the capture with the current name, and disarms recording.
    std::atomic<bool>   captureFinished  { false };

    //==========================================================================
    // Called on the message thread by the editor.
    void finalizeCapture (const juce::String& name);   // store captured take
    void selectRecording (int index);                  // set active ghost take
    void deleteActiveRecording();                       // remove active take

    int          getNumRecordings() const              { return (int) history.size(); }
    int          getActiveRecording() const            { return activeIndex; }
    juce::String getRecordingName (int i) const
    {
        return juce::isPositiveAndBelow (i, (int) history.size()) ? history[(size_t) i].name
                                                                  : juce::String();
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    double currentSampleRate { 44100.0 };

    // Metering state (mono sum of channels).
    double smoothedMeanSquare { 0.0 };
    float  peakEnvelope       { 0.0f };

    // The take currently being captured (audio thread writes while recording).
    std::vector<float> captureEnvelope;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording  { false };   // audio-thread edge detect

    // The active take being played back as the ghost (audio thread reads only).
    std::vector<float> playbackEnvelope;

    // Stored takes (message thread owns this).
    std::vector<Recording> history;
    int                    activeIndex { -1 };

    void fillPlaybackFromActive();
    void writeSlots (int fromSlot, int toSlot, float value) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineVUAudioProcessor)
};
