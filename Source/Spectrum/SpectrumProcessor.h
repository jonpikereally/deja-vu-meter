#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

//==============================================================================
// Deja VU Spectrum — pass-through FFT spectrum analyser. Measures a log-spaced
// band spectrum, records it locked to the host timeline, and plays back a
// recorded "ghost" spectrum against the live one (same take workflow as Deja VU).
//==============================================================================
class DejaVUSpectrumAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kFftOrder       = 11;
    static constexpr int kFftSize        = 1 << kFftOrder;    // 2048
    static constexpr int kBands          = 31;                // ~1/3 octave
    static constexpr int kSlotsPerSecond = 30;               // ~33 ms frames
    static constexpr int kMaxSeconds     = 1800;             // 30 min cap
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;

    struct Recording
    {
        juce::String       name;
        std::vector<float> frames;   // numSlots * kBands, band-major per slot
        int                numSlots { 0 };
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

    std::array<std::atomic<float>, kBands> liveBands;   // linear amplitude est.
    std::array<std::atomic<float>, kBands> recBands;    // ghost

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    double currentSampleRate { 44100.0 };

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize,
        juce::dsp::WindowingFunction<float>::hann };

    std::array<float, kFftSize>     fifo {};
    int                             fifoIndex { 0 };
    std::array<float, 2 * kFftSize> fftData {};
    std::array<float, kBands>       bandLevel {};       // smoothed linear
    std::array<float, kBands>       bandCentreHz {};
    std::array<int, kBands>         binLo {}, binHi {};

    std::vector<float> captureFrames;   // kMaxSlots * kBands
    std::vector<float> playbackFrames;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };

    std::vector<Recording> history;
    int                    activeIndex { -1 };

    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    void computeBands();
    void doFFT();
    void fillPlaybackFromActive();

    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUSpectrumAudioProcessor)
};
