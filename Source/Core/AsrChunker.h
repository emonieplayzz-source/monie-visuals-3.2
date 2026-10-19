// Turns the live mono audio stream into fixed-length 16 kHz chunks for the offline speech recogniser, stamped with TIMELINE time.
// Robust to pause / seek / loop / restart: a discontinuity flushes the partial chunk (if long enough) and starts a new one, so a
// chunk never contains audio from two different timeline positions. Silent chunks are flagged so no recognition work is wasted.
// Pure logic, no threads, no JUCE; unit tested.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace monie
{
struct AsrChunk { std::vector<int16_t> pcm; double startTime = 0, duration = 0; float rms = 0; bool silent = false; };

class AsrChunker
{
public:
    void configure (double inRate, int chunkSeconds, float silenceRms = 0.004f, double minFlushSeconds = 2.0)
    { rate = inRate; chunkLen = (size_t) (chunkSeconds * 16000); silence = silenceRms; minFlush = (size_t) (minFlushSeconds * 16000); reset(); }
    void reset() { buf.clear(); expectedNext = -1; phase = 0; lastSeek = -1; }
    // timeOfFirst: timeline seconds of samples[0]. playing=false -> input is ignored and any partial chunk flushed.
    void push (const float* x, int n, double timeOfFirst, bool playing, int seekCount, std::vector<AsrChunk>& out)
    {
        if (! playing) { flush (out); expectedNext = -1; return; }
        if (seekCount != lastSeek || (expectedNext >= 0 && std::fabs (timeOfFirst - expectedNext) > 0.5)) { flush (out); expectedNext = -1; lastSeek = seekCount; }
        if (buf.empty()) { startTime = timeOfFirst; phase = 0; }
        const double step = rate / 16000.0;
        // linear-interpolating decimator (input is band-limited well below 8 kHz for speech; a simple 2-tap average avoids gross aliasing)
        for (int i = 0; i < n; ++i)
        {
            phase += 1.0;
            while (phase >= step) { phase -= step; const float s = 0.5f * (x[i] + (i > 0 ? x[i - 1] : x[i])); buf.push_back (s); if (buf.size() >= chunkLen) emit (out, false); }
        }
        expectedNext = timeOfFirst + (double) n / rate;
    }
    void flush (std::vector<AsrChunk>& out) { if (buf.size() >= minFlush) emit (out, true); buf.clear(); }
    size_t pending() const { return buf.size(); }
private:
    void emit (std::vector<AsrChunk>& out, bool)
    {
        AsrChunk c; c.startTime = startTime; c.duration = (double) buf.size() / 16000.0; double e = 0; c.pcm.resize (buf.size());
        for (size_t i = 0; i < buf.size(); ++i) { const float v = std::clamp (buf[i], -1.f, 1.f); e += (double) v * v; c.pcm[i] = (int16_t) std::lround (v * 32767.f); }
        c.rms = buf.empty() ? 0.f : (float) std::sqrt (e / (double) buf.size()); c.silent = c.rms < silence;
        out.push_back (std::move (c)); startTime += (double) buf.size() / 16000.0; buf.clear(); phase = 0;
    }
    double rate = 44100, expectedNext = -1, startTime = 0, phase = 0; size_t chunkLen = 16000 * 12, minFlush = 32000; float silence = 0.004f; int lastSeek = -1; std::vector<float> buf;
};

inline std::vector<uint8_t> makeWav16 (const std::vector<int16_t>& pcm, int rate = 16000)
{
    std::vector<uint8_t> w; auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) (v >> (8 * i))); }; auto u16 = [&] (uint16_t v) { w.push_back ((uint8_t) v); w.push_back ((uint8_t) (v >> 8)); };
    const uint32_t dataBytes = (uint32_t) pcm.size() * 2;
    for (char c : { 'R', 'I', 'F', 'F' }) w.push_back ((uint8_t) c);
    u32 (36 + dataBytes);
    for (char c : { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }) w.push_back ((uint8_t) c);
    u32 (16); u16 (1); u16 (1); u32 ((uint32_t) rate); u32 ((uint32_t) rate * 2); u16 (2); u16 (16);
    for (char c : { 'd', 'a', 't', 'a' }) w.push_back ((uint8_t) c);
    u32 (dataBytes);
    for (int16_t s : pcm) u16 ((uint16_t) s);
    return w;
}
}
