// Audio -> parameter mapping engine (the logic behind the future "mapping editor" UI). Plain C++17, tested.
// Runs on the render/UI side, never on the audio thread.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "Randomizer.h"
namespace monie
{
enum class Source { Bass, LowMid, Mid, HighMid, Treble, Rms, Peak, Transient, Beat, Flux, Count };
enum class Curve { Linear, Squared, Sqrt, SCurve, Cubic };

struct Features { float v[(int) Source::Count] {}; bool beatNow = false; };

struct Mapping
{
    Source source = Source::Bass; std::string target;
    float sensitivity = 1.f, inMin = 0.f, inMax = 1.f, outMin = 0.f, outMax = 1.f;
    Curve curve = Curve::Linear;
    float smoothing = 0.f;                  // 0..1 extra low-pass (0..~500 ms)
    float attackMs = 10.f, releaseMs = 150.f;
    bool invert = false; float threshold = 0.f;    // 0..<1, below = 0
    bool quantizeToBeat = false;            // output only updates on beat events (sample & hold)
    float randomAmount = 0.f; uint32_t seed = 1;   // reproducible modulation, fraction of output range
    // runtime state
    float env = 0, lp = 0, held = 0, rnd = 0, rndTimer = 0; Rng rng { 1 }; bool seeded = false;
};

inline float applyCurve (Curve c, float x)
{
    x = std::clamp (x, 0.f, 1.f);
    switch (c) { case Curve::Linear: return x; case Curve::Squared: return x * x; case Curve::Sqrt: return std::sqrt (x);
                 case Curve::SCurve: return x * x * (3.f - 2.f * x); case Curve::Cubic: return x * x * x; }
    return x;
}

class MappingEngine
{
public:
    std::vector<Mapping> maps;
    int add (Mapping m) { maps.push_back (std::move (m)); return (int) maps.size() - 1; }
    void remove (int i) { if (i >= 0 && i < (int) maps.size()) maps.erase (maps.begin() + i); }
    void resetStates() { for (auto& m : maps) { m.env = m.lp = m.held = m.rnd = m.rndTimer = 0; m.seeded = false; } }

    // Several mappings may drive one target: their outputs are summed (caller clamps to the parameter range).
    std::map<std::string, float> process (const Features& f, float dt)
    {
        std::map<std::string, float> out;
        for (auto& m : maps) out[m.target] += step (m, f, dt);
        return out;
    }

    // simple line-based text format for saved mapping presets
    std::string toText() const
    {
        std::ostringstream o;
        for (auto& m : maps) o << (int) m.source << '|' << m.target << '|' << m.sensitivity << '|' << m.inMin << '|' << m.inMax << '|' << m.outMin << '|' << m.outMax
                               << '|' << (int) m.curve << '|' << m.smoothing << '|' << m.attackMs << '|' << m.releaseMs << '|' << m.invert << '|' << m.threshold
                               << '|' << m.quantizeToBeat << '|' << m.randomAmount << '|' << m.seed << '\n';
        return o.str();
    }
    static MappingEngine fromText (const std::string& t)
    {
        MappingEngine e; std::istringstream in (t); std::string line;
        while (std::getline (in, line))
        {
            std::vector<std::string> p; std::string c; std::istringstream ls (line); while (std::getline (ls, c, '|')) p.push_back (c);
            if (p.size() != 16) continue;
            Mapping m; m.source = (Source) std::clamp (std::atoi (p[0].c_str()), 0, (int) Source::Count - 1); m.target = p[1];
            m.sensitivity = std::stof (p[2]); m.inMin = std::stof (p[3]); m.inMax = std::stof (p[4]); m.outMin = std::stof (p[5]); m.outMax = std::stof (p[6]);
            m.curve = (Curve) std::clamp (std::atoi (p[7].c_str()), 0, 4); m.smoothing = std::stof (p[8]); m.attackMs = std::stof (p[9]); m.releaseMs = std::stof (p[10]);
            m.invert = p[11] == "1"; m.threshold = std::stof (p[12]); m.quantizeToBeat = p[13] == "1"; m.randomAmount = std::stof (p[14]); m.seed = (uint32_t) std::stoul (p[15]);
            e.add (m);
        }
        return e;
    }
private:
    static float step (Mapping& m, const Features& f, float dt)
    {
        if (! m.seeded) { m.rng = Rng (m.seed); m.seeded = true; }
        float x = f.v[(int) m.source] * m.sensitivity;
        const float span = m.inMax - m.inMin; x = span > 1e-6f ? (x - m.inMin) / span : 0.f; x = std::clamp (x, 0.f, 1.f);
        const float th = std::clamp (m.threshold, 0.f, 0.999f); x = x < th ? 0.f : (x - th) / (1.f - th);
        if (m.invert) x = 1.f - x;
        x = applyCurve (m.curve, x);
        const float ms = x > m.env ? m.attackMs : m.releaseMs;
        m.env += (x - m.env) * (ms <= 0.f ? 1.f : 1.f - std::exp (-dt * 1000.f / ms));
        const float tau = m.smoothing * 0.5f;                       // seconds
        m.lp += (m.env - m.lp) * (tau <= 1e-4f ? 1.f : 1.f - std::exp (-dt / tau));
        float y = m.lp;
        if (m.quantizeToBeat) { if (f.beatNow) m.held = y; y = m.held; }
        m.rndTimer -= dt; if (m.rndTimer <= 0.f) { m.rnd = m.rng.uniform() * 2.f - 1.f; m.rndTimer = 0.05f; }
        y += m.rnd * m.randomAmount;
        const float lo = std::min (m.outMin, m.outMax), hi = std::max (m.outMin, m.outMax);
        return std::clamp (m.outMin + y * (m.outMax - m.outMin), lo, hi);
    }
};
}
