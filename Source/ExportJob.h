#pragma once
#include <juce_core/juce_core.h>
#include "Core/ExportPlan.h"
class MonieProcessor; class PreviewComponent;

// Offline video export: renders every frame exactly (no real-time dependence), analyses a user-chosen audio file with the same
// analyser the live preview uses, and pipes raw RGBA frames into an external ffmpeg process. Runs on its own thread; the message
// thread only polls progress().
class ExportJob : private juce::Thread
{
public:
    ExportJob (MonieProcessor&, PreviewComponent&, monie::ExportSettings);
    ~ExportJob() override;
    void start() { startThread(); }
    void cancel() { cancelFlag = true; }
    bool finished() const { return isDone.load(); }
    float progress() const { return total > 0 ? (float) frame.load() / (float) total : 0.f; }
    double etaSeconds() const;
    juce::String status() const { const juce::ScopedLock sl (lock); return msg; }
    bool succeeded() const { return ok.load(); }
    bool wasCancelled() const { return cancelFlag.load(); }
private:
    void run() override;
    void setStatus (const juce::String& s) { const juce::ScopedLock sl (lock); msg = s; }
    bool loadAudio (std::vector<float>& mono, double& sr, juce::String& err);
    MonieProcessor& proc; PreviewComponent& preview; monie::ExportSettings s;
    std::atomic<int> frame { 0 }; int total = 0; std::atomic<bool> cancelFlag { false }, isDone { false }, ok { false };
    juce::CriticalSection lock; juce::String msg; double startMs = 0;
};
