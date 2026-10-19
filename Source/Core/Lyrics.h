// Lyrics engine: parse LRC (incl. enhanced word tags), SRT and plain text; export SRT/LRC; time queries for display.
// No JUCE, no network, no recognition: lyrics always come from files/text the USER supplies.
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
struct LyricWord { double start = 0, end = 0; std::string text; };
struct LyricLine { double start = -1, end = -1; std::string text; std::vector<LyricWord> words; float conf = -1.f; bool autoGen = false; };   // conf < 0 = manual / imported   // start < 0 = untimed
struct LyricDoc { std::vector<LyricLine> lines; };

namespace lyr_detail
{
inline std::string trim (const std::string& s)
{ size_t a = 0, b = s.size(); while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a; while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b; return s.substr (a, b - a); }
inline bool parseTime (const std::string& in, double& out)   // [hh:]mm:ss[.,]fff
{
    std::string s = trim (in); if (s.empty()) return false;
    std::vector<double> parts; std::string cur;
    for (char c : s) { if (c == ':') { parts.push_back (std::atof (cur.c_str())); cur.clear(); } else if (c == ',') cur += '.'; else if ((c >= '0' && c <= '9') || c == '.') cur += c; else return false; }
    parts.push_back (std::atof (cur.c_str()));
    if (parts.size() < 2 || parts.size() > 3) return false;
    out = 0; for (double p : parts) out = out * 60.0 + p; return true;
}
inline std::vector<std::string> splitLines (const std::string& t)
{ std::vector<std::string> v; std::string cur; for (char c : t) { if (c == '\n') { v.push_back (cur); cur.clear(); } else if (c != '\r') cur += c; } v.push_back (cur); return v; }
inline std::string fmt (double t, bool comma)
{
    if (t < 0) { t = 0; }
    long ms = (long) std::llround (t * 1000.0); long h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, f = ms % 1000; char b[48];
    std::snprintf (b, sizeof b, comma ? "%02ld:%02ld:%02ld,%03ld" : "%02ld:%02ld:%02ld.%03ld", h, m, s, f); return b;
}
}

// Positive [offset:ms] makes lyrics appear EARLIER (LRC convention). lastLineTail = duration given to the final line.
inline LyricDoc parseLRC (const std::string& text, double lastLineTail = 4.0)
{
    using namespace lyr_detail;
    LyricDoc doc; double offset = 0;
    for (auto& raw : splitLines (text))
    {
        std::string line = trim (raw); std::vector<double> stamps;
        while (! line.empty() && line[0] == '[')
        {
            const auto close = line.find (']'); if (close == std::string::npos) break;
            const std::string tag = line.substr (1, close - 1); double t;
            if (parseTime (tag, t)) stamps.push_back (t);
            else if (tag.rfind ("offset:", 0) == 0) offset = std::atof (tag.c_str() + 7) / 1000.0;
            line = trim (line.substr (close + 1));
        }
        if (stamps.empty()) continue;                       // metadata or junk
        LyricLine base; std::vector<LyricWord> words; std::string plain;
        for (size_t i = 0; i < line.size();)                // enhanced LRC: <mm:ss.xx>word
        {
            if (line[i] == '<') { const auto c = line.find ('>', i); double wt;
                if (c != std::string::npos && parseTime (line.substr (i + 1, c - i - 1), wt)) {
                    size_t j = c + 1, k = line.find ('<', j); std::string w = trim (line.substr (j, k == std::string::npos ? std::string::npos : k - j));
                    if (! w.empty()) { words.push_back ({ wt, wt, w }); if (! plain.empty()) plain += ' '; plain += w; }
                    i = (k == std::string::npos) ? line.size() : k; continue; } }
            size_t k = line.find ('<', i + 1); plain += line.substr (i, k == std::string::npos ? std::string::npos : k - i); i = (k == std::string::npos) ? line.size() : k;
        }
        base.text = words.empty() ? trim (plain) : plain;
        for (double st : stamps)
        {
            LyricLine l = base; l.start = st - offset; l.words = words;
            for (auto& w : l.words) { w.start -= offset; w.end -= offset; }
            doc.lines.push_back (l);
        }
    }
    std::stable_sort (doc.lines.begin(), doc.lines.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    for (size_t i = 0; i < doc.lines.size(); ++i)
    {
        auto& l = doc.lines[i]; l.end = i + 1 < doc.lines.size() ? doc.lines[i + 1].start : l.start + lastLineTail;
        for (size_t w = 0; w < l.words.size(); ++w) l.words[w].end = w + 1 < l.words.size() ? l.words[w + 1].start : l.end;
    }
    return doc;
}

inline LyricDoc parseSRT (const std::string& text)
{
    using namespace lyr_detail;
    LyricDoc doc; std::vector<std::string> block; auto flush = [&]
    {
        size_t ti = 0; while (ti < block.size() && block[ti].find ("-->") == std::string::npos) ++ti;
        if (ti < block.size())
        {
            const auto& tl = block[ti]; const auto arrow = tl.find ("-->"); double a, b;
            if (parseTime (tl.substr (0, arrow), a) && parseTime (tl.substr (arrow + 3), b))
            {
                std::string body; for (size_t i = ti + 1; i < block.size(); ++i) { if (! body.empty()) body += '\n'; body += block[i]; }
                std::string clean; bool inTag = false; for (char c : body) { if (c == '<') inTag = true; else if (c == '>') inTag = false; else if (! inTag) clean += c; }
                LyricLine l; l.start = a; l.end = b; l.text = trim (clean); doc.lines.push_back (l);
            }
        }
        block.clear();
    };
    for (auto& raw : splitLines (text)) { if (trim (raw).empty()) flush(); else block.push_back (trim (raw)); }
    flush();
    std::stable_sort (doc.lines.begin(), doc.lines.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    return doc;
}

inline LyricDoc parsePlain (const std::string& text)
{
    LyricDoc doc; for (auto& l : lyr_detail::splitLines (text)) { auto t = lyr_detail::trim (l); if (! t.empty()) { LyricLine x; x.text = t; doc.lines.push_back (x); } }
    return doc;
}
// Rough auto-timing for untimed lyrics: spreads lines over [t0,t1] proportionally to text length. A starting point to correct by hand.
inline void distributeEvenly (LyricDoc& d, double t0, double t1)
{
    double total = 0; for (auto& l : d.lines) total += (double) std::max<size_t> (l.text.size(), 4);
    double t = t0; for (auto& l : d.lines) { const double len = (t1 - t0) * (double) std::max<size_t> (l.text.size(), 4) / total; l.start = t; l.end = t + len; t += len; l.words.clear(); }
}
inline void shift (LyricDoc& d, double seconds)
{ for (auto& l : d.lines) { if (l.start >= 0) { l.start += seconds; l.end += seconds; } for (auto& w : l.words) { w.start += seconds; w.end += seconds; } } }

inline std::string toSRT (const LyricDoc& d)
{
    std::string o; int n = 1; for (auto& l : d.lines) { if (l.start < 0) continue; o += std::to_string (n++) + "\n" + lyr_detail::fmt (l.start, true) + " --> " + lyr_detail::fmt (l.end, true) + "\n" + l.text + "\n\n"; } return o;
}
inline std::string toLRC (const LyricDoc& d)
{
    auto stamp = [] (double t) { if (t < 0) t = 0; long cs = (long) std::llround (t * 100.0); char b[32]; std::snprintf (b, sizeof b, "%02ld:%02ld.%02ld", cs / 6000, (cs / 100) % 60, cs % 100); return std::string (b); };
    std::string o;
    for (auto& l : d.lines) { if (l.start < 0) continue; o += "[" + stamp (l.start) + "]";
        if (l.words.empty()) o += l.text; else for (size_t i = 0; i < l.words.size(); ++i) o += (i ? " <" : "<") + stamp (l.words[i].start) + ">" + l.words[i].text;
        o += "\n"; }
    return o;
}

// ---- display queries ----
inline int activeLine (const LyricDoc& d, double t, double leadIn = 0.0)
{ int best = -1; for (size_t i = 0; i < d.lines.size(); ++i) { auto& l = d.lines[i]; if (l.start >= 0 && t >= l.start - leadIn && t < l.end) best = (int) i; } return best; }

// Words with timing: real word timestamps if present, otherwise estimated across the line proportional to word length.
inline std::vector<LyricWord> wordsOf (const LyricLine& l)
{
    if (! l.words.empty()) return l.words;
    std::vector<LyricWord> out; std::istringstream ss (l.text); std::string w; std::vector<std::string> ws; size_t total = 0;
    while (ss >> w) { ws.push_back (w); total += w.size(); }
    double t = l.start; for (auto& x : ws) { const double len = (l.end - l.start) * (double) x.size() / (double) std::max<size_t> (total, 1); out.push_back ({ t, t + len, x }); t += len; }
    return out;
}
inline int activeWord (const LyricLine& l, double t)
{ auto w = wordsOf (l); int best = -1; for (size_t i = 0; i < w.size(); ++i) if (t >= w[i].start) best = (int) i; return (t >= l.start && t < l.end) ? best : -1; }

struct LyricView { int current = -1; std::vector<int> previous, upcoming; };
inline LyricView viewAt (const LyricDoc& d, double t, int numPrev, int numNext, double leadIn = 0.0)
{
    LyricView v; v.current = activeLine (d, t, leadIn); const int n = (int) d.lines.size(); int anchor;
    if (v.current >= 0) anchor = v.current + 1;
    else { anchor = 0; while (anchor < n && d.lines[(size_t) anchor].start <= t) ++anchor; }
    const int curOrAnchor = v.current >= 0 ? v.current : anchor;
    for (int i = std::max (0, curOrAnchor - numPrev); i < curOrAnchor; ++i) v.previous.push_back (i);
    for (int i = anchor; i < std::min (n, anchor + numNext); ++i) v.upcoming.push_back (i);
    return v;
}
}
