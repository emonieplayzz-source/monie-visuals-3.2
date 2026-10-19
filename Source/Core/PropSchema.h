// Parameter schema system: ONE description of a parameter drives the inspector widget, serialization, defaults,
// reset, keyframing and audio mapping. Adding a parameter = adding one line to a schema. No JUCE dependency.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <algorithm>
namespace monie
{
enum class PKind { Slider, Toggle, Choice, Color, Text, Font };

struct PDef
{
    std::string id, label, section, tip;
    PKind kind = PKind::Slider;
    float minV = 0, maxV = 1, defV = 0, step = 0;
    std::vector<std::string> choices;
    std::string sdef;            // default for Text / Color (#rrggbb) / Font
    bool advanced = false;       // shown only when the "Advanced" part of its section is expanded
    bool keyable = true;         // sliders: can be keyframed / audio-mapped
};

inline std::vector<std::string> splitBar (const std::string& s)
{ std::vector<std::string> v; std::string c; for (char ch : s) { if (ch == '|') { v.push_back (c); c.clear(); } else c += ch; } v.push_back (c); return v; }

inline PDef S (const char* id, const char* label, float mn, float mx, float df, const char* sec, const char* tip = "", bool adv = false, float step = 0)
{ PDef d; d.id = id; d.label = label; d.minV = mn; d.maxV = mx; d.defV = df; d.section = sec; d.tip = tip; d.advanced = adv; d.step = step; return d; }
inline PDef B (const char* id, const char* label, bool df, const char* sec, const char* tip = "", bool adv = false)
{ PDef d; d.id = id; d.label = label; d.kind = PKind::Toggle; d.defV = df ? 1.f : 0.f; d.maxV = 1; d.section = sec; d.tip = tip; d.advanced = adv; d.keyable = false; return d; }
inline PDef Ch (const char* id, const char* label, const char* bar, int df, const char* sec, const char* tip = "", bool adv = false)
{ PDef d; d.id = id; d.label = label; d.kind = PKind::Choice; d.choices = splitBar (bar); d.defV = (float) df; d.maxV = (float) d.choices.size() - 1; d.section = sec; d.tip = tip; d.advanced = adv; d.keyable = false; return d; }
inline PDef Co (const char* id, const char* label, const char* hex, const char* sec, const char* tip = "", bool adv = false)
{ PDef d; d.id = id; d.label = label; d.kind = PKind::Color; d.sdef = hex; d.section = sec; d.tip = tip; d.advanced = adv; d.keyable = false; return d; }
inline PDef Tx (const char* id, const char* label, const char* df, const char* sec, const char* tip = "", bool adv = false)
{ PDef d; d.advanced = adv; d.id = id; d.label = label; d.kind = PKind::Text; d.sdef = df; d.section = sec; d.tip = tip; d.keyable = false; return d; }
inline PDef Fo (const char* id, const char* label, const char* df, const char* sec, const char* tip = "")
{ PDef d; d.id = id; d.label = label; d.kind = PKind::Font; d.sdef = df; d.section = sec; d.tip = tip; d.keyable = false; return d; }

struct PropBag
{
    std::map<std::string, float> f;
    std::map<std::string, std::string> s;
    float getF (const PDef& d) const { auto it = f.find (d.id); return it == f.end() ? d.defV : it->second; }
    float getF (const std::string& id, float def) const { auto it = f.find (id); return it == f.end() ? def : it->second; }
    std::string getS (const PDef& d) const { auto it = s.find (d.id); return it == s.end() ? d.sdef : it->second; }
    std::string getS (const std::string& id, const std::string& def) const { auto it = s.find (id); return it == s.end() ? def : it->second; }
    void setF (const PDef& d, float v) { f[d.id] = clampTo (d, v); }
    void setS (const PDef& d, const std::string& v) { s[d.id] = v; }
    static float clampTo (const PDef& d, float v)
    {
        if (d.kind == PKind::Toggle) return v > .5f ? 1.f : 0.f;
        if (d.kind == PKind::Choice) return (float) std::clamp ((int) (v + .5f), 0, (int) d.choices.size() - 1);
        return std::clamp (v, d.minV, d.maxV);
    }
    bool operator== (const PropBag& o) const { return f == o.f && s == o.s; }
};

inline const PDef* findDef (const std::vector<PDef>& sc, const std::string& id)
{ for (auto& d : sc) if (d.id == id) return &d; return nullptr; }

// ---------------------------------------------------------------------------------------------------------------
inline const char* blendNames() { return "Normal|Add|Screen|Multiply|Overlay|Lighten|Darken|Difference"; }
inline const char* audioSrcNames() { return "None|Bass|Low-Mid|Mid|High-Mid|Treble|RMS|Peak|Transient|Beat|Flux"; }

// Common transform / look / color / morph block used by both image and video layers.
inline void mediaCommon (std::vector<PDef>& v)
{
    v.push_back (S ("x", "Position X", -1.5f, 1.5f, 0, "Transform", "Horizontal position (-1 = left edge, +1 = right edge). You can also drag the layer in the preview."));
    v.push_back (S ("y", "Position Y", -1.5f, 1.5f, 0, "Transform", "Vertical position (-1 = bottom, +1 = top)."));
    v.push_back (S ("scale", "Scale", 0.05f, 4.f, 1.f, "Transform", "Uniform size multiplier."));
    v.push_back (S ("rot", "Rotation", -180.f, 180.f, 0, "Transform", "Rotation in degrees."));
    v.push_back (Ch ("fit", "Fit Mode", "Contain|Cover|Stretch", 0, "Transform", "How the media is fitted into the canvas before scale/position are applied."));
    v.push_back (S ("cropL", "Crop Left", 0, .9f, 0, "Crop", "Hide this fraction of the media from the left.", true));
    v.push_back (S ("cropR", "Crop Right", 0, .9f, 0, "Crop", "", true));
    v.push_back (S ("cropT", "Crop Top", 0, .9f, 0, "Crop", "", true));
    v.push_back (S ("cropB", "Crop Bottom", 0, .9f, 0, "Crop", "", true));
    v.push_back (S ("opacity", "Opacity", 0, 1, 1, "Appearance", "Layer transparency."));
    v.push_back (Ch ("blend", "Blend Mode", blendNames(), 0, "Appearance", "How this layer combines with the layers beneath it."));
    v.push_back (S ("brightness", "Brightness", 0, 2, 1, "Color", "Per-layer brightness."));
    v.push_back (S ("contrast", "Contrast", 0, 2, 1, "Color"));
    v.push_back (S ("saturation", "Saturation", 0, 2, 1, "Color"));
    v.push_back (S ("gamma", "Gamma", .4f, 2.5f, 1, "Color", "", true));
    v.push_back (S ("hue", "Hue", 0, 1, 0, "Color", "", true));
    v.push_back (S ("colormap", "Gradient Map", 0, 1, 0, "Color", "Replace the layer's colors with the active gradient (duotone/tritone look).", true));
    v.push_back (S ("blur", "Blur", 0, 1, 0, "Detail", "Soften the layer."));
    v.push_back (S ("sharpen", "Sharpen", 0, 2, 0, "Detail", "Unsharp mask strength."));
    v.push_back (S ("distortion", "Distortion", 0, 1, 0, "Morph", "Liquid warp amount. Driven by the audio source below."));
    v.push_back (S ("morphScale", "Morph Scale", .3f, 8.f, 2.f, "Morph", "Size of the warp features (higher = finer ripples)."));
    v.push_back (S ("morphSpeed", "Morph Speed", 0, 4.f, 1.f, "Morph", "How fast the warp evolves."));
    v.push_back (S ("displace", "Displacement", 0, 1, 0, "Morph", "Pushes the image sideways with audio (like a displacement map).", true));
    v.push_back (S ("noise", "Noise Amount", 0, 1, 0, "Morph", "Adds animated noise to the warp.", true));
    v.push_back (S ("rgb", "RGB Split", 0, 1, 0, "Morph", "Separates the red/green/blue channels.", true));
    v.push_back (Ch ("audioSrc", "Audio Source", audioSrcNames(), 1, "Audio", "Which audio feature drives the morph and displacement."));
    v.push_back (S ("audioSens", "Audio Sensitivity", 0, 3.f, 1.f, "Audio", "How strongly the audio source moves this layer."));
}

inline const std::vector<PDef>& imageSchema()
{ static const std::vector<PDef> s = [] { std::vector<PDef> v; mediaCommon (v); return v; }(); return s; }

inline const std::vector<PDef>& videoSchema()
{
    static const std::vector<PDef> s = [] {
        std::vector<PDef> v; v.push_back (S ("speed", "Playback Speed", .1f, 4.f, 1.f, "Playback", "1 = normal. Video plays forward only."));
        v.push_back (B ("loop", "Loop", true, "Playback", "Restart from the start point when the end point is reached."));
        v.push_back (S ("start", "Start Time (s)", 0, 3600.f, 0, "Playback", "Trim: where in the file playback begins."));
        v.push_back (S ("end", "End Time (s)", 0, 3600.f, 0, "Playback", "Trim: where playback ends. 0 = end of file."));
        v.push_back (B ("blendFrames", "Frame Blending", false, "Playback", "Cross-fade between decoded frames for smoother slow motion.", true));
        mediaCommon (v); return v; }();
    return s;
}

inline const char* fontStyleNames() { return "Regular|Bold|Italic|Bold Italic"; }

inline void textCommon (std::vector<PDef>& v, bool caption)
{
    if (! caption)
        v.push_back (Tx ("text", "Text", "MONIE", "Content", "Type any text. Press Enter for a new line."));
    v.push_back (Fo ("font", "Font", "", "Font", "Any installed font, or import a .ttf/.otf in the Fonts browser."));
    v.push_back (Ch ("style", "Style", fontStyleNames(), 1, "Font", "Bold / italic where the font provides it."));
    v.push_back (S ("size", "Size", 2.f, 60.f, caption ? 6.f : 14.f, "Font", "Font height as % of the canvas height."));
    v.push_back (Ch ("align", "Alignment", "Left|Center|Right", 1, "Layout", "Paragraph alignment."));
    v.push_back (S ("letterSp", "Letter Spacing", -.3f, 1.5f, 0, "Layout", "Extra space between characters (em)."));
    v.push_back (S ("wordSp", "Word Spacing", -.5f, 2.f, 0, "Layout", "Extra space between words (em).", true));
    v.push_back (S ("lineSp", "Line Spacing", .5f, 2.5f, 1.15f, "Layout", "Line height multiplier.", false));
    v.push_back (S ("boxW", "Text Box Width", 0, 2.f, caption ? 1.6f : 0, "Layout", "Wrap text at this width (canvas units, 2 = full width). 0 = no wrapping."));
    v.push_back (S ("curve", "Curve", -1.f, 1.f, 0, "Layout", "Bends the text along an arc.", true));
    v.push_back (B ("vertical", "Vertical Text", false, "Layout", "Stack characters top to bottom.", true));
    v.push_back (S ("x", "Position X", -1.5f, 1.5f, 0, "Transform", "Drag the text in the preview, or type here."));
    v.push_back (S ("y", "Position Y", -1.5f, 1.5f, caption ? -.72f : 0, "Transform"));
    v.push_back (S ("scale", "Scale", .05f, 4.f, 1.f, "Transform"));
    v.push_back (S ("rot", "Rotation", -180.f, 180.f, 0, "Transform"));
    v.push_back (S ("skewX", "Skew X", -1.f, 1.f, 0, "Transform", "", true));
    v.push_back (S ("skewY", "Skew Y", -1.f, 1.f, 0, "Transform", "", true));
    v.push_back (S ("tiltX", "Perspective Tilt X", -1.2f, 1.2f, 0, "Transform", "3D tilt around the horizontal axis.", true));
    v.push_back (S ("tiltY", "Perspective Tilt Y", -1.2f, 1.2f, 0, "Transform", "3D tilt around the vertical axis.", true));
    v.push_back (Co ("fill", "Fill Color", "#ffffff", "Fill"));
    v.push_back (B ("gradient", "Gradient Fill", false, "Fill", "Fill with a two-color gradient."));
    v.push_back (Tx ("fillImage", "Image Fill (file path)", "", "Fill", "Fill the letters with a picture (type or paste an image path, or use the Browse button).", true));
    v.push_back (Co ("fill2", "Gradient Color", "#ff2a6d", "Fill"));
    v.push_back (S ("gradAngle", "Gradient Angle", -180.f, 180.f, 90.f, "Fill", "", true));
    v.push_back (S ("gradAnim", "Gradient Animation", 0, 2.f, 0, "Fill", "Scrolls the gradient colors over time.", true));
    v.push_back (S ("colorCycle", "Color Cycling", 0, 2.f, 0, "Fill", "Continuously rotates the hue of the text.", true));
    v.push_back (S ("o1w", "Outline Width", 0, 12.f, 0, "Outline", "Outline thickness in pixels (at 1080p)."));
    v.push_back (Co ("o1c", "Outline Color", "#000000", "Outline"));
    v.push_back (S ("o2w", "2nd Outline Width", 0, 24.f, 0, "Outline", "A second, outer outline.", true));
    v.push_back (Co ("o2c", "2nd Outline Color", "#ff2a6d", "Outline", "", true));
    v.push_back (S ("shOp", "Shadow Opacity", 0, 1, 0, "Shadow"));
    v.push_back (S ("shDist", "Shadow Distance", 0, 40.f, 6.f, "Shadow", "", true));
    v.push_back (S ("shAngle", "Shadow Angle", -180.f, 180.f, 45.f, "Shadow", "", true));
    v.push_back (S ("shBlur", "Shadow Blur", 0, 30.f, 6.f, "Shadow", "", true));
    v.push_back (Co ("shC", "Shadow Color", "#000000", "Shadow", "", true));
    v.push_back (S ("glow", "Glow", 0, 2.f, 0, "Glow", "Soft light around the letters."));
    v.push_back (S ("glowR", "Glow Radius", 2.f, 60.f, 18.f, "Glow", "", true));
    v.push_back (Co ("glowC", "Glow Color", "#00e5ff", "Glow", "", true));
    v.push_back (B ("bg", "Background Box", false, "Background Box", "Draw a panel behind the text."));
    v.push_back (Co ("bgC", "Box Color", "#000000", "Background Box"));
    v.push_back (S ("bgOp", "Box Opacity", 0, 1, .6f, "Background Box"));
    v.push_back (S ("bgPadX", "Padding X", 0, 200.f, 28.f, "Background Box", "", true));
    v.push_back (S ("bgPadY", "Padding Y", 0, 200.f, 14.f, "Background Box", "", true));
    v.push_back (S ("bgRadius", "Corner Radius", 0, 100.f, 12.f, "Background Box", "", true));
    v.push_back (S ("opacity", "Opacity", 0, 1, 1, "Appearance"));
    v.push_back (Ch ("blend", "Blend Mode", blendNames(), 0, "Appearance"));
    v.push_back (S ("blur", "Blur", 0, 1, 0, "Look", "Softens the letters.", true));
    v.push_back (S ("distortion", "Liquid Distortion", 0, 1, 0, "Look", "Wobbly warp of the letters.", true));
    v.push_back (S ("noise", "Noise", 0, 1, 0, "Look", "Grainy breakup.", true));
    v.push_back (S ("rgb", "RGB Split", 0, 1, 0, "Look", "Splits color channels.", true));
    v.push_back (S ("audioScale", "Bass Scale", 0, 1, 0, "Audio", "Text pulses with the bass."));
    v.push_back (S ("audioFlicker", "Treble Flicker", 0, 1, 0, "Audio", "Opacity flickers with treble.", true));
    v.push_back (S ("audioWiggle", "Mid Wiggle", 0, 1, 0, "Audio", "Letters wiggle with the mids.", true));
}

inline const std::vector<PDef>& textSchema()
{ static const std::vector<PDef> s = [] { std::vector<PDef> v; textCommon (v, false); return v; }(); return s; }

inline const std::vector<PDef>& captionSchema()
{
    static const std::vector<PDef> s = [] {
        std::vector<PDef> v;
        v.push_back (Ch ("mode", "Display Mode", "Line by line|Word by word|Phrase groups", 0, "Captions", "How lyrics are shown."));
        v.push_back (S ("groupWords", "Words per Group", 1, 8, 3, "Captions", "Used by Phrase groups.", false, 1));
        v.push_back (S ("maxChars", "Max Characters per Line", 8, 80, 32, "Captions", "Longer lines are wrapped automatically.", false, 1));
        v.push_back (S ("maxLines", "Max Lines", 1, 6, 2, "Captions", "", false, 1));
        v.push_back (B ("highlight", "Highlight Active Word", true, "Captions", "Colors the word being sung (uses word timestamps, or an estimate when timing is per line)."));
        v.push_back (Co ("hiC", "Highlight Color", "#ffe14a", "Captions"));
        v.push_back (B ("showPrev", "Show Previous Line", false, "Captions"));
        v.push_back (B ("showNext", "Show Next Line", false, "Captions"));
        v.push_back (S ("ctxOpacity", "Previous/Next Opacity", 0, 1, .35f, "Captions", "", true));
        v.push_back (S ("delay", "Caption Delay (s)", -5.f, 5.f, 0, "Timing", "Shifts captions later (+) or earlier (-) relative to the music."));
        v.push_back (S ("leadIn", "Lead-In (s)", 0, 2.f, .1f, "Timing", "Show a line this long before its timestamp."));
        v.push_back (S ("minDur", "Minimum Duration (s)", 0, 6.f, 1.0f, "Timing", "Keeps very short lines readable.", true));
        v.push_back (S ("fadeIn", "Fade In (s)", 0, 1.5f, .15f, "Timing", "Entrance fade time.", true));
        v.push_back (S ("fadeOut", "Fade Out (s)", 0, 1.5f, .2f, "Timing", "Exit fade time.", true));
        v.push_back (S ("hold", "Hold After End (s)", 0, 4.f, .25f, "Timing", "Keep the line on screen briefly after it ends.", true));
        v.push_back (S ("minConf", "Min Confidence", 0, 1, .35f, "Recognition", "Automatic captions below this confidence are hidden (manual lyrics are always shown)."));
        textCommon (v, true);
        return v; }();
    return s;
}

enum class LayerType { Image, Video, Text, Caption };
inline const std::vector<PDef>& schemaFor (LayerType t)
{ switch (t) { case LayerType::Image: return imageSchema(); case LayerType::Video: return videoSchema(); case LayerType::Text: return textSchema(); case LayerType::Caption: return captionSchema(); } return imageSchema(); }
inline const char* layerTypeName (LayerType t)
{ switch (t) { case LayerType::Image: return "Image"; case LayerType::Video: return "Video"; case LayerType::Text: return "Text"; case LayerType::Caption: return "Caption"; } return "Layer"; }
}
