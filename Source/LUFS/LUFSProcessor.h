#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>

//==============================================================================
// Deja VU LUFS — pass-through loudness meter (BS.1770-style Momentary /
// Short-term / Integrated + True Peak), records the short-term loudness locked
// to the timeline and plays it back as a ghost against live.
//==============================================================================
class DejaVULUFSAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 30;
    static constexpr int kMaxSeconds     = 1800;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;

    struct Recording
    {
        juce::String       name;
        std::vector<float> data;     // short-term LUFS per slot
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
    DejaVULUFSAudioProcessor();
    ~DejaVULUFSAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU LUFS"; }
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

    std::atomic<float>  momentary   { -100.0f };
    std::atomic<float>  shortTerm   { -100.0f };
    std::atomic<float>  integrated  { -100.0f };
    std::atomic<float>  truePeakDb  { -100.0f };
    std::atomic<float>  recordedShortTerm { -100.0f };   // ghost at playhead

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    float targetLufs() const;   // 0 = off returns -1000

    //==========================================================================
    void finalizeCapture (const juce::String& name);
    void selectRecording (int index);
    void deleteActiveRecording();
    void deleteAllRecordings();
    void renameActiveRecording (const juce::String& newName);

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    //==========================================================================
    struct Biquad
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
        double z1 { 0 }, z2 { 0 };
        void reset() { z1 = z2 = 0.0; }
        inline float process (float in) noexcept
        {
            const double x = (double) in;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void setHighPass (double fs, double f0, double Q);
        void setHighShelf (double fs, double f0, double dBgain, double S);
    };

    double currentSampleRate { 48000.0 };

    // K-weighting: two biquads per channel (shelf then high-pass).
    std::array<Biquad, 2> kShelf, kHP;

    // 100 ms chunk accumulation of K-weighted mean square (summed over channels).
    double chunkSum { 0.0 };
    int    chunkSamples { 0 };
    int    chunkLen { 4800 };
    std::array<double, 30> zRing {};   // last 30 chunks (3 s)
    int    zCount { 0 };
    std::vector<double> gatingBlocks;  // 400 ms block Z values (integrated)
    bool   prevPlaying { false };

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osChannels { 2 };

    // Timeline record.
    std::vector<float> captureEnv, playbackEnv;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };
    std::vector<Recording> history;
    int                    activeIndex { -1 };
    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    void pushChunk();
    void fillPlaybackFromActive();
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVULUFSAudioProcessor)
};
