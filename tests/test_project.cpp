#include "ProjectModel.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: "); } else std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
using namespace monie;
int main()
{
    Project p = defaultProject();
    CHECK (p.layers.size() == 1 && p.layers[0].anims.size() == 2, "default project has a title text layer with 2 animations");
    int img = p.addLayer (LayerType::Image, "Photo \"A\"\n"); p.find (img)->media = "C:\\Users\\me\\a b.png"; p.find (img)->props.f["opacity"] = .5f;
    p.find (img)->fx.push_back (makeFx (6, p.newId())); p.find (img)->keys["x"].add (1.0, .2f, Interp::EaseOut); p.find (img)->keys["x"].add (3.0, -.4f);
    int cap = p.addLayer (LayerType::Caption); p.addFx (p.fx, 14); p.scene = 17; p.sceneMem[17] = { .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f };
    p.lyrics = parseLRC ("[00:01.00]hello \"world\"\n[00:04.00]line two\\ok"); LyricLine al; al.start = 6; al.end = 8; al.text = "auto é 😀"; al.conf = .8f; al.autoGen = true; al.words.push_back ({ 6, 7, "auto" }); p.autoLyrics.lines.push_back (al);
    p.markers.push_back ({ 12.5, "drop" }); p.loopOn = true; p.loopA = 4; p.loopB = 12; p.asr.exePath = "C:\\tools\\whisper-cli.exe"; p.asr.consent = true; p.favScenes = { 3, 9 }; p.favFonts = { "Arial" };
    p.gradient = builtinPalettes()[3]; p.userPalettes.push_back (builtinPalettes()[5]); MappingEngine me; Mapping m; m.target = "layer:1:scale"; me.add (m); p.mapping = me;
    std::string j1 = projectToJson (p); Project q; std::string err;
    CHECK (projectFromJson (j1, q, &err), "parse own output (%s)", err.c_str());
    std::string j2 = projectToJson (q); CHECK (j1 == j2, "serialise -> parse -> serialise is identical (%zu bytes)", j1.size());
    CHECK (q.find (img) && q.find (img)->media == "C:\\Users\\me\\a b.png" && q.find (img)->name == "Photo \"A\"\n", "paths/escapes survive");
    CHECK (q.find (img)->fAt ("x", 2.0) > -.4f && q.find (img)->fAt ("x", 2.0) < .2f && q.find (img)->f ("opacity") == .5f, "keyframes + props survive (x@2s=%.3f)", q.find (img)->fAt ("x", 2.0));
    CHECK (q.autoLyrics.lines.size() == 1 && q.autoLyrics.lines[0].text == "auto \xc3\xa9 \xf0\x9f\x98\x80" && q.autoLyrics.lines[0].autoGen, "unicode lyrics survive");
    CHECK (! projectFromJson ("{\"name\":1}", q) && ! projectFromJson ("garbage", q) && ! projectFromJson ("", q), "invalid project files are rejected");
    { Project keep = q; Project junk; std::string e; bool ok = projectFromJson ("{\"version\":3,\"layers\":[{\"id\":5,\"type\":99,\"fx\":[{\"type\":999}]}],\"scene\":999,\"future\":{\"x\":1}}", junk, &e); CHECK (ok && junk.scene == 29 && junk.layers[0].type == LayerType::Caption && junk.layers[0].fx[0].type == 0, "out-of-range values are clamped, unknown fields ignored"); }
    // editing operations
    int c2 = q.duplicateLayer (img); CHECK (c2 > 0 && q.indexOf (c2) == q.indexOf (img) + 1 && q.find (c2)->name.find ("copy") != std::string::npos && q.find (c2)->fx[0].id != q.find (img)->fx[0].id, "duplicate layer gets fresh ids");
    int first = q.layers.front().id; q.moveLayer (first, (int) q.layers.size() - 1); CHECK (q.layers.back().id == first, "reorder layer to top");
    int g = q.groupLayers ({ img, c2 }); CHECK (q.find (img)->group == g && q.find (c2)->group == g, "group"); q.ungroup (g); CHECK (q.find (img)->group == 0, "ungroup");
    CHECK (q.removeLayer (c2) && ! q.find (c2) && ! q.removeLayer (c2), "delete layer");
    CHECK (q.find (cap) && q.find (cap)->f ("size") > 0 && q.find (cap)->s ("text") == "", "schema defaults resolve for unset props");
    std::printf (failures ? "\n%d FAILURES\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
