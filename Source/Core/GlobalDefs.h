// The global composition controls. These are the AUTOMATABLE host parameters (stable ids -> never rename a shipped id).
// The same table drives the APVTS layout, the Settings/Overview sliders, the final-pass uniforms (u_<id>) and the project file.
#pragma once
#include <string>
#include <vector>
namespace monie
{
struct GDef { const char* id; const char* name; float minV, maxV, defV; const char* section; const char* tip; bool shader; };
// shader = true: also exposed to final.frag as u_<id> (otherwise consumed on the CPU)
inline const std::vector<GDef>& globalDefs()
{
    static const std::vector<GDef> g = {
        { "master",     "Master Intensity",   0, 1,    .75f, "Motion",   "Overall brightness / energy of the composition.", true },
        { "speed",      "Motion Speed",       0, 3,    1,    "Motion",   "Speed of every animated scene.", false },
        { "zoom",       "Scene Zoom",         .4f, 3,  1,    "Motion",   "Zooms the generated scene (layers are unaffected).", false },
        { "rotation",   "Scene Rotation",     -180, 180, 0,  "Motion",   "Rotates the generated scene (degrees).", false },
        { "xfade",      "Scene Crossfade (s)", .05f, 5, 1,   "Transitions", "How long switching scenes takes.", false },
        { "imgspeed",   "Image Transition Speed", .1f, 4, 1, "Transitions", "How quickly a new scene image fades in.", false },
        { "sensBass",   "Bass Sensitivity",   0, 2,    1,    "Audio",    "Gain applied to the low band before it drives visuals.", false },
        { "sensMid",    "Mid Sensitivity",    0, 2,    1,    "Audio",    "Gain applied to the mid band.", false },
        { "sensTreble", "Treble Sensitivity", 0, 2,    1,    "Audio",    "Gain applied to the high band.", false },
        { "sensBeat",   "Beat Sensitivity",   0, 2,    1,    "Audio",    "Strength of beat pulses.", false },
        { "smooth",     "Audio Smoothing",    0, 1,    .3f,  "Audio",    "Higher = calmer, slower reaction.", false },
        { "attack",     "Attack",             0, 1,    .6f,  "Audio",    "How fast visuals rise on loud sounds.", false },
        { "release",    "Release",            0, 1,    .5f,  "Audio",    "How fast visuals fall after loud sounds.", false },
        { "bright",     "Brightness",         0, 2,    1,    "Color Grade", "Final brightness.", true },
        { "contrast",   "Contrast",           0, 2,    1,    "Color Grade", "Final contrast.", true },
        { "sat",        "Saturation",         0, 2,    1,    "Color Grade", "Final colour saturation.", true },
        { "gamma",      "Gamma",              .4f, 2.5f, 1,  "Color Grade", "Mid-tone curve.", true },
        { "hue",        "Hue Shift",          0, 1,    0,    "Color Grade", "Rotates every colour around the colour wheel.", true },
        { "temp",       "Temperature",        -1, 1,   0,    "Color Grade", "Cool (blue) to warm (orange).", true },
        { "tint",       "Tint",               -1, 1,   0,    "Color Grade", "Magenta to green.", true },
        { "exposure",   "Exposure (EV)",      -2, 2,   0,    "Color Grade", "Exposure in stops.", true },
        { "lift",       "Black Lift",         0, .3f,  0,    "Color Grade", "Raises the blacks (matte look).", true },
        { "gmix",       "Gradient Map",       0, 1,    0,    "Color",    "Recolour the final image through the active gradient.", true },
        { "palCycle",   "Colour Cycling",     0, 2,    0,    "Color",    "Continuously rotates hue over time.", true },
        { "reactColor", "Audio Colour Drift", 0, 1,    0,    "Color",    "Loudness shifts the hue.", true },
        { "invert",     "Invert",             0, 1,    0,    "Color",    "Negative image.", true },
        { "glow",       "Glow",               0, 1,    0,    "Stylize",  "Bloom around bright areas.", true },
        { "rgb",        "Chromatic Split",    0, 1,    0,    "Stylize",  "Separates colour channels toward the edges.", true },
        { "grain",      "Film Grain",         0, 1,    0,    "Stylize",  "Animated grain.", true },
        { "vignette",   "Vignette",           0, 1,    0,    "Stylize",  "Darkens the corners.", true },
        { "scan",       "Scanlines",          0, 1,    0,    "Stylize",  "CRT scanlines.", true },
        { "pixel",      "Pixelation",         0, 1,    0,    "Stylize",  "Blocky pixels.", true },
        { "poster",     "Posterize",          0, 1,    0,    "Stylize",  "Reduces the number of tones.", true },
        { "thresh",     "Threshold",          0, 1,    0,    "Stylize",  "Two-colour hard split by brightness.", true },
        { "edge",       "Edge Outline",       0, 1,    0,    "Stylize",  "Draws outlines around shapes.", true },
    };
    return g;
}
inline int globalIndex (const char* id) { const auto& g = globalDefs(); for (size_t i = 0; i < g.size(); ++i) if (std::string (g[i].id) == id) return (int) i; return 0; }
inline const GDef* findGlobal (const std::string& id) { for (auto& d : globalDefs()) if (id == d.id) return &d; return nullptr; }
inline const char* qualityNames() { return "Low (50%)|Medium (75%)|High (100%)|Ultra (150%)"; }
inline float qualityScale (int i) { static const float s[] = { .5f, .75f, 1.f, 1.5f }; return s[i < 0 ? 0 : i > 3 ? 3 : i]; }
inline const char* fpsNames() { return "30 FPS|60 FPS|Match display"; }
}
