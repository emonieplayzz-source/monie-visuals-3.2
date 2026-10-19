// Unit tests for the v2 logic engines. Build: g++ -std=c++17 -I Source/Core tests/test_engines.cpp -o engines_test
#include "Timeline.h"
#include "Lyrics.h"
#include "AudioMapping.h"
#include "SceneRegistry.h"
#include "Randomizer.h"
#include <cstdio>
#include <set>
#include <string>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
static bool near (double a, double b, double e = 1e-3) { return std::fabs (a - b) < e; }
using namespace monie;

int main()
{
    // ---- easing / keyframes ----
    for (auto m : { Interp::Linear, Interp::Smooth, Interp::EaseIn, Interp::EaseOut, Interp::EaseInOut })
        CHECK (near (ease (m, 0), 0) && near (ease (m, 1), 1), "ease endpoints mode %d", (int) m);
    CHECK (near (ease (Interp::EaseIn, .5f), .25) && near (ease (Interp::EaseOut, .5f), .75), "ease-in/out midpoints");
    KeyframeTrack k (5.f);
    CHECK (near (k.evaluate (1.0), 5.0), "empty track returns default");
    k.add (2.0, 10.f, Interp::Linear); k.add (0.0, 0.f, Interp::Linear); k.add (4.0, 0.f, Interp::Step); k.add (6.0, 8.f);
    CHECK (near (k.evaluate (1.0), 5.0) && near (k.evaluate (-3), 0) && near (k.evaluate (9), 8), "linear interp + clamp outside");
    CHECK (near (k.evaluate (3.0), 5.0) && near (k.evaluate (5.9), 0.0), "step holds left value (got %.2f)", k.evaluate (5.9));
    k.add (2.0, 20.f, Interp::Linear);
    CHECK (k.all().size() == 4 && near (k.evaluate (2.0), 20.0), "adding at same time replaces key");
    auto clip = k.copyRange (0.0, 2.0); KeyframeTrack k2; k2.paste (clip, 10.0);
    CHECK (clip.size() == 2 && near (k2.evaluate (11.0), 10.0), "copy/paste keyframes (mid=%.2f)", k2.evaluate (11.0));

    // ---- clips ----
    ClipList cl; int a = cl.add (1.0, 4.0, "vid");
    int b = cl.split (a, 3.0);
    CHECK (b > 0 && near (cl.find (a)->length, 2.0) && near (cl.find (b)->start, 3.0) && near (cl.find (b)->inPoint, 2.0), "split keeps source in-point (right inPoint=%.1f)", cl.find (b)->inPoint);
    CHECK (cl.split (a, 1.0) == -1 && cl.split (a, 99) == -1, "split outside clip rejected");
    cl.trimStart (b, 3.5); CHECK (near (cl.find (b)->inPoint, 2.5) && near (cl.find (b)->length, 1.5), "trimStart advances in-point");
    cl.trimEnd (b, 4.0); CHECK (near (cl.find (b)->length, 0.5), "trimEnd");
    int d = cl.duplicate (a); CHECK (near (cl.find (d)->start, 3.0), "duplicate placed after original");
    cl.move (a, -5); CHECK (near (cl.find (a)->start, 0), "move clamps at 0");
    CHECK (near (ClipList::snap (3.04, { 1.0, 3.0, 5.0 }, 0.1), 3.0) && near (ClipList::snap (3.5, { 3.0 }, 0.1), 3.5), "snapping");

    // ---- undo/redo ----
    int value = 0; UndoStack u;
    u.execute ({ "set 5", [&] { value = 5; }, [&] { value = 0; } }); u.execute ({ "set 9", [&] { value = 9; }, [&] { value = 5; } });
    u.undo(); CHECK (value == 5 && u.canRedo(), "undo");
    u.undo(); u.redo(); CHECK (value == 5, "redo");
    u.execute ({ "set 1", [&] { value = 1; }, [&] { value = 5; } }); CHECK (! u.canRedo() && ! u.redo(), "new edit clears redo");

    // ---- lyrics ----
    const std::string lrc = "[ti:Placeholder]\n[offset:500]\n[00:10.00]First <00:10.00>one <00:11.00>two\n[00:12.50]Second line\n[00:20.00][00:30.00]Repeat me\n[00:35.00]\n";
    auto d1 = parseLRC (lrc);
    CHECK (d1.lines.size() == 5, "LRC: %zu lines (multi-timestamp expands)", d1.lines.size());
    CHECK (near (d1.lines[0].start, 9.5) && near (d1.lines[0].end, 12.0), "LRC offset (+500ms = earlier): start=%.2f end=%.2f", d1.lines[0].start, d1.lines[0].end);
    CHECK (d1.lines[0].words.size() == 2 && near (d1.lines[0].words[1].start, 10.5) && near (d1.lines[0].words[1].end, 12.0), "LRC enhanced word timing");
    CHECK (activeLine (d1, 13.0) == 1 && activeLine (d1, 5.0) == -1 && activeLine (d1, 9.0, 1.0) == 0, "active line + lead-in");
    CHECK (activeWord (d1.lines[0], 11.0) == 1 && activeWord (d1.lines[0], 10.0) == 0, "active word (real timestamps)");
    auto est = wordsOf (d1.lines[1]); CHECK (est.size() == 2 && near (est[0].start, 12.0) && est[1].end > est[0].end, "estimated word timing for untimed words");
    auto v = viewAt (d1, 13.0, 1, 2); CHECK (v.current == 1 && v.previous.size() == 1 && v.previous[0] == 0 && v.upcoming.size() == 2, "prev/next view");
    auto v2 = viewAt (d1, 1.0, 1, 1); CHECK (v2.current == -1 && v2.upcoming.size() == 1 && v2.upcoming[0] == 0, "view before first line");
    const std::string srt = "1\r\n00:00:01,000 --> 00:00:03,500\r\nHello <i>there</i>\r\nsecond row\r\n\r\n2\r\n00:00:04,000 --> 00:00:05,000\r\nBye\r\n";
    auto d2 = parseSRT (srt);
    CHECK (d2.lines.size() == 2 && near (d2.lines[0].end, 3.5) && d2.lines[0].text == "Hello there\nsecond row", "SRT parse (CRLF, tags stripped, multi-line)");
    auto d3 = parseSRT (toSRT (d2)); CHECK (d3.lines.size() == 2 && near (d3.lines[1].start, 4.0) && d3.lines[1].text == "Bye", "SRT round trip");
    auto d4 = parseLRC (toLRC (d1)); CHECK (d4.lines.size() == d1.lines.size() && near (d4.lines[0].words[1].start, d1.lines[0].words[1].start, 0.011), "LRC round trip incl. words");
    auto pl = parsePlain ("line one\n\nline two is longer\n"); distributeEvenly (pl, 0, 10);
    CHECK (pl.lines.size() == 2 && near (pl.lines[0].start, 0) && near (pl.lines[1].end, 10) && pl.lines[1].start > 3, "plain text + even distribution");
    shift (pl, 2.0); CHECK (near (pl.lines[0].start, 2.0), "manual offset shift");
    CHECK (parseLRC ("garbage\n[xx:yy]no\n").lines.empty() && parseSRT ("nonsense").lines.empty(), "malformed input yields empty doc, no crash");

    // ---- mapping engine ----
    { MappingEngine e; Mapping m; m.source = Source::Bass; m.target = "zoom"; m.outMin = 1.f; m.outMax = 3.f; m.attackMs = 0; m.releaseMs = 0;
      e.add (m); Features f; f.v[(int) Source::Bass] = 0.5f;
      CHECK (near (e.process (f, 0.016f)["zoom"], 2.0), "basic mapping 0.5 -> 2.0");
      f.v[(int) Source::Bass] = 5.f; CHECK (near (e.process (f, 0.016f)["zoom"], 3.0), "output clamped to range"); }
    { MappingEngine e; Mapping m; m.target = "x"; m.threshold = 0.5f; m.attackMs = 0; m.releaseMs = 0; m.invert = false; e.add (m);
      Features f; f.v[0] = .4f; float lo = e.process (f, .016f)["x"]; f.v[0] = .75f; float hi = e.process (f, .016f)["x"];
      CHECK (near (lo, 0) && near (hi, 0.5), "threshold %.2f / %.2f", lo, hi); }
    { MappingEngine e; Mapping m; m.target = "x"; m.invert = true; m.attackMs = 0; m.releaseMs = 0; e.add (m); Features f; f.v[0] = .25f; CHECK (near (e.process (f, .016f)["x"], .75), "invert"); }
    { MappingEngine e; Mapping m; m.target = "x"; m.attackMs = 100; m.releaseMs = 1000; e.add (m); Features f; f.v[0] = 1.f;
      float a1 = e.process (f, 0.05f)["x"]; f.v[0] = 0.f; float r1 = e.process (f, 0.05f)["x"];
      CHECK (a1 > .35f && a1 < .45f && r1 > .8f * a1 && r1 < a1, "attack fast / release slow (a=%.2f then %.2f)", a1, r1); }
    { MappingEngine e; Mapping m; m.target = "x"; m.quantizeToBeat = true; m.attackMs = 0; m.releaseMs = 0; e.add (m); Features f; f.v[0] = .8f;
      float before = e.process (f, .016f)["x"]; f.beatNow = true; float onBeat = e.process (f, .016f)["x"]; f.beatNow = false; f.v[0] = .1f; float after = e.process (f, .016f)["x"];
      CHECK (near (before, 0) && near (onBeat, .8) && near (after, .8), "beat quantization holds value between beats"); }
    { MappingEngine e1, e2; Mapping m; m.target = "x"; m.randomAmount = .3f; m.seed = 7; m.attackMs = 0; m.releaseMs = 0; e1.add (m); e2.add (m); Features f; bool same = true, moved = false; float first = 0;
      for (int i = 0; i < 60; ++i) { float a = e1.process (f, .02f)["x"], b = e2.process (f, .02f)["x"]; if (std::fabs (a - b) > 1e-6) same = false; if (i == 0) first = a; if (std::fabs (a - first) > 1e-3) moved = true; }
      CHECK (same && moved, "random modulation is seeded/reproducible and actually moves"); }
    { MappingEngine e; Mapping a; a.target = "g"; a.outMax = .3f; a.attackMs = 0; a.releaseMs = 0; Mapping b = a; b.source = Source::Treble; e.add (a); e.add (b);
      Features f; f.v[(int) Source::Bass] = 1; f.v[(int) Source::Treble] = 1; CHECK (near (e.process (f, .016f)["g"], .6), "several sources summed on one target"); }
    { MappingEngine e; Mapping m; m.source = Source::Flux; m.target = "glow"; m.sensitivity = 1.5f; m.curve = Curve::SCurve; m.invert = true; m.quantizeToBeat = true; m.seed = 99; m.randomAmount = .1f; e.add (m);
      auto e2 = MappingEngine::fromText (e.toText());
      CHECK (e2.maps.size() == 1 && e2.maps[0].source == Source::Flux && e2.maps[0].target == "glow" && e2.maps[0].curve == Curve::SCurve && e2.maps[0].invert && e2.maps[0].quantizeToBeat && e2.maps[0].seed == 99 && near (e2.maps[0].sensitivity, 1.5), "mapping preset text round trip");
      CHECK (MappingEngine::fromText ("junk\n1|2|3\n").maps.empty(), "malformed mapping preset ignored"); }

    // ---- randomizer / registry ----
    { std::vector<RParam> p = { { "a", 0, 1, .5f, false }, { "b", 0, 10, 5.f, true }, { "c", 0, 1, .5f, false } }, q = p, r = p;
      vary (p, 42, .2f); vary (q, 42, .2f); r[1].locked = false; vary (r, 42, .2f);
      CHECK (p[0].value == q[0].value && p[2].value == q[2].value, "variation reproducible for same seed");
      CHECK (p[1].value == 5.f && near (p[0].value, r[0].value) && near (p[2].value, r[2].value), "locked param untouched and doesn't shift others");
      std::vector<RParam> w = { { "a", 2, 3, 2.5f, false } }; randomize (w, 1); CHECK (w[0].value >= 2 && w[0].value <= 3, "randomize stays in range"); }
    { CHECK (sceneCatalogue().size() == 30 && (int) sceneCatalogue().size() == kNumScenes && sceneCategories().size() == 6, "catalogue: %zu scenes, %zu categories", sceneCatalogue().size(), sceneCategories().size());
      std::set<std::string> names; bool knobsOk = true; for (auto& sc : sceneCatalogue()) { names.insert (sc.name); if (sc.knobs.size() != 8) knobsOk = false; for (auto& kb : sc.knobs) if (kb.maxV <= kb.minV || kb.defV < kb.minV || kb.defV > kb.maxV) knobsOk = false; }
      CHECK (names.size() == 30 && knobsOk, "30 unique names; every scene has 8 valid knobs");
      CHECK (! searchScenes ("liquid").empty() && searchScenes ("", "Music-Reactive").size() == 4 && searchScenes ("zzzz").empty(), "search / category filter / no-match");
      std::set<int> fav { 3, 6 }; CHECK (searchScenes ("", "", &fav).size() == 2 && favoritesFromText (favoritesToText (fav)) == fav && favoritesFromText ("1,99,x").size() == 1, "favorites filter + text round trip"); }

    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
