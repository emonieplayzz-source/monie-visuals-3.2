// Timeline engine: easing, keyframe tracks (copy/paste), clip editing (move/trim/split/duplicate/snap), undo/redo.
// Plain C++17, no JUCE, fully unit-tested. Times are in seconds.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
namespace monie
{
enum class Interp { Linear, Smooth, EaseIn, EaseOut, EaseInOut, Step };

inline float ease (Interp m, float t)
{
    t = std::clamp (t, 0.f, 1.f);
    switch (m)
    {
        case Interp::Linear:    return t;
        case Interp::Smooth:    return t * t * (3.f - 2.f * t);
        case Interp::EaseIn:    return t * t;
        case Interp::EaseOut:   return 1.f - (1.f - t) * (1.f - t);
        case Interp::EaseInOut: return t < .5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t);
        case Interp::Step:      return 0.f;       // hold the left key's value until the next key
    }
    return t;
}

struct Keyframe { double time = 0; float value = 0; Interp interp = Interp::Smooth; };   // interp shapes the segment to the NEXT key

class KeyframeTrack
{
public:
    explicit KeyframeTrack (float defaultValue = 0.f) : def (defaultValue) {}
    void add (double time, float value, Interp i = Interp::Smooth)
    {
        for (auto& k : keys) if (std::fabs (k.time - time) < 1e-9) { k.value = value; k.interp = i; return; }
        keys.push_back ({ time, value, i });
        std::sort (keys.begin(), keys.end(), [] (auto& a, auto& b) { return a.time < b.time; });
    }
    bool removeAt (double time) { for (size_t i = 0; i < keys.size(); ++i) if (std::fabs (keys[i].time - time) < 1e-9) { keys.erase (keys.begin() + (long) i); return true; } return false; }
    float evaluate (double t) const
    {
        if (keys.empty()) return def;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time)  return keys.back().value;
        size_t i = 0; while (keys[i + 1].time <= t) ++i;
        const auto& a = keys[i]; const auto& b = keys[i + 1];
        const float u = (float) ((t - a.time) / (b.time - a.time));
        return a.value + (b.value - a.value) * ease (a.interp, u);
    }
    std::vector<Keyframe> copyRange (double t0, double t1) const
    {
        std::vector<Keyframe> out; for (auto k : keys) if (k.time >= t0 && k.time <= t1) { k.time -= t0; out.push_back (k); } return out;
    }
    void paste (const std::vector<Keyframe>& ks, double at) { for (auto k : ks) add (at + k.time, k.value, k.interp); }
    void assign (std::vector<Keyframe> ks) { keys = std::move (ks); std::sort (keys.begin(), keys.end(), [] (auto& a, auto& b) { return a.time < b.time; }); }
    const std::vector<Keyframe>& all() const { return keys; }
    bool empty() const { return keys.empty(); }
private:
    std::vector<Keyframe> keys; float def;
};

struct Clip { int id = 0; double start = 0, length = 1, inPoint = 0; std::string name; };

class ClipList
{
public:
    int add (double start, double length, std::string name) { Clip c; c.id = nextId++; c.start = std::max (0.0, start); c.length = std::max (minLen, length); c.name = std::move (name); clips.push_back (c); return c.id; }
    Clip* find (int id) { for (auto& c : clips) if (c.id == id) return &c; return nullptr; }
    bool remove (int id) { for (size_t i = 0; i < clips.size(); ++i) if (clips[i].id == id) { clips.erase (clips.begin() + (long) i); return true; } return false; }
    bool move (int id, double newStart) { if (auto* c = find (id)) { c->start = std::max (0.0, newStart); return true; } return false; }
    bool trimStart (int id, double newStart)    // source in-point advances so the visible content doesn't slide
    {
        auto* c = find (id); if (! c) return false;
        const double end = c->start + c->length; newStart = std::clamp (newStart, 0.0, end - minLen);
        c->inPoint = std::max (0.0, c->inPoint + (newStart - c->start)); c->length = end - newStart; c->start = newStart; return true;
    }
    bool trimEnd (int id, double newEnd) { auto* c = find (id); if (! c) return false; c->length = std::max (minLen, newEnd - c->start); return true; }
    int split (int id, double t)                // returns id of the new right-hand clip, or -1
    {
        auto* c = find (id); if (! c || t <= c->start + minLen || t >= c->start + c->length - minLen) return -1;
        Clip r = *c; r.id = nextId++; r.start = t; r.inPoint = c->inPoint + (t - c->start); r.length = c->start + c->length - t; r.name = c->name;
        c->length = t - c->start; clips.push_back (r); return r.id;
    }
    int duplicate (int id) { auto* c = find (id); if (! c) return -1; Clip d = *c; d.id = nextId++; d.start = c->start + c->length; d.name += " copy"; clips.push_back (d); return d.id; }
    // Snap t to the nearest guide within threshold (seconds); guides typically = playhead, markers, other clip edges, beat grid.
    static double snap (double t, const std::vector<double>& guides, double threshold)
    {
        double best = t, bd = threshold; for (double g : guides) if (std::fabs (g - t) <= bd) { bd = std::fabs (g - t); best = g; } return best;
    }
    std::vector<double> edges() const { std::vector<double> e; for (auto& c : clips) { e.push_back (c.start); e.push_back (c.start + c.length); } return e; }
    const std::vector<Clip>& all() const { return clips; }
private:
    std::vector<Clip> clips; int nextId = 1; static constexpr double minLen = 0.01;
};

class UndoStack
{
public:
    struct Cmd { std::string name; std::function<void()> redo, undo; };
    void execute (Cmd c) { c.redo(); done.push_back (std::move (c)); undone.clear(); }
    bool canUndo() const { return ! done.empty(); }
    bool canRedo() const { return ! undone.empty(); }
    bool undo() { if (done.empty()) return false; auto c = std::move (done.back()); done.pop_back(); c.undo(); undone.push_back (std::move (c)); return true; }
    bool redo() { if (undone.empty()) return false; auto c = std::move (undone.back()); undone.pop_back(); c.redo(); done.push_back (std::move (c)); return true; }
    std::string nextUndoName() const { return done.empty() ? "" : done.back().name; }
private:
    std::vector<Cmd> done, undone;
};
}
