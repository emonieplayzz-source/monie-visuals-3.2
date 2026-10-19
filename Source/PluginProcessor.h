#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/AudioAnalyzer.h"
#include "Core/AudioDrive.h"
#include "Core/TransportClock.h"
#include "Media/MediaPool.h"
#include "Params.h"
#include "ProjectStore.h"
#include <memory>

class CaptionService;

class MonieProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    MonieProcessor();
    ~MonieProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "monie visuals"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------- shared state ----------------
    juce::AudioProcessorValueTreeState apvts;
    monie::ProjectStore store;               // layers, effects, animations, lyrics, gradient ... (message thread edits, anyone reads snapshots)
    monie::MediaPool media;

    // Written by the analysis thread, read by the render thread. Plain atomics: no locks anywhere.
    struct Analysis
    {
        std::atomic<float> bass { 0 }, mid { 0 }, treble { 0 }, rms { 0 }, peak { 0 }, flux { 0 }, lowMid { 0 }, highMid { 0 }, transient { 0 };
        std::atomic<uint32_t> beatCount { 0 };
        std::atomic<float> bands[monie::AnalysisFrame::numBands];
        std::atomic<float> wave[monie::AnalysisFrame::numWave];
        std::atomic<int> framesReceived { 0 };
        Analysis() { for (auto& b : bands) b = 0; for (auto& w : wave) w = 0.5f; }
    } analysis;

    // Host transport as last reported by the audio callback (plain atomics; stamp = wall clock seconds of that callback)
    struct Host
    {
        std::atomic<bool> valid { false }, playing { false };
        std::atomic<double> seconds { 0.0 }, stamp { 0.0 }, bpm { 0.0 };
        std::atomic<int> stampCount { 0 };
    } host;

    void fillRaw (monie::RawAudio&) const;                  // any thread
    monie::DriveParams driveParams() const;                 // reads automatable sensitivity params
    void readGlobals (float* out) const;                    // values in monie::globalDefs() order
    void readKnobs (float* out8) const;                     // normalised sk0..7
    int  currentScene() const { return (int) apvts.getRawParameterValue ("scene")->load(); }

    // Timeline clock used by preview, captions and keyframes. Thread-safe.
    double clockNow();
    void   transportPlay();  void transportPause();  void transportStop();  void transportSeek (double sec);
    bool   transportIsPlaying();  bool hostControlled();  int seekCounter();
    void   setLoop (bool on, double a, double b);

    // Scene bookkeeping (message thread)
    void selectScene (int scene);                           // host-visible parameter change + knob memory
    void resetSceneKnobs();
    bool audioActive() const { return analysis.framesReceived.load() > 0; }
    std::atomic<bool> asrCapture { false };                 // set by CaptionService: audio thread feeds the ASR FIFO only while true
    juce::AbstractFifo asrFifo { 1 << 17 };
    std::vector<float> asrData;
    std::atomic<double> sampleRateAtomic { 44100.0 };
    std::atomic<int> sidechainChoice { 0 };                 // 0 = main mix, 1 = sidechain bus (when the host provides one)
    std::unique_ptr<CaptionService> captions;               // optional offline lyric recognition (idle unless the user enables it)

private:
    class AnalysisThread;
    void parameterChanged (const juce::String& id, float v) override;
    void handleAsyncUpdate() override;
    void startAnalysis (double sr);
    void stopAnalysis();
    juce::String projectStateString();

    static constexpr int fifoSize = 1 << 15;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::array<float, 1024> scratch {};
    std::unique_ptr<AnalysisThread> analysisThread;
    std::atomic<float>* beatSensParam = nullptr;
    double currentSampleRate = 44100.0;
    std::vector<std::atomic<float>*> globalParams; std::atomic<float>* knobParams[8] {}; std::atomic<float>* sceneParam = nullptr;

    juce::SpinLock clockLock; monie::TransportClock clock; int lastStampSeen = -1;
    int appliedScene = 0; bool restoring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonieProcessor)
};
