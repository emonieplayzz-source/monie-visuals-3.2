#pragma once
#include <juce_opengl/juce_opengl.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include "Core/AudioDrive.h"
#include "Core/GlobalDefs.h"
#include "Core/ProjectModel.h"
#include "Media/MediaPool.h"
#include "GlProgram.h"
#include "TextRaster.h"

namespace monie
{
struct FrameInputs
{
    std::shared_ptr<const Project> project;
    const float* globals = nullptr;                 // values in globalDefs() order
    int scene = 0; float knobs[8] {};               // normalised scene knobs (sk0..7)
    int quality = 2, fpsMode = 1;                   // 0 = 30 fps, 1 = 60 fps, 2 = display rate
    Drive drive; double clock = 0; float dt = 0.016f;
    // preview-only decorations
    bool overlay = false; unsigned guides = 0; float split = -1.f; int selectedLayer = -1;
    int paneHeightPx = 720;                          // used to pick the internal resolution
};
struct LayerBound { int id = 0; float cx = 0, cy = 0, hx = 0, hy = 0, rot = 0; };
struct RenderStatus
{
    juce::String shaderError, gpu; int missingMedia = 0; std::vector<juce::String> fontFallbacks, missingFiles;
    float fps = 0; int canvasW = 0, canvasH = 0; double frameMs = 0;
};

// All OpenGL work of the visual engine. Every method must be called on the GL thread (JUCE OpenGLContext render callback or
// OpenGLContext::executeOnGLThread). The audio thread never touches this class.
class Compositor
{
public:
    Compositor (MediaPool& pool);
    ~Compositor();
    void init (juce::OpenGLContext&);
    void shutdown();
    bool ready() const { return gpuOk; }

    // live: renders the scene when due and presents the final frame into the given viewport rectangle (pixels, GL origin bottom-left)
    void renderLive (const FrameInputs&, int vx, int vy, int vw, int vh);
    // offline: renders exactly one frame at the requested size and returns RGBA (top row first). Used by export.
    bool renderOffline (const FrameInputs&, int w, int h, std::vector<uint8_t>& rgba);
    // thumbnails of all 30 scenes (rendered progressively, one scene per call)
    void stepThumbnails();
    juce::Image sceneThumb (int index) const { const juce::ScopedLock sl (thumbLock); return index >= 0 && index < (int) thumbs.size() ? thumbs[(size_t) index] : juce::Image(); }
    int thumbsReady() const { return thumbCount.load(); }

    RenderStatus status() const { const juce::ScopedLock sl (statusLock); return st; }
    std::vector<LayerBound> layerBounds() const { const juce::ScopedLock sl (statusLock); return bounds; }
    void resetState();                                // clears feedback buffers, caches (call on seek / project load)
    void requestRdReset() { rdReset = true; }

private:
    struct Fbo { juce::OpenGLFrameBuffer fb; int w = 0, h = 0; bool ensure (juce::OpenGLContext&, int W, int H, bool nearest = false); void clear (float a = 1.f); GLuint tex() { return fb.getTextureID(); } };
    struct Tex { unsigned id = 0; int w = 0, h = 0; uint64_t ver = 0, used = 0; };
    struct TextEntry { Tex a, b; TextBitmap bm; uint64_t lastUse = 0; };

    void buildPrograms();
    void drawQuad (GlProgram&);
    template <class F> void pass (Fbo& dst, GlProgram& prog, F&& setup)
    {
        dst.fb.makeCurrentRenderingTarget(); juce::gl::glViewport (0, 0, dst.w, dst.h); prog.use(); setup(); drawQuad (prog); dst.fb.releaseAsRenderingTarget();
    }
    void bind (int unit, unsigned tex);
    void uploadRGBA (Tex&, const uint8_t*, int w, int h, uint64_t ver, bool nearest = false);
    Tex* mediaTexture (const std::string& path, bool video, double mediaTime, bool exact, float& aspect);
    TextEntry* textTexture (const TextRequest&);
    void releaseTex (Tex&);

    void runPipeline (const FrameInputs&, int cw, int ch, bool offline, bool heavyDue, unsigned& outTex, unsigned& sceneTex);
    void setAudioUniforms (GlProgram&, const Drive&);
    void scenePass (const FrameInputs&, Fbo& out, Fbo& prev, int cw, int ch);
    void rdStep (const FrameInputs&, const float* knobs);
    void copyPass (Fbo& dst, unsigned srcTex);
    void compositePass (Fbo& dst, unsigned dstTex, unsigned layerTex, float opacity, int blend);
    void fxPass (const FxInst&, Fbo& dst, unsigned srcTex, const FrameInputs&, int cw, int ch);
    void updateGradient (const Gradient&);
    void finalPass (const FrameInputs&, unsigned tex, unsigned origTex, int vx, int vy, int vw, int vh, bool overlay, bool flipForReadback);
    void setStatusError (const juce::String& e) { const juce::ScopedLock sl (statusLock); st.shaderError = e; }

    MediaPool& media; juce::OpenGLContext* ctx = nullptr; bool gpuOk = false;
    GlProgram progScene, progFx, progLayer, progComp, progRd, progFinal;
    unsigned vbo = 0, specTex = 0, gradTex = 0; unsigned char specBytes[64 * 2 * 4] {}; std::string gradKey;

    int cw = 0, ch = 0;
    Fbo sceneA, sceneB, sceneMix, xfade, compA, compB, layerA, layerB, fxA, fxB, rd[2], offFbo, thumbFbo, thumbPrev; int rdIdx = 0;
    std::map<std::string, float> mappedNow; unsigned beforeFxTex = 0; int lastCw = 0, lastCh = 0;
    int sceneWrite = 0;                                // which of sceneA/B was written last
    std::map<int, std::unique_ptr<Fbo>> fxHist;
    std::map<std::string, Tex> mediaTex; std::map<TextRequest, TextEntry> textCache; uint64_t frameNo = 0;
    Tex texA, texB, texC, texD; std::string pathA, pathB, pathC, pathD; float aspA = 1, aspB = 1, aspC = 1, aspD = 1, imgBlend = 0;
    int lastScene = -1; float sceneFade = 0; double lastHeavy = 0, lastNow = 0; float simTime = 0; bool rdReset = true; int rdScene = -1; bool rdInit = false;
    // mapping engine copy with its own state (rebuilt when the project's mapping changes)
    MappingEngine mapEngine; std::string mapText; const Project* mapProj = nullptr;
    LyricDoc mergedLyrics; const Project* mergedProj = nullptr; bool mergedCaptionsOn = false;
    std::vector<LayerBound> boundsBuild, bounds;
    mutable juce::CriticalSection statusLock, thumbLock; RenderStatus st;
    std::vector<juce::Image> thumbs; std::atomic<int> thumbCount { 0 }; int thumbNext = 0;
    unsigned lastFinalTex = 0;
};
} // namespace monie
