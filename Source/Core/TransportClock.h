// Timeline clock. In a plugin the host transport is authoritative while it plays; between host updates we extrapolate with the
// wall clock, slew small errors (no jitter) and snap on seeks/loops. When stopped the clock holds the host position.
// Standalone / no-host mode uses an internal transport (play, pause, seek, loop region). Offline export uses exact frame times.
// All times in seconds. Pure logic, unit tested; callers pass the wall clock so tests are deterministic.
#pragma once
#include <algorithm>
#include <cmath>
namespace monie
{
class TransportClock
{
public:
    enum class Mode { Host, Internal };
    Mode mode = Mode::Internal;
    int seekCount = 0;                      // incremented on every discontinuity so dependants (ASR, caches) can reset

    // ---- host mode ----
    void hostUpdate (bool valid, bool playing, double hostSeconds, double wall)
    {
        if (! valid) { if (mode == Mode::Host) { pos = now (wall); basePos = pos; baseWall = wall; running = false; mode = Mode::Internal; } return; }
        if (mode != Mode::Host) { mode = Mode::Host; basePos = hostSeconds; baseWall = wall; running = playing; ++seekCount; return; }
        if (playing && running)
        {
            const double predicted = now (wall), err = hostSeconds - predicted;
            if (std::fabs (err) > 0.25) { basePos = hostSeconds; baseWall = wall; ++seekCount; }     // seek / loop jump
            else { basePos = predicted + err * 0.2; baseWall = wall; }                              // slew
        }
        else
        {
            if (std::fabs (hostSeconds - basePos) > 0.01 || playing != running) { if (std::fabs (hostSeconds - now (wall)) > 0.25) ++seekCount; }
            basePos = hostSeconds; baseWall = wall; running = playing;
        }
    }
    // ---- internal mode ----
    void play (double wall) { if (mode == Mode::Internal && ! running) { basePos = pos; baseWall = wall; running = true; } }
    void pause (double wall) { if (mode == Mode::Internal && running) { pos = now (wall); basePos = pos; running = false; } }
    void seek (double t, double wall) { pos = std::max (0.0, t); basePos = pos; baseWall = wall; ++seekCount; }
    void stop (double wall) { pause (wall); seek (0.0, wall); }
    void setLoop (bool on, double a, double b) { loopOn = on && b > a + 0.01; loopA = a; loopB = b; }

    double now (double wall) const
    {
        double t = running ? basePos + (wall - baseWall) : basePos;
        if (mode == Mode::Internal && loopOn && running && t >= loopB) { const double len = loopB - loopA; t = loopA + std::fmod (t - loopA, len); }
        return std::max(0.0, t);
    }
    bool playing() const { return running; }
    // call once per frame in internal mode so a loop wrap becomes the new base (keeps pos continuous)
    void normalise (double wall)
    {
        if (mode == Mode::Internal && loopOn && running) { const double t = now (wall); if (t < basePos + (wall - baseWall) - 1e-9) { basePos = t; baseWall = wall; ++seekCount; } }
    }
private:
    double pos = 0, basePos = 0, baseWall = 0, loopA = 0, loopB = 0; bool running = false, loopOn = false;
};
}
