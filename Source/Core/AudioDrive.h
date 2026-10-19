// Turns raw analysis frames into the smoothed, sensitivity-scaled control signals the renderer uses (live and offline).
// Pure function of (previous state, dt, raw frame, params) so live preview and exported video behave identically.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "AudioMapping.h"
namespace monie
{
struct RawAudio
{
    float bass = 0, lowMid = 0, mid = 0, highMid = 0, treble = 0, rms = 0, peak = 0, flux = 0, transient = 0; uint32_t beatCount = 0;
    float bands[64] {}; float wave[64] {};
};
struct DriveParams { float sensBass = 1, sensMid = 1, sensTreble = 1, sensBeat = 1, master = .75f, smooth = .3f, attack = .6f, release = .5f; };
struct Drive
{
    float bass = 0, lmid = 0, mid = 0, hmid = 0, treb = 0, rms = 0, peak = 0, flux = 0, trans = 0, beat = 0;
    float beatAge = 10.f; uint32_t beatId = 0; bool beatNow = false;
    float bands[64] {}; float wave[64] {};
    // index = audioSrcNames() order: 0 None, 1 Bass, 2 Low-Mid, 3 Mid, 4 High-Mid, 5 Treble, 6 RMS, 7 Peak, 8 Transient, 9 Beat, 10 Flux
    float bySource (int i) const { switch (i) { case 1: return bass; case 2: return lmid; case 3: return mid; case 4: return hmid; case 5: return treb; case 6: return rms; case 7: return peak; case 8: return trans; case 9: return beat; case 10: return flux; } return 0.f; }
    Features features() const { Features f; f.v[(int) Source::Bass] = bass; f.v[(int) Source::LowMid] = lmid; f.v[(int) Source::Mid] = mid; f.v[(int) Source::HighMid] = hmid; f.v[(int) Source::Treble] = treb;
        f.v[(int) Source::Rms] = rms; f.v[(int) Source::Peak] = peak; f.v[(int) Source::Transient] = trans; f.v[(int) Source::Beat] = beat; f.v[(int) Source::Flux] = flux; f.beatNow = beatNow; return f; }
};
class AudioDrive
{
public:
    void reset() { d = Drive(); lastBeat = 0; first = true; beatEnv = 0; }
    const Drive& update (float dt, const RawAudio& a, const DriveParams& p)
    {
        dt = std::clamp (dt, 1e-4f, 0.25f);
        const float drive = .25f + p.master * 1.5f, sm = 1.f - .8f * p.smooth;
        const float atk = (3.f + p.attack * 77.f) * sm, rel = (30.f - p.release * 28.5f) * sm;
        auto env = [&] (float& e, float target) { const float r = target > e ? atk : rel; e += (target - e) * (1.f - std::exp (-dt * r)); };
        env (d.bass, std::min (1.6f, a.bass * p.sensBass * drive));
        env (d.lmid, std::min (1.6f, a.lowMid * p.sensMid * drive));
        env (d.mid, std::min (1.6f, a.mid * p.sensMid * drive));
        env (d.hmid, std::min (1.6f, a.highMid * p.sensTreble * drive));
        env (d.treb, std::min (1.6f, a.treble * p.sensTreble * drive));
        env (d.rms, std::min (1.6f, a.rms * 2.f * drive));
        env (d.peak, std::min (1.6f, a.peak * drive));
        env (d.flux, std::min (1.6f, a.flux * drive));
        env (d.trans, std::min (1.6f, a.transient * drive));
        d.beatNow = false; d.beatAge = std::min (30.f, d.beatAge + dt);
        if (first) { lastBeat = a.beatCount; first = false; }
        if (a.beatCount != lastBeat) { lastBeat = a.beatCount; beatEnv = 1.f; d.beatNow = true; d.beatId = d.beatId + 1; d.beatAge = 0.f; }
        beatEnv *= std::exp (-dt * 5.f);
        d.beat = std::min (1.6f, beatEnv * p.sensBeat * drive);
        for (int i = 0; i < 64; ++i)
        {
            const float sens = i < 16 ? p.sensBass : (i < 40 ? p.sensMid : p.sensTreble);
            env (d.bands[i], std::min (1.f, a.bands[i] * sens * drive)); d.wave[i] = a.wave[i];
        }
        return d;
    }
    float beatEnvelope() const { return beatEnv; }
private:
    Drive d; uint32_t lastBeat = 0; bool first = true; float beatEnv = 0;
};
}
