#include "CaptionService.h"
#include "PluginProcessor.h"
#include "Core/CaptionEngine.h"

using namespace monie;
struct LyricDocHolder { LyricDoc doc; };

CaptionService::CaptionService (MonieProcessor& p) : juce::Thread ("monie captions"), proc (p) { startThread (juce::Thread::Priority::background); }
CaptionService::~CaptionService()
{
    enabled = false; proc.asrCapture = false; cancelFlag = true; wake.signal();
    { std::lock_guard<std::mutex> g (childM); if (child) child->kill(); }
    stopThread (4000);
}

juce::String CaptionService::configProblem() const
{
    const auto& a = proc.store.snapshot()->asr;
    if (! a.consent) return "Tick the consent box in Captions > Recognition setup first. Audio is only ever processed on this computer.";
    if (a.exePath.empty() || ! juce::File (a.exePath).existsAsFile()) return "whisper.cpp executable not set (Captions > Recognition setup). See docs/CAPTIONS_SETUP.md.";
    if (a.modelPath.empty() || ! juce::File (a.modelPath).existsAsFile()) return "Whisper model file (ggml-*.bin) not set. See docs/CAPTIONS_SETUP.md.";
    return {};
}

CaptionService::State CaptionService::state() const
{
    if (errorFlag) return State::Error;
    if (busy) return State::Processing;
    if (! enabled) return State::Off;
    if (produced > 0) return State::Active;
    return proc.transportIsPlaying() ? State::Listening : State::On;
}
juce::String CaptionService::stateName() const
{
    switch (state()) { case State::Off: return "OFF"; case State::On: return "ON"; case State::Listening: return "LISTENING"; case State::Processing: return "PROCESSING"; case State::Active: return "CAPTIONS ACTIVE"; case State::Error: return "ERROR"; }
    return "OFF";
}

void CaptionService::setEnabled (bool on)
{
    errorFlag = false;
    if (! on) { enabled = false; proc.asrCapture = false; setMessage ({}); return; }
    auto why = configProblem();
    if (why.isNotEmpty()) { enabled = false; proc.asrCapture = false; setError (why); return; }
    proc.sidechainChoice = proc.store.snapshot()->asr.source;
    proc.asrFifo.reset(); produced = 0; enabled = true; proc.asrCapture = true; setMessage ("Listening to the host audio while it plays. Results appear a few seconds late.");
    wake.signal();
}
void CaptionService::transcribeFile (const juce::File& f)
{
    errorFlag = false;
    auto why = configProblem(); if (why.isNotEmpty()) { setError (why); return; }
    if (! f.existsAsFile()) { setError ("Audio file not found."); return; }
    Job j; j.file = f; j.isFile = true; { std::lock_guard<std::mutex> g (qm); queue.push_back (std::move (j)); } wake.signal();
}
void CaptionService::cancel()
{
    cancelFlag = true; { std::lock_guard<std::mutex> g (qm); queue.clear(); }
    { std::lock_guard<std::mutex> g (childM); if (child) child->kill(); }
}

void CaptionService::pump (AsrChunker& chunker)
{
    const int n = proc.asrFifo.getNumReady();
    if (n <= 0) return;
    std::vector<float> tmp ((size_t) n); int s1, n1, s2, n2; proc.asrFifo.prepareToRead (n, s1, n1, s2, n2);
    if (n1 > 0) std::copy (proc.asrData.begin() + s1, proc.asrData.begin() + s1 + n1, tmp.begin());
    if (n2 > 0) std::copy (proc.asrData.begin() + s2, proc.asrData.begin() + s2 + n2, tmp.begin() + n1);
    proc.asrFifo.finishedRead (n1 + n2);
    const double sr = proc.sampleRateAtomic.load();
    // Time of the first popped sample ~= timeline clock now minus the audio still waiting in the FIFO minus what we just popped.
    const double tFirst = proc.clockNow() - (double) (proc.asrFifo.getNumReady() + n) / sr;
    std::vector<AsrChunk> out; chunker.push (tmp.data(), n, std::max (0.0, tFirst), proc.transportIsPlaying(), proc.seekCounter(), out);
    for (auto& c : out)
    {
        if (c.silent) continue;
        std::lock_guard<std::mutex> g (qm);
        if (queue.size() >= 3) { queue.pop_front(); ++dropped; }      // bounded queue: recognition slower than real time -> drop the oldest, never grow
        Job j; j.pcm = std::move (c.pcm); j.startTime = c.startTime; queue.push_back (std::move (j));
    }
}

void CaptionService::run()
{
    AsrChunker chunker; double lastSr = 0; int lastChunk = 0;
    while (! threadShouldExit())
    {
        if (enabled)
        {
            const double sr = proc.sampleRateAtomic.load(); const int cs = proc.store.snapshot()->asr.chunkSec;
            if (sr != lastSr || cs != lastChunk) { chunker.configure (sr, cs); lastSr = sr; lastChunk = cs; }
            pump (chunker);
        }
        else if (chunker.pending() > 0) chunker.reset();
        Job j; bool have = false; { std::lock_guard<std::mutex> g (qm); if (! queue.empty()) { j = std::move (queue.front()); queue.pop_front(); have = true; } }
        if (have) { cancelFlag = false; busy = true; runJob (j); busy = false; ++done; continue; }
        wake.wait (enabled ? 60 : 500);
    }
}

void CaptionService::runJob (const Job& job)
{
    const auto asr = proc.store.snapshot()->asr;
    auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("monie_asr_" + juce::String (juce::Time::getMillisecondCounter()));
    tmpDir.createDirectory();
    struct Cleanup { juce::File d; ~Cleanup() { d.deleteRecursively(); } } cleanup { tmpDir };
    juce::File wav = tmpDir.getChildFile ("in.wav");
    double chunkStart = 0;
    if (job.isFile)
    {
        setMessage ("Preparing " + job.file.getFileName() + " ...");
        if (FfTools::available())
        {
            juce::ChildProcess ff; juce::StringArray a { FfTools::ffmpeg(), "-y", "-hide_banner", "-loglevel", "error", "-i", job.file.getFullPathName(), "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", wav.getFullPathName() };
            if (! ff.start (a) || ! ff.waitForProcessToFinish (600000) || ff.getExitCode() != 0 || ! wav.existsAsFile()) { setError ("FFmpeg could not read that audio file."); return; }
        }
        else if (job.file.hasFileExtension ("wav")) job.file.copyFileTo (wav);
        else { setError ("FFmpeg is needed to read " + job.file.getFileExtension() + " files (Settings > Tools). A 16-bit WAV works without it."); return; }
    }
    else
    {
        auto bytes = makeWav16 (job.pcm); wav.replaceWithData (bytes.data(), bytes.size()); chunkStart = job.startTime;
    }
    const juce::String prefix = tmpDir.getChildFile ("out").getFullPathName();
    juce::StringArray args { asr.exePath, "-m", asr.modelPath, "-f", wav.getFullPathName(), "-ojf", "-of", prefix, "-np", "-t", juce::String (juce::jlimit (1, 8, juce::SystemStats::getNumPhysicalCpus() / 2)) };
    if (asr.language != "auto" && ! asr.language.empty()) { args.add ("-l"); args.add (asr.language); }
    if (job.isFile) setMessage ("Recognising " + job.file.getFileName() + " (this can take a while) ...");
    auto proc2 = std::make_unique<juce::ChildProcess>();
    if (! proc2->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) { setError ("Could not start whisper.cpp. Check the executable path."); return; }
    { std::lock_guard<std::mutex> g (childM); child = std::move (proc2); }
    juce::String output; const auto t0 = juce::Time::getMillisecondCounter();
    for (;;)
    {
        { std::lock_guard<std::mutex> g (childM); if (! child) return; output += child->readAllProcessOutput(); if (! child->isRunning()) break; }
        if (threadShouldExit() || cancelFlag) { std::lock_guard<std::mutex> g (childM); if (child) child->kill(); child.reset(); setMessage ("Cancelled."); return; }
        if (juce::Time::getMillisecondCounter() - t0 > (job.isFile ? 3600000u : 180000u)) { std::lock_guard<std::mutex> g (childM); if (child) child->kill(); child.reset(); setError ("whisper.cpp timed out."); return; }
        juce::Thread::sleep (40);
    }
    uint32_t code; { std::lock_guard<std::mutex> g (childM); code = child->getExitCode(); child.reset(); }
    auto json = juce::File (prefix + ".json");
    if (code != 0 || ! json.existsAsFile()) { setError ("whisper.cpp failed (exit code " + juce::String ((int) code) + "). " + output.trim().substring (0, 300)); return; }
    auto res = parseWhisperJson (json.loadFileAsString().toStdString(), chunkStart);
    if (! res.ok) { setError (res.error); return; }
    double from = chunkStart, to = job.isFile ? 1e9 : chunkStart + (double) job.pcm.size() / 16000.0;
    auto holder = std::make_shared<LyricDocHolder>(); holder->doc = std::move (res.doc); const int nLines = (int) holder->doc.lines.size();
    juce::WeakReference<CaptionService> weak (this); MonieProcessor* pp = &proc;
    juce::MessageManager::callAsync ([weak, holder, from, to, pp, nLines, isFile = job.isFile]
    {
        if (weak == nullptr) return;
        pp->store.edit ("Automatic captions", [&] (Project& p)
        {
            auto& L = p.autoLyrics.lines; L.erase (std::remove_if (L.begin(), L.end(), [&] (const LyricLine& l) { return l.start >= from - 0.001 && l.start < to; }), L.end());
            for (auto& l : holder->doc.lines) L.push_back (l);
            std::stable_sort (L.begin(), L.end(), [] (auto& a, auto& b) { return a.start < b.start; });
            if (! p.captionsOn && nLines > 0) p.captionsOn = true;
        }, {}, false);
        if (nLines > 0) weak->produced++;
        weak->setMessage (isFile ? juce::String (nLines) + " caption lines recognised. Review and correct them in Captions > Lyrics." : weak->msg);
    });
}
