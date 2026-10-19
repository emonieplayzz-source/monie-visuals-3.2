// The project: everything that is saved with an FL Studio project / standalone project file, except the host-automatable
// global parameters (those live in the plugin's parameter tree). Plain C++17, no JUCE, round-trip tested.
#pragma once
#include <array>
#include <map>
#include <set>
#include "AudioMapping.h"
#include "CaptionEngine.h"
#include "EffectDefs.h"
#include "Gradient.h"
#include "JsonOut.h"
#include "MiniJson.h"
#include "PropSchema.h"
#include "SceneRegistry.h"
#include "TextAnim.h"
#include "Timeline.h"
namespace monie
{
inline const char* aspectNames() { return "16:9|9:16|1:1|4:5|4:3|21:9"; }
inline float aspectValue (int i) { static const float a[] = { 16.f / 9.f, 9.f / 16.f, 1.f, 0.8f, 4.f / 3.f, 21.f / 9.f }; return a[i < 0 ? 0 : i > 5 ? 5 : i]; }
struct Layer
{
    int id = 0; LayerType type = LayerType::Image; std::string name; bool visible = true, locked = false; int group = 0;
    double clipStart = 0, clipLen = -1;             // clipLen < 0 = lasts for the whole timeline
    PropBag props; std::string media;               // media: absolute path (image / video)
    std::vector<AnimInst> anims; std::vector<FxInst> fx; bool fxBypass = false;
    std::map<std::string, KeyframeTrack> keys;      // parameter id -> animation curve
    float f (const char* pid) const { const auto* d = findDef (schemaFor (type), pid); return d ? props.getF (*d) : props.getF (pid, 0.f); }
    std::string s (const char* pid) const { const auto* d = findDef (schemaFor (type), pid); return d ? props.getS (*d) : props.getS (pid, ""); }
    // value of a parameter at layer time t (keyframes override the base value)
    float fAt (const char* pid, double t) const { auto it = keys.find (pid); if (it != keys.end() && ! it->second.empty()) return it->second.evaluate (t); return f (pid); }
};
struct Marker { double t = 0; std::string name; };
struct AsrConfig { bool enabled = false, consent = false; std::string exePath, modelPath, language = "auto"; int chunkSec = 12; int source = 0; /* 0 host mix, 1 sidechain */ };

struct Project
{
    int version = 3; std::string name = "Untitled"; int canvasAspect = 0;     // index into aspectNames()
    int scene = 0; std::map<int, std::array<float, 8>> sceneMem;     // normalised knob values remembered per scene
    std::string sceneImage;                                          // main image for the image-based scenes
    std::vector<std::string> collageImages;                          // extra images for Multi-Layer Collage (up to 3 more)
    std::vector<Layer> layers;                                       // index 0 = bottom
    std::vector<FxInst> fx; bool fxBypass = false;                   // global effect stack
    Gradient gradient = builtinPalettes()[15];   // 'Sunset'
    std::vector<Gradient> userPalettes;
    MappingEngine mapping;
    LyricDoc lyrics, autoLyrics;
    std::vector<Marker> markers; bool loopOn = false; double loopA = 0, loopB = 8, duration = 180;
    AsrConfig asr; bool captionsOn = false;
    std::set<int> favScenes, favFx; std::set<std::string> favFonts, favPalettes; std::vector<std::string> importedFonts;
    int nextId = 1;
    int newId() { return nextId++; }

    Layer* find (int id) { for (auto& l : layers) if (l.id == id) return &l; return nullptr; }
    const Layer* find (int id) const { for (auto& l : layers) if (l.id == id) return &l; return nullptr; }
    int indexOf (int id) const { for (size_t i = 0; i < layers.size(); ++i) if (layers[i].id == id) return (int) i; return -1; }

    int addLayer (LayerType t, const std::string& nm = "")
    {
        Layer l; l.id = newId(); l.type = t; l.name = nm.empty() ? std::string (layerTypeName (t)) + " " + std::to_string (l.id) : nm; layers.push_back (l); return l.id;
    }
    int duplicateLayer (int id)
    {
        int i = indexOf (id); if (i < 0) return -1; Layer c = layers[(size_t) i]; c.id = newId(); c.name += " copy"; for (auto& a : c.anims) a.id = newId(); for (auto& e : c.fx) e.id = newId();
        layers.insert (layers.begin() + i + 1, c); return c.id;
    }
    bool removeLayer (int id) { int i = indexOf (id); if (i < 0) return false; layers.erase (layers.begin() + i); return true; }
    bool moveLayer (int id, int newIndex) { int i = indexOf (id); if (i < 0) return false; newIndex = std::clamp (newIndex, 0, (int) layers.size() - 1); Layer l = layers[(size_t) i]; layers.erase (layers.begin() + i); layers.insert (layers.begin() + newIndex, l); return true; }
    int groupLayers (const std::vector<int>& ids) { int g = newId(); for (int id : ids) if (auto* l = find (id)) l->group = g; return g; }
    void ungroup (int g) { for (auto& l : layers) if (l.group == g) l.group = 0; }
    int addFx (std::vector<FxInst>& stack, int type) { auto f = makeFx (type, newId()); stack.push_back (f); return f.id; }
};

// ---------------------------------------------------------------- serialisation
inline void writeFx (JsonWriter& w, const FxInst& f)
{
    w.beginObj().kv ("id", f.id).kv ("type", f.type).kv ("on", f.enabled).kv ("amt", f.amount).kv ("mix", f.blend).key ("p").beginArr();
    for (float x : f.p) w.val (x);
    w.endArr().kv ("color", f.color).kv ("src", f.audioSrc).kv ("sens", f.audioSens).kv ("name", f.name).endObj();
}
inline void writeAnim (JsonWriter& w, const AnimInst& a)
{
    w.beginObj().kv ("id", a.id).kv ("preset", a.preset).kv ("start", a.start).kv ("dur", a.dur).kv ("speed", a.speed).kv ("int", a.intensity).kv ("dir", a.dir)
        .kv ("ease", a.easing).kv ("src", a.audioSrc).kv ("amt", a.audioAmt).kv ("on", a.enabled).endObj();
}
inline void writeProps (JsonWriter& w, const PropBag& b)
{
    w.beginObj().key ("f").beginObj(); for (auto& kv : b.f) w.kv (kv.first, kv.second); w.endObj().key ("s").beginObj(); for (auto& kv : b.s) w.kv (kv.first, kv.second); w.endObj().endObj();
}
inline void writeLyrics (JsonWriter& w, const LyricDoc& d)
{
    w.beginArr();
    for (auto& l : d.lines)
    {
        w.beginObj().kv ("s", l.start).kv ("e", l.end).kv ("t", l.text).kv ("c", l.conf).kv ("a", l.autoGen).key ("w").beginArr();
        for (auto& x : l.words) w.beginArr().val (x.start).val (x.end).val (x.text).endArr();
        w.endArr().endObj();
    }
    w.endArr();
}
inline std::string projectToJson (const Project& p)
{
    JsonWriter w; w.beginObj().kv ("version", p.version).kv ("name", p.name).kv ("aspect", p.canvasAspect).kv ("scene", p.scene).kv ("sceneImage", p.sceneImage).kv ("nextId", p.nextId);
    w.key ("collage").beginArr(); for (auto& c : p.collageImages) w.val (c); w.endArr();
    w.key ("sceneMem").beginObj(); for (auto& kv : p.sceneMem) { w.key (std::to_string (kv.first)).beginArr(); for (float x : kv.second) w.val (x); w.endArr(); } w.endObj();
    w.key ("layers").beginArr();
    for (auto& l : p.layers)
    {
        w.beginObj().kv ("id", l.id).kv ("type", (int) l.type).kv ("name", l.name).kv ("visible", l.visible).kv ("locked", l.locked).kv ("group", l.group)
            .kv ("clipStart", l.clipStart).kv ("clipLen", l.clipLen).kv ("media", l.media).kv ("fxBypass", l.fxBypass);
        w.key ("props"); writeProps (w, l.props);
        w.key ("anims").beginArr(); for (auto& a : l.anims) writeAnim (w, a); w.endArr();
        w.key ("fx").beginArr(); for (auto& f : l.fx) writeFx (w, f); w.endArr();
        w.key ("keys").beginObj();
        for (auto& kv : l.keys) { w.key (kv.first).beginArr(); for (auto& k : kv.second.all()) w.beginArr().val (k.time).val (k.value).val ((int) k.interp).endArr(); w.endArr(); }
        w.endObj().endObj();
    }
    w.endArr();
    w.key ("fx").beginArr(); for (auto& f : p.fx) writeFx (w, f); w.endArr(); w.kv ("fxBypass", p.fxBypass);
    w.kv ("gradient", p.gradient.toText()); w.key ("userPalettes").beginArr(); for (auto& g : p.userPalettes) w.val (g.toText()); w.endArr();
    w.kv ("mapping", p.mapping.toText());
    w.key ("lyrics"); writeLyrics (w, p.lyrics); w.key ("autoLyrics"); writeLyrics (w, p.autoLyrics);
    w.key ("markers").beginArr(); for (auto& m : p.markers) w.beginArr().val (m.t).val (m.name).endArr(); w.endArr();
    w.kv ("loopOn", p.loopOn).kv ("loopA", p.loopA).kv ("loopB", p.loopB).kv ("duration", p.duration).kv ("captionsOn", p.captionsOn);
    w.key ("asr").beginObj().kv ("enabled", p.asr.enabled).kv ("consent", p.asr.consent).kv ("exe", p.asr.exePath).kv ("model", p.asr.modelPath).kv ("lang", p.asr.language).kv ("chunk", p.asr.chunkSec).kv ("source", p.asr.source).endObj();
    w.key ("favScenes").beginArr(); for (int i : p.favScenes) w.val (i); w.endArr();
    w.key ("favFx").beginArr(); for (int i : p.favFx) w.val (i); w.endArr();
    w.key ("favFonts").beginArr(); for (auto& s : p.favFonts) w.val (s); w.endArr();
    w.key ("favPalettes").beginArr(); for (auto& s : p.favPalettes) w.val (s); w.endArr();
    w.key ("fonts").beginArr(); for (auto& s : p.importedFonts) w.val (s); w.endArr();
    w.endObj(); return w.str();
}

namespace pj
{
inline double N (const JVal& v, double d = 0) { return v.t == JVal::Num ? v.n : d; }
inline FxInst readFx (const JVal& j)
{
    FxInst f; f.id = (int) j.num ("id"); f.type = (int) j.num ("type"); f.enabled = j.get ("on") ? j.get ("on")->b : true; f.amount = (float) j.num ("amt", 1); f.blend = (float) j.num ("mix", 1);
    if (auto* p = j.get ("p")) for (size_t i = 0; i < 4 && i < p->a.size(); ++i) f.p[i] = (float) N (p->a[i]);
    f.color = j.str ("color", "#ffffff"); f.audioSrc = (int) j.num ("src"); f.audioSens = (float) j.num ("sens"); f.name = j.str ("name");
    if (f.type < 0 || f.type >= kNumFx) f.type = 0;
    return f;
}
inline AnimInst readAnim (const JVal& j)
{
    AnimInst a; a.id = (int) j.num ("id"); a.preset = (int) j.num ("preset"); a.start = j.num ("start"); a.dur = j.num ("dur", .6); a.speed = (float) j.num ("speed", 1); a.intensity = (float) j.num ("int", 1);
    a.dir = (int) j.num ("dir"); a.easing = (int) j.num ("ease", 3); a.audioSrc = (int) j.num ("src"); a.audioAmt = (float) j.num ("amt"); a.enabled = j.get ("on") ? j.get ("on")->b : true; return a;
}
inline LyricDoc readLyrics (const JVal* j)
{
    LyricDoc d; if (! j || j->t != JVal::Arr) return d;
    for (auto& l : j->a)
    {
        LyricLine L; L.start = l.num ("s", -1); L.end = l.num ("e", -1); L.text = l.str ("t"); L.conf = (float) l.num ("c", -1); L.autoGen = l.get ("a") && l.get ("a")->b;
        if (auto* w = l.get ("w")) for (auto& x : w->a) if (x.a.size() == 3) { LyricWord W; W.start = N (x.a[0]); W.end = N (x.a[1]); W.text = x.a[2].s; L.words.push_back (W); }
        d.lines.push_back (L);
    }
    return d;
}
}

// Returns false (project untouched) when the text isn't a valid project; unknown fields are ignored, missing fields keep defaults.
inline bool projectFromJson (const std::string& text, Project& out, std::string* error = nullptr)
{
    JVal r; if (! JsonParser::parse (text, r) || r.t != JVal::Obj) { if (error) *error = "not valid JSON"; return false; }
    if (r.num ("version", 0) < 1) { if (error) *error = "missing project version"; return false; }
    Project p; p.name = r.str ("name", "Untitled"); p.canvasAspect = std::clamp ((int) r.num ("aspect"), 0, 5); p.scene = std::clamp ((int) r.num ("scene"), 0, kNumScenes - 1); p.sceneImage = r.str ("sceneImage"); p.nextId = std::max (1, (int) r.num ("nextId", 1));
    if (auto* c = r.get ("collage")) for (auto& x : c->a) p.collageImages.push_back (x.s);
    if (auto* m = r.get ("sceneMem")) for (auto& kv : m->o) { std::array<float, 8> a {}; for (size_t i = 0; i < 8 && i < kv.second.a.size(); ++i) a[i] = (float) pj::N (kv.second.a[i]); int idx = std::atoi (kv.first.c_str()); if (idx >= 0 && idx < kNumScenes) p.sceneMem[idx] = a; }
    if (auto* ls = r.get ("layers")) for (auto& j : ls->a)
    {
        Layer l; l.id = (int) j.num ("id"); l.type = (LayerType) std::clamp ((int) j.num ("type"), 0, 3); l.name = j.str ("name"); l.visible = ! j.get ("visible") || j.get ("visible")->b; l.locked = j.get ("locked") && j.get ("locked")->b;
        l.group = (int) j.num ("group"); l.clipStart = j.num ("clipStart"); l.clipLen = j.num ("clipLen", -1); l.media = j.str ("media"); l.fxBypass = j.get ("fxBypass") && j.get ("fxBypass")->b;
        if (auto* pr = j.get ("props")) { if (auto* f = pr->get ("f")) for (auto& kv : f->o) l.props.f[kv.first] = (float) pj::N (kv.second); if (auto* s = pr->get ("s")) for (auto& kv : s->o) l.props.s[kv.first] = kv.second.s; }
        if (auto* a = j.get ("anims")) for (auto& x : a->a) l.anims.push_back (pj::readAnim (x));
        if (auto* f = j.get ("fx")) for (auto& x : f->a) l.fx.push_back (pj::readFx (x));
        if (auto* k = j.get ("keys")) for (auto& kv : k->o)
        { std::vector<Keyframe> ks; for (auto& x : kv.second.a) if (x.a.size() == 3) ks.push_back ({ pj::N (x.a[0]), (float) pj::N (x.a[1]), (Interp) std::clamp ((int) pj::N (x.a[2]), 0, 5) }); KeyframeTrack t; t.assign (ks); l.keys.emplace (kv.first, t); }
        p.layers.push_back (l); p.nextId = std::max (p.nextId, l.id + 1);
    }
    if (auto* f = r.get ("fx")) for (auto& x : f->a) p.fx.push_back (pj::readFx (x));
    p.fxBypass = r.get ("fxBypass") && r.get ("fxBypass")->b;
    { Gradient g; if (Gradient::fromText (r.str ("gradient"), g)) p.gradient = g; }
    if (auto* u = r.get ("userPalettes")) for (auto& x : u->a) { Gradient g; if (Gradient::fromText (x.s, g)) p.userPalettes.push_back (g); }
    p.mapping = MappingEngine::fromText (r.str ("mapping"));
    p.lyrics = pj::readLyrics (r.get ("lyrics")); p.autoLyrics = pj::readLyrics (r.get ("autoLyrics"));
    if (auto* m = r.get ("markers")) for (auto& x : m->a) if (x.a.size() == 2) p.markers.push_back ({ pj::N (x.a[0]), x.a[1].s });
    p.loopOn = r.get ("loopOn") && r.get ("loopOn")->b; p.loopA = r.num ("loopA"); p.loopB = r.num ("loopB", 8); p.duration = r.num ("duration", 180); p.captionsOn = r.get ("captionsOn") && r.get ("captionsOn")->b;
    if (auto* a = r.get ("asr")) { p.asr.enabled = a->get ("enabled") && a->get ("enabled")->b; p.asr.consent = a->get ("consent") && a->get ("consent")->b; p.asr.exePath = a->str ("exe"); p.asr.modelPath = a->str ("model"); p.asr.language = a->str ("lang", "auto"); p.asr.chunkSec = std::clamp ((int) a->num ("chunk", 12), 4, 60); p.asr.source = (int) a->num ("source"); }
    if (auto* f = r.get ("favScenes")) for (auto& x : f->a) p.favScenes.insert ((int) pj::N (x));
    if (auto* f = r.get ("favFx")) for (auto& x : f->a) p.favFx.insert ((int) pj::N (x));
    if (auto* f = r.get ("favFonts")) for (auto& x : f->a) p.favFonts.insert (x.s);
    if (auto* f = r.get ("favPalettes")) for (auto& x : f->a) p.favPalettes.insert (x.s);
    if (auto* f = r.get ("fonts")) for (auto& x : f->a) p.importedFonts.push_back (x.s);
    out = std::move (p); return true;
}

inline float canvasAspectOf (const Project& p) { return aspectValue (p.canvasAspect); }

inline Project defaultProject()
{
    Project p; p.name = "Untitled"; p.scene = 0; p.gradient = builtinPalettes()[15];
    int t = p.addLayer (LayerType::Text, "Title"); auto* l = p.find (t);
    l->props.s["text"] = "MONIE"; l->props.f["y"] = 0.f;
    // loop animations only: an entrance animation would make the title invisible while the timeline sits at 0:00
    AnimInst a; a.id = p.newId(); a.preset = 20; a.intensity = .5f; l->anims.push_back (a);          // Float
    AnimInst b; b.id = p.newId(); b.preset = 21; b.intensity = .6f; l->anims.push_back (b);          // Beat Pulse
    return p;
}
}
