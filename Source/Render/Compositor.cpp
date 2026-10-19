#include "Compositor.h"
#include <BinaryData.h>
#include <cmath>

using namespace juce::gl;

namespace monie
{
namespace
{
inline int gidx (const char* id) { const auto& g = globalDefs(); for (size_t i = 0; i < g.size(); ++i) if (std::string (g[i].id) == id) return (int) i; return 0; }
inline RGB hexOr (const std::string& h, RGB d) { RGB c; return parseHex (h, c) ? c : d; }
inline float rad (float deg) { return deg * 3.14159265f / 180.f; }
inline float hashf (float x) { float s = std::sin (x * 12.9898f) * 43758.5453f; return s - std::floor (s); }
}

// ------------------------------------------------------------------ FBO helper
bool Compositor::Fbo::ensure (juce::OpenGLContext& c, int W, int H, bool nearest)
{
    W = std::max (W, 2); H = std::max (H, 2);
    if (fb.isValid() && w == W && h == H) return true;
    fb.release(); if (! fb.initialise (c, W, H)) { w = h = 0; return false; }
    w = W; h = H;
    glBindTexture (GL_TEXTURE_2D, fb.getTextureID());
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    clear (1.f); return true;
}
void Compositor::Fbo::clear (float a)
{
    if (! fb.isValid()) return;
    fb.makeCurrentRenderingTarget(); glClearColor (0, 0, 0, a); glClear (GL_COLOR_BUFFER_BIT); fb.releaseAsRenderingTarget();
}

// ------------------------------------------------------------------ lifetime
Compositor::Compositor (MediaPool& p) : media (p) { thumbs.resize ((size_t) kNumScenes); }
Compositor::~Compositor() {}

void Compositor::buildPrograms()
{
    juce::String err;
    const juce::String vs (BinaryData::pass_vert, (size_t) BinaryData::pass_vertSize);
    auto mk = [&] (GlProgram& p, const char* name, const char* src, int size)
    {
        juce::String e; if (! p.build (*ctx, vs, juce::String (src, (size_t) size), e)) err += juce::String ("[") + name + "] " + e + "\n";
    };
    mk (progScene, "scene", BinaryData::scene_frag, BinaryData::scene_fragSize);
    mk (progFx, "effects", BinaryData::fx_frag, BinaryData::fx_fragSize);
    mk (progLayer, "layer", BinaryData::layer_frag, BinaryData::layer_fragSize);
    mk (progComp, "composite", BinaryData::composite_frag, BinaryData::composite_fragSize);
    mk (progRd, "reaction-diffusion", BinaryData::rd_frag, BinaryData::rd_fragSize);
    mk (progFinal, "final", BinaryData::final_frag, BinaryData::final_fragSize);
    setStatusError (err);
    gpuOk = err.isEmpty();
}

void Compositor::init (juce::OpenGLContext& c)
{
    ctx = &c; buildPrograms();
    const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenBuffers (1, &vbo); glBindBuffer (GL_ARRAY_BUFFER, vbo); glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW); glBindBuffer (GL_ARRAY_BUFFER, 0);
    auto mkTex = [] (unsigned& t, int w, int h, const void* data, bool lin)
    {
        glGenTextures (1, &t); glBindTexture (GL_TEXTURE_2D, t);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lin ? GL_LINEAR : GL_NEAREST); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lin ? GL_LINEAR : GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    };
    mkTex (specTex, 64, 2, specBytes, true);
    std::vector<unsigned char> g (256 * 4, 255); builtinPalettes()[0].bake (g.data()); mkTex (gradTex, 256, 1, g.data(), true);
    glBindTexture (GL_TEXTURE_2D, 0);
    {
        const juce::ScopedLock sl (statusLock);
        st.gpu = juce::String::fromUTF8 ((const char*) glGetString (GL_RENDERER));
    }
    rdReset = true; thumbNext = 0; thumbCount = 0;
}

void Compositor::releaseTex (Tex& t) { if (t.id != 0) { glDeleteTextures (1, &t.id); t = Tex(); } }

void Compositor::shutdown()
{
    gpuOk = false;
    for (auto* f : { &sceneA, &sceneB, &sceneMix, &xfade, &compA, &compB, &layerA, &layerB, &fxA, &fxB, &rd[0], &rd[1], &offFbo, &thumbFbo, &thumbPrev }) { f->fb.release(); f->w = f->h = 0; }
    fxHist.clear();
    for (auto& kv : mediaTex) releaseTex (kv.second); mediaTex.clear();
    for (auto& kv : textCache) { releaseTex (kv.second.a); releaseTex (kv.second.b); } textCache.clear();
    texA = texB = texC = texD = Tex();
    pathA.clear(); pathB.clear(); pathC.clear(); pathD.clear();
    progScene.p.reset(); progFx.p.reset(); progLayer.p.reset(); progComp.p.reset(); progRd.p.reset(); progFinal.p.reset();
    if (vbo) { glDeleteBuffers (1, &vbo); vbo = 0; }
    if (specTex) { glDeleteTextures (1, &specTex); specTex = 0; }
    if (gradTex) { glDeleteTextures (1, &gradTex); gradTex = 0; }
    gradKey.clear(); lastScene = -1; cw = ch = 0;
}

void Compositor::resetState()
{
    for (auto* f : { &sceneA, &sceneB, &sceneMix, &xfade }) f->clear (1.f);
    for (auto& kv : fxHist) kv.second->clear (0.f);
    rdReset = true; sceneFade = 0; mapProj = nullptr;
}

// ------------------------------------------------------------------ small GL helpers
void Compositor::drawQuad (GlProgram& pr)
{
    glBindBuffer (GL_ARRAY_BUFFER, vbo); glEnableVertexAttribArray ((GLuint) pr.pos);
    glVertexAttribPointer ((GLuint) pr.pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr); glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray ((GLuint) pr.pos); glBindBuffer (GL_ARRAY_BUFFER, 0);
}
void Compositor::bind (int unit, unsigned tex) { glActiveTexture ((GLenum) (GL_TEXTURE0 + unit)); glBindTexture (GL_TEXTURE_2D, tex); }

void Compositor::uploadRGBA (Tex& t, const uint8_t* px, int w, int h, uint64_t ver, bool nearest)
{
    if (t.id == 0)
    {
        glGenTextures (1, &t.id); glBindTexture (GL_TEXTURE_2D, t.id);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else glBindTexture (GL_TEXTURE_2D, t.id);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    if (t.w == w && t.h == h) glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    else glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    t.w = w; t.h = h; t.ver = ver;
}

Compositor::Tex* Compositor::mediaTexture (const std::string& path, bool video, double mediaTime, bool exact, float& aspect)
{
    PixelPtr px = video ? media.videoFrame (path, mediaTime, exact) : media.image (path);
    if (! px || px->w <= 0) { media.request (path); return nullptr; }
    auto& t = mediaTex[path]; t.used = frameNo;
    if (t.id == 0 || t.ver != px->version) uploadRGBA (t, px->rgba.data(), px->w, px->h, px->version);
    aspect = (float) px->w / (float) px->h; return &t;
}

Compositor::TextEntry* Compositor::textTexture (const TextRequest& rq)
{
    auto it = textCache.find (rq);
    if (it == textCache.end())
    {
        if (textCache.size() > 40)
        {   // evict the least recently used entry
            auto victim = textCache.begin(); for (auto i = textCache.begin(); i != textCache.end(); ++i) if (i->second.lastUse < victim->second.lastUse) victim = i;
            releaseTex (victim->second.a); releaseTex (victim->second.b); textCache.erase (victim);
        }
        TextEntry e; e.bm = rasteriseText (rq);
        if (e.bm.w > 0 && ! e.bm.masksA.empty()) { uploadRGBA (e.a, e.bm.masksA.data(), e.bm.w, e.bm.h, 1); uploadRGBA (e.b, e.bm.masksB.data(), e.bm.w, e.bm.h, 1); e.bm.masksA.clear(); e.bm.masksA.shrink_to_fit(); e.bm.masksB.clear(); e.bm.masksB.shrink_to_fit(); }
        it = textCache.emplace (rq, std::move (e)).first;
    }
    it->second.lastUse = frameNo; return &it->second;
}

void Compositor::updateGradient (const Gradient& g)
{
    const std::string key = g.toText(); if (key == gradKey) return; gradKey = key;
    unsigned char lut[256 * 4]; g.bake (lut); glBindTexture (GL_TEXTURE_2D, gradTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, lut);
}

void Compositor::setAudioUniforms (GlProgram& p, const Drive& d)
{
    p.f1 ("a_bass", d.bass); p.f1 ("a_lmid", d.lmid); p.f1 ("a_mid", d.mid); p.f1 ("a_hmid", d.hmid); p.f1 ("a_treb", d.treb);
    p.f1 ("a_beat", d.beat); p.f1 ("a_rms", d.rms); p.f1 ("a_flux", d.flux); p.f1 ("a_trans", d.trans);
}

void Compositor::copyPass (Fbo& dst, unsigned srcTex)
{
    pass (dst, progComp, [&] { bind (0, srcTex); bind (1, srcTex); progComp.i1 ("u_dst", 0); progComp.i1 ("u_layer", 1); progComp.f1 ("u_opacity", 1.f); progComp.i1 ("u_blend", 0); });
}
void Compositor::compositePass (Fbo& dst, unsigned dstTex, unsigned layerTex, float opacity, int blend)
{
    pass (dst, progComp, [&] { bind (0, dstTex); bind (1, layerTex); progComp.i1 ("u_dst", 0); progComp.i1 ("u_layer", 1); progComp.f1 ("u_opacity", opacity); progComp.i1 ("u_blend", blend); });
}

// ------------------------------------------------------------------ effects
void Compositor::fxPass (const FxInst& fx, Fbo& dst, unsigned srcTex, const FrameInputs& in, int W, int H)
{
    const FxDef* def = fxDef (fx.type);
    float amt = fx.amount * fx.blend;
    if (fx.audioSrc > 0 && fx.audioSens > 0.001f) { const float r = std::min (1.f, fx.audioSens), lvl = in.drive.bySource (fx.audioSrc); amt *= (1.f - r) + r * std::min(1.f, lvl * 1.6f); }
    Fbo* hist = nullptr;
    if (fx.type == 19 || fx.type == 20)
    {
        auto& h = fxHist[fx.id]; if (! h) h = std::make_unique<Fbo>(); h->ensure (*ctx, W, H); hist = h.get();
    }
    const RGB col = hexOr (fx.color, { 1, 1, 1 });
    pass (dst, progFx, [&]
    {
        bind (0, srcTex); bind (1, hist ? hist->tex() : srcTex); bind (2, gradTex);
        progFx.i1 ("u_src", 0); progFx.i1 ("u_hist", 1); progFx.i1 ("u_grad", 2);
        progFx.i1 ("u_type", fx.type); progFx.f2 ("u_res", (float) W, (float) H); progFx.f1 ("u_time", simTime); progFx.f1 ("u_amt", std::clamp (amt, 0.f, 1.f));
        progFx.f4 ("u_p", fx.p[0], fx.p[1], fx.p[2], fx.p[3]); progFx.f3 ("u_col", col.r, col.g, col.b);
        progFx.f1 ("a_bass", in.drive.bass); progFx.f1 ("a_beat", in.drive.beat); progFx.f1 ("a_treb", in.drive.treb); progFx.f1 ("a_rms", in.drive.rms); progFx.f1 ("a_flux", in.drive.flux);
    });
    (void) def;
    if (hist) copyPass (*hist, dst.tex());
}

// ------------------------------------------------------------------ reaction-diffusion
void Compositor::rdStep (const FrameInputs& in, const float* k)
{
    rd[0].ensure (*ctx, 256, 256, true); rd[1].ensure (*ctx, 256, 256, true);
    const int steps = std::clamp ((int) std::lround (k[1] * 4.f * (in.quality >= 2 ? 1.f : .6f)), 2, 40);
    const float var = k[0] - .5f, feed = k[2] + var * 0.006f, kill = k[3] + var * 0.002f;
    for (int i = 0; i < steps; ++i)
    {
        const bool init = rdReset && i == 0; if (init) rdReset = false;
        float sx = 0, sy = 0, sr = 0;
        if (i == 0 && in.drive.beatNow && k[5] > 0.05f) { sx = hashf ((float) in.drive.beatId * 1.7f); sy = hashf ((float) in.drive.beatId * 3.1f + 5.f); sr = 0.015f * k[5]; }
        Fbo& src = rd[rdIdx]; Fbo& dst = rd[1 - rdIdx];
        pass (dst, progRd, [&] { bind (0, src.tex()); progRd.i1 ("u_state", 0); progRd.f2 ("u_texel", 1.f / 256.f, 1.f / 256.f); progRd.f1 ("u_feed", feed); progRd.f1 ("u_kill", kill); progRd.f1 ("u_dt", 1.f); progRd.f3 ("u_seed", sx, sy, sr); progRd.f1 ("u_init", init ? 1.f : 0.f); });
        rdIdx = 1 - rdIdx;
    }
}

// ------------------------------------------------------------------ scene pass
void Compositor::scenePass (const FrameInputs& in, Fbo& out, Fbo& prev, int W, int H)
{
    const int sc = std::clamp (in.scene, 0, kNumScenes - 1); const auto& si = sceneCatalogue()[(size_t) sc];
    float k[8]; for (int i = 0; i < 8; ++i) { k[i] = si.toNative (i, in.knobs[i]); auto it = mappedNow.find ("scene:k" + std::to_string (i)); if (it != mappedNow.end()) k[i] = std::clamp (k[i] + it->second, si.knobs[(size_t) i].minV, si.knobs[(size_t) i].maxV); }
    if (sc == 21) rdStep (in, k);
    const auto& d = in.drive;
    for (int i = 0; i < 64; ++i)
    {
        specBytes[i * 4] = (unsigned char) std::clamp ((int) (d.bands[i] * 255.f), 0, 255); specBytes[i * 4 + 3] = 255;
        specBytes[(64 + i) * 4] = (unsigned char) std::clamp ((int) (d.wave[i] * 255.f), 0, 255); specBytes[(64 + i) * 4 + 3] = 255;
    }
    glBindTexture (GL_TEXTURE_2D, specTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 64, 2, GL_RGBA, GL_UNSIGNED_BYTE, specBytes);
    const bool hasImg = texA.id != 0;
    pass (out, progScene, [&]
    {
        bind (0, prev.tex()); bind (1, texA.id); bind (2, sc == 29 ? texB.id : (texB.id ? texB.id : texA.id)); bind (3, specTex); bind (4, gradTex); bind (5, rd[rdIdx].tex()); bind (6, texC.id); bind (7, texD.id);
        progScene.i1 ("u_prev", 0); progScene.i1 ("u_imgA", 1); progScene.i1 ("u_imgB", 2); progScene.i1 ("u_spec", 3); progScene.i1 ("u_grad", 4); progScene.i1 ("u_rd", 5); progScene.i1 ("u_imgC", 6); progScene.i1 ("u_imgD", 7);
        progScene.i1 ("u_scene", sc); progScene.f1 ("u_time", simTime); progScene.f2 ("u_res", (float) W, (float) H);
        progScene.fv ("u_k[0]", k, 8);
        setAudioUniforms (progScene, d); progScene.f1 ("u_beatAge", d.beatAge); progScene.f1 ("u_beatId", (float) (d.beatId % 1000));
        progScene.f1 ("u_hasImg", hasImg ? 1.f : 0.f); progScene.f1 ("u_blend", sc == 29 ? 0.f : (texB.id ? imgBlend : 0.f));
        progScene.f1 ("u_aspA", aspA); progScene.f1 ("u_aspB", sc == 29 ? aspB : (texB.id ? aspB : aspA)); progScene.f1 ("u_aspC", aspC); progScene.f1 ("u_aspD", aspD);
        progScene.f1 ("u_hasC", texC.id ? 1.f : 0.f); progScene.f1 ("u_hasD", texD.id ? 1.f : 0.f);
        const float* g = in.globals;
        progScene.f1 ("u_palShift", g[gidx ("palCycle")] * simTime * 0.03f + g[gidx ("reactColor")] * d.rms * 0.2f);
        progScene.f1 ("u_zoom", g[gidx ("zoom")]); progScene.f1 ("u_rot", rad (g[gidx ("rotation")]));
    });
}

// ------------------------------------------------------------------ layers
struct PrimDesc
{
    int mode = 0; unsigned tex1 = 0, tex2 = 0, fillTex = 0; float cx = 0, cy = 0, hx = 1, hy = 1, rot = 0, skx = 0, sky = 0, tx = 0, ty = 0;
    float crop[4] {}; float opacity = 1, alpha = 1; int blend = 0; float aud = 0;
    float bright = 1, contrast = 1, sat = 1, gamma = 1, hue = 0, cmap = 0, blur = 0, sharp = 0, dist = 0, mscale = 2, mspeed = 1, disp = 0, noise = 0, rgb = 0;
    RGB fillC, fill2C, o1C, o2C, shC, glowC, bgC, hiC; float gradOn = 0, gradAngle = 90, gradAnim = 0, cycle = 0, useFill = 0, shOp = 0, glow = 0, bgOp = 0, shOffX = 0, shOffY = 0;
    float reveal = 1, waveAmp = 0, waveFreq = 12, wavePhase = 0, shake = 0, glitch = 0, aBlur = 0, aRgb = 0, hueAnim = 0, glowBoost = 0;
};

void Compositor::runPipeline (const FrameInputs& in, int W, int H, bool offline, bool heavyDue, unsigned& outTex, unsigned& sceneTexOut)
{
    (void) heavyDue; const Project& prj = *in.project; const float* g = in.globals;
    const float aspect = (float) W / (float) H; boundsBuild.clear();
    int missing = 0; std::vector<juce::String> fbs, missFiles;

    // ---- mapping engine (state lives here; reset when the mapping definition changes) ----
    if (mapProj != &prj) { const std::string t = prj.mapping.toText(); if (t != mapText) { mapEngine = MappingEngine::fromText (t); mapText = t; } mapProj = &prj; }
    mappedNow = mapEngine.process (in.drive.features(), std::max (in.dt, 1e-4f));

    updateGradient (prj.gradient);

    // ---- scene images (main with crossfade, plus the collage set) ----
    {
        auto fetchTex = [&] (const std::string& path, Tex& tex, float& asp)
        { if (path.empty()) { tex = Tex(); return; } float a = 1; if (auto* t = mediaTexture (path, false, 0, false, a)) { tex = *t; asp = a; } else tex = Tex(); };
        if (prj.sceneImage != pathA) { if (texA.id != 0 && ! prj.sceneImage.empty()) { texB = texA; aspB = aspA; imgBlend = 1.f; } pathA = prj.sceneImage; }
        fetchTex (pathA, texA, aspA);
        if (in.scene == 29)
        {
            auto at = [&] (size_t i) { return i < prj.collageImages.size() ? prj.collageImages[i] : std::string(); };
            fetchTex (at (0), texB, aspB); fetchTex (at (1), texC, aspC); fetchTex (at (2), texD, aspD);
        }
        else { texC = texD = Tex(); if (imgBlend <= 0.f) texB = Tex(); }
    }

    // ---- scene ----
    if (in.scene != lastScene)
    {
        if (lastScene >= 0 && sceneA.fb.isValid()) { xfade.ensure (*ctx, W, H); copyPass (xfade, (sceneWrite == 0 ? sceneA : sceneB).tex()); sceneFade = 1.f; }
        sceneA.clear (1.f); sceneB.clear (1.f); rdReset = true; lastScene = in.scene;
    }
    {
        Fbo& out = sceneWrite == 0 ? sceneB : sceneA; Fbo& prev = sceneWrite == 0 ? sceneA : sceneB;
        scenePass (in, out, prev, W, H); sceneWrite = 1 - sceneWrite;
    }
    unsigned sceneTex = (sceneWrite == 0 ? sceneA : sceneB).tex();      // sceneWrite was flipped: the other buffer is the newest output
    if (sceneFade > 0.001f) { sceneMix.ensure (*ctx, W, H); compositePass (sceneMix, sceneTex, xfade.tex(), sceneFade, 0); sceneTex = sceneMix.tex(); }
    sceneTexOut = sceneTex;

    // ---- mapped value helper ----
    auto layerP = [&] (const Layer& L, const char* id, double localT) -> float
    {
        float v = L.fAt (id, localT);
        auto it = mappedNow.find ("layer:" + std::to_string (L.id) + ":" + id); if (it != mappedNow.end()) v += it->second;
        if (auto* d = findDef (schemaFor (L.type), id)) if (d->kind == PKind::Slider) v = std::clamp (v, d->minV, d->maxV);
        return v;
    };

    // ---- captions text (merged manual + automatic) ----
    if (mergedProj != &prj || mergedCaptionsOn != prj.captionsOn) { mergedLyrics = mergeDocs (prj.lyrics, prj.autoLyrics); mergedProj = &prj; mergedCaptionsOn = prj.captionsOn; }

    unsigned cur = sceneTex; Fbo* dstC = &compA; Fbo* otherC = &compB;
    compA.ensure (*ctx, W, H); compB.ensure (*ctx, W, H); layerA.ensure (*ctx, W, H); layerB.ensure (*ctx, W, H);
    for (const auto& L : prj.layers)
    {
        if (! L.visible) continue;
        if (in.clock < L.clipStart || (L.clipLen >= 0 && in.clock >= L.clipStart + L.clipLen)) continue;
        const double localT = in.clock - L.clipStart, layerLen = L.clipLen;
        std::vector<PrimDesc> prims;
        AnimCtx actx; actx.t = localT; actx.layerLen = layerLen; actx.beatEnv = in.drive.beat; for (int i = 1; i < 11; ++i) actx.audio[i] = in.drive.bySource (i);
        auto P = [&] (const char* id) { return layerP (L, id, localT); };
        auto colorOf = [&] (const char* id, RGB d) { return hexOr (L.s (id), d); };

        if (L.type == LayerType::Image || L.type == LayerType::Video)
        {
            if (L.media.empty()) continue;
            const bool vid = L.type == LayerType::Video; double mt = 0;
            if (vid)
            {
                const auto info = media.info (L.media); const double sp = P ("speed"), st0 = L.f ("start"); double en = L.f ("end"); if (en <= st0 + 0.01) en = info.duration > 0 ? info.duration : st0 + 3600;
                mt = st0 + localT * sp; if (L.f ("loop") > .5f) { const double len = std::max (0.05, en - st0); mt = st0 + std::fmod (localT * sp, len); } else mt = std::min (mt, en);
            }
            float asp = 1; Tex* t = mediaTexture (L.media, vid, mt, offline, asp);
            if (! t) { const auto info = media.info (L.media); if (info.missing || info.error.isNotEmpty()) { ++missing; missFiles.push_back (juce::String::fromUTF8 (L.media.c_str())); } continue; }
            AnimState S = evalAnims (L.anims, actx);
            PrimDesc d; d.mode = 0; d.tex1 = t->id; const int fit = (int) L.f ("fit");
            float bw, bh; const float ca = aspect;
            if (fit == 2) { bw = 2 * ca; bh = 2; } else if ((asp > ca) == (fit == 0)) { bw = 2 * ca; bh = 2 * ca / asp; } else { bh = 2; bw = 2 * asp; }
            const float sc = P ("scale") * S.scale; d.hx = bw * 0.5f * sc; d.hy = bh * 0.5f * sc;
            d.cx = P ("x") * ca + S.dx * 2.f; d.cy = P ("y") + S.dy * 2.f; d.rot = rad (P ("rot")) + S.rot; d.skx = S.skewX;
            d.crop[0] = P ("cropL"); d.crop[1] = P ("cropR"); d.crop[2] = P ("cropT"); d.crop[3] = P ("cropB");
            d.opacity = P ("opacity"); d.blend = (int) L.f ("blend"); d.alpha = S.alpha;
            d.bright = P ("brightness"); d.contrast = P ("contrast"); d.sat = P ("saturation"); d.gamma = P ("gamma"); d.hue = P ("hue"); d.cmap = P ("colormap");
            d.blur = std::clamp (P ("blur") + S.blur * 0.5f, 0.f, 1.f); d.sharp = P ("sharpen"); d.dist = P ("distortion"); d.mscale = P ("morphScale"); d.mspeed = P ("morphSpeed"); d.disp = P ("displace");
            d.noise = P ("noise"); d.rgb = P ("rgb") + S.rgb; d.aud = in.drive.bySource ((int) L.f ("audioSrc")) * P ("audioSens"); d.hueAnim = S.hue;
            prims.push_back (d);
        }
        else
        {   // text / caption
            const bool cap = L.type == LayerType::Caption;
            struct Line { std::string text; int active; float alpha, yoff; bool main; };
            std::vector<Line> lines; double tT = localT, tLen = layerLen;
            if (cap)
            {
                if (! prj.captionsOn) continue;
                CaptionSettings cs; cs.mode = (int) L.f ("mode"); cs.groupWords = (int) L.f ("groupWords"); cs.maxChars = (int) L.f ("maxChars"); cs.maxLines = (int) L.f ("maxLines");
                cs.showPrev = L.f ("showPrev") > .5f; cs.showNext = L.f ("showNext") > .5f; cs.delay = L.f ("delay"); cs.leadIn = L.f ("leadIn"); cs.minDur = L.f ("minDur"); cs.hold = L.f ("hold");
                cs.fadeIn = L.f ("fadeIn"); cs.fadeOut = L.f ("fadeOut"); cs.minConf = L.f ("minConf");
                const auto f = captionAt (mergedLyrics, in.clock, cs);
                if (! f.hasContent || f.text.empty()) continue;
                tT = in.clock - f.appear; tLen = f.length;
                const float ctxA = L.f ("ctxOpacity");
                if (! f.prevText.empty()) lines.push_back ({ f.prevText, -1, ctxA * f.alpha, +1.f, false });
                lines.push_back ({ f.text, L.f ("highlight") > .5f ? f.activeWord : -1, f.alpha, 0.f, true });
                if (! f.nextText.empty()) lines.push_back ({ f.nextText, -1, ctxA * f.alpha, -1.f, false });
            }
            else lines.push_back ({ L.s ("text"), -1, 1.f, 0.f, true });
            actx.t = tT; actx.layerLen = tLen; AnimState S = evalAnims (L.anims, actx);
            const float hPx = (float) H;
            for (auto& ln : lines)
            {
                TextRequest rq; rq.text = ln.text; rq.font = L.s ("font"); rq.style = (int) L.f ("style"); rq.sizePx = layerP (L, "size", localT) / 100.f * hPx;
                rq.letterSp = P ("letterSp"); rq.wordSp = P ("wordSp"); rq.lineSp = P ("lineSp"); rq.boxWpx = P ("boxW") * 0.5f * (float) W; rq.curve = P ("curve"); rq.align = (int) L.f ("align"); rq.vertical = L.f ("vertical") > .5f;
                const float px = hPx / 1080.f; rq.o1w = P ("o1w") * px; rq.o2w = P ("o2w") * px; rq.shBlur = P ("shBlur") * px; rq.shDist = P ("shDist") * px; rq.glowR = P ("glowR") * px;
                rq.bg = L.f ("bg") > .5f; rq.padX = P ("bgPadX") * px; rq.padY = P ("bgPadY") * px; rq.radius = P ("bgRadius") * px;
                rq.highlightWord = ln.active;
                auto* te = textTexture (rq); if (! te || te->a.id == 0) continue;
                if (te->bm.fontFallback) fbs.push_back (juce::String::fromUTF8 (rq.font.c_str()));
                PrimDesc d; d.mode = 1; d.tex1 = te->a.id; d.tex2 = te->b.id;
                const float audScale = 1.f + L.f ("audioScale") * in.drive.bass * 0.3f;
                const float sc = P ("scale") * S.scale * audScale * te->bm.quadScale;
                d.hx = (float) te->bm.w / hPx * sc; d.hy = (float) te->bm.h / hPx * sc;          // half size in aspect space: (px/2) * (2/H)
                const float lineStep = rq.sizePx * 1.25f / hPx * 2.f * P ("scale");
                d.cx = P ("x") * aspect + S.dx * 2.f; d.cy = P ("y") + S.dy * 2.f + ln.yoff * lineStep * (float) std::max (1, 1);
                d.rot = rad (P ("rot")) + S.rot; d.skx = P ("skewX") + S.skewX; d.sky = P ("skewY"); d.tx = P ("tiltY") * 0.5f; d.ty = P ("tiltX") * 0.5f;
                d.opacity = P ("opacity") * ln.alpha;
                float flick = 1.f; if (L.f ("audioFlicker") > 0.f) flick = 1.f - L.f ("audioFlicker") * in.drive.treb * 0.6f * hashf (std::floor ((float) in.clock * 30.f));
                d.alpha = S.alpha * flick; d.blend = (int) L.f ("blend");
                d.fillC = colorOf ("fill", { 1, 1, 1 }); d.fill2C = colorOf ("fill2", { 1, .16f, .43f }); d.o1C = colorOf ("o1c", { 0, 0, 0 }); d.o2C = colorOf ("o2c", { 1, .16f, .43f });
                d.shC = colorOf ("shC", { 0, 0, 0 }); d.glowC = colorOf ("glowC", { 0, .9f, 1 }); d.bgC = colorOf ("bgC", { 0, 0, 0 }); d.hiC = ln.active >= 0 ? colorOf ("hiC", { 1, .88f, .29f }) : d.fillC;
                d.gradOn = L.f ("gradient") > .5f ? 1.f : 0.f; d.gradAngle = P ("gradAngle"); d.gradAnim = P ("gradAnim"); d.cycle = P ("colorCycle");
                if (const std::string fi = L.s ("fillImage"); ! fi.empty()) { float a = 1; if (auto* t = mediaTexture (fi, false, 0, false, a)) { d.fillTex = t->id; d.useFill = 1.f; } }
                d.shOp = P ("shOp"); d.glow = P ("glow"); d.bgOp = L.f ("bg") > .5f ? P ("bgOp") : 0.f;
                const float sa = rad (P ("shAngle")); d.shOffX = std::cos (sa) * P ("shDist") * px / (float) te->bm.w; d.shOffY = std::sin (sa) * P ("shDist") * px / (float) te->bm.h;
                d.blur = P ("blur"); d.dist = P ("distortion"); d.noise = P ("noise"); d.rgb = P ("rgb");
                d.reveal = S.reveal; d.waveAmp = S.waveAmp + L.f ("audioWiggle") * in.drive.mid * 0.03f; d.waveFreq = S.waveFreq; d.wavePhase = S.wavePhase; d.shake = S.shake; d.glitch = S.glitch;
                d.aBlur = S.blur; d.aRgb = S.rgb; d.hueAnim = S.hue; d.glowBoost = S.glow;
                prims.push_back (d);
            }
        }
        for (auto& d : prims)
        {
            // ---- draw this primitive into the layer buffer (premultiplied, transparent) ----
            layerA.clear (0.f);
            pass (layerA, progLayer, [&]
            {
                bind (0, d.tex1); bind (1, d.tex2 ? d.tex2 : d.tex1); bind (2, d.fillTex ? d.fillTex : d.tex1); bind (3, gradTex);
                auto& P2 = progLayer; P2.i1 ("u_tex", 0); P2.i1 ("u_tex2", 1); P2.i1 ("u_fill", 2); P2.i1 ("u_grad", 3);
                P2.i1 ("u_mode", d.mode); P2.f2 ("u_res", (float) W, (float) H); P2.f1 ("u_time", simTime);
                P2.f2 ("u_ctr", d.cx, d.cy); P2.f2 ("u_half", std::max (d.hx, 1e-4f), std::max (d.hy, 1e-4f)); P2.f1 ("u_rot", d.rot); P2.f2 ("u_skew", d.skx, d.sky); P2.f2 ("u_tilt", d.tx, d.ty);
                P2.f4 ("u_crop", d.crop[0], d.crop[1], d.crop[2], d.crop[3]);
                P2.f1 ("u_bright", d.bright); P2.f1 ("u_contrast", d.contrast); P2.f1 ("u_sat", d.sat); P2.f1 ("u_gamma", d.gamma); P2.f1 ("u_hue", d.hue); P2.f1 ("u_cmap", d.cmap);
                P2.f1 ("u_blur", d.blur); P2.f1 ("u_sharp", d.sharp); P2.f1 ("u_dist", d.dist); P2.f1 ("u_mscale", d.mscale); P2.f1 ("u_mspeed", d.mspeed); P2.f1 ("u_disp", d.disp);
                P2.f1 ("u_noise", d.noise); P2.f1 ("u_rgb", d.rgb); P2.f1 ("u_aud", d.aud);
                P2.f3 ("u_fillC", d.fillC.r, d.fillC.g, d.fillC.b); P2.f3 ("u_fill2C", d.fill2C.r, d.fill2C.g, d.fill2C.b); P2.f3 ("u_o1C", d.o1C.r, d.o1C.g, d.o1C.b); P2.f3 ("u_o2C", d.o2C.r, d.o2C.g, d.o2C.b);
                P2.f3 ("u_shC", d.shC.r, d.shC.g, d.shC.b); P2.f3 ("u_glowC", d.glowC.r, d.glowC.g, d.glowC.b); P2.f3 ("u_bgC", d.bgC.r, d.bgC.g, d.bgC.b); P2.f3 ("u_hiC", d.hiC.r, d.hiC.g, d.hiC.b);
                P2.f1 ("u_gradOn", d.gradOn); P2.f1 ("u_gradAngle", d.gradAngle); P2.f1 ("u_gradAnim", d.gradAnim); P2.f1 ("u_cycle", d.cycle); P2.f1 ("u_useFillTex", d.useFill);
                P2.f1 ("u_shOp", d.shOp); P2.f1 ("u_glow", d.glow); P2.f1 ("u_bgOp", d.bgOp); P2.f2 ("u_shOff", d.shOffX, d.shOffY);
                P2.f1 ("u_reveal", d.reveal); P2.f1 ("u_waveAmp", d.waveAmp); P2.f1 ("u_waveFreq", d.waveFreq); P2.f1 ("u_wavePhase", d.wavePhase); P2.f1 ("u_shake", d.shake);
                P2.f1 ("u_glitch", d.glitch); P2.f1 ("u_aBlur", d.aBlur); P2.f1 ("u_aRgb", d.aRgb); P2.f1 ("u_alpha", d.alpha); P2.f1 ("u_hueAnim", d.hueAnim); P2.f1 ("u_glowBoost", d.glowBoost);
            });
            unsigned lt = layerA.tex(); Fbo* lo = &layerA; Fbo* lother = &layerB;
            if (! L.fxBypass) for (const auto& fx : L.fx) if (fx.enabled) { fxPass (fx, *lother, lt, in, W, H); lt = lother->tex(); std::swap (lo, lother); }
            compositePass (*dstC, cur, lt, std::clamp (d.opacity, 0.f, 1.f), d.blend);
            cur = dstC->tex(); std::swap (dstC, otherC);
        }
        if (! prims.empty())
        {
            const auto& d = prims.back(); LayerBound b; b.id = L.id; b.cx = d.cx; b.cy = d.cy; b.hx = d.hx; b.hy = d.hy; b.rot = d.rot;
            if (L.type != LayerType::Image && L.type != LayerType::Video) { /* text bitmaps include margins; shrink to the glyph box for a tighter frame */ }
            boundsBuild.push_back (b);
        }
    }
    (void) otherC;
    beforeFxTex = cur;

    // ---- global effect stack ----
    if (! prj.fxBypass)
    {
        fxA.ensure (*ctx, W, H); fxB.ensure (*ctx, W, H); Fbo* a = &fxA; Fbo* b = &fxB;
        for (const auto& fx : prj.fx) if (fx.enabled) { fxPass (fx, *a, cur, in, W, H); cur = a->tex(); std::swap (a, b); }
    }
    outTex = cur;
    {
        const juce::ScopedLock sl (statusLock);
        st.missingMedia = missing; st.fontFallbacks = fbs; st.missingFiles = missFiles; st.canvasW = W; st.canvasH = H; bounds = boundsBuild;
    }
    // evict the least recently used media textures to bound GPU memory (never one that is currently bound as a scene image)
    while (mediaTex.size() > 24)
    {
        auto victim = mediaTex.end();
        for (auto it = mediaTex.begin(); it != mediaTex.end(); ++it) if (it->second.id != texA.id && it->second.id != texB.id && it->second.id != texC.id && it->second.id != texD.id && (victim == mediaTex.end() || it->second.used < victim->second.used)) victim = it;
        if (victim == mediaTex.end()) break; releaseTex (victim->second); mediaTex.erase (victim);
    }
}

// ------------------------------------------------------------------ final
void Compositor::finalPass (const FrameInputs& in, unsigned tex, unsigned origTex, int vx, int vy, int vw, int vh, bool overlay, bool)
{
    const float* g = in.globals;
    glViewport (vx, vy, vw, vh); progFinal.use();
    bind (0, tex); bind (1, origTex); bind (2, gradTex);
    progFinal.i1 ("u_tex", 0); progFinal.i1 ("u_orig", 1); progFinal.i1 ("u_grad", 2);
    progFinal.f2 ("u_res", (float) vw, (float) vh); progFinal.f1 ("u_time", simTime);
    progFinal.f1 ("a_bass", in.drive.bass); progFinal.f1 ("a_mid", in.drive.mid); progFinal.f1 ("a_treb", in.drive.treb); progFinal.f1 ("a_beat", in.drive.beat); progFinal.f1 ("a_rms", in.drive.rms);
    const auto& defs = globalDefs();
    for (size_t i = 0; i < defs.size(); ++i)
        if (defs[i].shader)
        {
            float v = g[i]; auto it = mappedNow.find (std::string ("global:") + defs[i].id); if (it != mappedNow.end()) v = std::clamp (v + it->second, defs[i].minV, defs[i].maxV);
            progFinal.f1 ((std::string ("u_") + defs[i].id).c_str(), v);
        }
    progFinal.f1 ("u_ovl", overlay ? 1.f : 0.f); progFinal.f1 ("u_guides", (float) in.guides); progFinal.f1 ("u_split", in.split);
    float sel[4] = { 0, 0, -1, 0 }; float selRot = 0;
    if (overlay && in.selectedLayer >= 0) for (auto& b : boundsBuild) if (b.id == in.selectedLayer) { sel[0] = b.cx; sel[1] = b.cy; sel[2] = b.hx; sel[3] = b.hy; selRot = b.rot; }
    progFinal.f4 ("u_sel", sel[0], sel[1], sel[2], sel[3]); progFinal.f1 ("u_selRot", selRot);
    progFinal.f4 ("u_status", 0, 0, 0, 0);
    drawQuad (progFinal);
}

// ------------------------------------------------------------------ public render entry points
static const int kBuckets[] = { 360, 540, 720, 1080, 1440, 2160 };

void Compositor::renderLive (const FrameInputs& in, int vx, int vy, int vw, int vh)
{
    if (! gpuOk || ! in.project || vw < 4 || vh < 4) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001; const float dt = (float) std::clamp (now - lastNow, 0.0005, 0.1); lastNow = now;
    ++frameNo;
    // canvas aspect from the project; internal resolution is bucketed so text caches survive small window resizes
    const float aspect = canvasAspectOf (*in.project);
    const float want = (float) in.paneHeightPx * qualityScale (in.quality);
    int bh = kBuckets[5]; for (int b : kBuckets) if ((float) b >= want * 0.9f) { bh = b; break; }
    bh = std::min (bh, 1440);
    const int bw = (int) std::lround ((float) bh * aspect);
    if (bw != lastCw || bh != lastCh) { lastCw = bw; lastCh = bh; lastFinalTex = 0; }
    cw = bw; ch = bh;
    const float* g = in.globals; simTime += dt * g[gidx ("speed")];
    imgBlend = std::max (0.f, imgBlend - dt * g[gidx ("imgspeed")]);
    sceneFade = std::max (0.f, sceneFade - dt / std::max (0.05f, g[gidx ("xfade")]));

    static const float fpsTargets[] = { 30.f, 60.f, 1000.f };
    const double minDt = 1.0 / fpsTargets[std::clamp (in.fpsMode, 0, 2)];
    const bool due = (now - lastHeavy) >= minDt - 0.002 || lastFinalTex == 0;
    if (due)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const double fd = now - lastHeavy; lastHeavy = now;
        sceneA.ensure (*ctx, bw, bh); sceneB.ensure (*ctx, bw, bh);
        unsigned out = 0, sceneT = 0; FrameInputs fi = in; fi.dt = (float) std::max (fd, 0.001);
        runPipeline (fi, bw, bh, false, true, out, sceneT);
        lastFinalTex = out;
        const juce::ScopedLock sl (statusLock); st.frameMs = juce::Time::getMillisecondCounterHiRes() - t0; if (fd > 0) st.fps = st.fps * 0.9f + (float) (1.0 / fd) * 0.1f;
    }
    if (lastFinalTex != 0) finalPass (in, lastFinalTex, beforeFxTex, vx, vy, vw, vh, in.overlay, false);
}

bool Compositor::renderOffline (const FrameInputs& in, int w, int h, std::vector<uint8_t>& rgba)
{
    if (! gpuOk || ! in.project) return false;
    ++frameNo; cw = w; ch = h; const float* g = in.globals;
    simTime += in.dt * g[gidx ("speed")]; imgBlend = std::max (0.f, imgBlend - in.dt * g[gidx ("imgspeed")]); sceneFade = std::max (0.f, sceneFade - in.dt / std::max (0.05f, g[gidx ("xfade")]));
    if (! sceneA.ensure (*ctx, w, h) || ! sceneB.ensure (*ctx, w, h) || ! offFbo.ensure (*ctx, w, h)) return false;
    unsigned out = 0, sceneT = 0; runPipeline (in, w, h, true, true, out, sceneT);
    offFbo.fb.makeCurrentRenderingTarget(); glViewport (0, 0, w, h);
    finalPass (in, out, beforeFxTex, 0, 0, w, h, false, true);
    rgba.resize ((size_t) w * (size_t) h * 4);
    glPixelStorei (GL_PACK_ALIGNMENT, 1); glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    offFbo.fb.releaseAsRenderingTarget();
    // OpenGL rows are bottom-up: flip so row 0 is the top of the picture
    std::vector<uint8_t> row ((size_t) w * 4);
    for (int y = 0; y < h / 2; ++y) { auto* a = &rgba[(size_t) y * w * 4]; auto* b = &rgba[(size_t) (h - 1 - y) * w * 4]; std::copy (a, a + w * 4, row.begin()); std::copy (b, b + w * 4, a); std::copy (row.begin(), row.end(), b); }
    return true;
}

// ------------------------------------------------------------------ thumbnails
void Compositor::stepThumbnails()
{
    if (! gpuOk || thumbNext >= kNumScenes) return;
    const int W = 256, H = 144;
    thumbFbo.ensure (*ctx, W, H); thumbPrev.ensure (*ctx, W, H); thumbFbo.clear (1.f); thumbPrev.clear (1.f);
    rd[0].ensure (*ctx, 256, 256, true); rd[1].ensure (*ctx, 256, 256, true);
    // save live state we are about to disturb
    const auto savedTime = simTime; Tex sA = texA, sB = texB, sC = texC, sD = texD; const auto sMap = mappedNow; texA = texB = texC = texD = Tex(); mappedNow.clear();
    FrameInputs fi; fi.project = std::make_shared<const Project>(); fi.scene = thumbNext; float defaults[64]; const auto& gd = globalDefs(); for (size_t i = 0; i < gd.size(); ++i) defaults[i] = gd[i].defV; fi.globals = defaults;
    const auto& si = sceneCatalogue()[(size_t) thumbNext]; for (int i = 0; i < 8; ++i) fi.knobs[i] = si.defNorm (i);
    Drive d; for (int i = 0; i < 64; ++i) { const float x = (float) i / 63.f; d.bands[i] = std::clamp (0.75f * std::exp (-x * 3.f) + 0.25f * std::sin (x * 17.f) * std::sin (x * 17.f), 0.f, 1.f); d.wave[i] = 0.5f + 0.35f * std::sin (x * 25.f) * (0.6f + 0.4f * std::sin (x * 5.f)); }
    d.bass = .7f; d.lmid = .5f; d.mid = .5f; d.hmid = .4f; d.treb = .35f; d.rms = .5f; d.flux = .3f; d.trans = .2f; d.beat = .5f; d.beatAge = .2f; d.beatId = 3; fi.drive = d;
    rdReset = true;
    const int frames = (thumbNext == 21) ? 4 : 14;
    for (int f = 0; f < frames; ++f)
    {
        simTime = 3.0f + (float) f * 0.05f; fi.drive.beatNow = (f == 0);
        if (thumbNext == 21) { float kk[8]; for (int i = 0; i < 8; ++i) kk[i] = si.toNative (i, fi.knobs[i]); kk[1] = 8; for (int s = 0; s < 40; ++s) rdStep (fi, kk); }
        scenePass (fi, f % 2 == 0 ? thumbFbo : thumbPrev, f % 2 == 0 ? thumbPrev : thumbFbo, W, H);
    }
    Fbo& last = (frames - 1) % 2 == 0 ? thumbFbo : thumbPrev;
    std::vector<uint8_t> px ((size_t) W * H * 4);
    last.fb.makeCurrentRenderingTarget(); glPixelStorei (GL_PACK_ALIGNMENT, 1); glReadPixels (0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data()); last.fb.releaseAsRenderingTarget();
    juce::Image img (juce::Image::ARGB, W, H, false);
    { juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly); for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { const uint8_t* p = &px[((size_t) (H - 1 - y) * W + x) * 4]; bd.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2])); } }
    { const juce::ScopedLock sl (thumbLock); thumbs[(size_t) thumbNext] = img; }
    ++thumbNext; thumbCount = thumbNext;
    texA = sA; texB = sB; texC = sC; texD = sD; simTime = savedTime; mappedNow = sMap; rdReset = true;
}
} // namespace monie
