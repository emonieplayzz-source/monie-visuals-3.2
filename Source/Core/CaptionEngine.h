// Caption engine: turns a LyricDoc + time into what should be on screen (line / word / group modes, wrapping, active
// word, previous/next context, lead-in, delay, fades, confidence filtering) and parses whisper.cpp JSON into lyric lines.
// Stateless w.r.t. time (pure function of the clock), so seeking, pausing and restarting can never desync captions.
#pragma once
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include "Lyrics.h"
#include "MiniJson.h"
namespace monie
{
struct CaptionSettings
{
    int mode = 0;                 // 0 line, 1 word, 2 group
    int groupWords = 3, maxChars = 32, maxLines = 2;
    bool showPrev = false, showNext = false;
    double delay = 0, leadIn = 0.1, minDur = 1.0, hold = 0.25, fadeIn = 0.15, fadeOut = 0.2;
    float minConf = 0.35f;
};
struct CaptionFrame
{
    bool hasContent = false; std::string text, prevText, nextText; int activeWord = -1; float alpha = 0.f; int lineIndex = -1;
    double appear = 0, length = 0;      // clock time the current line appears and how long it stays (for per-line animations)
};

inline std::vector<std::string> splitWords (const std::string& s) { std::vector<std::string> w; std::istringstream ss (s); std::string x; while (ss >> x) w.push_back (x); return w; }

// Greedy word wrap. Honors manual '\n'. Words longer than maxChars stay on their own line.
inline std::vector<std::string> wrapText (const std::string& text, int maxChars)
{
    std::vector<std::string> out; std::istringstream paras (text); std::string para;
    while (std::getline (paras, para, '\n'))
    {
        std::string cur;
        for (auto& w : splitWords (para))
        {
            if (cur.empty()) cur = w;
            else if ((int) (cur.size() + 1 + w.size()) <= maxChars) cur += " " + w;
            else { out.push_back (cur); cur = w; }
        }
        if (! cur.empty() || para.empty()) out.push_back (cur);
    }
    if (out.empty()) out.push_back ("");
    return out;
}
inline std::string joinLines (const std::vector<std::string>& l, size_t from, size_t count)
{ std::string o; for (size_t i = from; i < std::min (l.size(), from + count); ++i) { if (! o.empty()) o += '\n'; o += l[i]; } return o; }

// Effective end of a line on screen (minimum duration + hold), never past the next line's lead-in start.
inline double visibleEnd (const LyricDoc& d, int i, const CaptionSettings& cs)
{
    auto& l = d.lines[(size_t) i]; double e = std::max (l.end, l.start + cs.minDur) + cs.hold;
    if (i + 1 < (int) d.lines.size()) e = std::min (e, d.lines[(size_t) i + 1].start - cs.leadIn + 1e-6);
    return std::max (e, l.start + 0.05);
}

inline CaptionFrame captionAt (const LyricDoc& doc, double clock, const CaptionSettings& cs)
{
    CaptionFrame f; const double t = clock - cs.delay;
    int cur = -1;
    for (int i = 0; i < (int) doc.lines.size(); ++i)
    {
        auto& l = doc.lines[(size_t) i];
        if (l.start < 0 || (l.autoGen && l.conf >= 0 && l.conf < cs.minConf) || l.text.empty()) continue;
        if (t >= l.start - cs.leadIn && t < visibleEnd (doc, i, cs)) cur = i;
    }
    auto neighbor = [&] (int from, int dir) -> std::string
    {
        for (int i = from + dir; i >= 0 && i < (int) doc.lines.size(); i += dir)
        { auto& l = doc.lines[(size_t) i]; if (l.start >= 0 && ! l.text.empty() && ! (l.autoGen && l.conf >= 0 && l.conf < cs.minConf)) return joinLines (wrapText (l.text, cs.maxChars), 0, (size_t) cs.maxLines); }
        return "";
    };
    if (cur < 0)
    {   // between lines: optionally show the upcoming line dimmed via 'next' context
        if (cs.showNext) { int n = -1; for (int i = 0; i < (int) doc.lines.size(); ++i) if (doc.lines[(size_t) i].start > t) { n = i; break; } if (n >= 0) { f.nextText = neighbor (n - 1, 1); f.hasContent = ! f.nextText.empty(); f.alpha = 0.f; } }
        return f;
    }
    auto& L = doc.lines[(size_t) cur]; f.lineIndex = cur; f.hasContent = true;
    const double vend = visibleEnd (doc, cur, cs);
    float a = 1.f;
    if (cs.fadeIn > 1e-4) a = std::min (a, (float) std::clamp ((t - (L.start - cs.leadIn)) / cs.fadeIn, 0.0, 1.0));
    if (cs.fadeOut > 1e-4) a = std::min (a, (float) std::clamp ((vend - t) / cs.fadeOut, 0.0, 1.0));
    f.alpha = a; f.appear = (L.start - cs.leadIn) + cs.delay; f.length = vend - (L.start - cs.leadIn);
    const double dur = std::max (L.end - L.start, 0.05), prog = std::clamp ((t - L.start) / dur, 0.0, 0.999999);
    auto words = wordsOf (L); const int wi = activeWord (L, t);
    if (cs.mode == 1)                                    // one word at a time
    {
        int idx = wi >= 0 ? wi : (t < L.start ? 0 : (int) words.size() - 1); idx = std::clamp (idx, 0, std::max (0, (int) words.size() - 1));
        f.text = words.empty() ? L.text : words[(size_t) idx].text; f.activeWord = 0;
    }
    else if (cs.mode == 2)                               // groups of N words
    {
        int idx = std::max (0, wi); int g = std::max (1, cs.groupWords); int gs = (idx / g) * g;
        std::string s; for (int i = gs; i < std::min ((int) words.size(), gs + g); ++i) { if (! s.empty()) s += ' '; s += words[(size_t) i].text; }
        f.text = joinLines (wrapText (s, cs.maxChars), 0, (size_t) cs.maxLines); f.activeWord = wi >= 0 ? wi - gs : -1;
    }
    else                                                 // whole line, paged if it needs more than maxLines
    {
        auto lines = wrapText (L.text, cs.maxChars); const int pages = std::max (1, ((int) lines.size() + cs.maxLines - 1) / cs.maxLines);
        const int page = std::min (pages - 1, (int) (prog * pages));
        f.text = joinLines (lines, (size_t) page * (size_t) cs.maxLines, (size_t) cs.maxLines);
        if (wi >= 0) { int before = 0; for (int p2 = 0; p2 < page * cs.maxLines && p2 < (int) lines.size(); ++p2) before += (int) splitWords (lines[(size_t) p2]).size(); f.activeWord = wi - before; }
    }
    if (cs.showPrev) f.prevText = neighbor (cur, -1);
    if (cs.showNext) f.nextText = neighbor (cur, 1);
    return f;
}

// manual lines win; automatic lines are only used where they don't overlap a manual one
inline LyricDoc mergeDocs (const LyricDoc& manual, const LyricDoc& autoDoc)
{
    LyricDoc out = manual;
    for (auto& a : autoDoc.lines)
    {
        bool overlap = false; for (auto& m : manual.lines) if (m.start >= 0 && a.start < m.end && a.end > m.start) { overlap = true; break; }
        if (! overlap) out.lines.push_back (a);
    }
    std::stable_sort (out.lines.begin(), out.lines.end(), [] (auto& x, auto& y) { return x.start < y.start; });
    for (size_t i = 0; i < out.lines.size(); ++i)   // untimed lines sort first (start<0); keep them out of the way
        if (out.lines[i].start < 0) out.lines[i].end = -1;
    return out;
}

// ---- whisper.cpp JSON (-oj / -ojf) -> lyric lines; times are shifted by chunkStart (timeline seconds) ----
struct AsrParseResult { bool ok = false; std::string error; LyricDoc doc; int dropped = 0; };

inline bool isNonSpeech (const std::string& raw)
{
    std::string s; for (char c : raw) if (c != ' ' && c != '\n' && c != '\t') s += c;
    if (s.empty()) return true;
    const char a = s.front(), b = s.back();
    return (a == '[' && b == ']') || (a == '(' && b == ')') || (a == '*' && b == '*') || (a == '\xe2' && s.size() > 3 && s.find ("\xe2\x99\xaa") == 0);   // [MUSIC] (music) *music* ♪
}
inline AsrParseResult parseWhisperJson (const std::string& json, double chunkStart)
{
    AsrParseResult r; JVal root;
    if (! JsonParser::parse (json, root) || root.t != JVal::Obj) { r.error = "whisper output is not valid JSON"; return r; }
    auto* tr = root.get ("transcription"); if (! tr || tr->t != JVal::Arr) { r.error = "no 'transcription' array in whisper output"; return r; }
    for (auto& seg : tr->a)
    {
        auto* off = seg.get ("offsets"); if (! off) continue;
        const double t0 = chunkStart + off->num ("from") / 1000.0, t1 = chunkStart + off->num ("to") / 1000.0;
        std::string text = seg.str ("text");
        if (isNonSpeech (text)) { ++r.dropped; continue; }
        LyricLine L; L.start = t0; L.end = std::max (t1, t0 + 0.05); L.autoGen = true;
        if (auto* toks = seg.get ("tokens"); toks && toks->t == JVal::Arr)
        {
            LyricWord cur; bool open = false; double psum = 0; int pn = 0; double wp = 0; int wn = 0; std::vector<double> wordConf;
            auto flush = [&] { if (open && ! cur.text.empty()) { L.words.push_back (cur); wordConf.push_back (wn ? wp / wn : 0.0); } open = false; wp = 0; wn = 0; };
            for (auto& tk : toks->a)
            {
                std::string tt = tk.str ("text"); if (tt.size() >= 2 && ((tt[0] == '[' && tt[1] == '_') || (tt[0] == '<' && tt[1] == '|'))) continue;   // special tokens
                auto* to = tk.get ("offsets"); const double p = tk.num ("p", 0.5);
                if (tt.empty()) continue;
                const bool startsWord = tt[0] == ' ' || ! open;
                if (startsWord) { flush(); cur = {}; open = true; }
                size_t a0 = tt.find_first_not_of (' '); cur.text += a0 == std::string::npos ? "" : tt.substr (a0);
                if (to) { const double s0 = chunkStart + to->num ("from") / 1000.0, s1 = chunkStart + to->num ("to") / 1000.0; if (startsWord || cur.start == 0) cur.start = s0; cur.end = s1; }
                wp += p; ++wn; psum += p; ++pn;
            }
            flush();
            bool timed = ! L.words.empty(); for (auto& w : L.words) if (w.end <= w.start) timed = false;
            if (! timed) L.words.clear();
            L.conf = pn ? (float) (psum / pn) : 0.f;
        }
        else L.conf = 0.5f;                              // no token info: unknown confidence, treat as middling
        size_t a0 = text.find_first_not_of (" \n\t"), b0 = text.find_last_not_of (" \n\t"); L.text = a0 == std::string::npos ? "" : text.substr (a0, b0 - a0 + 1);
        if (! L.words.empty()) { std::string w; for (auto& x : L.words) { if (! w.empty()) w += ' '; w += x.text; } L.text = w; }
        if (! L.text.empty()) r.doc.lines.push_back (L);
    }
    r.ok = true; return r;
}
}
