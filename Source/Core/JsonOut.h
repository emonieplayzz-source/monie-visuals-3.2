// Minimal streaming JSON writer (pairs with MiniJson.h for reading). Used for the project file and exports.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
namespace monie
{
class JsonWriter
{
public:
    std::string str() const { return out; }
    JsonWriter& beginObj() { sep(); out += '{'; first.push_back (true); return *this; }
    JsonWriter& endObj() { out += '}'; first.pop_back(); return *this; }
    JsonWriter& beginArr() { sep(); out += '['; first.push_back (true); return *this; }
    JsonWriter& endArr() { out += ']'; first.pop_back(); return *this; }
    JsonWriter& key (const std::string& k) { sep(); quote (k); out += ':'; pendingValue = true; return *this; }
    JsonWriter& val (const std::string& v) { sep(); quote (v); return *this; }
    JsonWriter& val (const char* v) { return val (std::string (v)); }
    JsonWriter& val (double v) { sep(); char b[40]; std::snprintf (b, sizeof b, "%.9g", v); out += b; return *this; }
    JsonWriter& val (float v) { return val ((double) v); }
    JsonWriter& val (int v) { sep(); out += std::to_string (v); return *this; }
    JsonWriter& val (bool v) { sep(); out += v ? "true" : "false"; return *this; }
    template <class T> JsonWriter& kv (const std::string& k, const T& v) { key (k); return val (v); }
private:
    std::string out; std::vector<bool> first; bool pendingValue = false;
    void sep() { if (pendingValue) { pendingValue = false; return; } if (! first.empty()) { if (! first.back()) out += ','; first.back() = false; } }
    void quote (const std::string& s)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c) { case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
                default: if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); out += b; } else out += (char) c; }
        }
        out += '"';
    }
};
}
