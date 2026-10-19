// Animation engine for text / caption (and image) layers: ~36 presets, several can be stacked on one layer.
// Pure functions of (layer-local time, audio levels) -> AnimState, so scrubbing, seeking and offline export are exact.
// NOTE: the "per letter" looks (Typewriter, Letter Wave) are driven by horizontal position inside the text box in the shader,
// not by true per-glyph transforms. See docs/V3_STATUS.md.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
namespace monie
{
enum class AnimEase { Linear, Smooth, EaseIn, EaseOut, EaseInOut, Back, Bounce, Elastic };
inline const char* animEaseNames() { return "Linear|Smooth|Ease In|Ease Out|Ease In-Out|Back (overshoot)|Bounce|Elastic"; }
inline float bounceOut (float t) { const float n = 7.5625f, d = 2.75f; if (t < 1 / d) return n * t * t; if (t < 2 / d) { t -= 1.5f / d; return n * t * t + .75f; } if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + .9375f; } t -= 2.625f / d; return n * t * t + .984375f; }
inline float easeAnim (AnimEase e, float t)
{
    t = std::clamp (t, 0.f, 1.f);
    switch (e)
    {
        case AnimEase::Linear: return t;
        case AnimEase::Smooth: return t * t * (3 - 2 * t);
        case AnimEase::EaseIn: return t * t * t;
        case AnimEase::EaseOut: return 1 - std::pow (1 - t, 3.f);
        case AnimEase::EaseInOut: return t < .5f ? 4 * t * t * t : 1 - std::pow (-2 * t + 2, 3.f) / 2;
        case AnimEase::Back: { const float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * std::pow (t - 1, 3.f) + c1 * std::pow (t - 1, 2.f); }
        case AnimEase::Bounce: return bounceOut (t);
        case AnimEase::Elastic: return t <= 0 ? 0.f : t >= 1 ? 1.f : std::pow (2.f, -10 * t) * std::sin ((t * 10 - .75f) * (2 * 3.14159265f / 3)) + 1;
    }
    return t;
}

enum class AnimKind { In, Out, Loop };
struct AnimPreset { const char* name; AnimKind kind; bool usesDir; const char* desc; };
inline const std::vector<AnimPreset>& animPresets()
{
    static const std::vector<AnimPreset> p = {
        { "Fade In", AnimKind::In, false, "Opacity rises from zero." },
        { "Slide In", AnimKind::In, true, "Slides in from the chosen direction while fading." },
        { "Pop In", AnimKind::In, false, "Scales up from small with a springy settle (use Back easing)." },
        { "Zoom In", AnimKind::In, false, "Starts large and settles to size." },
        { "Blur In", AnimKind::In, false, "Comes into focus." },
        { "Typewriter", AnimKind::In, false, "Reveals left to right in steps (position-based, not per glyph)." },
        { "Wipe In", AnimKind::In, false, "Soft left-to-right reveal." },
        { "Spin In", AnimKind::In, false, "Rotates and scales into place." },
        { "Drop In", AnimKind::In, false, "Falls from above and bounces." },
        { "Glitch In", AnimKind::In, false, "Tears and splits, then stabilises." },
        { "RGB Split In", AnimKind::In, false, "Colour channels converge." },
        { "Rise & Fade", AnimKind::In, false, "Floats up gently while fading in." },
        { "Elastic In", AnimKind::In, false, "Overshoots and wobbles to size." },
        { "Fade Out", AnimKind::Out, false, "Opacity falls to zero at the end of the layer." },
        { "Slide Out", AnimKind::Out, true, "Slides away toward the chosen direction." },
        { "Zoom Out", AnimKind::Out, false, "Grows and fades away." },
        { "Blur Out", AnimKind::Out, false, "Dissolves out of focus." },
        { "Wipe Out", AnimKind::Out, false, "Soft right-to-left hide." },
        { "Glitch Out", AnimKind::Out, false, "Breaks up and disappears." },
        { "Shrink Out", AnimKind::Out, false, "Shrinks to nothing." },
        { "Float", AnimKind::Loop, false, "Gentle drifting motion." },
        { "Beat Pulse", AnimKind::Loop, false, "Scales on every detected beat." },
        { "Letter Wave", AnimKind::Loop, false, "A travelling wave moves along the text." },
        { "Shake", AnimKind::Loop, false, "Jittery camera-shake." },
        { "Wobble", AnimKind::Loop, false, "Rocking rotation." },
        { "Flicker", AnimKind::Loop, false, "Random brightness dropouts." },
        { "Glitch Flicker", AnimKind::Loop, false, "Occasional glitch bursts." },
        { "RGB Pulse", AnimKind::Loop, false, "Colour channels breathe apart and together." },
        { "Glow Breath", AnimKind::Loop, false, "Glow swells and fades." },
        { "Colour Cycle", AnimKind::Loop, false, "Slowly rotates hue." },
        { "Slow Drift", AnimKind::Loop, false, "Very slow push-in (Ken Burns)." },
        { "Strobe", AnimKind::Loop, false, "Hard on/off blinking." },
        { "Swing", AnimKind::Loop, false, "Pendulum swing with skew." },
        { "Bounce", AnimKind::Loop, false, "Continuous hopping." },
        { "Heartbeat", AnimKind::Loop, false, "Double-thump scale pulse." },
        { "Beat Glow Flash", AnimKind::Loop, false, "Glow flashes with the beat." },
    };
    return p;
}

struct AnimInst
{
    int id = 0, preset = 0; double start = 0, dur = .6; float speed = 1, intensity = 1; int dir = 0, easing = 3, audioSrc = 0; float audioAmt = 0; bool enabled = true;
    // dir: 0 up, 1 down, 2 left, 3 right; start: seconds after layer start (In/Loop) or before layer end (Out)
};
struct AnimState
{
    float dx = 0, dy = 0, scale = 1, rot = 0, alpha = 1, blur = 0, rgb = 0, glitch = 0, reveal = 1, waveAmp = 0, waveFreq = 12, wavePhase = 0, shake = 0, skewX = 0, hue = 0, glow = 0;
};
struct AnimCtx { double t = 0, layerLen = -1; float audio[16] {}; float beatEnv = 0; };   // audio[] indexed like the audioSrcNames() list (0 = none)

inline float animHash (float x) { float s = std::sin (x * 127.1f) * 43758.5453f; return s - std::floor (s); }
inline void dirVec (int d, float& x, float& y) { x = d == 2 ? -1.f : d == 3 ? 1.f : 0.f; y = d == 0 ? 1.f : d == 1 ? -1.f : 0.f; }

inline void applyAnim (const AnimInst& a, const AnimCtx& c, AnimState& s)
{
    const auto& presets = animPresets(); if (! a.enabled || a.preset < 0 || a.preset >= (int) presets.size()) return;
    const auto kind = presets[(size_t) a.preset].kind;
    float I = a.intensity; if (a.audioSrc > 0 && a.audioSrc < 16) I *= 1.f + a.audioAmt * c.audio[a.audioSrc];
    const double d = std::max (0.02, a.dur / std::max (0.05f, a.speed));
    float u = 0, e = 0; const auto ez = (AnimEase) std::clamp (a.easing, 0, 7);
    if (kind == AnimKind::In) { u = (float) std::clamp ((c.t - a.start) / d, 0.0, 1.0); e = easeAnim (ez, u); if (u >= 1.f && a.preset != 12) return; }
    else if (kind == AnimKind::Out) { if (c.layerLen < 0) return; const double t0 = c.layerLen - a.start - d; u = (float) std::clamp ((c.t - t0) / d, 0.0, 1.0); e = easeAnim (ez, u); if (u <= 0.f) return; }
    const float ph = (float) c.t * a.speed;
    float dxv = 0, dyv = 0; dirVec (a.dir, dxv, dyv);
    switch (a.preset)
    {
        case 0: s.alpha *= e; break;
        case 1: s.dx += -dxv * (1 - e) * .35f * I; s.dy += -dyv * (1 - e) * .35f * I; s.alpha *= std::min (1.f, e * 2.f); break;
        case 2: s.scale *= std::max (0.f, 1 - .7f * I * (1 - e)); s.alpha *= std::clamp (u * 3.f, 0.f, 1.f); break;
        case 3: s.scale *= 1 + (1 - e) * 1.5f * I; s.alpha *= e; break;
        case 4: s.blur += (1 - e) * I; s.alpha *= std::clamp (u * 2.f, 0.f, 1.f); break;
        case 5: s.reveal = std::min (s.reveal, std::floor (u * 24.f + .0001f) / 24.f + (u >= 1.f ? 1.f : 0.f)); break;
        case 6: s.reveal = std::min (s.reveal, e >= 1.f ? 1.f : e); break;
        case 7: s.rot += (1 - e) * 3.14159f * I; s.scale *= .3f + .7f * e; s.alpha *= e; break;
        case 8: s.dy += (1 - bounceOut (u)) * .6f * I; s.alpha *= std::clamp (u * 4.f, 0.f, 1.f); break;
        case 9: s.glitch += (1 - u) * I; s.rgb += (1 - u) * I * .6f; s.alpha *= u < .12f ? 0.f : 1.f; break;
        case 10: s.rgb += (1 - e) * I * 1.5f; s.alpha *= e; break;
        case 11: s.dy -= (1 - e) * .12f * I; s.alpha *= e; break;
        case 12: s.scale *= u >= 1.f ? 1.f : std::max (0.f, easeAnim (AnimEase::Elastic, u)); s.alpha *= std::clamp (u * 4.f, 0.f, 1.f); break;
        case 13: s.alpha *= 1 - e; break;
        case 14: s.dx += dxv * e * .35f * I; s.dy += dyv * e * .35f * I; s.alpha *= 1 - e * .8f; break;
        case 15: s.scale *= 1 + e * 1.2f * I; s.alpha *= 1 - e; break;
        case 16: s.blur += e * I; s.alpha *= 1 - e * e; break;
        case 17: s.reveal = std::min (s.reveal, 1 - e); break;
        case 18: s.glitch += e * I; s.rgb += e * I * .6f; s.alpha *= u > .9f ? 0.f : 1.f; break;
        case 19: s.scale *= std::max (0.f, 1 - e * .9f * I); s.alpha *= 1 - e * e; break;
        case 20: s.dy += std::sin (ph * 2.f) * .015f * I; s.dx += std::sin (ph * 1.3f) * .008f * I; break;
        case 21: s.scale *= 1 + I * .12f * c.beatEnv; break;
        case 22: s.waveAmp += .06f * I; s.waveFreq = 10.f; s.wavePhase = ph * 4.f; break;
        case 23: s.shake += .02f * I; break;
        case 24: s.rot += std::sin (ph * 3.f) * .08f * I; break;
        case 25: s.alpha *= 1 - std::clamp (I, 0.f, 1.5f) * .6f * (animHash (std::floor (ph * 24.f)) > .7f ? 1.f : 0.f); break;
        case 26: s.glitch += I * (animHash (std::floor (ph * 10.f)) > .8f ? 1.f : 0.f); break;
        case 27: s.rgb += I * .8f * (.5f + .5f * std::sin (ph * 4.f)); break;
        case 28: s.glow += I * (.5f + .5f * std::sin (ph * 2.f)); break;
        case 29: s.hue += ph * .1f - std::floor (ph * .1f); break;
        case 30: s.scale *= 1 + std::min (.25f, .004f * ph * I); break;
        case 31: s.alpha *= std::fmod (ph * 4.f, 1.f) < .5f ? 1.f : 0.f; break;
        case 32: s.rot += std::sin (ph * 2.f) * .2f * I; s.skewX += std::sin (ph * 2.f) * .1f * I; break;
        case 33: s.dy += std::fabs (std::sin (ph * 3.f)) * .05f * I; break;
        case 34: s.scale *= 1 + I * .1f * std::pow (std::max (0.f, std::sin (ph * 5.f)), 8.f); break;
        case 35: s.glow += I * c.beatEnv * 2.f; break;
        default: break;
    }
}
inline AnimState evalAnims (const std::vector<AnimInst>& list, const AnimCtx& c)
{ AnimState s; for (auto& a : list) applyAnim (a, c, s); s.alpha = std::clamp (s.alpha, 0.f, 1.f); s.scale = std::max (0.f, s.scale); s.blur = std::max (0.f, s.blur); s.reveal = std::clamp (s.reveal, 0.f, 1.f); return s; }
}
