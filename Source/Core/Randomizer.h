// Seeded, reproducible randomization / variation with per-parameter locks. No JUCE dependency.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace monie
{
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed ? seed * 2654435761u : 0x9e3779b9u) { next(); next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (float) (next() >> 8) / 16777216.f; }   // [0,1)
};
struct RParam { std::string id; float minV, maxV, value; bool locked; };

// Moves each unlocked parameter by up to +/- amount * range (amount 0..1). Same seed -> same result.
inline void vary (std::vector<RParam>& ps, uint32_t seed, float amount)
{
    Rng r (seed);
    for (auto& p : ps)
    {
        const float u = r.uniform();                     // always consume, so locking one param doesn't change the others
        if (p.locked) continue;
        p.value = std::clamp (p.value + (u * 2.f - 1.f) * amount * (p.maxV - p.minV), p.minV, p.maxV);
    }
}
// Full randomization within ranges (locked params untouched).
inline void randomize (std::vector<RParam>& ps, uint32_t seed)
{
    Rng r (seed);
    for (auto& p : ps) { const float u = r.uniform(); if (! p.locked) p.value = p.minV + u * (p.maxV - p.minV); }
}
}
