#pragma once
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "Render/Compositor.h"

// The GL canvas. Owns the Compositor (GL thread) and turns processor state into per-frame inputs.
class PreviewComponent : public juce::Component, public juce::OpenGLRenderer
{
public:
    explicit PreviewComponent (MonieProcessor&);
    ~PreviewComponent() override;

    // view settings (message thread)
    enum Guide { Grid = 1, Safe = 2, Cross = 4, Thirds = 8 };
    void setZoomMode (int mode, float zoom = 1.f) { zoomMode = mode; zoomFactor = zoom; panX = panY = 0; }   // mode 0 = fit, 1 = fill, 2 = manual zoom
    int  getZoomMode() const { return zoomMode; }
    void setGuides (unsigned g) { guides = g; }
    unsigned getGuides() const { return guides; }
    void setSplit (bool on) { splitOn = on; }
    bool isSplit() const { return splitOn; }
    void setSelectedLayer (int id) { selected = id; }
    int  getSelectedLayer() const { return selected; }
    monie::RenderStatus status() const { return compositor.status(); }
    monie::Compositor& comp() { return compositor; }
    juce::Rectangle<int> canvasRect() const;                 // canvas area inside this component, in component pixels
    // converts component pixels to canvas coordinates (x in [-aspect, aspect], y in [-1, 1], y up)
    juce::Point<float> toCanvas (juce::Point<float> p) const;
    float canvasAspect() const;
    std::function<void (int layerId, juce::Point<float> canvasPos)> onLayerDragged;
    std::function<void (int layerId)> onLayerPicked;
    std::function<void()> onFirstFrame;
    void requestOfflineBegin (bool on) { exporting = on; }
    // blocking helper for the export thread: runs fn on the GL thread
    void runOnGL (std::function<void()> fn);
    juce::OpenGLContext& context() { return ctx; }

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void paint (juce::Graphics&) override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    monie::AudioDrive driveState;                            // used by the GL thread
    juce::String selfTestOut;                                // when set (env MONIE_SELFTEST), thumbnails + frame are written there

private:
    MonieProcessor& proc;
    juce::OpenGLContext ctx;
    monie::Compositor compositor;
    std::atomic<int> zoomMode { 0 }; std::atomic<float> zoomFactor { 1.f }, panX { 0.f }, panY { 0.f };
    std::atomic<unsigned> guides { 0 }; std::atomic<bool> splitOn { false }; std::atomic<int> selected { -1 }; std::atomic<bool> exporting { false };
    double lastRenderT = 0; std::atomic<bool> firstFrameDone { false }; int hitDragId = -1; juce::Point<float> dragOffset; int frameCounter = 0; double lastSeekTime = 0; int lastSeekCount = 0;
    juce::Point<float> lastMouse;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewComponent)
};
