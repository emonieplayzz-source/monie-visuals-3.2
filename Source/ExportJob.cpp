#include "ExportJob.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "UI/PreviewComponent.h"
#include <cstdio>

using namespace monie;

ExportJob::ExportJob (MonieProcessor& p, PreviewComponent& pv, ExportSettings st) : juce::Thread ("monie export"), proc (p), preview (pv), s (std::move (st)) { total = frameCount (s); }
ExportJob::~ExportJob() { cancelFlag = true; stopThread (10000); }
double ExportJob::etaSeconds() const
{
    const int f = frame.load(); if (f < 5) return -1;
    const double el = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0; return el / f * (total - f);
}

bool ExportJob::loadAudio (std::vector<float>& mono, double& sr, juce::String& err)
{
    juce::File src (s.audioFile), wav = src;
    juce::File tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("monie_export_audio_" + juce::String (juce::Time::getMillisecondCounter()) + ".wav");
    struct Del { juce::File f; ~Del() { f.deleteFile(); } } del { tmp };
    if (FfTools::available())
    {   // let ffmpeg decode anything (mp3, m4a, flac, video files ...) to a plain stereo 44.1 kHz wav
        juce::ChildProcess ff; juce::StringArray a { FfTools::ffmpeg(), "-y", "-hide_banner", "-loglevel", "error", "-i", src.getFullPathName(), "-vn", "-ar", "44100", "-ac", "2", "-c:a", "pcm_s16le", tmp.getFullPathName() };
        if (! ff.start (a) || ! ff.waitForProcessToFinish (600000) || ff.getExitCode() != 0 || ! tmp.existsAsFile()) { err = "FFmpeg could not decode the audio file."; return false; }
        wav = tmp;
    }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wav));
    if (! rd) { err = "The audio file could not be read."; return false; }
    sr = rd->sampleRate;
    const juce::int64 n = rd->lengthInSamples; mono.assign ((size_t) n, 0.f);
    juce::AudioBuffer<float> b ((int) rd->numChannels, 65536);
    for (juce::int64 pos = 0; pos < n; pos += 65536)
    {
        const int m = (int) std::min<juce::int64> (65536, n - pos); rd->read (&b, 0, m, pos, true, true);
        for (int i = 0; i < m; ++i) { float v = 0; for (int c = 0; c < b.getNumChannels(); ++c) v += b.getSample (c, i); mono[(size_t) (pos + i)] = v / (float) b.getNumChannels(); }
        if (threadShouldExit() || cancelFlag) return false;
    }
    return true;
}

void ExportJob::run()
{
    startMs = juce::Time::getMillisecondCounterHiRes();
    auto fail = [&] (const juce::String& e) { setStatus (e); isDone = true; };
    evenSize (s.width, s.height);
    if (auto why = validateExport (s, FfTools::available()); ! why.empty()) { fail (juce::String (why)); return; }

    setStatus ("Analysing audio ...");
    std::vector<float> audio; double sr = 44100; juce::String err;
    if (! loadAudio (audio, sr, err)) { fail (cancelFlag ? "Cancelled." : err); return; }

    auto project = std::make_shared<Project> (*proc.store.snapshot());
    if (! s.captions) for (auto& l : project->layers) if (l.type == LayerType::Caption) l.visible = false;
    std::shared_ptr<const Project> proj = project;
    std::vector<float> globals (globalDefs().size()); proc.readGlobals (globals.data());
    float knobs[8]; proc.readKnobs (knobs); const int scene = proc.currentScene();
    const DriveParams dp = proc.driveParams();

    juce::File logFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("monie_ffmpeg_" + juce::String (juce::Time::getMillisecondCounter()) + ".log");
    ExportSettings es = s; es.logFile = logFile.getFullPathName().toStdString();
    const std::string cmd = buildCommandLine (buildFfmpegArgs (es, FfTools::ffmpeg().toStdString()), es.logFile);
   #ifdef _WIN32
    FILE* pipe = _wpopen (juce::String (cmd).toWideCharPointer(), L"wb");
   #else
    FILE* pipe = popen (cmd.c_str(), "w");
   #endif
    if (! pipe) { fail ("Could not start FFmpeg."); return; }

    AudioAnalyzer analyzer; analyzer.prepare (sr); analyzer.setBeatSensitivity (globals[(size_t) globalIndex ("sensBeat")]);
    AudioDrive drive; drive.reset(); size_t fed = 0; RawAudio raw; std::vector<uint8_t> rgba;
    const float dt = 1.f / (float) s.fps;
    auto& comp = preview.comp(); const int oldProxy = proc.media.getProxyWidth(); proc.media.setProxyWidth (0);
    preview.requestOfflineBegin (true); juce::Thread::sleep (80);
    preview.runOnGL ([&] { comp.resetState(); comp.requestRdReset(); });
    bool good = true; juce::String failMsg; int written = 0;

    for (int i = 0; i < total && good; ++i)
    {
        if (threadShouldExit() || cancelFlag) break;
        const double t = frameTime (s, i);
        const size_t upTo = std::min (audio.size(), (size_t) std::max (0.0, std::round (t * sr)));
        while (fed < upTo) { const size_t m = std::min<size_t> (upTo - fed, 512); analyzer.push (audio.data() + fed, (int) m); fed += m; }
        const auto& f = analyzer.getFrame();
        raw.bass = f.bass; raw.lowMid = f.lowMid; raw.mid = f.mid; raw.highMid = f.highMid; raw.treble = f.treble; raw.rms = f.rms; raw.peak = f.peak; raw.flux = f.flux; raw.transient = f.transient; raw.beatCount = f.beatCount;
        for (int k = 0; k < 64; ++k) { raw.bands[k] = f.bands[k]; raw.wave[k] = f.wave[k]; }
        FrameInputs in; in.project = proj; in.globals = globals.data(); in.scene = scene; for (int k = 0; k < 8; ++k) in.knobs[k] = knobs[k];
        in.quality = 2; in.drive = drive.update (dt, raw, dp); in.clock = t; in.dt = dt; in.overlay = false;
        bool rendered = false; preview.runOnGL ([&] { rendered = comp.renderOffline (in, s.width, s.height, rgba); });
        if (! rendered) { good = false; failMsg = "The GPU renderer failed on frame " + juce::String (i) + "."; break; }
        if (std::fwrite (rgba.data(), 1, rgba.size(), pipe) != rgba.size()) { good = false; failMsg = "FFmpeg stopped accepting frames."; break; }
        written = i + 1; frame = written;
        setStatus ("Rendering frame " + juce::String (written) + " / " + juce::String (total));
    }
    preview.requestOfflineBegin (false); proc.media.setProxyWidth (oldProxy);
    preview.runOnGL ([&] { comp.resetState(); });
   #ifdef _WIN32
    const int rc = _pclose (pipe);
   #else
    const int rc = pclose (pipe);
   #endif
    const bool cancelled = cancelFlag.load();
    juce::String log = logFile.existsAsFile() ? logFile.loadFileAsString().trim() : juce::String(); logFile.deleteFile();
    if (cancelled) { juce::File (s.output).deleteFile(); fail ("Cancelled. The partial file was removed."); return; }
    if (! good || rc != 0) { fail ((failMsg.isNotEmpty() ? failMsg + " " : juce::String()) + "FFmpeg said: " + (log.isEmpty() ? "(no message)" : log.substring (0, 600))); return; }

    if (s.writeSrt)
    {
        auto merged = mergeDocs (project->lyrics, project->autoLyrics); shift (merged, -s.start);
        for (auto& l : merged.lines) if (l.end > 0 && l.start < s.end - s.start) {} 
        juce::File (juce::String (s.output)).withFileExtension ("srt").replaceWithText (juce::String (toSRT (merged)));
    }
    ok = true; setStatus ("Done: " + juce::String (s.output)); isDone = true;
}
