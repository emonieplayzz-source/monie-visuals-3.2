// Effect catalogue (30 GPU effects). Each effect has up to 4 generic parameters with its own labels, plus the common
// intensity / blend / audio controls. The GLSL side is Shaders/fx.frag (branch on u_type). No JUCE dependency.
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include "PropSchema.h"
namespace monie
{
struct FxParamDef { std::string label; float minV = 0, maxV = 1, defV = .5f; };
struct FxDef { int type; std::string name, category, desc; FxParamDef p[4]; bool usesColor = false; std::string colorDef = "#ffffff"; };

inline const std::vector<FxDef>& fxCatalogue()
{
    using P = FxParamDef; const P none { "", 0, 1, 0 };
    static const std::vector<FxDef> d = {
        { 0, "Liquid Distortion", "Distortion", "Flowing liquid warp of everything beneath it.", { P{"Scale", .3f, 8, 2}, P{"Turbulence", 0, 1, .5f}, P{"Speed", 0, 4, 1}, none } },
        { 1, "Wave Distortion", "Distortion", "Sine ripples across the image.", { P{"Frequency", 1, 40, 10}, P{"Angle", -180, 180, 0}, P{"Speed", 0, 6, 1.5f}, none } },
        { 2, "Swirl", "Distortion", "Twists the image around a point.", { P{"Radius", .05f, 1.5f, .6f}, P{"Center X", -1, 1, 0}, P{"Center Y", -1, 1, 0}, P{"Twist", -6, 6, 2} } },
        { 3, "Noise Displacement", "Noise", "Pushes pixels around with animated noise.", { P{"Scale", 1, 30, 8}, P{"Speed", 0, 4, 1}, P{"Detail", 0, 1, .5f}, P{"Bias Angle", -180, 180, 0} } },
        { 4, "Kaleidoscope", "Geometry", "Mirrored radial segments.", { P{"Segments", 2, 16, 6}, P{"Rotation", -180, 180, 0}, P{"Center X", -1, 1, 0}, P{"Center Y", -1, 1, 0} } },
        { 5, "Mirror", "Geometry", "Reflect the image for instant symmetry.", { P{"Mode (0-3)", 0, 3, 0}, P{"Offset", -.5f, .5f, 0}, none, none } },
        { 6, "RGB Split", "Glitch", "Separates the color channels.", { P{"Spread", 0, .1f, .02f}, P{"Angle", -180, 180, 0}, none, none } },
        { 7, "Chromatic Aberration", "Lens", "Lens-style color fringing toward the edges.", { P{"Strength", 0, .1f, .03f}, P{"Falloff", .5f, 4, 2}, none, none } },
        { 8, "Pixelate", "Stylize", "Chunky pixels.", { P{"Pixel Size", 2, 120, 16}, none, none, none } },
        { 9, "Posterize", "Color", "Limits the number of tone levels.", { P{"Levels", 2, 16, 5}, none, none, none } },
        { 10, "Threshold", "Stylize", "Hard black/color split by brightness.", { P{"Level", 0, 1, .5f}, P{"Softness", 0, .5f, .02f}, none, none }, true, "#ffffff" },
        { 11, "Invert", "Color", "Inverts colors.", { none, none, none, none } },
        { 12, "Blur", "Blur & Sharpen", "Soft blur, optionally directional.", { P{"Radius", 0, 40, 6}, P{"Angle", -180, 180, 0}, P{"Directional", 0, 1, 0}, none } },
        { 13, "Sharpen", "Blur & Sharpen", "Unsharp mask.", { P{"Radius", .5f, 6, 1.5f}, none, none, none } },
        { 14, "Bloom", "Glow", "Bright areas bleed light.", { P{"Threshold", 0, 1, .6f}, P{"Radius", 1, 40, 12}, P{"Intensity", 0, 3, 1.2f}, none } },
        { 15, "Glow", "Glow", "Soft tinted glow around the whole image.", { P{"Radius", 1, 40, 10}, P{"Intensity", 0, 3, 1}, P{"Tint", 0, 1, .5f}, none }, true, "#00e5ff" },
        { 16, "Film Grain", "Film & Analog", "Animated film grain.", { P{"Size", .5f, 4, 1}, P{"Color Amount", 0, 1, .3f}, P{"Speed", 0, 4, 1}, none } },
        { 17, "Vignette", "Film & Analog", "Darkens the corners.", { P{"Radius", .2f, 1.5f, .75f}, P{"Softness", .05f, 1, .5f}, none, none } },
        { 18, "Scanlines", "Film & Analog", "CRT scanlines.", { P{"Density", 100, 1200, 400}, P{"Thickness", 0, 1, .5f}, P{"Flicker", 0, 1, .1f}, none } },
        { 19, "Feedback Trails", "Feedback", "Leaves fading trails; slowly zooms and rotates the echo.", { P{"Persistence", 0, .98f, .8f}, P{"Zoom", -.05f, .05f, .006f}, P{"Rotation", -.1f, .1f, 0}, none } },
        { 20, "Smear", "Feedback", "Directional motion smear.", { P{"Length", 0, 1, .4f}, P{"Angle", -180, 180, 90}, P{"Persistence", 0, .98f, .5f}, none } },
        { 21, "Gradient Map", "Color", "Recolors by brightness using the active gradient.", { P{"Shift", -1, 1, 0}, P{"Contrast", .2f, 3, 1}, none, none } },
        { 22, "Edge Detect", "Stylize", "Outlines edges.", { P{"Strength", .5f, 6, 2}, P{"Thickness", .5f, 4, 1}, P{"Keep Original", 0, 1, 0}, none }, true, "#ffffff" },
        { 23, "Glitch", "Glitch", "Digital block and slice corruption.", { P{"Block Size", .01f, .3f, .08f}, P{"Frequency", 0, 1, .4f}, P{"Slice Shift", 0, 1, .4f}, P{"Rate", 1, 30, 8} } },
        { 24, "VHS", "Film & Analog", "Tape tracking, bleed and noise.", { P{"Tracking", 0, 1, .4f}, P{"Color Bleed", 0, 1, .5f}, P{"Noise", 0, 1, .3f}, P{"Wobble", 0, 1, .3f} } },
        { 25, "Color Grade", "Color", "Quick hue / saturation / contrast / brightness.", { P{"Hue", 0, 1, 0}, P{"Saturation", 0, 2, 1}, P{"Contrast", 0, 2, 1}, P{"Brightness", 0, 2, 1} } },
        { 26, "Radial Blur", "Blur & Sharpen", "Zoom blur from a center point.", { P{"Strength", 0, 1, .3f}, P{"Center X", -1, 1, 0}, P{"Center Y", -1, 1, 0}, none } },
        { 27, "Halftone", "Stylize", "Print-style dots.", { P{"Dot Size", 3, 30, 10}, P{"Angle", -90, 90, 30}, P{"Contrast", .3f, 3, 1.2f}, none } },
        { 28, "Lens Warp", "Distortion", "Barrel / pincushion lens.", { P{"Amount", -1, 1, .5f}, P{"Center X", -1, 1, 0}, P{"Center Y", -1, 1, 0}, none } },
        { 29, "Pixel Sort", "Glitch", "Bright pixels streak along a direction.", { P{"Threshold", 0, 1, .4f}, P{"Direction", -180, 180, 90}, P{"Length", 0, 1, .5f}, P{"Jitter", 0, 1, .2f} } },
    };
    return d;
}
constexpr int kNumFx = 30;
inline const FxDef* fxDef (int type) { auto& c = fxCatalogue(); return type >= 0 && type < (int) c.size() ? &c[(size_t) type] : nullptr; }

struct FxInst
{
    int id = 0, type = 0; bool enabled = true; float amount = 1.f, blend = 1.f; float p[4] { 0, 0, 0, 0 };
    std::string color = "#ffffff"; int audioSrc = 0; float audioSens = 0.f; std::string name;
    bool operator== (const FxInst& o) const { return id == o.id && type == o.type && enabled == o.enabled && amount == o.amount && blend == o.blend && p[0] == o.p[0] && p[1] == o.p[1] && p[2] == o.p[2] && p[3] == o.p[3] && color == o.color && audioSrc == o.audioSrc && audioSens == o.audioSens && name == o.name; }
};
inline FxInst makeFx (int type, int id)
{
    FxInst f; f.id = id; f.type = type; if (auto* d = fxDef (type)) { for (int i = 0; i < 4; ++i) f.p[i] = d->p[i].defV; f.color = d->colorDef; f.name = d->name; } return f;
}
}
