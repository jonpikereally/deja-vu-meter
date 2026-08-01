#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>

//==============================================================================
// Deja VU Suite — Meter + LUFS + Spectrum + Width in one pass-through plugin,
// with a selectable multi-panel layout and a unified take that records all four
// metrics locked to the timeline for a ghost A/B across every panel.
//==============================================================================
class DejaVUSuiteAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kSlotsPerSecond = 30;
    static constexpr int kMaxSeconds     = 1800;
    static constexpr int kMaxSlots       = kSlotsPerSecond * kMaxSeconds;
    static constexpr int kMaxRecordings  = 5;

    static constexpr int kBands   = 31;
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kFftHop   = kFftSize / 2;
    static constexpr int kGonio    = 512;

    // Per-slot frame layout.
    static constexpr int OFF_VUL   = 0;
    static constexpr int OFF_VUR   = 1;
    static constexpr int OFF_LUFS  = 2;
    static constexpr int OFF_WIDTH = 3;
    static constexpr int OFF_BANDS = 4;
    static constexpr int kStride   = OFF_BANDS + kBands;   // 35

    struct Recording
    {
        juce::String       name;
        std::vector<float> frames;   // numSlots * kStride
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
    DejaVUSuiteAudioProcessor();
    ~DejaVUSuiteAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Deja VU Suite"; }
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

    // Meter.
    std::atomic<float> vuL { 0.0f }, vuR { 0.0f }, vuPeakL { 0.0f }, vuPeakR { 0.0f };
    std::atomic<float> recVuL { 0.0f }, recVuR { 0.0f };
    // LUFS.
    std::atomic<float> lufsMom { -100.0f }, lufsShort { -100.0f }, lufsInteg { -100.0f }, lufsTP { -100.0f };
    std::atomic<float> recLufs { -100.0f };
    // Spectrum.
    std::array<std::atomic<float>, kBands> bands, recBands;
    // Width.
    std::atomic<float> corr { 1.0f }, width { 0.0f }, balance { 0.0f }, recWidth { 0.0f };
    std::array<float, 2 * kGonio> gonio {};
    std::atomic<int>              gonioWrite { 0 };

    std::atomic<bool>   transportPlaying { false };
    std::atomic<bool>   recordingNow     { false };
    std::atomic<bool>   hasRecording     { false };
    std::atomic<double> playheadSeconds  { 0.0 };
    std::atomic<double> playheadPpq      { 0.0 };
    std::atomic<int>    timeSigNum       { 4 };
    std::atomic<int>    timeSigDen       { 4 };
    std::atomic<bool>   captureFinished  { false };

    float getBandCentreHz (int b) const { return b >= 0 && b < kBands ? bandCentreHz[(size_t) b] : 0.0f; }
    float targetLufs() const;

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
    struct Biquad
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
        void reset() { z1 = z2 = 0.0; }
        inline float process (float in) noexcept
        {
            const double x = (double) in, y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y;
        }
        void setHighPass (double fs, double f0, double Q);
        void setHighShelf (double fs, double f0, double dBgain, double S);
    };

    double currentSampleRate { 48000.0 };

    // Meter state.
    double smMsL { 0.0 }, smMsR { 0.0 };
    float  pkEnvL { 0.0f }, pkEnvR { 0.0f };

    // LUFS state.
    std::array<Biquad, 2> kShelf, kHP;
    double chunkSum { 0.0 }; int chunkSamples { 0 }, chunkLen { 4800 };
    std::array<double, 30> zRing {}; int zCount { 0 };
    std::vector<double> gatingBlocks;
    bool prevPlaying { false };
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int osChannels { 2 };

    // Spectrum state.
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kFftSize> fifo {}; int writePos { 0 }, hopCounter { 0 };
    std::array<float, 2 * kFftSize> fftData {};
    std::array<float, kBands> bandLevel {}, bandCentreHz {};
    std::array<int, kBands> binLo {}, binHi {};

    // Width state.
    double corrS { 1.0 }, widthS { 0.0 }, balS { 0.0 }; int gonioSub { 0 };

    // Recording.
    std::vector<float> captureFrames, playbackFrames;
    std::atomic<int>   captureMaxSlot { -1 };
    bool               prevRecording { false };
    std::vector<Recording> history;
    int                    activeIndex { -1 };
    std::atomic<double> capStartSecs { 0.0 }, capEndSecs { 0.0 };
    std::atomic<int>    capStartBar { 1 }, capStartBeat { 1 }, capEndBar { 1 }, capEndBeat { 1 };

    void computeBands();
    void doFFT();
    void pushChunk();
    void fillPlaybackFromActive();
    static void computeBarBeat (double ppq, int num, int den, int& bar, int& beat);
    bool         nameExists (const juce::String&) const;
    juce::String makeUniqueName (const juce::String& base) const;
    juce::String defaultTakeName() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DejaVUSuiteAudioProcessor)
};
