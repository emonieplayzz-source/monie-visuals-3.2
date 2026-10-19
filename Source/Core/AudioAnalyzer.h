// monie visuals - audio analysis core. Plain C++17, no JUCE dependency, so it is unit-testable anywhere.
// Runs on the ANALYSIS thread only (never on the audio thread).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace monie
{
struct AnalysisFrame
{
    static constexpr int numBands = 64;
    static constexpr int numWave = 64;
    float bass = 0, mid = 0, treble = 0;   // 0..1, level-normalised (per-band AGC)
    float lowMid = 0, highMid = 0;         // 250-800 Hz and 2-4 kHz sub-bands
    float transient = 0;                   // positive jump of normalised flux, 0..1
    float rms = 0, peak = 0;               // 0..1 (linear, 0 dBFS = 1)
    float flux = 0;                        // 0..1 normalised spectral flux
    uint32_t beatCount = 0;                // increments on every detected beat
    float bands[numBands] {};              // 0..1 log-spaced spectrum
    float wave[numWave] {};                // 0..1, 0.5 = zero
};

class AudioAnalyzer
{
public:
    static constexpr int fftSize = 2048;
    static constexpr int hop = 512;
    static constexpr int numBands = AnalysisFrame::numBands;

    AudioAnalyzer() { buildTables(); prepare (44100.0); }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 8000.0 ? sampleRate : 44100.0;
        reset();
        const double binHz = sr / fftSize;
        auto bin = [&] (double hz) { return std::clamp ((int) std::ceil (hz / binHz), 1, fftSize / 2 - 1); };
        bassLo = bin (20);  bassHi = bin (250);
        midHi  = bin (4000);
        lowMidHi = bin (800); highMidLo = bin (2000);
        trebHi = bin (std::min (16000.0, sr * 0.45));
        fluxHi = bin (std::min (8000.0, sr * 0.45));
        const double top = std::min (16000.0, sr * 0.45);
        for (int b = 0; b < numBands; ++b)
        {
            const double f0 = 30.0 * std::pow (top / 30.0, (double) b / numBands);
            const double f1 = 30.0 * std::pow (top / 30.0, (double) (b + 1) / numBands);
            bandLo[b] = bin (f0);
            bandHi[b] = std::max (bandLo[b], bin (f1));
        }
        refractoryFrames = std::max (1, (int) std::ceil (0.12 * sr / hop));
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.f);
        std::fill (prevLog.begin(), prevLog.end(), 0.f);
        fluxHist.fill (0.f);
        agcBass = agcMid = agcTreb = agcFlux = agcLowMid = agcHighMid = 1e-3f; prevFluxN = 0;
        std::fill (std::begin (agcBand), std::end (agcBand), 1e-3f);
        wpos = 0; sinceHop = 0; histPos = 0; histCount = 0; sinceBeat = 1000;
        frame = {};
        accSq = 0; accPeak = 0; peakHold = 0;
    }

    // 1.0 = default. Higher = more beats detected.
    void setBeatSensitivity (float s) { beatK = std::clamp (2.2f - 0.5f * s, 0.6f, 3.0f); }

    // Returns true if at least one new analysis frame was produced.
    bool push (const float* x, int n)
    {
        bool updated = false;
        for (int i = 0; i < n; ++i)
        {
            const float s = x[i];
            ring[(size_t) wpos] = s;
            wpos = (wpos + 1) & (fftSize - 1);
            accSq += s * s;
            accPeak = std::max (accPeak, std::fabs (s));
            if (++sinceHop >= hop) { processFrame(); sinceHop = 0; updated = true; }
        }
        return updated;
    }

    const AnalysisFrame& getFrame() const { return frame; }

private:
    void buildTables()
    {
        ring.assign (fftSize, 0.f);
        prevLog.assign (fftSize / 2, 0.f);
        win.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            win[(size_t) i] = 0.5f * (1.f - std::cos (2.f * 3.14159265358979f * i / (fftSize - 1))); // Hann
        cosT.resize (fftSize / 2); sinT.resize (fftSize / 2);
        for (int i = 0; i < fftSize / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (-2.0 * 3.14159265358979 * i / fftSize);
            sinT[(size_t) i] = (float) std::sin (-2.0 * 3.14159265358979 * i / fftSize);
        }
        re.resize (fftSize); im.resize (fftSize); mag.resize (fftSize / 2);
        rev.resize (fftSize);
        int bits = 0; while ((1 << bits) < fftSize) ++bits;
        for (int i = 0; i < fftSize; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void fft()
    {
        for (int i = 0; i < fftSize; ++i) { int j = rev[(size_t) i]; if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); } }
        for (int len = 2; len <= fftSize; len <<= 1)
        {
            const int half = len >> 1, step = fftSize / len;
            for (int i = 0; i < fftSize; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float c = cosT[(size_t) (k * step)], s = sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[(size_t) b] * c - im[(size_t) b] * s;
                    const float ti = re[(size_t) b] * s + im[(size_t) b] * c;
                    re[(size_t) b] = re[(size_t) a] - tr; im[(size_t) b] = im[(size_t) a] - ti;
                    re[(size_t) a] += tr;                 im[(size_t) a] += ti;
                }
        }
    }

    float bandEnergy (int lo, int hi) const
    {
        double s = 0; for (int k = lo; k <= hi; ++k) s += (double) mag[(size_t) k] * mag[(size_t) k];
        return (float) std::sqrt (s);
    }
    static float agc (float e, float& state, float decay)
    {
        state = std::max ({ e, state * decay, 1e-3f });
        return std::clamp (e / state, 0.f, 1.f);
    }

    void processFrame()
    {
        for (int i = 0; i < fftSize; ++i)
        {
            re[(size_t) i] = ring[(size_t) ((wpos + i) & (fftSize - 1))] * win[(size_t) i];
            im[(size_t) i] = 0.f;
        }
        fft();
        const float norm = 4.f / fftSize; // Hann coherent gain -> sine of amplitude A reads ~A
        for (int k = 0; k < fftSize / 2; ++k)
            mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm;

        const float decay = 0.9993f;
        frame.bass   = agc (bandEnergy (bassLo, bassHi), agcBass, decay);
        frame.mid    = agc (bandEnergy (bassHi + 1, midHi), agcMid, decay);
        frame.treble = agc (bandEnergy (midHi + 1, trebHi), agcTreb, decay);
        frame.lowMid  = agc (bandEnergy (bassHi + 1, lowMidHi), agcLowMid, decay);
        frame.highMid = agc (bandEnergy (highMidLo, midHi), agcHighMid, decay);

        for (int b = 0; b < numBands; ++b)
        {
            double s = 0; for (int k = bandLo[b]; k <= bandHi[b]; ++k) s += mag[(size_t) k];
            frame.bands[b] = agc ((float) (s / (bandHi[b] - bandLo[b] + 1)) * 4.f, agcBand[b], 0.9996f);
        }

        // spectral flux (positive log-magnitude differences)
        float flux = 0;
        for (int k = 1; k <= fluxHi; ++k)
        {
            const float l = std::log1p (mag[(size_t) k] * 100.f);
            flux += std::max (0.f, l - prevLog[(size_t) k]);
            prevLog[(size_t) k] = l;
        }
        flux /= (float) fluxHi;
        frame.flux = agc (flux, agcFlux, 0.998f);
        frame.transient = std::max (0.f, frame.flux - prevFluxN); prevFluxN = frame.flux;

        // beat detection: adaptive threshold mean + k*sigma over ~0.5 s of flux history
        float mean = 0, var = 0;
        const int n = std::min (histCount, (int) fluxHist.size());
        for (int i = 0; i < n; ++i) mean += fluxHist[(size_t) i];
        if (n > 0) mean /= (float) n;
        for (int i = 0; i < n; ++i) { const float d = fluxHist[(size_t) i] - mean; var += d * d; }
        const float sigma = n > 0 ? std::sqrt (var / (float) n) : 0.f;
        ++sinceBeat;
        const float rmsNow = std::sqrt (accSq / hop);
        if (n >= 16 && sinceBeat >= refractoryFrames && rmsNow > 1e-3f
            && flux > mean + beatK * sigma && flux > 0.004f)
        {
            ++frame.beatCount; sinceBeat = 0;
        }
        fluxHist[(size_t) histPos] = flux; histPos = (histPos + 1) % (int) fluxHist.size(); ++histCount;

        // levels
        frame.rms = std::min (1.f, rmsNow);
        peakHold = std::max (accPeak, peakHold * 0.92f);
        frame.peak = std::min (1.f, peakHold);
        accSq = 0; accPeak = 0;

        // waveform snapshot (last 64*8 samples, decimated)
        for (int i = 0; i < AnalysisFrame::numWave; ++i)
            frame.wave[i] = std::clamp (0.5f + 0.5f * ring[(size_t) ((wpos + fftSize - AnalysisFrame::numWave * 8 + i * 8) & (fftSize - 1))], 0.f, 1.f);
    }

    double sr = 44100.0;
    int bassLo = 1, bassHi = 5, midHi = 90, trebHi = 370, fluxHi = 180, lowMidHi = 40, highMidLo = 100, refractoryFrames = 10;
    int bandLo[numBands] {}, bandHi[numBands] {};
    std::vector<float> ring, win, cosT, sinT, re, im, mag, prevLog;
    std::vector<int> rev;
    std::array<float, 43> fluxHist {};
    float agcBass = 1e-3f, agcMid = 1e-3f, agcTreb = 1e-3f, agcFlux = 1e-3f, agcLowMid = 1e-3f, agcHighMid = 1e-3f, prevFluxN = 0, agcBand[numBands] {};
    int wpos = 0, sinceHop = 0, histPos = 0, histCount = 0, sinceBeat = 1000;
    float accSq = 0, accPeak = 0, peakHold = 0, beatK = 1.7f;
    AnalysisFrame frame;
};
} // namespace monie
