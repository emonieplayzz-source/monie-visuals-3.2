// Standalone test of the analysis core. Build: g++ -std=c++17 -O2 -I Source/Core tests/test_analyzer.cpp -o analyzer_test
#include "AudioAnalyzer.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace monie;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else { std::printf("ok:   "); } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

static std::vector<float> kicks (double sr, double bpm, double seconds, float gain, bool hats)
{
    std::vector<float> x ((size_t) (sr * seconds), 0.f);
    std::mt19937 rng (1); std::uniform_real_distribution<float> u (-1, 1);
    const int period = (int) (sr * 60.0 / bpm);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) (i % (size_t) period) / sr;
        float s = (float) (std::exp (-t / 0.06) * std::sin (2 * 3.14159265 * 60.0 * t) * 0.9);
        if (hats) { const double th = (double) ((i + (size_t) period / 2) % (size_t) period) / sr; s += (float) (std::exp (-th / 0.015) * u (rng) * 0.25); }
        x[i] = s * gain;
    }
    return x;
}
static void run (AudioAnalyzer& a, const std::vector<float>& x, int block)
{ for (size_t i = 0; i < x.size(); i += (size_t) block) a.push (x.data() + i, (int) std::min<size_t> ((size_t) block, x.size() - i)); }

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        AudioAnalyzer a; a.prepare (sr);
        auto x = kicks (sr, 120, 8.0, 1.f, false);
        run (a, x, 480);   // odd block size on purpose
        const auto n = (int) a.getFrame().beatCount;
        CHECK (n >= 13 && n <= 19, "sr=%.0f: 16 kicks @120bpm -> %d beats detected", sr, n);
        AudioAnalyzer h; h.prepare (sr); run (h, kicks (sr, 120, 8.0, 1.f, true), 480);
        CHECK (h.getFrame().beatCount >= 13 && h.getFrame().beatCount <= 34, "sr=%.0f: kicks+offbeat hats (16 kicks + 16 hats) -> %u onsets", sr, h.getFrame().beatCount);
    }
    { // level invariance: -20 dB signal must still find roughly the same number of beats
        AudioAnalyzer a, b; auto x = kicks (48000, 120, 8, 1.f, false), y = kicks (48000, 120, 8, 0.1f, false);
        a.prepare (48000); b.prepare (48000); run (a, x, 256); run (b, y, 256);
        CHECK (std::abs ((int) a.getFrame().beatCount - (int) b.getFrame().beatCount) <= 3, "level invariance: %u vs %u beats", a.getFrame().beatCount, b.getFrame().beatCount);
    }
    { // band separation
        AudioAnalyzer a; a.prepare (48000); std::vector<float> x (48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * std::sin (2 * 3.14159265f * 80.f * (float) i / 48000.f);
        run (a, x, 512);
        CHECK (a.getFrame().bass > 0.8f && a.getFrame().treble < 0.1f, "80 Hz sine: bass=%.2f mid=%.2f treble=%.2f", a.getFrame().bass, a.getFrame().mid, a.getFrame().treble);
        std::vector<float> h (48000 * 2);
        AudioAnalyzer c; c.prepare (48000);
        for (size_t i = 0; i < h.size(); ++i) h[i] = 0.5f * std::sin (2 * 3.14159265f * 9000.f * (float) i / 48000.f);
        run (c, h, 512);
        CHECK (c.getFrame().treble > 0.8f && c.getFrame().bass < 0.1f, "9 kHz sine: bass=%.2f mid=%.2f treble=%.2f", c.getFrame().bass, c.getFrame().mid, c.getFrame().treble);
        CHECK (std::abs (a.getFrame().rms - 0.5f / std::sqrt (2.f)) < 0.03f, "RMS of 0.5-amplitude sine = %.3f (expect 0.354)", a.getFrame().rms);
    }
    { // new sub-bands: 500 Hz -> lowMid, 3 kHz -> highMid; transient fires on an onset
        AudioAnalyzer a; a.prepare (48000); std::vector<float> x (48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * std::sin (2 * 3.14159265f * 500.f * (float) i / 48000.f);
        run (a, x, 512);
        CHECK (a.getFrame().lowMid > 0.8f && a.getFrame().highMid < 0.1f, "500 Hz: lowMid=%.2f highMid=%.2f", a.getFrame().lowMid, a.getFrame().highMid);
        AudioAnalyzer b; b.prepare (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * std::sin (2 * 3.14159265f * 3000.f * (float) i / 48000.f);
        run (b, x, 512);
        CHECK (b.getFrame().highMid > 0.8f && b.getFrame().lowMid < 0.1f, "3 kHz: lowMid=%.2f highMid=%.2f", b.getFrame().lowMid, b.getFrame().highMid);
        AudioAnalyzer t; t.prepare (48000); std::vector<float> sil (48000, 0.f), burst (2048); std::mt19937 rg (3); std::uniform_real_distribution<float> uu (-1, 1);
        for (auto& v : burst) v = uu (rg) * 0.5f; run (t, sil, 512);
        float maxT = 0; for (size_t i = 0; i < burst.size(); i += 512) { t.push (burst.data() + i, 512); maxT = std::max (maxT, t.getFrame().transient); }
        CHECK (maxT > 0.3f, "transient on noise burst after silence = %.2f", maxT);
    }
    { // silence must produce no beats and no NaNs
        AudioAnalyzer a; std::vector<float> z (48000 * 3, 0.f); run (a, z, 512);
        CHECK (a.getFrame().beatCount == 0 && std::isfinite (a.getFrame().bass), "silence: %u beats", a.getFrame().beatCount);
    }
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
