// Export planning: validation, frame timing and the ffmpeg command line. Pure logic so it can be unit tested without ffmpeg.
#pragma once
#include <cmath>
#include <string>
#include <vector>
namespace monie
{
struct ExportSettings
{
    std::string output, audioFile, logFile; int width = 1920, height = 1080, fps = 30, crf = 18; double start = 0, end = 10; bool captions = true, writeSrt = false, useX264 = true; std::string preset = "medium";
};
// returns empty string when valid, otherwise the reason (shown to the user before any work is done)
inline std::string validateExport (const ExportSettings& s, bool ffmpegFound)
{
    if (! ffmpegFound) return "FFmpeg was not found. Install FFmpeg and point monie visuals at it in Settings > Tools (see docs/FFMPEG.md).";
    if (s.output.empty()) return "Choose an output file.";
    if (s.width < 64 || s.height < 64 || s.width > 7680 || s.height > 4320) return "Resolution must be between 64x64 and 7680x4320.";
    if (s.fps < 1 || s.fps > 120) return "Frame rate must be between 1 and 120.";
    if (! (s.end > s.start + 0.05)) return "The end time must be after the start time.";
    if ((s.end - s.start) * s.fps > 60.0 * 60 * 120) return "That is more than an hour of frames - export in parts.";
    if (s.audioFile.empty()) return "Pick the audio file to analyse. Exported visuals react to this file (the plugin cannot capture the host's audio offline).";
    return {};
}
inline int frameCount (const ExportSettings& s) { return (int) std::ceil ((s.end - s.start) * s.fps - 1e-9); }
inline double frameTime (const ExportSettings& s, int i) { return s.start + (double) i / s.fps; }

// Even dimensions are required by yuv420p; we round DOWN to the nearest even number.
inline void evenSize (int& w, int& h) { w &= ~1; h &= ~1; }

inline std::vector<std::string> buildFfmpegArgs (const ExportSettings& s, const std::string& ffmpeg)
{
    std::vector<std::string> a { ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", std::to_string (s.width) + "x" + std::to_string (s.height), "-r", std::to_string (s.fps), "-i", "-" };
    if (! s.audioFile.empty()) { a.push_back ("-ss"); a.push_back (std::to_string (s.start)); a.push_back ("-t"); a.push_back (std::to_string (s.end - s.start)); a.push_back ("-i"); a.push_back (s.audioFile); }
    a.push_back ("-map"); a.push_back ("0:v:0"); if (! s.audioFile.empty()) { a.push_back ("-map"); a.push_back ("1:a:0"); }
    if (s.useX264) { for (auto* x : { "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf" }) a.push_back (x); a.push_back (std::to_string (s.crf)); a.push_back ("-preset"); a.push_back (s.preset); }
    else { for (auto* x : { "-c:v", "mpeg4", "-q:v", "3", "-pix_fmt", "yuv420p" }) a.push_back (x); }
    if (! s.audioFile.empty()) { for (auto* x : { "-c:a", "aac", "-b:a", "192k", "-shortest" }) a.push_back (x); }
    a.push_back ("-movflags"); a.push_back ("+faststart"); a.push_back (s.output);
    return a;
}
// popen()-style single command string (quoted for cmd.exe / sh). stderr goes to the log file so failures can be reported.
inline std::string quoteArg (const std::string& a) { if (a.find_first_of (" \t\"&()<>|^") == std::string::npos && ! a.empty()) return a; std::string o = "\""; for (char c : a) { if (c == '"') o += "\\\""; else o += c; } return o + "\""; }
inline std::string buildCommandLine (const std::vector<std::string>& args, const std::string& logFile)
{
    std::string c; for (auto& a : args) { if (! c.empty()) c += ' '; c += quoteArg (a); }
    c += " 2> " + quoteArg (logFile);
   #ifdef _WIN32
    c = "\"" + c + "\"";                       // cmd.exe /c strips the outermost quote pair
   #endif
    return c;
}
}
