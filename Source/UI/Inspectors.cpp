#include "Inspectors.h"
#include <random>

using namespace monie;
namespace ui
{
int gSelectedStop = 0;
static std::function<void (int, juce::Colour)> gThemeHook;
std::function<void (int, juce::Colour)> themeChangedHook() { return gThemeHook; }
void setThemeHook (std::function<void (int, juce::Colour)> f) { gThemeHook = std::move (f); }

juce::File userDataDir() { auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MonieVisuals"); d.createDirectory(); return d; }
juce::File presetsDir() { auto d = userDataDir().getChildFile ("Presets"); d.createDirectory(); return d; }
juce::PropertiesFile& userSettings()
{
    static std::unique_ptr<juce::PropertiesFile> pf;
    if (! pf) { juce::PropertiesFile::Options o; o.applicationName = "MonieVisuals"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support"; o.folderName = userDataDir().getFullPathName(); o.storageFormat = juce::PropertiesFile::storeAsXML; pf = std::make_unique<juce::PropertiesFile> (o); }
    return *pf;
}
void setParamPlain (MonieProcessor& p, const juce::String& id, float v) { if (auto* par = p.apvts.getParameter (id)) par->setValueNotifyingHost (par->convertTo0to1 (v)); }
void paramGesture (MonieProcessor& p, const juce::String& id, bool begin)
{ if (auto* par = p.apvts.getParameter (id)) { if (begin) par->beginChangeGesture(); else par->endChangeGesture(); } }

void chooseFile (const juce::String& title, const juce::File& start, const juce::String& wildcard, bool save, std::function<void (juce::File)> cb)
{
    auto fc = std::make_shared<juce::FileChooser> (title, start, wildcard);
    const int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting) : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    fc->launchAsync (flags, [fc, cb] (const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) cb (f); });
}
void chooseFolder (const juce::String& title, const juce::File& start, std::function<void (juce::File)> cb)
{
    auto fc = std::make_shared<juce::FileChooser> (title, start);
    fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [fc, cb] (const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) cb (f); });
}

// ============================================================================================ layer properties
static void setLayerProp (UiCtx* c, int id, const PDef* d, float v)
{
    const double t = [&] { auto* l = c->proc.store.get().find (id); return l ? c->layerTime (*l) : 0.0; }();
    c->proc.store.edit (juce::String ("Edit ") + d->label, [=] (Project& p)
    {
        auto* l = p.find (id); if (! l) return; const float cv = PropBag::clampTo (*d, v);
        auto it = l->keys.find (d->id);
        if (it != l->keys.end() && ! it->second.empty()) it->second.add (t, cv); else l->props.setF (*d, cv);
    }, "L" + juce::String (id) + d->id);
}
static void toggleKey (UiCtx* c, int id, const PDef* d)
{
    const double t = [&] { auto* l = c->proc.store.get().find (id); return l ? c->layerTime (*l) : 0.0; }();
    c->proc.store.edit (juce::String ("Keyframe ") + d->label, [=] (Project& p)
    {
        auto* l = p.find (id); if (! l) return;
        auto& tr = l->keys[d->id]; const float cur = l->fAt (d->id.c_str(), t);
        double hit = -1; for (auto& k : tr.all()) if (std::fabs (k.time - t) < 0.06) hit = k.time;
        if (hit >= 0) { tr.removeAt (hit); if (tr.empty()) { l->props.f[d->id] = cur; l->keys.erase (d->id); } }
        else { tr.add (t, cur, Interp::Smooth); }
    });
    if (c->relayout) c->relayout();
}

void buildLayerProps (PropPanel& pp, UiCtx& c)
{
    const Layer* L = c.project().find (c.selectedLayer);
    if (! L) { pp.section ("Layer"); pp.info ("Select a layer in the layer panel or click it in the preview.");  return; }
    const int id = L->id; UiCtx* cp = &c; const auto type = L->type; MonieProcessor* proc = &c.proc;
    auto layerGet = [cp, id] () -> const Layer* { return cp->proc.store.get().find (id); };

    if (c.match ("Layer name visible locked clip", "Layer"))
    {
        pp.section ("Layer", false, "L:layer");
        pp.text ("Name", "Rename the layer.", [=] { auto* l = layerGet(); return l ? juce::String (l->name) : juce::String(); }, [=] (juce::String s) { proc->store.edit ("Rename layer", [=] (Project& p) { if (auto* l = p.find (id)) l->name = s.toStdString(); }, "name" + juce::String (id)); });
        pp.toggle ("Visible", "Show or hide the layer.", [=] { auto* l = layerGet(); return l && l->visible; }, [=] (bool v) { proc->store.edit ("Layer visibility", [=] (Project& p) { if (auto* l = p.find (id)) l->visible = v; }); });
        pp.toggle ("Locked", "Locked layers cannot be dragged in the preview.", [=] { auto* l = layerGet(); return l && l->locked; }, [=] (bool v) { proc->store.edit ("Lock layer", [=] (Project& p) { if (auto* l = p.find (id)) l->locked = v; }); });
        const double dur = juce::jmax (10.0, c.project().duration);
        pp.slider ("Clip start (s)", "When the layer first appears on the timeline.", 0, dur, 0, 0.01, [=] { auto* l = layerGet(); return l ? l->clipStart : 0.0; }, [=] (double v) { proc->store.edit ("Clip start", [=] (Project& p) { if (auto* l = p.find (id)) l->clipStart = v; }, "cs" + juce::String (id)); });
        pp.slider ("Clip length (s)", "0 = the layer lasts for the whole timeline.", 0, dur, 0, 0.01, [=] { auto* l = layerGet(); return l && l->clipLen > 0 ? l->clipLen : 0.0; }, [=] (double v) { proc->store.edit ("Clip length", [=] (Project& p) { if (auto* l = p.find (id)) l->clipLen = v < 0.02 ? -1.0 : v; }, "cl" + juce::String (id)); });
    }
    if (type == LayerType::Image || type == LayerType::Video)
    {
        pp.section ("Media file", false, "L:media");
        pp.text ("File", "Path of the image or video.", [=] { auto* l = layerGet(); return l ? juce::String (l->media) : juce::String(); }, [=] (juce::String s) { proc->store.edit ("Set media", [=] (Project& p) { if (auto* l = p.find (id)) l->media = s.toStdString(); }, "media" + juce::String (id)); proc->media.request (s.toStdString()); });
        auto choose = [=] { chooseFile (type == LayerType::Video ? "Choose a video" : "Choose an image", juce::File(), type == LayerType::Video ? "*.mp4;*.mov;*.mkv;*.webm;*.avi;*.m4v;*.gif" : "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.webp",
                                  false, [=] (juce::File f) { auto why = MediaPool::unsupportedReason (f); if (why.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot use this file", why); return; }
                                                               proc->media.request (f.getFullPathName().toStdString()); proc->store.edit ("Set media", [=] (Project& p) { if (auto* l = p.find (id)) { l->media = f.getFullPathName().toStdString(); } }); if (cp->rebuild) cp->rebuild(); }); };
        pp.buttons ({ { "Choose file...", choose }, { "Relink...", choose } }, "Pick a replacement if the original file moved.");
        if (auto* l = layerGet(); l && ! l->media.empty())
        {
            auto info = proc->media.info (l->media);
            juce::String st = info.missing ? "MISSING - the file was not found. Use Relink." : info.loading ? "Loading..." : info.error.isNotEmpty() ? "Error: " + info.error : juce::String (info.w) + " x " + juce::String (info.h) + (info.isVideo ? "  video  " + juce::String (info.duration, 1) + " s" : "  image");
            pp.info (st);
        }
        else pp.info ("No file chosen yet.");
    }

    // ---- schema-driven parameters ----
    const auto& schema = schemaFor (type);
    std::vector<std::string> order; for (auto& d : schema) if (std::find (order.begin(), order.end(), d.section) == order.end()) order.push_back (d.section);
    int secIdx = 0;
    for (auto& sec : order)
    {
        const bool open = secIdx++ < 3;
        for (int pass = 0; pass < 2; ++pass)
        {
            bool any = false;
            for (auto& d : schema) if (d.section == sec && d.advanced == (pass == 1) && c.match (d.label, d.section)) { any = true; break; }
            if (! any) continue;
            const juce::String title = juce::String (sec) + (pass ? " - advanced" : ""), key = "L:" + juce::String (layerTypeName (type)) + ":" + title;
            pp.section (title, pass ? true : ! open, key);
            for (auto& dd : schema)
            {
                if (dd.section != sec || dd.advanced != (pass == 1) || ! c.match (dd.label, dd.section)) continue;
                const PDef* d = &dd; juce::String tip = d->tip;
                switch (d->kind)
                {
                    case PKind::Slider:
                        pp.slider (d->label, tip, d->minV, d->maxV, d->defV, d->step, [=] { auto* l = layerGet(); return l ? (double) l->fAt (d->id.c_str(), cp->layerTime (*l)) : (double) d->defV; },
                                   [=] (double v) { setLayerProp (cp, id, d, (float) v); },
                                   d->keyable ? std::function<void()> ([=] { toggleKey (cp, id, d); }) : std::function<void()>(),
                                   [=] { auto* l = layerGet(); if (! l) return false; auto it = l->keys.find (d->id); return it != l->keys.end() && ! it->second.empty(); });
                        break;
                    case PKind::Toggle: pp.toggle (d->label, tip, [=] { auto* l = layerGet(); return l && l->props.getF (*d) > .5f; }, [=] (bool v) { setLayerProp (cp, id, d, v ? 1.f : 0.f); }); break;
                    case PKind::Choice:
                    { juce::StringArray items; for (auto& s : d->choices) items.add (s);
                      pp.choice (d->label, tip, items, [=] { auto* l = layerGet(); return l ? (int) l->props.getF (*d) : 0; }, [=] (int v) { setLayerProp (cp, id, d, (float) v); }); break; }
                    case PKind::Color:
                        pp.colour (d->label, tip, [=] { auto* l = layerGet(); return l ? juce::String (l->props.getS (*d)) : juce::String (d->sdef); }, [=] (juce::String v) { proc->store.edit (juce::String ("Edit ") + d->label, [=] (Project& p) { if (auto* l = p.find (id)) l->props.setS (*d, v.toStdString()); }, "L" + juce::String (id) + d->id); }, d->sdef); break;
                    case PKind::Text:
                        pp.text (d->label, tip, [=] { auto* l = layerGet(); return l ? juce::String (juce::CharPointer_UTF8 (l->props.getS (*d).c_str())) : juce::String(); },
                                 [=] (juce::String v) { proc->store.edit ("Edit text", [=] (Project& p) { if (auto* l = p.find (id)) l->props.setS (*d, v.toStdString()); }, "L" + juce::String (id) + d->id); }, d->id == "text"); break;
                    case PKind::Font:
                    {
                        juce::StringArray fams; fams.add ("(default font)");
                        for (auto& f : c.project().favFonts) fams.addIfNotAlreadyThere (juce::String (f));
                        for (auto& f : FontRegistry::get().installedFamilies()) fams.addIfNotAlreadyThere (f);
                        pp.choice (d->label, tip, fams, [=] { auto* l = layerGet(); if (! l) return 0; auto s = juce::String (l->props.getS (*d)); int i = fams.indexOf (s); return s.isEmpty() ? 0 : (i < 0 ? 0 : i); },
                                   [=] (int i) { proc->store.edit ("Change font", [=] (Project& p) { if (auto* l = p.find (id)) l->props.setS (*d, i <= 0 ? std::string() : fams[i].toStdString()); }); });
                        if (auto* l = layerGet(); l && ! l->props.getS (*d).empty() && ! FontRegistry::get().isAvailable (juce::String (l->props.getS (*d)))) pp.info ("This font is not installed - a fallback font is used.");
                        break;
                    }
                }
            }
        }
    }
}

// ============================================================================================ animations
void buildLayerAnims (PropPanel& pp, UiCtx& c)
{
    const Layer* L = c.project().find (c.selectedLayer);
    if (! L) {  return; }
    const int id = L->id; MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    auto animEdit = [=] (int aid, std::function<void (AnimInst&)> fn, const juce::String& label) { proc->store.edit (label, [=] (Project& p) { if (auto* l = p.find (id)) for (auto& a : l->anims) if (a.id == aid) fn (a); }, "A" + juce::String (aid) + label); };
    pp.section ("Animations", false, "A:head");
    pp.info ("Stack several animations. In = entrance, Out = exit, Loop = continuous.");
    pp.buttons ({ { "+ Add animation", [=]
    {
        juce::PopupMenu m, in, out, loop; const auto& pr = animPresets();
        for (size_t i = 0; i < pr.size(); ++i) (pr[i].kind == AnimKind::In ? in : pr[i].kind == AnimKind::Out ? out : loop).addItem ((int) i + 1, pr[i].name);
        m.addSubMenu ("Entrance (In)", in); m.addSubMenu ("Exit (Out)", out); m.addSubMenu ("Loop", loop);
        m.showMenuAsync (juce::PopupMenu::Options(), [=] (int r) { if (r <= 0) return;
            proc->store.edit ("Add animation", [=] (Project& p) { if (auto* l = p.find (id)) { AnimInst a; a.id = p.newId(); a.preset = r - 1; a.dur = .8; a.easing = animPresets()[(size_t) (r - 1)].kind == AnimKind::Loop ? 1 : 3; l->anims.push_back (a); } });
            if (cp->rebuild) cp->rebuild(); }); } } });
    int n = 0;
    for (auto& a : L->anims)
    {
        const int aid = a.id; ++n; const auto& pr = animPresets(); const auto& pre = pr[(size_t) juce::jlimit (0, (int) pr.size() - 1, a.preset)];
        pp.section (juce::String (n) + ". " + pre.name, false, "A:" + juce::String (aid));
        auto get = [=] () -> const AnimInst* { auto* l = cp->proc.store.get().find (id); if (l) for (auto& x : l->anims) if (x.id == aid) return &x; return nullptr; };
        pp.toggle ("Enabled", "Turn this animation on or off.", [=] { auto* x = get(); return x && x->enabled; }, [=] (bool v) { animEdit (aid, [=] (AnimInst& x) { x.enabled = v; }, "Toggle animation"); });
        juce::StringArray names; for (auto& q : pr) names.add (juce::String (q.kind == AnimKind::In ? "In: " : q.kind == AnimKind::Out ? "Out: " : "Loop: ") + q.name);
        pp.choice ("Preset", pre.desc, names, [=] { auto* x = get(); return x ? x->preset : 0; }, [=] (int i) { animEdit (aid, [=] (AnimInst& x) { x.preset = i; }, "Change animation"); if (cp->rebuild) cp->rebuild(); });
        pp.slider ("Start (s)", "Seconds after the layer starts (In/Loop) or before it ends (Out).", 0, 30, 0, 0.01, [=] { auto* x = get(); return x ? x->start : 0.0; }, [=] (double v) { animEdit (aid, [=] (AnimInst& x) { x.start = v; }, "Anim start"); });
        pp.slider ("Duration (s)", "How long the animation takes.", 0.05, 10, .6, 0.01, [=] { auto* x = get(); return x ? x->dur : .6; }, [=] (double v) { animEdit (aid, [=] (AnimInst& x) { x.dur = v; }, "Anim duration"); });
        pp.slider ("Speed", "Playback speed multiplier.", 0.1, 4, 1, 0.01, [=] { auto* x = get(); return x ? (double) x->speed : 1.0; }, [=] (double v) { animEdit (aid, [=] (AnimInst& x) { x.speed = (float) v; }, "Anim speed"); });
        pp.slider ("Intensity", "Strength of the effect.", 0, 3, 1, 0.01, [=] { auto* x = get(); return x ? (double) x->intensity : 1.0; }, [=] (double v) { animEdit (aid, [=] (AnimInst& x) { x.intensity = (float) v; }, "Anim intensity"); });
        if (pre.usesDir) pp.choice ("Direction", "Where it comes from / goes to.", juce::StringArray { "Up", "Down", "Left", "Right" }, [=] { auto* x = get(); return x ? x->dir : 0; }, [=] (int i) { animEdit (aid, [=] (AnimInst& x) { x.dir = i; }, "Anim direction"); });
        pp.choice ("Easing", "Shape of the motion curve.", juce::StringArray::fromTokens (animEaseNames(), "|", ""), [=] { auto* x = get(); return x ? x->easing : 3; }, [=] (int i) { animEdit (aid, [=] (AnimInst& x) { x.easing = i; }, "Anim easing"); });
        pp.choice ("Audio source", "Make this animation react to the music.", juce::StringArray::fromTokens (audioSrcNames(), "|", ""), [=] { auto* x = get(); return x ? x->audioSrc : 0; }, [=] (int i) { animEdit (aid, [=] (AnimInst& x) { x.audioSrc = i; }, "Anim audio"); });
        pp.slider ("Audio amount", "How strongly the audio source modulates it.", 0, 2, 0, 0.01, [=] { auto* x = get(); return x ? (double) x->audioAmt : 0.0; }, [=] (double v) { animEdit (aid, [=] (AnimInst& x) { x.audioAmt = (float) v; }, "Anim audio amount"); });
        pp.buttons ({ { "Remove", [=] { proc->store.edit ("Remove animation", [=] (Project& p) { if (auto* l = p.find (id)) l->anims.erase (std::remove_if (l->anims.begin(), l->anims.end(), [=] (const AnimInst& x) { return x.id == aid; }), l->anims.end()); }); if (cp->rebuild) cp->rebuild(); } } });
    }
}

// ============================================================================================ effect stack
void buildFxStack (PropPanel& pp, UiCtx& c)
{
    const bool global = c.fxTarget == 1 || c.selectedLayer < 0; const int lid = global ? -1 : c.selectedLayer;
    const Layer* L = global ? nullptr : c.project().find (lid);
    if (! global && ! L) {  return; }
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    auto stackOf = [lid] (Project& p) -> std::vector<FxInst>* { if (lid < 0) return &p.fx; auto* l = p.find (lid); return l ? &l->fx : nullptr; };
    const auto& stack = global ? c.project().fx : L->fx;
    pp.section (global ? "Global effect stack" : "Layer effects: " + juce::String (L->name), false, "F:head");
    pp.info (global ? "Effects here are applied to the whole picture, top to bottom." : "Effects here only affect this layer, top to bottom.");
    pp.toggle ("Bypass whole stack", "Hear/see the picture without any of these effects.", [=] { auto& p = cp->proc.store.get(); if (lid < 0) return p.fxBypass; auto* l = p.find (lid); return l && l->fxBypass; },
              [=] (bool v) { proc->store.edit ("Bypass stack", [=] (Project& p) { if (lid < 0) p.fxBypass = v; else if (auto* l = p.find (lid)) l->fxBypass = v; }); });
    pp.buttons ({ { "+ Add effect", [=]
    {
        juce::PopupMenu m; std::map<std::string, juce::PopupMenu> cats; for (auto& d : fxCatalogue()) cats[d.category].addItem (d.type + 1, d.name);
        for (auto& kv : cats) m.addSubMenu (kv.first, kv.second);
        m.showMenuAsync (juce::PopupMenu::Options(), [=] (int r) { if (r <= 0) return; proc->store.edit ("Add effect", [=] (Project& p) { if (auto* s = stackOf (p)) p.addFx (*s, r - 1); }); if (cp->rebuild) cp->rebuild(); }); } },
        { "Clear all", [=] { proc->store.edit ("Clear effects", [=] (Project& p) { if (auto* s = stackOf (p)) s->clear(); }); if (cp->rebuild) cp->rebuild(); } } });
    int n = 0;
    for (auto& f : stack)
    {
        const int fid = f.id; ++n; auto* def = fxDef (f.type); if (! def) continue;
        pp.section (juce::String (n) + ". " + def->name + (f.enabled ? "" : "  (off)"), false, "F:" + juce::String (fid));
        auto get = [=] () -> const FxInst* { auto& p = cp->proc.store.get(); const std::vector<FxInst>* s = lid < 0 ? &p.fx : (p.find (lid) ? &p.find (lid)->fx : nullptr); if (s) for (auto& x : *s) if (x.id == fid) return &x; return nullptr; };
        auto edit = [=] (std::function<void (FxInst&)> fn, const juce::String& label) { proc->store.edit (label, [=] (Project& p) { if (auto* s = stackOf (p)) for (auto& x : *s) if (x.id == fid) fn (x); }, "F" + juce::String (fid) + label); };
        pp.info (def->desc);
        pp.toggle ("Enabled", "Bypass just this effect.", [=] { auto* x = get(); return x && x->enabled; }, [=] (bool v) { edit ([=] (FxInst& x) { x.enabled = v; }, "Toggle effect"); if (cp->rebuild) cp->rebuild(); });
        pp.slider ("Amount", "Strength of the effect.", 0, 2, 1, 0.01, [=] { auto* x = get(); return x ? (double) x->amount : 1.0; }, [=] (double v) { edit ([=] (FxInst& x) { x.amount = (float) v; }, "Effect amount"); });
        pp.slider ("Mix", "Blend between the original (0) and the effect (1).", 0, 1, 1, 0.01, [=] { auto* x = get(); return x ? (double) x->blend : 1.0; }, [=] (double v) { edit ([=] (FxInst& x) { x.blend = (float) v; }, "Effect mix"); });
        for (int k = 0; k < 4; ++k)
        {
            if (def->p[k].label.empty()) continue; const auto& pd = def->p[k];
            pp.slider (pd.label, "", pd.minV, pd.maxV, pd.defV, 0, [=] { auto* x = get(); return x ? (double) x->p[k] : (double) pd.defV; }, [=] (double v) { edit ([=] (FxInst& x) { x.p[k] = (float) v; }, "Effect parameter"); });
        }
        if (def->usesColor) pp.colour ("Colour", "", [=] { auto* x = get(); return x ? juce::String (x->color) : juce::String ("#ffffff"); }, [=] (juce::String s) { edit ([=] (FxInst& x) { x.color = s.toStdString(); }, "Effect colour"); });
        pp.choice ("Audio source", "Make the effect strength follow the music.", juce::StringArray::fromTokens (audioSrcNames(), "|", ""), [=] { auto* x = get(); return x ? x->audioSrc : 0; }, [=] (int i) { edit ([=] (FxInst& x) { x.audioSrc = i; }, "Effect audio"); });
        pp.slider ("Audio sensitivity", "", 0, 3, 0, 0.01, [=] { auto* x = get(); return x ? (double) x->audioSens : 0.0; }, [=] (double v) { edit ([=] (FxInst& x) { x.audioSens = (float) v; }, "Effect audio sens"); });
        pp.buttons ({ { "Up", [=] { proc->store.edit ("Move effect", [=] (Project& p) { if (auto* s = stackOf (p)) for (size_t i = 1; i < s->size(); ++i) if ((*s)[i].id == fid) { std::swap ((*s)[i], (*s)[i - 1]); break; } }); if (cp->rebuild) cp->rebuild(); } },
                      { "Down", [=] { proc->store.edit ("Move effect", [=] (Project& p) { if (auto* s = stackOf (p)) for (size_t i = 0; i + 1 < s->size(); ++i) if ((*s)[i].id == fid) { std::swap ((*s)[i], (*s)[i + 1]); break; } }); if (cp->rebuild) cp->rebuild(); } },
                      { "Duplicate", [=] { proc->store.edit ("Duplicate effect", [=] (Project& p) { if (auto* s = stackOf (p)) for (size_t i = 0; i < s->size(); ++i) if ((*s)[i].id == fid) { FxInst cpy = (*s)[i]; cpy.id = p.newId(); s->insert (s->begin() + (long) i + 1, cpy); break; } }); if (cp->rebuild) cp->rebuild(); } },
                      { "Remove", [=] { proc->store.edit ("Remove effect", [=] (Project& p) { if (auto* s = stackOf (p)) s->erase (std::remove_if (s->begin(), s->end(), [=] (const FxInst& x) { return x.id == fid; }), s->end()); }); if (cp->rebuild) cp->rebuild(); } } });
    }
}

// ============================================================================================ scene panel
static std::mt19937& rng() { static std::mt19937 r ((unsigned) juce::Time::currentTimeMillis()); return r; }
static float rnd01() { return std::uniform_real_distribution<float> (0.f, 1.f) (rng()); }
static std::array<float, 8> gKnobClip {}; static bool gHaveClip = false;
struct ABSlot { bool have = false; int scene = 0; float knobs[8] {}; std::vector<float> globals; };
static ABSlot gAB[2];
static void storeAB (MonieProcessor& p, int i) { auto& s = gAB[i]; s.have = true; s.scene = p.currentScene(); p.readKnobs (s.knobs); s.globals.assign (globalDefs().size(), 0.f); p.readGlobals (s.globals.data()); }
static void recallAB (MonieProcessor& p, int i)
{
    auto& s = gAB[i]; if (! s.have) return; p.selectScene (s.scene);
    for (int k = 0; k < 8; ++k) setParamPlain (p, "sk" + juce::String (k), s.knobs[k]);
    for (size_t g = 0; g < globalDefs().size(); ++g) setParamPlain (p, globalDefs()[g].id, s.globals[g]);
}

void buildScenePanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c; const int sc = proc->currentScene(); const auto& si = sceneCatalogue()[(size_t) sc];
    pp.section (juce::String (sc + 1) + ". " + juce::String (si.name), false, "S:head");
    pp.info (juce::String (si.category) + " - " + juce::String (si.desc));
    pp.toggle ("Favourite", "Starred scenes appear under Favourites in the scene browser.", [=] { return cp->proc.store.get().favScenes.count (sc) > 0; },
               [=] (bool v) { proc->store.edit ("Favourite scene", [=] (Project& p) { if (v) p.favScenes.insert (sc); else p.favScenes.erase (sc); }); });
    pp.section ("Scene controls", false, "S:knobs");
    for (int i = 0; i < 8; ++i)
    {
        const auto& kn = si.knobs[(size_t) i]; if (! c.match (kn.label, "Scene")) continue; const juce::String pid = "sk" + juce::String (i);
        pp.slider (kn.label, "Scene control " + juce::String (i + 1) + ". Automatable in your DAW as 'Scene Knob " + juce::String (i + 1) + "'.", kn.minV, kn.maxV, kn.defV, 0,
                   [=] { return (double) sceneCatalogue()[(size_t) cp->proc.currentScene()].toNative (i, cp->proc.apvts.getRawParameterValue (pid)->load()); },
                   [=] (double v) { setParamPlain (*proc, pid, sceneCatalogue()[(size_t) proc->currentScene()].toNorm (i, (float) v)); }, {}, {}, [=] (bool b) { paramGesture (*proc, pid, b); });
    }
    pp.buttons ({ { "Reset", [=] { proc->resetSceneKnobs(); if (cp->relayout) cp->relayout(); } },
                  { "Randomise", [=] { for (int i = 0; i < 8; ++i) setParamPlain (*proc, "sk" + juce::String (i), rnd01()); } },
                  { "Vary", [=] { for (int i = 0; i < 8; ++i) { float v = proc->apvts.getRawParameterValue ("sk" + juce::String (i))->load(); v += (rnd01() - .5f) * .3f; setParamPlain (*proc, "sk" + juce::String (i), juce::jlimit (0.f, 1.f, v)); } } } },
               "Randomise = completely new values. Vary = small random changes around the current look.");
    pp.buttons ({ { "Copy", [=] { float k[8]; proc->readKnobs (k); for (int i = 0; i < 8; ++i) gKnobClip[(size_t) i] = k[i]; gHaveClip = true; } },
                  { "Paste", [=] { if (gHaveClip) for (int i = 0; i < 8; ++i) setParamPlain (*proc, "sk" + juce::String (i), gKnobClip[(size_t) i]); } },
                  { "Store A", [=] { storeAB (*proc, 0); } }, { "Store B", [=] { storeAB (*proc, 1); } } });
    pp.buttons ({ { "Recall A", [=] { recallAB (*proc, 0); } }, { "Recall B", [=] { recallAB (*proc, 1); } } }, "A/B stores the scene, its controls and all global settings so you can compare two looks.");
    pp.section ("Scene images", true, "S:img");
    pp.info (juce::String (si.tags).contains ("image") ? "This scene uses your picture." : "This scene is generated; an image is optional (used by image-based scenes).");
    pp.text ("Main image", "Path of the picture used by image scenes.", [=] { return juce::String (cp->proc.store.get().sceneImage); }, [=] (juce::String s) { proc->store.edit ("Scene image", [=] (Project& p) { p.sceneImage = s.toStdString(); }, "sceneimg"); proc->media.request (s.toStdString()); });
    pp.buttons ({ { "Choose image...", [=] { chooseFile ("Choose an image", juce::File(), "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.webp", false, [=] (juce::File f) { proc->media.request (f.getFullPathName().toStdString()); proc->store.edit ("Scene image", [=] (Project& p) { p.sceneImage = f.getFullPathName().toStdString(); }); if (cp->rebuild) cp->rebuild(); }); } },
                  { "Clear", [=] { proc->store.edit ("Scene image", [=] (Project& p) { p.sceneImage.clear(); }); if (cp->rebuild) cp->rebuild(); } } });
    for (int k = 0; k < 3; ++k)
        pp.text ("Collage image " + juce::String (k + 2), "Extra pictures for Multi-Layer Collage.", [=] { auto& v = cp->proc.store.get().collageImages; return k < (int) v.size() ? juce::String (v[(size_t) k]) : juce::String(); },
                 [=] (juce::String s) { proc->store.edit ("Collage image", [=] (Project& p) { if ((int) p.collageImages.size() <= k) p.collageImages.resize ((size_t) k + 1); p.collageImages[(size_t) k] = s.toStdString(); }, "collage" + juce::String (k)); proc->media.request (s.toStdString()); });
}

// ============================================================================================ global parameters
void buildGlobalsPanel (PropPanel& pp, UiCtx& c, const juce::StringArray& sections)
{
    MonieProcessor* proc = &c.proc;
    std::vector<std::string> order; for (auto& d : globalDefs()) if (std::find (order.begin(), order.end(), d.section) == order.end()) order.push_back (d.section);
    for (auto& sec : order)
    {
        if (sections.size() > 0 && ! sections.contains (juce::String (sec))) continue;
        bool any = false; for (auto& d : globalDefs()) if (d.section == sec && c.match (d.name, d.section)) any = true;
        if (! any) continue;
        pp.section (sec, false, "G:" + juce::String (sec));
        for (auto& d : globalDefs())
        {
            if (d.section != sec || ! c.match (d.name, d.section)) continue; const juce::String pid = d.id; const GDef* gd = &d;
            pp.slider (d.name, juce::String (d.tip) + "\n(Host-automatable parameter '" + d.name + "')", d.minV, d.maxV, d.defV, 0, [=] { return (double) proc->apvts.getRawParameterValue (pid)->load(); },
                       [=] (double v) { setParamPlain (*proc, pid, (float) v); }, {}, {}, [=] (bool b) { paramGesture (*proc, pid, b); }); (void) gd;
        }
    }
}

// ============================================================================================ colour workspace
namespace
{
class GradientEditor : public juce::Component
{
public:
    explicit GradientEditor (UiCtx& cc) : c (cc) {}
    void paint (juce::Graphics& g) override
    {
        auto& lf = static_cast<MonieLnF&> (getLookAndFeel()); const auto& gr = c.project().gradient; auto bar = barRect();
        juce::ColourGradient cg (juce::Colours::black, bar.getX(), 0, juce::Colours::black, bar.getRight(), 0, false);
        cg.clearColours(); for (auto& s : gr.stops) cg.addColour (s.pos, juce::Colour::fromFloatRGBA (s.c.r, s.c.g, s.c.b, 1.f));
        g.setGradientFill (cg); g.fillRoundedRectangle (bar.toFloat(), 4.f); g.setColour (lf.t.line); g.drawRoundedRectangle (bar.toFloat(), 4.f, 1.f);
        for (size_t i = 0; i < gr.stops.size(); ++i)
        {
            const float x = bar.getX() + gr.stops[i].pos * bar.getWidth(); const bool sel = (int) i == gSelectedStop;
            juce::Path p; p.addTriangle (x, (float) bar.getBottom() + 2, x - 7, (float) bar.getBottom() + 14, x + 7, (float) bar.getBottom() + 14);
            g.setColour (juce::Colour::fromFloatRGBA (gr.stops[i].c.r, gr.stops[i].c.g, gr.stops[i].c.b, 1.f)); g.fillPath (p);
            g.setColour (sel ? lf.t.accent : lf.t.text); g.strokePath (p, juce::PathStrokeType (sel ? 2.5f : 1.f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto gr = c.project().gradient; auto bar = barRect(); drag = -1;
        for (size_t i = 0; i < gr.stops.size(); ++i) if (std::fabs (bar.getX() + gr.stops[i].pos * bar.getWidth() - e.position.x) < 9) { drag = (int) i; break; }
        if (drag < 0 && e.y <= bar.getBottom() + 14)
        {   // add a stop at the clicked position using the colour already there
            const float pos = juce::jlimit (0.f, 1.f, (e.position.x - (float) bar.getX()) / (float) bar.getWidth()); const auto col = gr.at (pos);
            c.proc.store.edit ("Add colour stop", [=] (Project& p) { Stop s; s.pos = pos; s.c = col; p.gradient.stops.push_back (s); p.gradient.normalise(); });
            auto g2 = c.project().gradient; for (size_t i = 0; i < g2.stops.size(); ++i) if (std::fabs (g2.stops[i].pos - pos) < 1e-6f) drag = (int) i;
        }
        if (drag >= 0) { gSelectedStop = drag; if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) removeStop (drag); else if (c.rebuild) c.rebuild(); }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0 || e.mods.isPopupMenu()) return; auto bar = barRect(); const float pos = juce::jlimit (0.f, 1.f, (e.position.x - (float) bar.getX()) / (float) bar.getWidth());
        drag2 = -1; const int d = drag; c.proc.store.edit ("Move colour stop", [=] (Project& p) { if (d < (int) p.gradient.stops.size()) { p.gradient.stops[(size_t) d].pos = pos;
            auto col = p.gradient.stops[(size_t) d].c; p.gradient.normalise(); for (size_t i = 0; i < p.gradient.stops.size(); ++i) if (p.gradient.stops[i].pos == pos && p.gradient.stops[i].c.r == col.r && p.gradient.stops[i].c.g == col.g) drag2 = (int) i; } }, "gradmove");
        if (drag2 >= 0) { drag = drag2; gSelectedStop = drag2; } repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { auto gr = c.project().gradient; auto bar = barRect(); for (size_t i = 0; i < gr.stops.size(); ++i) if (std::fabs (bar.getX() + gr.stops[i].pos * bar.getWidth() - e.position.x) < 9) removeStop ((int) i); }
private:
    juce::Rectangle<int> barRect() const { return getLocalBounds().reduced (10, 4).withHeight (26); }
    void removeStop (int i)
    {
        if (c.project().gradient.stops.size() <= 2) return;
        c.proc.store.edit ("Remove colour stop", [=] (Project& p) { if (i < (int) p.gradient.stops.size()) p.gradient.stops.erase (p.gradient.stops.begin() + i); });
        gSelectedStop = 0; if (c.rebuild) c.rebuild();
    }
    UiCtx& c; int drag = -1; int drag2 = -1;
};

class PaletteSwatch : public juce::Component {};
}

static void editStop (UiCtx& c, std::function<void (Stop&)> fn, const juce::String& label)
{
    c.proc.store.edit (label, [=] (Project& p) { if (gSelectedStop >= 0 && gSelectedStop < (int) p.gradient.stops.size()) fn (p.gradient.stops[(size_t) gSelectedStop]); }, "stop");
}

void buildColorPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    pp.section ("Gradient", false, "C:grad");
    pp.info ("Click the bar to add a colour stop, drag to move, double-click or right-click to remove.");
    pp.custom (new GradientEditor (c), 44);
    gSelectedStop = juce::jlimit (0, (int) c.project().gradient.stops.size() - 1, gSelectedStop);
    pp.text ("Palette name", "", [=] { return juce::String (cp->proc.store.get().gradient.name); }, [=] (juce::String s) { proc->store.edit ("Rename palette", [=] (Project& p) { p.gradient.name = s.toStdString(); }, "palname"); });
    pp.section ("Selected colour stop", false, "C:stop");
    auto stopGet = [=] () -> Stop { auto& g = cp->proc.store.get().gradient; return g.stops[(size_t) juce::jlimit (0, (int) g.stops.size() - 1, gSelectedStop)]; };
    pp.colour ("Colour (hex)", "Type a hex value such as #ff8800 or click the swatch.", [=] { return juce::String (toHex (stopGet().c)); }, [=] (juce::String h) { RGB col; if (parseHex (h.toStdString(), col)) editStop (*cp, [=] (Stop& s) { s.c = col; }, "Stop colour"); });
    pp.slider ("Position", "Where this stop sits on the gradient.", 0, 1, 0, 0.001, [=] { return (double) stopGet().pos; }, [=] (double v) { editStop (*cp, [=] (Stop& s) { s.pos = (float) v; }, "Stop position"); });
    for (int ch = 0; ch < 3; ++ch)
        pp.slider (ch == 0 ? "Red" : ch == 1 ? "Green" : "Blue", "", 0, 255, 0, 1, [=] { auto s = stopGet(); return (double) (ch == 0 ? s.c.r : ch == 1 ? s.c.g : s.c.b) * 255.0; },
                   [=] (double v) { editStop (*cp, [=] (Stop& s) { (ch == 0 ? s.c.r : ch == 1 ? s.c.g : s.c.b) = (float) v / 255.f; }, "Stop RGB"); });
    for (int ch = 0; ch < 3; ++ch)
        pp.slider (ch == 0 ? "Hue" : ch == 1 ? "Saturation" : "Value", "", 0, 1, 0, 0.001, [=] { auto s = stopGet(); float h, sa, v; rgb2hsv (s.c, h, sa, v); return (double) (ch == 0 ? h : ch == 1 ? sa : v); },
                   [=] (double val) { editStop (*cp, [=] (Stop& s) { float h, sa, v; rgb2hsv (s.c, h, sa, v); (ch == 0 ? h : ch == 1 ? sa : v) = (float) val; s.c = hsv2rgb (h, sa, v); }, "Stop HSV"); });
    pp.section ("Palette tools", false, "C:tools");
    pp.buttons ({ { "Reverse", [=] { proc->store.edit ("Reverse palette", [=] (Project& p) { auto& s = p.gradient.stops; std::reverse (s.begin(), s.end()); for (auto& x : s) x.pos = 1.f - x.pos; }); } },
                  { "Duotone", [=] { proc->store.edit ("Duotone", [=] (Project& p) { auto a = p.gradient.stops.front().c, b = p.gradient.stops.back().c; p.gradient.stops = { { 0.f, a }, { 1.f, b } }; }); gSelectedStop = 0; if (cp->rebuild) cp->rebuild(); } },
                  { "Tritone", [=] { proc->store.edit ("Tritone", [=] (Project& p) { auto a = p.gradient.stops.front().c, b = p.gradient.stops.back().c, m = p.gradient.at (.5f); p.gradient.stops = { { 0.f, a }, { .5f, m }, { 1.f, b } }; }); gSelectedStop = 0; if (cp->rebuild) cp->rebuild(); } } });
    pp.buttons ({ { "Save to my palettes", [=] { proc->store.edit ("Save palette", [=] (Project& p) { Gradient g = p.gradient; if (g.name.empty() || g.name == "Custom") g.name = "My palette " + std::to_string (p.userPalettes.size() + 1); p.userPalettes.push_back (g); }); } },
                  { "Export...", [=] { chooseFile ("Export palette", presetsDir().getChildFile ("palette.monipal"), "*.monipal", true, [=] (juce::File f) { f.replaceWithText (juce::String (cp->project().gradient.toText())); }); } },
                  { "Import...", [=] { chooseFile ("Import palette", presetsDir(), "*.monipal;*.txt", false, [=] (juce::File f) { Gradient g; if (Gradient::fromText (f.loadFileAsString().trim().toStdString(), g)) { proc->store.edit ("Import palette", [=] (Project& p) { p.gradient = g; }); gSelectedStop = 0; if (cp->rebuild) cp->rebuild(); } else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Not a palette", "That file is not a monie visuals palette."); }); } } });
    pp.section ("Colour grade and colour drivers", false, "C:grade");
    // reuse the global slider builder inline
    std::vector<std::string> secs { "Color Grade", "Color" };
    for (auto& d : globalDefs()) { bool in = false; for (auto& s : secs) if (d.section == s) in = true; if (! in || ! c.match (d.name, d.section)) continue; const juce::String pid = d.id;
        pp.slider (d.name, d.tip, d.minV, d.maxV, d.defV, 0, [=] { return (double) proc->apvts.getRawParameterValue (pid)->load(); }, [=] (double v) { setParamPlain (*proc, pid, (float) v); }, {}, {}, [=] (bool b) { paramGesture (*proc, pid, b); }); }
}

// ============================================================================================ audio workspace + mappings
namespace
{
struct MapTarget { std::string id; juce::String label; float lo, hi; };
std::vector<MapTarget> mapTargets (UiCtx& c)
{
    std::vector<MapTarget> t; const auto& si = sceneCatalogue()[(size_t) c.proc.currentScene()];
    for (int i = 0; i < 8; ++i) t.push_back ({ "scene:k" + std::to_string (i), "Scene: " + juce::String (si.knobs[(size_t) i].label), si.knobs[(size_t) i].minV, si.knobs[(size_t) i].maxV });
    for (auto& d : globalDefs()) t.push_back ({ std::string ("global:") + d.id, juce::String ("Global: ") + d.name, d.minV, d.maxV });
    if (auto* l = c.project().find (c.selectedLayer)) for (auto& d : schemaFor (l->type)) if (d.kind == PKind::Slider && d.keyable) t.push_back ({ "layer:" + std::to_string (l->id) + ":" + d.id, "Layer '" + juce::String (l->name) + "': " + d.label, d.minV, d.maxV });
    return t;
}
class Meters : public juce::Component, juce::Timer
{
public:
    explicit Meters (MonieProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto& lf = static_cast<MonieLnF&> (getLookAndFeel()); auto r = getLocalBounds().reduced (6, 4);
        const char* names[] = { "Bass", "Mid", "Treble", "RMS", "Beat" }; auto& a = proc.analysis;
        const float v[] = { a.bass, a.mid, a.treble, a.rms, a.transient };
        const int w = r.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
        {
            auto cell = r.withX (r.getX() + i * w).withWidth (w - 6); auto bar = cell.withTrimmedBottom (16);
            g.setColour (lf.t.line); g.fillRoundedRectangle (bar.toFloat(), 3.f);
            g.setColour (lf.t.accent); const float h = juce::jlimit (0.f, 1.f, v[i]) * (float) bar.getHeight(); g.fillRoundedRectangle (bar.getX() + 0.f, bar.getBottom() - h, (float) bar.getWidth(), h, 3.f);
            g.setColour (lf.t.dim); g.setFont (11.f); g.drawText (names[i], cell.removeFromBottom (16), juce::Justification::centred);
        }
        if (! proc.audioActive()) { g.setColour (lf.t.dim); g.setFont (12.f); g.drawText ("No audio is reaching the plugin yet - press play in the host.", getLocalBounds(), juce::Justification::centred); }
    }
    void timerCallback() override { repaint(); }
    MonieProcessor& proc;
};
}

void buildAudioPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    pp.section ("Audio input", false, "U:meters");
    pp.custom (new Meters (c.proc), 80);
    pp.info ("The plugin only listens to the audio the host sends it; it never changes that audio.");
    pp.choice ("Analysis source", "Main mix, or the optional sidechain input (route a vocal or drum stem to it).", juce::StringArray { "Host mix (main input)", "Sidechain bus (if routed)" }, [=] { return proc->sidechainChoice.load(); }, [=] (int i) { proc->sidechainChoice = i; });
    juce::StringArray secs { "Audio" }; // sensitivity sliders
    for (auto& d : globalDefs()) if (juce::String (d.section) == "Audio" && c.match (d.name, d.section)) { const juce::String pid = d.id;
        pp.slider (d.name, d.tip, d.minV, d.maxV, d.defV, 0, [=] { return (double) proc->apvts.getRawParameterValue (pid)->load(); }, [=] (double v) { setParamPlain (*proc, pid, (float) v); }, {}, {}, [=] (bool b) { paramGesture (*proc, pid, b); }); }

    pp.section ("Audio mappings", false, "U:maps");
    pp.info ("A mapping makes an audio feature move any parameter (added on top of its normal value).");
    pp.buttons ({ { "+ Add mapping", [=]
    {
        auto targets = std::make_shared<std::vector<MapTarget>> (mapTargets (*cp)); juce::PopupMenu m, sc, gl, ly; int id = 1;
        for (auto& t : *targets) { (t.id.rfind ("scene:", 0) == 0 ? sc : t.id.rfind ("global:", 0) == 0 ? gl : ly).addItem (id++, t.label); }
        m.addSubMenu ("Scene controls", sc); m.addSubMenu ("Global settings", gl); if (ly.getNumItems() > 0) m.addSubMenu ("Selected layer", ly);
        m.showMenuAsync (juce::PopupMenu::Options(), [=] (int r) { if (r <= 0) return; const auto& t = (*targets)[(size_t) r - 1]; const float span = t.hi - t.lo;
            proc->store.edit ("Add mapping", [=] (Project& p) { Mapping mp; mp.target = t.id; mp.source = Source::Bass; mp.outMin = 0; mp.outMax = span * .35f; mp.seed = (uint32_t) p.newId(); p.mapping.add (mp); });
            if (cp->rebuild) cp->rebuild(); }); } } });
    const auto targets = mapTargets (c); const auto& maps = c.project().mapping.maps; int n = 0;
    for (size_t mi = 0; mi < maps.size(); ++mi)
    {
        const auto& m = maps[mi]; juce::String tl = juce::String (m.target); float lo = 0, hi = 1;
        for (auto& t : targets) if (t.id == m.target) { tl = t.label; lo = t.lo; hi = t.hi; }
        if (tl == juce::String (m.target)) { if (m.target.rfind ("layer:", 0) == 0) tl = "Layer parameter (select that layer to edit its range)"; if (auto* g = findGlobal (m.target.substr (7))) { lo = g->minV; hi = g->maxV; } }
        const float span = hi - lo; const int idx = (int) mi; ++n;
        auto me = [=] (std::function<void (Mapping&)> fn, const juce::String& label) { proc->store.edit (label, [=] (Project& p) { if (idx < (int) p.mapping.maps.size()) fn (p.mapping.maps[(size_t) idx]); }, "M" + juce::String (idx) + label); };
        auto get = [=] () -> const Mapping* { auto& v = cp->proc.store.get().mapping.maps; return idx < (int) v.size() ? &v[(size_t) idx] : nullptr; };
        pp.section (juce::String (n) + ". " + tl, false, "U:m" + juce::String (idx));
        pp.choice ("Source", "Which audio feature drives it.", juce::StringArray { "Bass", "Low-Mid", "Mid", "High-Mid", "Treble", "RMS (loudness)", "Peak", "Transient", "Beat", "Flux (change)" }, [=] { auto* x = get(); return x ? (int) x->source : 0; }, [=] (int i) { me ([=] (Mapping& x) { x.source = (Source) i; }, "Mapping source"); });
        pp.slider ("Sensitivity", "Gain on the audio feature before mapping.", 0, 4, 1, 0.01, [=] { auto* x = get(); return x ? (double) x->sensitivity : 1.0; }, [=] (double v) { me ([=] (Mapping& x) { x.sensitivity = (float) v; }, "Mapping sensitivity"); });
        pp.slider ("Output at min", "Amount added when the audio feature is at its minimum.", -span, span, 0, 0, [=] { auto* x = get(); return x ? (double) x->outMin : 0.0; }, [=] (double v) { me ([=] (Mapping& x) { x.outMin = (float) v; }, "Mapping out min"); });
        pp.slider ("Output at max", "Amount added when the audio feature is at its maximum.", -span, span, span * .35, 0, [=] { auto* x = get(); return x ? (double) x->outMax : 0.0; }, [=] (double v) { me ([=] (Mapping& x) { x.outMax = (float) v; }, "Mapping out max"); });
        pp.choice ("Curve", "Response shape.", juce::StringArray { "Linear", "Squared", "Square root", "S-curve", "Cubic" }, [=] { auto* x = get(); return x ? (int) x->curve : 0; }, [=] (int i) { me ([=] (Mapping& x) { x.curve = (Curve) i; }, "Mapping curve"); });
        pp.slider ("Smoothing", "Extra smoothing.", 0, 1, 0, 0.01, [=] { auto* x = get(); return x ? (double) x->smoothing : 0.0; }, [=] (double v) { me ([=] (Mapping& x) { x.smoothing = (float) v; }, "Mapping smoothing"); });
        pp.slider ("Attack (ms)", "How fast it rises.", 0, 500, 10, 1, [=] { auto* x = get(); return x ? (double) x->attackMs : 10.0; }, [=] (double v) { me ([=] (Mapping& x) { x.attackMs = (float) v; }, "Mapping attack"); });
        pp.slider ("Release (ms)", "How fast it falls.", 0, 2000, 150, 1, [=] { auto* x = get(); return x ? (double) x->releaseMs : 150.0; }, [=] (double v) { me ([=] (Mapping& x) { x.releaseMs = (float) v; }, "Mapping release"); });
        pp.slider ("Threshold", "Ignore audio below this level.", 0, .99, 0, 0.01, [=] { auto* x = get(); return x ? (double) x->threshold : 0.0; }, [=] (double v) { me ([=] (Mapping& x) { x.threshold = (float) v; }, "Mapping threshold"); });
        pp.toggle ("Invert", "Louder = lower value.", [=] { auto* x = get(); return x && x->invert; }, [=] (bool v) { me ([=] (Mapping& x) { x.invert = v; }, "Mapping invert"); });
        pp.toggle ("Update on beats only", "Sample-and-hold: the value only changes when a beat is detected.", [=] { auto* x = get(); return x && x->quantizeToBeat; }, [=] (bool v) { me ([=] (Mapping& x) { x.quantizeToBeat = v; }, "Mapping beat quantise"); });
        pp.slider ("Random amount", "Adds repeatable random movement.", 0, 1, 0, 0.01, [=] { auto* x = get(); return x ? (double) x->randomAmount : 0.0; }, [=] (double v) { me ([=] (Mapping& x) { x.randomAmount = (float) v; }, "Mapping random"); });
        pp.buttons ({ { "Remove mapping", [=] { proc->store.edit ("Remove mapping", [=] (Project& p) { p.mapping.remove (idx); }); if (cp->rebuild) cp->rebuild(); } } });
    }
}

// ============================================================================================ captions
static void applyCaptionPreset (UiCtx& c, int which)
{
    std::map<std::string, std::string> v;
    switch (which)
    {
        case 0: v = { { "fill", "#ffffff" }, { "hiC", "#ffe14a" }, { "o1w", "4" }, { "o1c", "#000000" }, { "shOp", "0.6" }, { "bg", "0" }, { "size", "6.5" }, { "highlight", "1" }, { "mode", "0" }, { "style", "1" } }; break;   // karaoke
        case 1: v = { { "fill", "#f4f4f4" }, { "o1w", "0" }, { "bg", "1" }, { "bgC", "#000000" }, { "bgOp", "0.65" }, { "size", "5" }, { "highlight", "0" }, { "style", "0" }, { "shOp", "0" } }; break;                              // clean boxed
        case 2: v = { { "fill", "#ffffff" }, { "o1w", "6" }, { "o1c", "#ff2a6d" }, { "size", "10" }, { "mode", "1" }, { "highlight", "0" }, { "glow", "0.6" }, { "glowC", "#ff2a6d" }, { "style", "1" }, { "bg", "0" } }; break;   // big word-by-word
        default: v = { { "fill", "#ffd9a0" }, { "o1w", "2" }, { "o1c", "#2b0f05" }, { "size", "6" }, { "highlight", "1" }, { "hiC", "#ffffff" }, { "style", "2" }, { "bg", "0" }, { "shOp", "0.4" } }; break;               // warm italic
    }
    applyTextStyle (c, v);
}
void applyTextStyle (UiCtx& c, const std::map<std::string, std::string>& vals)
{
    const Layer* L = c.project().find (c.selectedLayer); if (! L) return; const int id = L->id; const auto type = L->type;
    c.proc.store.edit ("Apply style", [=] (Project& p)
    {
        auto* l = p.find (id); if (! l) return; const auto& sch = schemaFor (type);
        for (auto& kv : vals) { const PDef* d = findDef (sch, kv.first); if (! d) continue;
            if (d->kind == PKind::Color || d->kind == PKind::Text || d->kind == PKind::Font) l->props.setS (*d, kv.second); else l->props.setF (*d, std::strtof (kv.second.c_str(), nullptr)); }
    });
    if (c.rebuild) c.rebuild();
}

void buildCaptionPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c; CaptionService* cs = c.proc.captions.get();
    pp.section ("Captions", false, "K:head");
    pp.toggle ("CAPTIONS ON", "Shows lyrics / captions on the picture. With automatic recognition enabled this also starts listening.", [=] { return cp->proc.store.get().captionsOn; },
              [=] (bool v) { proc->store.edit ("Captions", [=] (Project& p) { p.captionsOn = v; }); setParamPlain (*proc, "captions", v ? 1.f : 0.f);
                             if (cp->project().asr.enabled && cs) cs->setEnabled (v); if (cp->rebuild) cp->rebuild(); });
    const bool auto_ = c.project().asr.enabled;
    juce::String stateLine = auto_ && cs ? cs->stateName() : (c.project().captionsOn ? "ON (manual lyrics)" : "OFF");
    pp.info ("Status: " + stateLine + (auto_ && cs && cs->message().isNotEmpty() ? " - " + cs->message().substring (0, 160) : juce::String()));
    if (auto_ && cs) pp.info ("Chunks recognised: " + juce::String (cs->chunksDone()) + "   dropped (too slow): " + juce::String (cs->chunksDropped()));
    if (! c.project().find (c.selectedLayer) || c.project().find (c.selectedLayer)->type != LayerType::Caption)
    {
        bool have = false; for (auto& l : c.project().layers) if (l.type == LayerType::Caption) have = true;
        pp.buttons ({ { have ? "Select caption layer" : "+ Add caption layer", [=] { int idn = -1; for (auto& l : cp->project().layers) if (l.type == LayerType::Caption) idn = l.id;
            if (idn < 0) proc->store.edit ("Add caption layer", [&] (Project& p) { idn = p.addLayer (LayerType::Caption, "Captions"); });
            if (cp->select) cp->select (idn); } } });
    }
    pp.section ("Lyrics", false, "K:lyrics");
    pp.info ("Timed lines as [mm:ss.xx] text. Plain lines without times can be spread over a time range.");
    pp.text ("Lyrics", "Edit the lyrics directly. Format: [01:23.45] line of text", [=] { return juce::String (toLRC (cp->project().lyrics)); },
             [=] (juce::String t) { auto d = parseLRC (t.toStdString()); if (d.lines.empty() && t.trim().isNotEmpty()) d = parsePlain (t.toStdString()); proc->store.edit ("Edit lyrics", [=] (Project& p) { p.lyrics = d; }, "lyrics"); }, true, 200);
    pp.buttons ({ { "Import...", [=] { chooseFile ("Import lyrics", juce::File(), "*.lrc;*.srt;*.txt", false, [=] (juce::File f)
        { auto t = f.loadFileAsString().toStdString(); LyricDoc d; auto ext = f.getFileExtension().toLowerCase();
          if (ext == ".srt") d = parseSRT (t); else if (ext == ".lrc") d = parseLRC (t); else { d = parsePlain (t); distributeEvenly (d, 0, juce::jmax (10.0, cp->project().duration * .5)); }
          proc->store.edit ("Import lyrics", [=] (Project& p) { p.lyrics = d; p.captionsOn = true; }); if (cp->rebuild) cp->rebuild(); }); } },
                  { "Export SRT...", [=] { chooseFile ("Export SRT", presetsDir().getChildFile ("lyrics.srt"), "*.srt", true, [=] (juce::File f) { f.replaceWithText (juce::String (toSRT (mergeDocs (cp->project().lyrics, cp->project().autoLyrics)))); }); } },
                  { "Export LRC...", [=] { chooseFile ("Export LRC", presetsDir().getChildFile ("lyrics.lrc"), "*.lrc", true, [=] (juce::File f) { f.replaceWithText (juce::String (toLRC (mergeDocs (cp->project().lyrics, cp->project().autoLyrics)))); }); } } });
    pp.buttons ({ { "Clear manual", [=] { proc->store.edit ("Clear lyrics", [=] (Project& p) { p.lyrics.lines.clear(); }); if (cp->rebuild) cp->rebuild(); } },
                  { "Clear automatic", [=] { proc->store.edit ("Clear auto captions", [=] (Project& p) { p.autoLyrics.lines.clear(); }); if (cp->rebuild) cp->rebuild(); } },
                  { "Auto lines -> manual", [=] { proc->store.edit ("Keep auto captions", [=] (Project& p) { p.lyrics = mergeDocs (p.lyrics, p.autoLyrics); p.autoLyrics.lines.clear(); }); if (cp->rebuild) cp->rebuild(); } } },
               "Copies the automatic captions into the editable lyrics so you can correct them.");
    pp.info (juce::String ((int) c.project().lyrics.lines.size()) + " manual lines, " + juce::String ((int) c.project().autoLyrics.lines.size()) + " automatic lines.");
    pp.slider ("Spread untimed lines to (s)", "Evenly times plain lines over 0..N seconds as a starting point.", 5, 600, 60, 1, [=] { return 60.0; }, [=] (double v) { proc->store.edit ("Distribute lyrics", [=] (Project& p) { distributeEvenly (p.lyrics, 0, v); }); });

    pp.section ("Style presets", false, "K:presets");
    pp.buttons ({ { "Karaoke", [=] { applyCaptionPreset (*cp, 0); } }, { "Clean box", [=] { applyCaptionPreset (*cp, 1); } }, { "Big words", [=] { applyCaptionPreset (*cp, 2); } }, { "Warm italic", [=] { applyCaptionPreset (*cp, 3); } } }, "Applies to the selected caption layer.");

    pp.section ("Automatic recognition (optional)", true, "K:asr");
    pp.info ("OFF by default. Needs a separately installed offline whisper.cpp program and model - see docs/CAPTIONS_SETUP.md.");
    pp.info ("Privacy: audio is processed only on this computer by that program. Nothing is uploaded. The plugin never records your microphone or system audio; it only reads the audio your DAW sends to it.");
    auto asrEdit = [=] (std::function<void (AsrConfig&)> fn, const juce::String& l) { proc->store.edit (l, [=] (Project& p) { fn (p.asr); }, "asr"); };
    pp.toggle ("I understand and consent", "Required before any audio is analysed.", [=] { return cp->project().asr.consent; }, [=] (bool v) { asrEdit ([=] (AsrConfig& a) { a.consent = v; if (! v) a.enabled = false; }, "ASR consent"); if (cp->rebuild) cp->rebuild(); });
    pp.toggle ("Recognise lyrics automatically from the host audio", "While the host plays, audio is analysed in chunks and captions appear a few seconds late.", [=] { return cp->project().asr.enabled; },
              [=] (bool v) { asrEdit ([=] (AsrConfig& a) { a.enabled = v; }, "ASR enable"); if (cs) cs->setEnabled (v && cp->project().captionsOn); if (cp->rebuild) cp->rebuild(); });
    pp.text ("whisper-cli program", "Full path to whisper-cli (.exe on Windows).", [=] { return juce::String (cp->project().asr.exePath); }, [=] (juce::String s) { asrEdit ([=] (AsrConfig& a) { a.exePath = s.toStdString(); }, "ASR exe"); });
    pp.buttons ({ { "Browse program...", [=] { chooseFile ("whisper-cli", juce::File(), "*", false, [=] (juce::File f) { asrEdit ([=] (AsrConfig& a) { a.exePath = f.getFullPathName().toStdString(); }, "ASR exe"); if (cp->rebuild) cp->rebuild(); }); } },
                  { "Browse model...", [=] { chooseFile ("Whisper model (ggml-*.bin)", juce::File(), "*.bin", false, [=] (juce::File f) { asrEdit ([=] (AsrConfig& a) { a.modelPath = f.getFullPathName().toStdString(); }, "ASR model"); if (cp->rebuild) cp->rebuild(); }); } } });
    pp.text ("Model file", "Path to a ggml model such as ggml-base.en.bin.", [=] { return juce::String (cp->project().asr.modelPath); }, [=] (juce::String s) { asrEdit ([=] (AsrConfig& a) { a.modelPath = s.toStdString(); }, "ASR model"); });
    pp.text ("Language", "'auto' or a code such as en, es, fr, de, ja.", [=] { return juce::String (cp->project().asr.language); }, [=] (juce::String s) { asrEdit ([=] (AsrConfig& a) { a.language = s.trim().isEmpty() ? "auto" : s.trim().toStdString(); }, "ASR language"); });
    pp.slider ("Chunk length (s)", "Longer = better accuracy, later captions.", 4, 60, 12, 1, [=] { return (double) cp->project().asr.chunkSec; }, [=] (double v) { asrEdit ([=] (AsrConfig& a) { a.chunkSec = (int) v; }, "ASR chunk"); });
    pp.choice ("Audio source", "A separate vocal stem on the sidechain input gives far better results than a full mix.", juce::StringArray { "Host mix", "Sidechain bus (vocal stem)" }, [=] { return cp->project().asr.source; }, [=] (int i) { asrEdit ([=] (AsrConfig& a) { a.source = i; }, "ASR source"); proc->sidechainChoice = i; });
    pp.buttons ({ { "Transcribe an audio file...", [=] { if (cs) { auto why = cs->configProblem(); if (why.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Recognition not ready", why); return; }
                       chooseFile ("Audio or vocal stem", juce::File(), "*.wav;*.mp3;*.flac;*.ogg;*.m4a", false, [=] (juce::File f) { cs->transcribeFile (f); }); } } },
                  { "Cancel", [=] { if (cs) cs->cancel(); } } }, "Most reliable way to get timed lyrics: transcribe the rendered vocal or full song once, then correct the text.");
}

// ============================================================================================ project / settings / presets
void buildProjectPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    pp.section ("Project", false, "P:head");
    pp.text ("Name", "", [=] { return juce::String (cp->project().name); }, [=] (juce::String s) { proc->store.edit ("Rename project", [=] (Project& p) { p.name = s.toStdString(); }, "pname"); });
    pp.choice ("Canvas shape", "Aspect ratio of the picture and of exported videos.", juce::StringArray::fromTokens (aspectNames(), "|", ""), [=] { return cp->project().canvasAspect; }, [=] (int i) { proc->store.edit ("Canvas shape", [=] (Project& p) { p.canvasAspect = i; }); });
    pp.slider ("Timeline length (s)", "Length of the timeline used for clips and export.", 5, 3600, 180, 1, [=] { return cp->project().duration; }, [=] (double v) { proc->store.edit ("Timeline length", [=] (Project& p) { p.duration = v; }, "dur"); });
    pp.section ("Loop and markers", false, "P:loop");
    pp.toggle ("Loop region", "Loops playback between A and B when running without a host transport (standalone).", [=] { return cp->project().loopOn; }, [=] (bool v) { proc->store.edit ("Loop", [=] (Project& p) { p.loopOn = v; }); proc->setLoop (v, cp->project().loopA, cp->project().loopB); });
    pp.slider ("Loop start (s)", "", 0, 3600, 0, 0.01, [=] { return cp->project().loopA; }, [=] (double v) { proc->store.edit ("Loop start", [=] (Project& p) { p.loopA = v; }, "loopa"); proc->setLoop (cp->project().loopOn, v, cp->project().loopB); });
    pp.slider ("Loop end (s)", "", 0, 3600, 8, 0.01, [=] { return cp->project().loopB; }, [=] (double v) { proc->store.edit ("Loop end", [=] (Project& p) { p.loopB = v; }, "loopb"); proc->setLoop (cp->project().loopOn, cp->project().loopA, v); });
    pp.buttons ({ { "Add marker at playhead", [=] { const double t = proc->clockNow(); proc->store.edit ("Add marker", [=] (Project& p) { p.markers.push_back ({ t, "Marker " + std::to_string (p.markers.size() + 1) }); }); if (cp->rebuild) cp->rebuild(); } },
                  { "Clear markers", [=] { proc->store.edit ("Clear markers", [=] (Project& p) { p.markers.clear(); }); if (cp->rebuild) cp->rebuild(); } } });
    int i = 0; for (auto& m : c.project().markers) { const int idx = i++; pp.text (juce::String (m.t, 2) + " s", "Marker name. Click the marker in the timeline to jump to it.", [=] { auto& v = cp->project().markers; return idx < (int) v.size() ? juce::String (v[(size_t) idx].name) : juce::String(); }, [=] (juce::String s) { proc->store.edit ("Rename marker", [=] (Project& p) { if (idx < (int) p.markers.size()) p.markers[(size_t) idx].name = s.toStdString(); }, "mk" + juce::String (idx)); }); }
}

void buildSettingsPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c; auto& us = userSettings();
    if (c.match ("theme accent colour appearance", "Appearance"))
    {
        pp.section ("Appearance", false, "T:look");
        pp.choice ("Theme", "Dark, Extra dark, Monochrome or High contrast.", juce::StringArray::fromTokens (themeNames(), "|", ""), [&us] { return us.getIntValue ("theme", 0); }, [&us] (int i) { us.setValue ("theme", i); us.saveIfNeeded(); if (auto h = themeChangedHook()) h (i, juce::Colour::fromString (us.getValue ("accent", "ff7c5cff"))); });
        pp.colour ("Accent colour", "Highlight colour used by sliders and buttons.", [&us] { return "#" + juce::Colour::fromString (us.getValue ("accent", "ff7c5cff")).toDisplayString (false).toLowerCase(); },
                   [&us] (juce::String h) { auto col = juce::Colour::fromString ("ff" + h.trimCharactersAtStart ("#")); us.setValue ("accent", col.toString()); us.saveIfNeeded(); if (auto f = themeChangedHook()) f (us.getIntValue ("theme", 0), col); }, "#7c5cff");
    }
    if (c.match ("quality frame rate fps preview proxy performance", "Performance"))
    {
        pp.section ("Preview and performance", false, "T:perf");
        pp.choice ("Preview quality", "Lower quality renders fewer pixels - use it if your computer struggles while producing.", juce::StringArray::fromTokens (qualityNames(), "|", ""), [=] { return (int) proc->apvts.getRawParameterValue ("quality")->load(); }, [=] (int i) { setParamPlain (*proc, "quality", (float) i); });
        pp.choice ("Preview frame rate", "", juce::StringArray::fromTokens (fpsNames(), "|", ""), [=] { return (int) proc->apvts.getRawParameterValue ("fps")->load(); }, [=] (int i) { setParamPlain (*proc, "fps", (float) i); });
        pp.choice ("Video preview width", "Videos are decoded at this width while previewing (export always uses full quality).", juce::StringArray { "480 px", "720 px", "960 px", "1280 px" }, [=] { int w = proc->media.getProxyWidth(); return w <= 480 ? 0 : w <= 720 ? 1 : w <= 960 ? 2 : 3; }, [=] (int i) { static const int w[] = { 480, 720, 960, 1280 }; proc->media.setProxyWidth (w[i]); userSettings().setValue ("proxyW", w[i]); });
    }
    if (c.match ("ffmpeg tools video export folder", "Tools"))
    {
        pp.section ("Tools", false, "T:tools");
        pp.info (FfTools::available() ? "FFmpeg found: " + FfTools::ffmpeg() : "FFmpeg NOT found - video layers and export need it. See docs/FFMPEG.md.");
        pp.buttons ({ { "Choose FFmpeg folder...", [=] { chooseFolder ("Folder containing ffmpeg and ffprobe", juce::File(), [=] (juce::File f) { FfTools::setFolder (f); userSettings().setValue ("ffmpegFolder", f.getFullPathName()); userSettings().saveIfNeeded(); if (cp->rebuild) cp->rebuild(); }); } },
                      { "Re-scan", [=] { FfTools::rescan(); if (cp->rebuild) cp->rebuild(); } } });
    }
    if (c.match ("keyboard shortcuts help keys", "Shortcuts"))
    {
        pp.section ("Keyboard shortcuts", true, "T:keys");
        for (auto s : { "Space - play / pause", "Home - go to start", "Ctrl+Z / Ctrl+Y - undo / redo", "Ctrl+S - save project", "Ctrl+O - open project", "Ctrl+D - duplicate layer", "Delete - delete layer", "F - fit preview", "G - toggle grid", "B - before / after split", "Ctrl+F - search parameters", "Right-click layers - more actions" }) pp.info (s);
    }
    pp.section ("About", true, "T:about");
    pp.info ("monie visuals - original code. See docs/LICENSES.md for third-party components.");
    pp.info (juce::String ("GPU: ") + c.preview.status().gpu);
}

// ============================================================================================ presets
static std::set<std::string> gLockedSections;
const char* presetExt (int k) { static const char* e[] = { ".monie", ".monifx", ".monitext", ".monipal", ".monimap", ".monicap" }; return e[k < 0 || k >= NumPresetKinds ? 0 : k]; }
const char* presetKindName (int k) { static const char* e[] = { "Project", "Effect chain", "Text style", "Palette", "Mapping", "Caption style" }; return e[k < 0 || k >= NumPresetKinds ? 0 : k]; }

static std::string propsToJson (const PropBag& b) { JsonWriter w; writeProps (w, b); return w.str(); }
void savePreset (UiCtx& c, int kind)
{
    UiCtx* cp = &c; std::string body; const Layer* L = c.project().find (c.selectedLayer);
    switch (kind)
    {
        case PresetProject: { Project p = c.project(); p.scene = c.proc.currentScene(); body = projectToJson (p); break; }
        case PresetFx: { const bool g = c.fxTarget == 1 || ! L; const auto& st = g ? c.project().fx : L->fx; JsonWriter w; w.beginArr(); for (auto& f : st) writeFx (w, f); w.endArr(); body = w.str(); break; }
        case PresetTextStyle: case PresetCaptionStyle: if (! L) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Select a text layer first", "Style presets are saved from the selected text or caption layer."); return; } body = propsToJson (L->props); break;
        case PresetPalette: body = c.project().gradient.toText(); break;
        case PresetMapping: body = c.project().mapping.toText(); break;
    }
    chooseFile (juce::String ("Save ") + presetKindName (kind) + " preset", presetsDir().getChildFile (juce::String ("preset") + presetExt (kind)), juce::String ("*") + presetExt (kind), true, [=] (juce::File f)
    { f.replaceWithText (juce::String::fromUTF8 (body.c_str())); if (cp->rebuild) cp->rebuild(); });
}
bool loadPresetFile (UiCtx& c, const juce::File& f, juce::String* err)
{
    auto fail = [&] (const juce::String& e) { if (err) *err = e; return false; };
    const auto ext = f.getFileExtension().toLowerCase(); const std::string t = f.loadFileAsString().toStdString(); MonieProcessor* proc = &c.proc;
    if (ext == ".monie")
    {
        Project p; std::string e; if (! projectFromJson (t, p, &e)) return fail ("Not a valid monie visuals project: " + juce::String (e));
        p.scene = std::clamp (p.scene, 0, kNumScenes - 1); const int sc = p.scene; const auto mem = p.sceneMem; for (auto& l : p.layers) if (! l.media.empty()) proc->media.request (l.media);
        proc->store.replace (p, true); proc->selectScene (sc); auto it = mem.find (sc); if (it != mem.end()) for (int i = 0; i < 8; ++i) setParamPlain (*proc, "sk" + juce::String (i), it->second[(size_t) i]);
        return true;
    }
    if (ext == ".monifx")
    {
        JVal r; if (! JsonParser::parse (t, r) || r.t != JVal::Arr) return fail ("Not a valid effect chain.");
        std::vector<FxInst> v; for (auto& x : r.a) v.push_back (pj::readFx (x)); const bool g = c.fxTarget == 1 || c.selectedLayer < 0; const int lid = c.selectedLayer;
        proc->store.edit ("Load effect chain", [=] (Project& p) { auto* s = g ? &p.fx : (p.find (lid) ? &p.find (lid)->fx : nullptr); if (! s) return; for (auto f : v) { f.id = p.newId(); s->push_back (f); } });
        return true;
    }
    if (ext == ".monitext" || ext == ".monicap")
    {
        JVal r; if (! JsonParser::parse (t, r) || r.t != JVal::Obj) return fail ("Not a valid style preset."); const Layer* L = c.project().find (c.selectedLayer); if (! L) return fail ("Select a text or caption layer first.");
        const int id = L->id; const auto type = L->type; auto data = std::make_shared<JVal> (r);
        proc->store.edit ("Load style", [=] (Project& p) { auto* l = p.find (id); if (! l) return; const auto& sch = schemaFor (type);
            if (auto* f = data->get ("f")) for (auto& kv : f->o) if (auto* d = findDef (sch, kv.first); d && d->kind != PKind::Text && d->id != "x" && d->id != "y") l->props.setF (*d, (float) pj::N (kv.second));
            if (auto* s = data->get ("s")) for (auto& kv : s->o) if (auto* d = findDef (sch, kv.first); d && d->id != "text" && d->id != "fillImage") l->props.setS (*d, kv.second.s); });
        return true;
    }
    if (ext == ".monipal") { Gradient g; if (! Gradient::fromText (f.loadFileAsString().trim().toStdString(), g)) return fail ("Not a valid palette."); proc->store.edit ("Load palette", [=] (Project& p) { p.gradient = g; }); return true; }
    if (ext == ".monimap") { auto m = MappingEngine::fromText (t); proc->store.edit ("Load mapping", [=] (Project& p) { for (auto& x : m.maps) p.mapping.add (x); }); return true; }
    return fail ("Unknown preset type.");
}

void buildPresetPanel (PropPanel& pp, UiCtx& c)
{
    MonieProcessor* proc = &c.proc; UiCtx* cp = &c;
    pp.section ("Save presets", false, "R:save");
    pp.info ("Presets are plain files in your MonieVisuals/Presets folder and appear in the browser on the left.");
    for (int k = 0; k < NumPresetKinds; k += 2)
    {
        std::vector<std::pair<juce::String, std::function<void()>>> row;
        for (int j = k; j < k + 2 && j < NumPresetKinds; ++j) row.push_back ({ juce::String ("Save ") + presetKindName (j) + "...", [=] { savePreset (*cp, j); } });
        pp.buttons (row);
    }
    pp.section ("Randomise (with locks)", false, "R:rand");
    pp.info ("Tick a group to LOCK it - randomise will leave locked groups alone.");
    std::vector<std::string> order; for (auto& d : globalDefs()) if (std::find (order.begin(), order.end(), d.section) == order.end()) order.push_back (d.section);
    for (auto& sec : order) pp.toggle ("Lock " + juce::String (sec), "", [sec] { return gLockedSections.count (sec) > 0; }, [sec] (bool v) { if (v) gLockedSections.insert (sec); else gLockedSections.erase (sec); });
    pp.toggle ("Lock scene choice", "", [] { return gLockedSections.count ("#scene") > 0; }, [] (bool v) { if (v) gLockedSections.insert ("#scene"); else gLockedSections.erase ("#scene"); });
    pp.toggle ("Lock palette", "", [] { return gLockedSections.count ("#palette") > 0; }, [] (bool v) { if (v) gLockedSections.insert ("#palette"); else gLockedSections.erase ("#palette"); });
    pp.buttons ({ { "Randomise unlocked", [=]
    {
        if (! gLockedSections.count ("#scene")) proc->selectScene ((int) (rnd01() * kNumScenes) % kNumScenes);
        if (! gLockedSections.count ("#scene")) for (int i = 0; i < 8; ++i) setParamPlain (*proc, "sk" + juce::String (i), rnd01());
        for (auto& d : globalDefs()) { if (gLockedSections.count (d.section)) continue; const juce::String id = d.id;
            if (id == "master" || id == "xfade" || id == "imgspeed" || id == "speed" || id == "zoom" || id == "rotation") continue;     // keep the picture usable
            const bool subtle = juce::String (d.section) != "Stylize" && juce::String (d.section) != "Color";
            const float v = subtle ? d.defV + (rnd01() - .5f) * .2f * (d.maxV - d.minV) : (rnd01() < .35f ? d.minV + rnd01() * .45f * (d.maxV - d.minV) : d.defV);
            setParamPlain (*proc, id, juce::jlimit (d.minV, d.maxV, v)); }
        if (! gLockedSections.count ("#palette")) proc->store.edit ("Random palette", [=] (Project& p) { p.gradient = builtinPalettes()[(size_t) (rnd01() * 23.f)]; });
    } }, { "Reset all globals", [=] { for (auto& d : globalDefs()) setParamPlain (*proc, d.id, d.defV); } } });
    pp.section ("Compare A / B", false, "R:ab");
    pp.buttons ({ { "Store A", [=] { storeAB (*proc, 0); } }, { "Store B", [=] { storeAB (*proc, 1); } }, { "Recall A", [=] { recallAB (*proc, 0); } }, { "Recall B", [=] { recallAB (*proc, 1); } } });
}
}
