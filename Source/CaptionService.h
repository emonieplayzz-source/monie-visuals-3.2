#pragma once
#include <juce_core/juce_core.h>
#include <deque>
#include <mutex>
#include "Core/AsrChunker.h"
class MonieProcessor;

// Optional automatic lyric captions. Everything heavy happens on this service's own thread and in a separate whisper.cpp process
// (`whisper-cli`) that the USER installs and points at in Settings. Nothing is uploaded anywhere; nothing runs unless the user has
// ticked the consent box AND switched captions on. The audio callback only copies samples into a lock-free FIFO (see PluginProcessor).
class CaptionService : private juce::Thread
{
public:
    enum class State { Off, On, Listening, Processing, Active, Error };
    explicit CaptionService (MonieProcessor&);
    ~CaptionService() override;

    State state() const;
    juce::String stateName() const;
    juce::String message() const { const juce::ScopedLock sl (lock); return msg; }
    int chunksDone() const { return done.load(); }
    int chunksDropped() const { return dropped.load(); }

    // message thread
    void setEnabled (bool on);                                   // live recognition of host audio
    void transcribeFile (const juce::File& audio);               // offline recognition of a file the user chose
    void cancel();
    juce::String configProblem() const;                          // empty = ready to run

    JUCE_DECLARE_WEAK_REFERENCEABLE (CaptionService)
private:
    struct Job { std::vector<int16_t> pcm; juce::File file; double startTime = 0; bool isFile = false; };
    void run() override;
    void pump (monie::AsrChunker&);
    void runJob (const Job&);
    void publish (const struct LyricDocHolder&, double from, double to, bool replaceAll);
    void setError (const juce::String& e) { { const juce::ScopedLock sl (lock); msg = e; } errorFlag = true; }
    void setMessage (const juce::String& m) { const juce::ScopedLock sl (lock); msg = m; }

    MonieProcessor& proc;
    mutable juce::CriticalSection lock; juce::String msg;
    std::mutex qm; std::deque<Job> queue; juce::WaitableEvent wake;
    std::atomic<bool> enabled { false }, errorFlag { false }, busy { false }, cancelFlag { false }; std::atomic<int> done { 0 }, dropped { 0 }, produced { 0 };
    std::unique_ptr<juce::ChildProcess> child; std::mutex childM;
};
