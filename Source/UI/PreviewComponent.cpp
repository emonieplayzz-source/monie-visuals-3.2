#include "PreviewComponent.h"

using namespace juce::gl;
using namespace monie;

PreviewComponent::PreviewComponent (MonieProcessor& p) : proc (p), compositor (p.media)
{
    setOpaque (true); setWantsKeyboardFocus (false);
    ctx.setRenderer (this);
    ctx.setComponentPaintingEnabled (false);
    ctx.setContinuousRepainting (true);
    ctx.setSwapInterval (1);
    ctx.attachTo (*this);
    proc.media.addChangeListener (nullptr);   // no-op: keeps the MediaPool alive for the lifetime of the preview
}
PreviewComponent::~PreviewComponent() { ctx.detach(); }

float PreviewComponent::canvasAspect() const { return canvasAspectOf (*proc.store.snapshot()); }

juce::Rectangle<int> PreviewComponent::canvasRect() const
{
    const float aspect = canvasAspect(); const int W = getWidth(), H = getHeight();
    if (W < 4 || H < 4) return {};
    float cw, chh; const float margin = 0.f;
    if (zoomMode == 0 || zoomMode == 2) { if ((float) W / (float) H > aspect) { chh = (float) H - margin; cw = chh * aspect; } else { cw = (float) W - margin; chh = cw / aspect; } }
    else { if ((float) W / (float) H > aspect) { cw = (float) W; chh = cw / aspect; } else { chh = (float) H; cw = chh * aspect; } }
    if (zoomMode == 2) { cw *= zoomFactor; chh *= zoomFactor; }
    return juce::Rectangle<float> (cw, chh).withCentre ({ (float) W * 0.5f + panX, (float) H * 0.5f + panY }).toNearestInt();
}
juce::Point<float> PreviewComponent::toCanvas (juce::Point<float> p) const
{
    auto r = canvasRect(); if (r.isEmpty()) return {};
    const float aspect = canvasAspect();
    return { ((p.x - (float) r.getX()) / (float) r.getWidth() * 2.f - 1.f) * aspect, -((p.y - (float) r.getY()) / (float) r.getHeight() * 2.f - 1.f) };
}

void PreviewComponent::runOnGL (std::function<void()> fn) { ctx.executeOnGLThread ([fn] (juce::OpenGLContext&) { fn(); }, true); }

void PreviewComponent::newOpenGLContextCreated() { compositor.init (ctx); lastRenderT = juce::Time::getMillisecondCounterHiRes() * 0.001; }
void PreviewComponent::openGLContextClosing() { compositor.shutdown(); }

void PreviewComponent::renderOpenGL()
{
    jassert (juce::OpenGLHelpers::isContextActive());
    const float scaleF = (float) ctx.getRenderingScale();
    const int pw = juce::jmax (16, juce::roundToInt (getWidth() * scaleF)), ph = juce::jmax (16, juce::roundToInt (getHeight() * scaleF));
    glViewport (0, 0, pw, ph); glDisable (GL_BLEND); glDisable (GL_DEPTH_TEST); glDisable (GL_CULL_FACE);
    glClearColor (0.055f, 0.055f, 0.065f, 1.f); glClear (GL_COLOR_BUFFER_BIT);
    if (! compositor.ready() || exporting.load()) return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001; const float dt = (float) juce::jlimit (0.0005, 0.1, now - lastRenderT); lastRenderT = now;
    if (frameCounter < 2000 && compositor.thumbsReady() < kNumScenes && ++frameCounter > 2) compositor.stepThumbnails();   // one scene thumbnail per frame, right after startup

    FrameInputs in; in.project = proc.store.snapshot();
    static thread_local std::vector<float> g; g.resize (globalDefs().size()); proc.readGlobals (g.data()); in.globals = g.data();
    in.scene = proc.currentScene(); proc.readKnobs (in.knobs);
    in.quality = (int) proc.apvts.getRawParameterValue ("quality")->load(); in.fpsMode = (int) proc.apvts.getRawParameterValue ("fps")->load();
    RawAudio raw; proc.fillRaw (raw); in.drive = driveState.update (dt, raw, proc.driveParams());
    in.clock = proc.clockNow(); in.dt = dt;
    const int sc = proc.seekCounter(); if (sc != lastSeekCount) { lastSeekCount = sc; /* feedback buffers stay: a seek must not flash black */ }
    in.overlay = true; in.guides = guides.load(); in.split = splitOn.load() ? 0.5f : -1.f; in.selectedLayer = selected.load(); in.paneHeightPx = ph;

    auto r = canvasRect(); if (r.isEmpty()) return;
    const int vx = juce::roundToInt (r.getX() * scaleF), vw = juce::roundToInt (r.getWidth() * scaleF), vh = juce::roundToInt (r.getHeight() * scaleF);
    const int vy = ph - juce::roundToInt (r.getBottom() * scaleF);
    in.paneHeightPx = vh;
    // NOTE: no scissor here - a scissor rectangle would also clip the off-screen passes (their framebuffers are larger than the pane).
    // The native GL surface only covers this component, so a zoomed canvas cannot draw outside it anyway.
    glDisable (GL_SCISSOR_TEST);
    compositor.renderLive (in, vx, vy, vw, vh);
    if (! firstFrameDone.exchange (true) && onFirstFrame) juce::MessageManager::callAsync ([this] { if (onFirstFrame) onFirstFrame(); });
}

// ---- direct manipulation: click selects the top-most layer under the pointer, drag moves it ----
void PreviewComponent::mouseDown (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const auto c = toCanvas (e.position); hitDragId = -1;
    auto bounds = compositor.layerBounds();
    for (auto it = bounds.rbegin(); it != bounds.rend(); ++it)
    {
        const float dx = c.x - it->cx, dy = c.y - it->cy, cs = std::cos (-it->rot), sn = std::sin (-it->rot), lx = dx * cs - dy * sn, ly = dx * sn + dy * cs;
        if (std::fabs (lx) <= it->hx && std::fabs (ly) <= it->hy) { hitDragId = it->id; dragOffset = { it->cx - c.x, it->cy - c.y }; break; }
    }
    if (hitDragId >= 0 && onLayerPicked) onLayerPicked (hitDragId); else if (hitDragId < 0 && onLayerPicked) onLayerPicked (-1);
}
void PreviewComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isMiddleButtonDown() || (hitDragId < 0 && zoomMode == 2)) { panX = panX + (e.position.x - lastMouse.x); panY = panY + (e.position.y - lastMouse.y); lastMouse = e.position; return; }
    if (hitDragId >= 0 && onLayerDragged) { const auto c = toCanvas (e.position); onLayerDragged (hitDragId, { c.x + dragOffset.x, c.y + dragOffset.y }); }
    lastMouse = e.position;
}
void PreviewComponent::mouseUp (const juce::MouseEvent&) { hitDragId = -1; }
void PreviewComponent::mouseDoubleClick (const juce::MouseEvent&) { setZoomMode (0); }
void PreviewComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (zoomMode != 2) { zoomMode = 2; zoomFactor = 1.f; }
    zoomFactor = juce::jlimit (0.25f, 8.f, zoomFactor.load() * (1.f + w.deltaY * 0.5f));
}
