// v3 logic tests: PropSchema, EffectDefs, CaptionEngine, MiniJson, whisper JSON parser.
#include "PropSchema.h"
#include "EffectDefs.h"
#include "CaptionEngine.h"
#include <cstdio>
#include <set>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
using namespace monie;
int main()
{
    // schemas
    for (auto lt : { LayerType::Image, LayerType::Video, LayerType::Text, LayerType::Caption })
    {
        auto& sch = schemaFor (lt); std::set<std::string> ids; bool ok = ! sch.empty();
        for (auto& d : sch) { if (! ids.insert (d.id).second) ok = false; if (d.kind == PKind::Slider && (d.maxV <= d.minV || d.defV < d.minV || d.defV > d.maxV)) ok = false; }
        CHECK (ok, "schema %s: %zu unique valid params", layerTypeName (lt), sch.size());
    }
    // effects
    CHECK (fxCatalogue().size() == 30 && kNumFx == 30, "30 effects");
    { std::set<std::string> n; for (auto& f : fxCatalogue()) n.insert (f.name); CHECK (n.size() == 30, "effect names unique"); }
    // caption display
    LyricDoc d = parseLRC ("[00:01.00]hello brave new world\n[00:05.00]second line here\n[00:09.00]third");
    CaptionSettings cs; cs.fadeIn = cs.fadeOut = 0; cs.hold = 0; cs.leadIn = 0;
    CHECK (! captionAt (d, 0.2, cs).hasContent, "nothing before first line");
    CHECK (captionAt (d, 1.5, cs).text == "hello brave new world", "line mode text '%s'", captionAt (d, 1.5, cs).text.c_str());
    CHECK (captionAt (d, 5.5, cs).lineIndex == 1, "second line active");
    cs.delay = 1.0; CHECK (captionAt (d, 2.5, cs).lineIndex == 0 && ! captionAt (d, 1.5, cs).hasContent, "delay shifts captions later"); cs.delay = 0;
    cs.mode = 1; { auto f = captionAt (d, 1.5, cs); CHECK (! f.text.empty() && f.text.find (' ') == std::string::npos, "word mode shows a single word ('%s')", f.text.c_str()); }
    cs.mode = 2; cs.groupWords = 2; { auto f = captionAt (d, 1.5, cs); CHECK (f.text == "hello brave", "group mode '%s'", f.text.c_str()); }
    cs.mode = 0; cs.maxChars = 11; { auto f = captionAt (d, 1.5, cs); CHECK (f.text.find ('\n') != std::string::npos, "wrapping at maxChars"); }
    cs.maxChars = 32;
    // seek robustness: same time => same output regardless of history
    { auto a = captionAt (d, 5.5, cs); captionAt (d, 9.5, cs); captionAt (d, 0.0, cs); auto b = captionAt (d, 5.5, cs); CHECK (a.text == b.text && a.alpha == b.alpha, "pure function of clock (seek-safe)"); }
    // fades
    { CaptionSettings f2; f2.fadeIn = 1.0; f2.leadIn = 0; f2.fadeOut = 0; auto f = captionAt (d, 1.5, f2); CHECK (f.alpha > .45f && f.alpha < .55f, "fade-in alpha %.2f", f.alpha); }
    // confidence filter
    { LyricDoc a; LyricLine l; l.start = 0; l.end = 2; l.text = "mumble"; l.autoGen = true; l.conf = 0.1f; a.lines.push_back (l); CaptionSettings c2; c2.minConf = .5f; CHECK (! captionAt (a, 1.0, c2).hasContent, "low-confidence auto line hidden");
      c2.minConf = .05f; CHECK (captionAt (a, 1.0, c2).hasContent, "...shown when threshold lowered"); a.lines[0].autoGen = false; c2.minConf = .9f; CHECK (captionAt (a, 1.0, c2).hasContent, "manual line never confidence filtered"); }
    // merge
    { LyricDoc m = parseLRC ("[00:01.00]manual\n[00:03.00]end"); LyricDoc a; LyricLine l; l.start = 1.2; l.end = 2; l.text = "auto overlap"; l.autoGen = true; l.conf = .9f; a.lines.push_back (l);
      l.start = 10; l.end = 11; l.text = "auto later"; a.lines.push_back (l); auto mg = mergeDocs (m, a); CHECK (mg.lines.size() == 3, "merge keeps manual, drops overlapping auto (%zu lines)", mg.lines.size()); }
    // JSON
    { JVal v; CHECK (JsonParser::parse (R"({"a":[1,2.5,{"b":"x\né😀"}],"t":true,"n":null})", v) && v.get ("a")->a.size() == 3 && v.get ("a")->a[2].str ("b") == "x\n\xc3\xa9\xf0\x9f\x98\x80", "json parse with escapes/surrogates");
      CHECK (! JsonParser::parse ("{\"a\":", v) && ! JsonParser::parse ("[1,2", v) && ! JsonParser::parse ("{} x", v), "json rejects malformed"); }
    // whisper json
    { std::string js = R"({"transcription":[
      {"timestamps":{"from":"00:00:01,000","to":"00:00:03,000"},"offsets":{"from":1000,"to":3000},"text":" Hello world","tokens":[
        {"text":"[_BEG_]","offsets":{"from":1000,"to":1000},"id":50363,"p":0.9},
        {"text":" Hello","offsets":{"from":1000,"to":1800},"id":1,"p":0.9},
        {"text":" wor","offsets":{"from":1900,"to":2300},"id":2,"p":0.7},
        {"text":"ld","offsets":{"from":2300,"to":3000},"id":3,"p":0.5}]},
      {"offsets":{"from":4000,"to":5000},"text":" [MUSIC]"},
      {"offsets":{"from":6000,"to":7000},"text":" no tokens line"}]})";
      auto r = parseWhisperJson (js, 10.0);
      CHECK (r.ok && r.doc.lines.size() == 2 && r.dropped == 1, "whisper: 2 lines, 1 non-speech dropped (%zu, %d)", r.doc.lines.size(), r.dropped);
      auto& L = r.doc.lines[0]; CHECK (L.text == "Hello world" && L.words.size() == 2 && std::fabs (L.start - 11.0) < 1e-6 && std::fabs (L.words[1].start - 11.9) < 1e-6 && std::fabs (L.words[1].end - 13.0) < 1e-6, "tokens merged into words, chunk offset applied ('%s')", L.text.c_str());
      CHECK (std::fabs (L.conf - 0.7) < 0.01 && L.autoGen, "confidence = mean token p (%.2f)", L.conf);
      CHECK (! parseWhisperJson ("garbage", 0).ok && ! parseWhisperJson ("{}", 0).ok, "whisper parser reports errors"); }
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
