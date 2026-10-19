#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"

namespace ui
{
// Bottom panel: layer list (visibility, lock, rename, reorder, duplicate, delete, group) on the left and a real timeline on the right
// (ruler, playhead scrub, markers, loop region, draggable/trimmable clips, keyframe diamonds, zoom / scroll).
class LayerTimeline : public juce::Component, private juce::Timer
{
public:
    explicit LayerTimeline (MonieProcessor&);
    ~LayerTimeline() override { stopTimer(); }
    std::function<void (int)> onSelect; std::function<void (int)> onAddLayer;   // 0 image 1 video 2 text 3 caption
    int selected = -1;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void duplicateSelected(); void deleteSelected(); void moveSelected (int delta); void groupSelected();
private:
    void timerCallback() override { repaint(); }
    static constexpr int headW = 250, rulerH = 22, rowH = 26, toolH = 28;
    double xToTime (float x) const { return (double) (x - headW) / pps + scrollT; }
    float timeToX (double t) const { return (float) ((t - scrollT) * pps) + headW; }
    int rowAt (int y) const { return (y - toolH - rulerH) / rowH; }          // 0 = top row (top-most layer)
    const monie::Layer* layerAtRow (int row) const;
    enum Drag { None, Scrub, MoveClip, TrimL, TrimR, Reorder } drag = None;
    void layerMenu (int layerId);
    MonieProcessor& proc; double pps = 40, scrollT = 0; int dragLayer = -1; double dragGrab = 0, dragOrigStart = 0, dragOrigLen = 0; int reorderTarget = -1;
    juce::TextButton addBtn { "+ Layer" }, dupBtn { "Duplicate" }, delBtn { "Delete" }, upBtn { "Up" }, downBtn { "Down" }, grpBtn { "Group" };
    std::unique_ptr<juce::TextEditor> renamer;
};
}
