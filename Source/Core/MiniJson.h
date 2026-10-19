// Tiny JSON parser (objects, arrays, strings with escapes, numbers, bool, null). Used for whisper.cpp / ffprobe output.
#pragma once
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
namespace monie
{
struct JVal
{
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0; bool b = false; std::string s; std::vector<JVal> a; std::vector<std::pair<std::string, JVal>> o;
    const JVal* get (const std::string& k) const { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
    double num (const std::string& k, double d = 0) const { auto* v = get (k); return v && v->t == Num ? v->n : (v && v->t == Str ? std::atof (v->s.c_str()) : d); }
    std::string str (const std::string& k, const std::string& d = "") const { auto* v = get (k); return v && v->t == Str ? v->s : d; }
};

class JsonParser
{
public:
    static bool parse (const std::string& text, JVal& out) { JsonParser p (text); p.ws(); if (! p.value (out, 0)) return false; p.ws(); return p.i == p.t.size(); }
private:
    explicit JsonParser (const std::string& s) : t (s) {}
    const std::string& t; size_t i = 0;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    static void utf8 (std::string& o, unsigned cp)
    { if (cp < 0x80) o += (char) cp; else if (cp < 0x800) { o += (char) (0xC0 | (cp >> 6)); o += (char) (0x80 | (cp & 63)); }
      else if (cp < 0x10000) { o += (char) (0xE0 | (cp >> 12)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); }
      else { o += (char) (0xF0 | (cp >> 18)); o += (char) (0x80 | ((cp >> 12) & 63)); o += (char) (0x80 | ((cp >> 6) & 63)); o += (char) (0x80 | (cp & 63)); } }
    bool str (std::string& o)
    {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size() && t[i] != '"')
        {
            char c = t[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) { case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': { if (i + 4 > t.size()) return false; unsigned cp = (unsigned) std::strtoul (t.substr (i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') { unsigned lo = (unsigned) std::strtoul (t.substr (i + 2, 4).c_str(), nullptr, 16); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                    utf8 (o, cp); break; }
                default: o += e; }
        }
        if (i >= t.size()) return false;
        ++i;
        return true;
    }
    bool value (JVal& v, int depth)
    {
        if (depth > 64 || i >= t.size()) return false;
        char c = t[i];
        if (c == '{') { v.t = JVal::Obj; ++i; ws(); if (i < t.size() && t[i] == '}') { ++i; return true; }
            for (;;) { ws(); std::string k; if (! str (k)) return false; ws(); if (i >= t.size() || t[i] != ':') return false; ++i; ws(); JVal x; if (! value (x, depth + 1)) return false; v.o.emplace_back (k, std::move (x)); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; } if (i < t.size() && t[i] == '}') { ++i; return true; } return false; } }
        if (c == '[') { v.t = JVal::Arr; ++i; ws(); if (i < t.size() && t[i] == ']') { ++i; return true; }
            for (;;) { ws(); JVal x; if (! value (x, depth + 1)) return false; v.a.push_back (std::move (x)); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; } if (i < t.size() && t[i] == ']') { ++i; return true; } return false; } }
        if (c == '"') { v.t = JVal::Str; return str (v.s); }
        if (t.compare (i, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; i += 4; return true; }
        if (t.compare (i, 5, "false") == 0) { v.t = JVal::Bool; v.b = false; i += 5; return true; }
        if (t.compare (i, 4, "null") == 0) { v.t = JVal::Null; i += 4; return true; }
        char* end = nullptr; v.n = std::strtod (t.c_str() + i, &end); if (end == t.c_str() + i) return false; v.t = JVal::Num; i = (size_t) (end - t.c_str()); return true;
    }
};
}
