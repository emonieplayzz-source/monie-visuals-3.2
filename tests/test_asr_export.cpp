#include "AsrChunker.h"
#include "ExportPlan.h"
#include <cstdio>
#include <cstring>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
using namespace monie;
static std::vector<float> tone (int n, double rate, double f, float a) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = a * (float) std::sin (6.2831853 * f * i / rate); return v; }
int main()
{
    // ---- chunker ----
    { AsrChunker c; c.configure (44100, 10); std::vector<AsrChunk> out; auto x = tone (44100, 44100, 220, .3f);
      for (int s = 0; s < 25; ++s) c.push (x.data(), 44100, 100.0 + s, true, 1, out);
      CHECK (out.size() == 2, "25 s of audio -> 2 full 10 s chunks (%zu)", out.size());
      CHECK (out.size() >= 2 && std::fabs (out[0].startTime - 100.0) < 0.01 && std::fabs (out[0].duration - 10.0) < 0.01 && std::fabs (out[1].startTime - 110.0) < 0.02, "chunk start times are timeline times (%.3f, %.3f)", out.empty() ? 0.0 : out[0].startTime, out.size() > 1 ? out[1].startTime : 0.0);
      CHECK (out[0].pcm.size() == 160000 && ! out[0].silent && out[0].rms > .15f, "resampled to 16 kHz, rms %.3f", out[0].rms);
      c.push (x.data(), 44100, 125.0, false, 1, out); CHECK (out.size() == 3 && std::fabs (out[2].duration - 5.0) < 0.05, "pause flushes the partial 5 s chunk (%zu)", out.size()); }
    { AsrChunker c; c.configure (48000, 10); std::vector<AsrChunk> out; auto x = tone (48000, 48000, 220, .3f);
      for (int s = 0; s < 6; ++s) c.push (x.data(), 48000, 10.0 + s, true, 1, out);
      c.push (x.data(), 48000, 3.0, true, 1, out);        // jump backwards without a seekCount change -> detected by time gap
      CHECK (out.size() == 1 && std::fabs (out[0].startTime - 10.0) < .01 && std::fabs (out[0].duration - 6.0) < .05, "time discontinuity flushes the old chunk, never mixes positions");
      c.push (x.data(), 48000, 4.0, true, 1, out); c.push (x.data(), 48000, 5.0, true, 1, out); c.push (x.data(), 48000, 6.0, true, 1, out);   // 4 s pending (>= 2 s min)
      c.push (x.data(), 48000, 7.0, true, 2, out); CHECK (out.size() == 2 && std::fabs (out[1].duration - 4.0) < .05, "seek counter change flushes the pending 4 s chunk"); }
    { AsrChunker c; c.configure (44100, 4); std::vector<AsrChunk> out; std::vector<float> z (44100, 0.f); for (int s = 0; s < 5; ++s) c.push (z.data(), 44100, s, true, 1, out); CHECK (out.size() == 1 && out[0].silent, "silence is flagged so it is skipped"); }
    { AsrChunker c; c.configure (44100, 10); std::vector<AsrChunk> out; auto x = tone (44100, 44100, 220, .3f); c.push (x.data(), 44100, 0, true, 1, out); c.push (x.data(), 44100, 1, false, 1, out); CHECK (out.empty(), "very short partial chunk (<2 s) is dropped, not sent"); }
    // ---- wav ----
    { std::vector<int16_t> p (1600, 1000); auto w = makeWav16 (p); CHECK (w.size() == 44 + 3200 && std::memcmp (w.data(), "RIFF", 4) == 0 && std::memcmp (w.data() + 8, "WAVEfmt ", 8) == 0 && w[22] == 1 && (w[24] | (w[25] << 8)) == 16000 && w[44] == 0xE8 && w[45] == 0x03, "WAV header + payload (%zu bytes)", w.size()); }
    // ---- export plan ----
    { ExportSettings s; s.output = "out.mp4"; s.audioFile = "song.wav"; s.end = 10;
      CHECK (validateExport (s, true).empty(), "valid settings accepted");
      CHECK (! validateExport (s, false).empty(), "missing ffmpeg is reported before any work");
      ExportSettings b = s; b.audioFile.clear(); CHECK (! validateExport (b, true).empty(), "audio file required for reactive export");
      b = s; b.end = b.start; CHECK (! validateExport (b, true).empty(), "empty range rejected"); b = s; b.width = 10; CHECK (! validateExport (b, true).empty(), "tiny resolution rejected"); b = s; b.output.clear(); CHECK (! validateExport (b, true).empty(), "no output rejected");
      s.fps = 30; s.end = 2.5; CHECK (frameCount (s) == 75 && std::fabs (frameTime (s, 30) - 1.0) < 1e-9, "75 frames for 2.5 s @ 30 fps, frame 30 at t=1.0");
      int w = 1921, h = 1081; evenSize (w, h); CHECK (w == 1920 && h == 1080, "odd sizes rounded to even");
      auto a = buildFfmpegArgs (s, "ffmpeg"); std::string j; for (auto& x : a) j += x + " ";
      CHECK (j.find ("-f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i - ") != std::string::npos && j.find ("libx264") != std::string::npos && j.find ("-map 1:a:0") != std::string::npos && a.back() == "out.mp4", "ffmpeg args: %s", j.c_str());
      s.output = "C:\\My Videos\\out file.mp4"; s.audioFile = "D:\\a b.wav"; auto cmd = buildCommandLine (buildFfmpegArgs (s, "C:\\ff mpeg\\ffmpeg.exe"), "log file.txt");
      CHECK (cmd.find ("\"C:\\My Videos\\out file.mp4\"") != std::string::npos && cmd.find ("2> \"log file.txt\"") != std::string::npos && cmd.find ("\"C:\\ff mpeg\\ffmpeg.exe\"") != std::string::npos, "paths with spaces are quoted"); }
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
