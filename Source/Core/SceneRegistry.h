// Data-driven scene catalogue (30 scenes). Each scene owns up to 8 "knobs" with its own labels/ranges/defaults.
// Adding a scene = one entry here + one GLSL function in scene.frag (see docs/ADDING_SCENES.md). No JUCE dependency.
#pragma once
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <vector>
namespace monie
{
struct Knob { std::string label; float minV = 0, maxV = 1, defV = .5f; };

struct SceneInfo
{
    int index; std::string name, category, desc, tags; std::vector<Knob> knobs;
    float toNative (int k, float norm) const { auto& kn = knobs[(size_t) k]; return kn.minV + std::clamp (norm, 0.f, 1.f) * (kn.maxV - kn.minV); }
    float toNorm (int k, float v) const { auto& kn = knobs[(size_t) k]; return kn.maxV > kn.minV ? std::clamp ((v - kn.minV) / (kn.maxV - kn.minV), 0.f, 1.f) : 0.f; }
    float defNorm (int k) const { return toNorm (k, knobs[(size_t) k].defV); }
};

inline const std::vector<SceneInfo>& sceneCatalogue()
{
    using K = Knob;
    static const std::vector<SceneInfo> s = {
        // ---- A: Image distortion ----
        { 0, "Liquid Image Morph", "Image Distortion", "Fluid-like displacement of your image. Bass pushes, mids warp.", "image liquid warp morph portrait fluid",
          { K{"Displacement", 0, 2, .8f}, K{"Turbulence", 0, 1, .5f}, K{"Scale", .5f, 8, 2.5f}, K{"Flow Speed", 0, 3, 1}, K{"Bass Push", 0, 2, 1}, K{"Mid Warp", 0, 2, 1}, K{"RGB Fringe", 0, 1, .2f}, K{"Detail", 0, 1, .5f} } },
        { 1, "Melting Image", "Image Distortion", "Your image drips and stretches in any direction.", "image melt drip stretch liquid",
          { K{"Melt Amount", 0, 2, .8f}, K{"Melt Direction", -180, 180, -90}, K{"Drip Scale", 1, 12, 4}, K{"Stretch", 0, 2, .8f}, K{"Viscosity Speed", 0, 2, 1}, K{"Wobble", 0, 1, .3f}, K{"Audio Melt", 0, 2, 1}, K{"Tint", 0, 1, 0} } },
        { 2, "Swirl Distortion", "Image Distortion", "Rotational displacement around an adjustable center.", "image swirl twist vortex rotate",
          { K{"Strength", -8, 8, 3}, K{"Radius", .1f, 2, .8f}, K{"Center X", -1, 1, 0}, K{"Center Y", -1, 1, 0}, K{"Rings", 1, 5, 1}, K{"Audio Pulse", 0, 2, 1}, K{"Spin Speed", 0, 3, .5f}, K{"Falloff", .2f, 4, 1.5f} } },
        { 3, "Kaleidoscope", "Image Distortion", "Mirrored segments of your image with audio-driven movement.", "image kaleidoscope mirror symmetry",
          { K{"Segments", 2, 16, 8}, K{"Rotation Speed", -2, 2, .2f}, K{"Center X", -1, 1, 0}, K{"Center Y", -1, 1, 0}, K{"Zoom", .3f, 3, 1}, K{"Audio Spin", 0, 2, 1}, K{"Segment Warp", 0, 1, .3f}, K{"Fold Sharpness", 0, 1, 1} } },
        { 4, "Image Feedback", "Image Distortion", "Recursive image trails with zoom and rotation feedback.", "image feedback recursion trails tunnel",
          { K{"Feedback", 0, .98f, .86f}, K{"Zoom Feedback", -.05f, .05f, .012f}, K{"Rotation Feedback", -.1f, .1f, .012f}, K{"Fade", 0, .1f, .015f}, K{"Image Mix", 0, 1, .5f}, K{"Hue Shift / Frame", 0, .05f, .006f}, K{"Bass Kick", 0, 2, 1}, K{"Drift", 0, 1, .2f} } },
        { 5, "RGB Image Split", "Image Distortion", "Independent red, green and blue displacement, punched by the beat.", "image rgb split channel chromatic",
          { K{"Red X", -.2f, .2f, .05f}, K{"Red Y", -.2f, .2f, 0}, K{"Green X", -.2f, .2f, 0}, K{"Green Y", -.2f, .2f, .02f}, K{"Blue X", -.2f, .2f, -.05f}, K{"Blue Y", -.2f, .2f, 0}, K{"Beat Boost", 0, 3, 1.5f}, K{"Separation Scale", 0, 3, 1} } },
        // ---- B: Analog & digital corruption ----
        { 6, "VHS Distortion", "Analog & Digital Corruption", "Tape tracking errors, color bleed, scanlines and noise.", "vhs analog tape scanlines noise retro",
          { K{"Scanlines", 0, 1, .5f}, K{"Color Bleed", 0, 1, .6f}, K{"Tracking", 0, 1, .5f}, K{"Noise", 0, 1, .4f}, K{"Wobble", 0, 1, .4f}, K{"Analog Intensity", 0, 2, 1}, K{"Jitter", 0, 1, .3f}, K{"Dropouts", 0, 1, .3f} } },
        { 7, "Digital Glitch", "Analog & Digital Corruption", "Block corruption, horizontal tearing and channel splits.", "glitch digital corruption blocks tear",
          { K{"Frequency", 0, 1, .5f}, K{"Block Size", .02f, .4f, .1f}, K{"Shift", 0, 1, .5f}, K{"Channel Split", 0, 1, .4f}, K{"Corruption", 0, 1, .4f}, K{"Flash", 0, 1, .3f}, K{"Scan Tear", 0, 1, .3f}, K{"Seed Rate", 1, 30, 10} } },
        { 8, "Pixel Sorting Style", "Analog & Digital Corruption", "Bright regions smear along a direction like sorted pixels.", "pixel sort smear threshold",
          { K{"Threshold", 0, 1, .45f}, K{"Direction", -180, 180, 0}, K{"Length", 0, 1, .5f}, K{"Sort Strength", 0, 2, 1}, K{"Audio Reactivity", 0, 2, 1}, K{"Banding", 0, 1, .3f}, K{"Jitter", 0, 1, .2f}, K{"Invert", 0, 1, 0} } },
        { 9, "Datamosh Trails", "Analog & Digital Corruption", "Motion smear blocks that persist and reset on the beat.", "datamosh smear trails feedback blocks",
          { K{"Persistence", 0, .99f, .9f}, K{"Smear Amount", 0, 2, 1}, K{"Block Size", 4, 64, 20}, K{"Motion Direction", -180, 180, 45}, K{"Beat Reset", 0, 1, .6f}, K{"Color Drift", 0, 1, .2f}, K{"Noise", 0, 1, .2f}, K{"Hold", 0, 1, .3f} } },
        { 10, "Monochrome Corruption", "Analog & Digital Corruption", "Harsh black-and-white with grain, tearing and strobing.", "monochrome black white strobe grain threshold",
          { K{"Threshold", 0, 1, .5f}, K{"Grain", 0, 1, .4f}, K{"Invert", 0, 1, 0}, K{"Flash", 0, 2, 1}, K{"Contrast", .5f, 4, 2}, K{"Tear", 0, 1, .4f}, K{"Static", 0, 1, .3f}, K{"Pattern", 0, 1, .3f} } },
        { 11, "Low-Resolution Digital", "Analog & Digital Corruption", "Chunky pixels, limited colors and dithering.", "pixel lowres retro dither posterize",
          { K{"Resolution", 8, 200, 48}, K{"Colors", 2, 32, 8}, K{"Posterize Mix", 0, 1, .6f}, K{"Dither", 0, 1, .5f}, K{"Pixel Motion", 0, 1, .4f}, K{"Audio Pulse", 0, 2, 1}, K{"Scan", 0, 1, .2f}, K{"Gradient Mix", 0, 1, .5f} } },
        // ---- C: Geometry & spatial ----
        { 12, "Wireframe Tunnel", "Geometry & Spatial", "Perspective grid tunnel that surges with the bass.", "wireframe tunnel grid perspective neon",
          { K{"Tunnel Speed", 0, 4, 1.2f}, K{"Grid Density", 2, 24, 10}, K{"Sides", 3, 12, 6}, K{"Line Width", .002f, .03f, .008f}, K{"Bass Zoom", 0, 2, 1}, K{"Twist", 0, 2, .3f}, K{"Depth Fade", .2f, 3, 1}, K{"Glow", 0, 2, 1} } },
        { 13, "Infinite Fractal Zoom", "Geometry & Spatial", "Endless zoom into an escape-time fractal.", "fractal zoom infinite mandelbrot julia",
          { K{"Zoom Speed", 0, 2, .4f}, K{"Rotation", -1, 1, .1f}, K{"Detail", 1, 8, 5}, K{"Color Scale", .2f, 4, 1.5f}, K{"Fractal Power", 1.5f, 6, 2}, K{"Center Wander", 0, 1, .3f}, K{"Audio Zoom", 0, 2, 1}, K{"Glow", 0, 2, .8f} } },
        { 14, "Kaleidoscopic Geometry", "Geometry & Spatial", "Procedural symmetrical line patterns.", "geometry kaleidoscope symmetry lines pattern",
          { K{"Symmetry", 3, 16, 8}, K{"Rotation Speed", -2, 2, .3f}, K{"Complexity", 1, 8, 4}, K{"Line Width", .002f, .03f, .008f}, K{"Scale", .3f, 3, 1}, K{"Audio Warp", 0, 2, 1}, K{"Layers", 1, 4, 3}, K{"Fill", 0, 1, .4f} } },
        { 15, "Neon Wireframe Landscape", "Geometry & Spatial", "Procedural terrain flying toward a glowing sun.", "landscape terrain wireframe mountains retro synthwave",
          { K{"Grid Density", 4, 40, 14}, K{"Terrain Height", 0, 2, .8f}, K{"Movement Speed", 0, 4, 1}, K{"Line Width", .002f, .03f, .008f}, K{"Sun", 0, 1, .7f}, K{"Audio Height", 0, 2, 1}, K{"Roll", 0, 1, .1f}, K{"Horizon", 0, .5f, .2f} } },
        { 16, "Recursive Geometry", "Geometry & Spatial", "Nested repeating shapes that rotate and breathe.", "recursive geometry nested shapes polygons",
          { K{"Recursion Depth", 1, 8, 5}, K{"Rotation", -2, 2, .3f}, K{"Scale Factor", .5f, .95f, .8f}, K{"Shape Sides", 3, 8, 4}, K{"Line Width", .002f, .03f, .006f}, K{"Audio Scale", 0, 2, 1}, K{"Spread", 0, 1, .5f}, K{"Fill", 0, 1, .2f} } },
        { 17, "Abstract 3D Forms", "Geometry & Spatial", "Lit, raymarched sphere/torus forms with surface distortion.", "3d forms raymarch sphere torus shaded lighting",
          { K{"Shape Mix", 0, 1, .3f}, K{"Rotation Speed", 0, 3, .6f}, K{"Surface Distortion", 0, 1, .4f}, K{"Light Angle", -180, 180, 45}, K{"Glossiness", 0, 1, .5f}, K{"Size", .3f, 1.5f, .8f}, K{"Audio Warp", 0, 2, 1}, K{"Rim Light", 0, 2, 1} } },
        // ---- D: Organic & atmospheric ----
        { 18, "Procedural Plasma", "Organic & Atmospheric", "Classic animated plasma using your gradient.", "plasma procedural color gradient psychedelic",
          { K{"Turbulence", 0, 1, .5f}, K{"Speed", 0, 3, 1}, K{"Scale", .5f, 8, 2.5f}, K{"Color Shift", 0, 2, .5f}, K{"Audio Color Move", 0, 2, 1}, K{"Warp", 0, 2, .6f}, K{"Contrast", .5f, 3, 2.2f}, K{"Banding", 0, 1, .3f} } },
        { 19, "Nebula Particles", "Organic & Atmospheric", "Layered gas clouds with twinkling particles.", "nebula particles space stars dust clouds",
          { K{"Density", 0, 1, .6f}, K{"Particle Size", .3f, 3, 1}, K{"Glow", 0, 3, 1.2f}, K{"Drift Speed", 0, 3, .6f}, K{"Cloud Amount", 0, 2, 1}, K{"Treble Twinkle", 0, 2, 1}, K{"Depth Layers", 1, 5, 3}, K{"Swirl", 0, 1, .3f} } },
        { 20, "Fluid Noise Field", "Organic & Atmospheric", "Flowing procedural noise with directional drift.", "fluid noise flow field smoke organic",
          { K{"Scale", .5f, 10, 3}, K{"Flow Direction", -180, 180, 45}, K{"Distortion", 0, 2, .8f}, K{"Speed", 0, 3, .7f}, K{"Audio Displace", 0, 2, 1}, K{"Contrast", .5f, 3, 1.3f}, K{"Octaves", 1, 6, 4}, K{"Ridge", 0, 1, .3f} } },
        { 21, "Reaction-Diffusion", "Organic & Atmospheric", "A real Gray-Scott simulation; beats seed new growth.", "reaction diffusion turing cells organic simulation",
          { K{"Pattern Variation", 0, 1, .5f}, K{"Evolution Speed", 1, 8, 3}, K{"Feed", .02f, .07f, .037f}, K{"Kill", .05f, .07f, .06f}, K{"Contrast", .5f, 3, 1.4f}, K{"Beat Seeds", 0, 2, 1}, K{"Quality", 0, 2, 1}, K{"Edge Glow", 0, 2, .6f} } },
        { 22, "Electric Storm", "Organic & Atmospheric", "Branching lightning with beat-triggered flashes.", "electric storm lightning bolts flash",
          { K{"Branch Density", 1, 8, 4}, K{"Glow", 0, 3, 1.4f}, K{"Flash", 0, 2, 1}, K{"Bolt Width", .002f, .03f, .01f}, K{"Speed", 0, 3, 1}, K{"Storm Clouds", 0, 1, .5f}, K{"Jitter", 0, 1, .5f}, K{"Beat Strike", 0, 2, 1} } },
        { 23, "Particle Explosion", "Organic & Atmospheric", "Bursts of particles launched on the beat.", "particles explosion burst fireworks gravity",
          { K{"Density", 0, 1, .6f}, K{"Velocity", 0, 3, 1}, K{"Gravity", -1, 1, .3f}, K{"Burst Size", .2f, 2, 1}, K{"Particle Size", .3f, 3, 1}, K{"Trail", 0, 1, .5f}, K{"Burst Rate", 0, 2, 1}, K{"Spread", 0, 1, 1} } },
        // ---- E: Music-reactive graphics ----
        { 24, "Radial Spectrum", "Music-Reactive", "Circular frequency bars - bass, mids and treble.", "spectrum radial circle bars frequency",
          { K{"Segments", 16, 128, 64}, K{"Radius", .1f, .6f, .25f}, K{"Thickness", .01f, .3f, .08f}, K{"Bass Weight", 0, 2, 1}, K{"Mid Weight", 0, 2, 1}, K{"Treble Weight", 0, 2, 1}, K{"Rotation Speed", -2, 2, .2f}, K{"Inner Glow", 0, 2, 1} } },
        { 25, "Oscilloscope", "Music-Reactive", "Phosphor waveform trace with persistence.", "oscilloscope waveform line phosphor scope",
          { K{"Line Thickness", .002f, .05f, .01f}, K{"Persistence", 0, .98f, .85f}, K{"Amplitude", .2f, 3, 1.2f}, K{"Phosphor Glow", 0, 3, 1.2f}, K{"Mirror", 0, 1, 0}, K{"Grid", 0, 1, .3f}, K{"Time Zoom", .5f, 4, 1}, K{"XY Mode", 0, 1, 0} } },
        { 26, "Reactive Geometry Grid", "Music-Reactive", "A perspective grid displaced by frequency bands.", "grid reactive geometry displacement perspective bands",
          { K{"Density", 4, 40, 16}, K{"Displacement", 0, 2, 1}, K{"Perspective", 0, 1, .5f}, K{"Line Width", .002f, .03f, .006f}, K{"Bass Zone", 0, 2, 1}, K{"Treble Zone", 0, 2, 1}, K{"Rotation", -1, 1, 0}, K{"Fill Cells", 0, 1, .3f} } },
        { 27, "Spectrum Particle Field", "Music-Reactive", "Particles whose height and brightness follow the spectrum.", "particles spectrum field frequency stars",
          { K{"Density", 0, 1, .7f}, K{"Particle Speed", 0, 3, 1}, K{"Size", .3f, 3, 1}, K{"Beat Response", 0, 3, 1.5f}, K{"Frequency Spread", 0, 1, .6f}, K{"Trails", 0, 1, .5f}, K{"Depth", 0, 1, .5f}, K{"Glow", 0, 3, 1} } },
        // ---- F: Creative overlays ----
        { 28, "Typography Reactor", "Creative Overlays", "Your own text (set below) pulses, warps and echoes with the music.", "typography text reactor letters beat bass",
          { K{"Beat Scale", 0, 2, 1}, K{"Bass Distortion", 0, 2, 1}, K{"Color Speed", 0, 3, 1}, K{"Background Geometry", 0, 1, .6f}, K{"Rows", 1, 8, 3}, K{"Echo", 0, 1, .5f}, K{"Tracking", 0, 1, .1f}, K{"Glitch", 0, 1, .3f} } },
        { 29, "Multi-Layer Collage", "Creative Overlays", "Up to four of your images composed with independent motion.", "collage multi image layers panels blend",
          { K{"Panels", 1, 4, 4}, K{"Rotation Spread", 0, 1, .4f}, K{"Scale Variance", 0, 1, .5f}, K{"Blend (Norm/Scr/Diff/Add)", 0, 3, 1}, K{"Audio Shuffle", 0, 2, 1}, K{"Per-Panel Color", 0, 1, .3f}, K{"Overlap", 0, 1, .6f}, K{"Drift Speed", 0, 2, .5f} } },
    };
    return s;
}
constexpr int kNumScenes = 30;
inline std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
inline std::vector<std::string> sceneCategories()
{ std::vector<std::string> c; for (auto& s : sceneCatalogue()) if (std::find (c.begin(), c.end(), s.category) == c.end()) c.push_back (s.category); return c; }

inline std::vector<int> searchScenes (const std::string& query, const std::string& category = "", const std::set<int>* favoritesOnly = nullptr)
{
    std::vector<std::string> words; { std::istringstream q (lower (query)); std::string w; while (q >> w) words.push_back (w); }
    std::vector<int> out;
    for (auto& s : sceneCatalogue())
    {
        if (! category.empty() && s.category != category) continue;
        if (favoritesOnly && ! favoritesOnly->count (s.index)) continue;
        const std::string hay = lower (s.name + " " + s.category + " " + s.tags + " " + s.desc); bool ok = true;
        for (auto& w : words) if (hay.find (w) == std::string::npos) { ok = false; break; }
        if (ok) out.push_back (s.index);
    }
    return out;
}
inline std::string favoritesToText (const std::set<int>& f) { std::string o; for (int i : f) { if (! o.empty()) o += ','; o += std::to_string (i); } return o; }
inline std::set<int> favoritesFromText (const std::string& t)
{ std::set<int> f; std::istringstream s (t); std::string c; while (std::getline (s, c, ',')) { if (c.empty() || c.find_first_not_of ("0123456789") != std::string::npos) continue; int v = std::atoi (c.c_str()); if (v < kNumScenes) f.insert (v); } return f; }
}
