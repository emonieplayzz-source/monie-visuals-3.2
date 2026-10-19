// Gradient / palette model: arbitrary stops, hex parsing, evaluation, LUT baking, built-in palettes (deliberately NOT all neon),
// text import/export. No JUCE dependency.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
namespace monie
{
struct RGB { float r = 0, g = 0, b = 0; };
inline bool parseHex (const std::string& h, RGB& out)
{
    std::string s = h; if (! s.empty() && s[0] == '#') s.erase (0, 1);
    if (s.size() == 3) s = std::string { s[0], s[0], s[1], s[1], s[2], s[2] };
    if (s.size() != 6 || s.find_first_not_of ("0123456789abcdefABCDEF") != std::string::npos) return false;
    unsigned v = (unsigned) std::strtoul (s.c_str(), nullptr, 16); out = { ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f }; return true;
}
inline std::string toHex (RGB c)
{ char b[16]; auto q = [] (float x) { return (int) std::lround (std::clamp (x, 0.f, 1.f) * 255.f); }; std::snprintf (b, sizeof b, "#%02x%02x%02x", q (c.r), q (c.g), q (c.b)); return b; }
inline void rgb2hsv (RGB c, float& h, float& s, float& v)
{
    float mx = std::max ({ c.r, c.g, c.b }), mn = std::min ({ c.r, c.g, c.b }), d = mx - mn; v = mx; s = mx > 0 ? d / mx : 0; h = 0;
    if (d > 0) { if (mx == c.r) h = std::fmod ((c.g - c.b) / d, 6.f); else if (mx == c.g) h = (c.b - c.r) / d + 2; else h = (c.r - c.g) / d + 4; h /= 6.f; if (h < 0) h += 1; }
}
inline RGB hsv2rgb (float h, float s, float v)
{
    h = h - std::floor (h); float c = v * s, x = c * (1 - std::fabs (std::fmod (h * 6, 2.f) - 1)), m = v - c; RGB r;
    int i = (int) (h * 6); switch (i % 6) { case 0: r = { c, x, 0 }; break; case 1: r = { x, c, 0 }; break; case 2: r = { 0, c, x }; break; case 3: r = { 0, x, c }; break; case 4: r = { x, 0, c }; break; default: r = { c, 0, x }; }
    return { r.r + m, r.g + m, r.b + m };
}

struct Stop { float pos = 0; RGB c; };
struct Gradient
{
    std::string name = "Custom"; std::vector<Stop> stops;
    bool operator== (const Gradient& o) const
    { if (name != o.name || stops.size() != o.stops.size()) return false; for (size_t i = 0; i < stops.size(); ++i) if (stops[i].pos != o.stops[i].pos || stops[i].c.r != o.stops[i].c.r || stops[i].c.g != o.stops[i].c.g || stops[i].c.b != o.stops[i].c.b) return false; return true; }
    void normalise() { std::stable_sort (stops.begin(), stops.end(), [] (auto& a, auto& b) { return a.pos < b.pos; }); for (auto& s : stops) s.pos = std::clamp (s.pos, 0.f, 1.f); }
    RGB at (float t) const
    {
        if (stops.empty()) return { t, t, t };
        if (stops.size() == 1) return stops[0].c;
        t = std::clamp (t, 0.f, 1.f); if (t <= stops.front().pos) return stops.front().c; if (t >= stops.back().pos) return stops.back().c;
        for (size_t i = 1; i < stops.size(); ++i) if (t <= stops[i].pos)
        { auto &a = stops[i - 1], &b = stops[i]; float f = b.pos > a.pos ? (t - a.pos) / (b.pos - a.pos) : 0; return { a.c.r + (b.c.r - a.c.r) * f, a.c.g + (b.c.g - a.c.g) * f, a.c.b + (b.c.b - a.c.b) * f }; }
        return stops.back().c;
    }
    // 256 x RGBA8 lookup table for the GPU
    void bake (unsigned char* rgba, int n = 256) const { for (int i = 0; i < n; ++i) { auto c = at ((float) i / (float) (n - 1)); rgba[i * 4] = (unsigned char) std::lround (c.r * 255); rgba[i * 4 + 1] = (unsigned char) std::lround (c.g * 255); rgba[i * 4 + 2] = (unsigned char) std::lround (c.b * 255); rgba[i * 4 + 3] = 255; } }
    // "name|0:#rrggbb,0.5:#rrggbb,1:#rrggbb"
    std::string toText() const { std::string o = name + "|"; for (size_t i = 0; i < stops.size(); ++i) { char b[24]; std::snprintf (b, sizeof b, "%.4g:", stops[i].pos); if (i) o += ','; o += b + toHex (stops[i].c); } return o; }
    static bool fromText (const std::string& t, Gradient& g)
    {
        auto bar = t.find ('|'); if (bar == std::string::npos) return false; Gradient r; r.name = t.substr (0, bar); std::istringstream ss (t.substr (bar + 1)); std::string item;
        while (std::getline (ss, item, ',')) { auto c = item.find (':'); if (c == std::string::npos) return false; Stop s; char* e = nullptr; s.pos = std::strtof (item.c_str(), &e); if (e == item.c_str()) return false; if (! parseHex (item.substr (c + 1), s.c)) return false; r.stops.push_back (s); }
        if (r.stops.size() < 2) return false;
        r.normalise(); g = r; return true;
    }
    // multi-line palette text: one hex per line / comma / space (also accepts GIMP .gpl "r g b name" lines and Adobe-like hex lists)
    static bool fromHexList (const std::string& text, const std::string& name, Gradient& g)
    {
        Gradient r; r.name = name; std::vector<RGB> cols; std::istringstream ls (text); std::string line;
        while (std::getline (ls, line))
        {
            if (line.empty() || line.rfind ("GIMP", 0) == 0 || line.rfind ("Name:", 0) == 0 || line.rfind ("Columns:", 0) == 0 || line.rfind ("# ", 0) == 0) continue;
            std::string t = line; for (auto& ch : t) if (ch == ',' || ch == ';' || ch == '\t') ch = ' '; std::istringstream ws (t); std::string w; std::vector<std::string> toks; while (ws >> w) toks.push_back (w);
            if (toks.size() >= 3 && toks[0].find_first_not_of ("0123456789") == std::string::npos && toks[1].find_first_not_of ("0123456789") == std::string::npos && toks[2].find_first_not_of ("0123456789") == std::string::npos)
            { int a = std::atoi (toks[0].c_str()), b = std::atoi (toks[1].c_str()), c = std::atoi (toks[2].c_str()); if (a < 256 && b < 256 && c < 256) { cols.push_back ({ a / 255.f, b / 255.f, c / 255.f }); continue; } }
            for (auto& tk : toks) { RGB c; std::string h = tk; if (h[0] != '#' && h.size() == 6) h = "#" + h; if (h[0] == '#' && parseHex (h, c)) cols.push_back (c); }
        }
        if (cols.size() < 2) return false;
        for (size_t i = 0; i < cols.size(); ++i) r.stops.push_back ({ (float) i / (float) (cols.size() - 1), cols[i] });
        g = r; return true;
    }
};

inline Gradient makeGrad (const char* name, std::initializer_list<const char*> hex)
{ Gradient g; g.name = name; size_t n = hex.size(), i = 0; for (auto h : hex) { Stop s; s.pos = n > 1 ? (float) i / (float) (n - 1) : 0; parseHex (h, s.c); g.stops.push_back (s); ++i; } return g; }

inline const std::vector<Gradient>& builtinPalettes()
{
    static const std::vector<Gradient> p = {
        makeGrad ("Midnight Ink",    { "#05070f", "#16213e", "#3a5a8c", "#d9e4f5" }),
        makeGrad ("Ember",           { "#0d0505", "#5c1a0b", "#d4541c", "#ffd9a0" }),
        makeGrad ("Forest Floor",    { "#050b07", "#1b3a24", "#5b8a3c", "#e8e2b8" }),
        makeGrad ("Ocean Depth",     { "#02070c", "#0b3a4f", "#2a8c9a", "#d0f0ea" }),
        makeGrad ("Rose Dust",       { "#120a0e", "#5b2a40", "#c46c86", "#f6dfe0" }),
        makeGrad ("Monochrome",      { "#000000", "#555555", "#aaaaaa", "#ffffff" }),
        makeGrad ("Sepia Film",      { "#120a05", "#4a2e18", "#a8754a", "#f1deb8" }),
        makeGrad ("Ice",             { "#050a14", "#1c3b66", "#7fb4e0", "#f3fbff" }),
        makeGrad ("Desert",          { "#1a0e08", "#7a3b1f", "#d99a4a", "#f7e7bd" }),
        makeGrad ("Plum & Gold",     { "#0f0614", "#3b1450", "#8a3b78", "#e9b44c" }),
        makeGrad ("Slate & Coral",   { "#0c1014", "#2b3a47", "#8895a0", "#ff7a5c" }),
        makeGrad ("Olive Haze",      { "#0a0a05", "#3a3a1a", "#8a8a3a", "#ece8c0" }),
        makeGrad ("Vapor",           { "#0b0620", "#4b2a8a", "#e35d9c", "#ffe9a8" }),
        makeGrad ("Neon Night",      { "#050010", "#ff2a6d", "#05d9e8", "#f9f871" }),
        makeGrad ("Acid",            { "#000000", "#2b00ff", "#00ff9c", "#f2ff00" }),
        makeGrad ("Sunset",          { "#0d0221", "#6a1b5f", "#e8564a", "#ffd27a" }),
        makeGrad ("Aurora",          { "#02060a", "#0b5d4a", "#3ad29f", "#b8a4ff" }),
        makeGrad ("Blood Moon",      { "#000000", "#3a0508", "#a1121a", "#f4d6c8" }),
        makeGrad ("Terminal",        { "#000a00", "#003a10", "#00a844", "#c8ffd8" }),
        makeGrad ("Candy",           { "#2a0a3a", "#ff5fa2", "#ffd0e6", "#7ee8fa" }),
        makeGrad ("Sage & Cream",    { "#101510", "#4a5d4a", "#a3b59a", "#f7f1e1" }),
        makeGrad ("Copper",          { "#0a0604", "#4a2410", "#b8672f", "#7fd1c4" }),
        makeGrad ("Rainbow",         { "#ff004c", "#ff9a00", "#f2ff00", "#00e676", "#00b0ff", "#7a3cff", "#ff004c" }),
        makeGrad ("Duotone Blue/Amber", { "#0a1a3a", "#ffb43a" }),
    };
    return p;
}
}
