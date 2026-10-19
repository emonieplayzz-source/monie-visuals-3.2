#include "TextAnim.h"
#include "TransportClock.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
using namespace monie;
static bool near (double a, double b, double e = 1e-3) { return std::fabs (a - b) < e; }
int main()
{
    CHECK (animPresets().size() >= 25, "%zu animation presets", animPresets().size());
    // every preset finite + sane over time, with and without audio
    bool fin = true;
    for (size_t p = 0; p < animPresets().size(); ++p)
        for (int e = 0; e < 8; ++e)
            for (double t = -1; t < 6; t += .13)
            { AnimInst a; a.preset = (int) p; a.easing = e; a.audioSrc = 1; a.audioAmt = .5f; AnimCtx c; c.t = t; c.layerLen = 5; c.audio[1] = .7f; c.beatEnv = .5f; auto s = evalAnims ({ a }, c);
              float v[] = { s.dx, s.dy, s.scale, s.rot, s.alpha, s.blur, s.rgb, s.glitch, s.reveal, s.waveAmp, s.shake, s.skewX, s.hue, s.glow }; for (float x : v) if (! std::isfinite (x)) fin = false; if (s.alpha < -1e-4f || s.alpha > 1.0001f || s.scale < -1e-4f) fin = false; }
    CHECK (fin, "all presets x easings produce finite, bounded state");
    { AnimInst a; a.preset = 0; a.dur = 1; a.easing = 0; AnimCtx c; c.t = 0; CHECK (near (evalAnims ({ a }, c).alpha, 0), "fade in starts at 0");
      c.t = .5; CHECK (near (evalAnims ({ a }, c).alpha, .5), "fade in halfway"); c.t = 2; CHECK (near (evalAnims ({ a }, c).alpha, 1), "fade in settled"); }
    { AnimInst a; a.preset = 13; a.dur = 1; a.easing = 0; AnimCtx c; c.layerLen = 5; c.t = 2; CHECK (near (evalAnims ({ a }, c).alpha, 1), "fade out inactive before its window");
      c.t = 4.5; CHECK (near (evalAnims ({ a }, c).alpha, .5), "fade out halfway"); c.t = 5; CHECK (near (evalAnims ({ a }, c).alpha, 0), "fade out complete at layer end");
      c.layerLen = -1; c.t = 4.9; CHECK (near (evalAnims ({ a }, c).alpha, 1), "out animation ignored when layer length unknown"); }
    { AnimInst a, b; a.preset = 0; a.dur = 1; a.easing = 0; b.preset = 13; b.dur = 1; b.easing = 0; AnimCtx c; c.layerLen = 5; c.t = .5; CHECK (near (evalAnims ({ a, b }, c).alpha, .5), "multiple animations stack (in + out)"); }
    { AnimInst a; a.preset = 5; a.dur = 1; a.easing = 0; AnimCtx c; c.t = .5; float r = evalAnims ({ a }, c).reveal; CHECK (r > .45f && r < .55f, "typewriter reveal %.2f", r); c.t = 1.1; CHECK (evalAnims ({ a }, c).reveal >= 1.f, "typewriter completes"); }
    { AnimInst a; a.preset = 1; a.dir = 2; a.dur = 1; a.easing = 0; AnimCtx c; c.t = 0; auto s = evalAnims ({ a }, c); CHECK (s.dx > 0 && near (s.dy, 0), "slide in from left starts offset to the left (dx=%.2f)", s.dx); }
    { AnimInst a; a.preset = 21; a.intensity = 1; AnimCtx c; c.beatEnv = 1; CHECK (evalAnims ({ a }, c).scale > 1.1f, "beat pulse reacts to beat envelope"); c.beatEnv = 0; CHECK (near (evalAnims ({ a }, c).scale, 1), "beat pulse idle without beat"); }
    { AnimInst a; a.preset = 22; AnimCtx c; c.t = 1.234; auto s1 = evalAnims ({ a }, c); auto s2 = evalAnims ({ a }, c); CHECK (s1.wavePhase == s2.wavePhase && s1.waveAmp > 0, "evaluation is deterministic"); }
    // ---- transport clock ----
    { TransportClock t; t.hostUpdate (true, true, 10.0, 100.0); CHECK (t.mode == TransportClock::Mode::Host, "host mode engaged");
      CHECK (near (t.now (100.5), 10.5), "extrapolates between host updates");
      t.hostUpdate (true, true, 10.52, 100.5); CHECK (near (t.now (100.5), 10.504, .01), "small error is slewed, not snapped (%.3f)", t.now (100.5));
      int sc = t.seekCount; t.hostUpdate (true, true, 3.0, 100.6); CHECK (near (t.now (100.6), 3.0) && t.seekCount == sc + 1, "seek snaps immediately and bumps seekCount");
      t.hostUpdate (true, false, 3.2, 101.0); CHECK (near (t.now (105.0), 3.2), "paused: holds host position regardless of wall clock");
      t.hostUpdate (true, true, 3.2, 110.0); CHECK (near (t.now (110.25), 3.45), "resume continues from held position");
      t.hostUpdate (false, false, 0, 111.0); CHECK (t.mode == TransportClock::Mode::Internal, "host disappears -> internal mode"); }
    { TransportClock t; t.play (0); CHECK (near (t.now (2.0), 2.0), "internal play"); t.pause (2.0); CHECK (near (t.now (9.0), 2.0), "internal pause holds");
      t.seek (5.0, 9.0); t.play (9.0); CHECK (near (t.now (10.0), 6.0), "internal seek+play"); t.setLoop (true, 4.0, 8.0); t.seek (4.0, 20.0); t.play (20.0); CHECK (near (t.now (23.0), 7.0) && near (t.now (25.0), 5.0), "loop region wraps (%.2f)", t.now (25.0));
      t.stop (30.0); CHECK (near (t.now (31.0), 0.0), "stop returns to zero"); }
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
