#include "LayerTimeline.h"
using namespace monie;
namespace ui
{
LayerTimeline::LayerTimeline (MonieProcessor& p) : proc (p)
{
    for (auto* b : { &addBtn, &dupBtn, &delBtn, &upBtn, &downBtn, &grpBtn }) addAndMakeVisible (b);
    addBtn.setTooltip ("Add a new layer"); dupBtn.setTooltip ("Duplicate the selected layer (Ctrl+D)"); delBtn.setTooltip ("Delete the selected layer (Delete)");
    upBtn.setTooltip ("Move the layer up (in front)"); downBtn.setTooltip ("Move the layer down (behind)"); grpBtn.setTooltip ("Group / ungroup the selected layer with the one above");
    addBtn.onClick = [this] { juce::PopupMenu m; m.addItem (1, "Image layer..."); m.addItem (2, "Video layer..."); m.addItem (3, "Text layer"); m.addItem (4, "Caption layer");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [this] (int r) { if (r > 0 && onAddLayer) onAddLayer (r - 1); }); };
    dupBtn.onClick = [this] { duplicateSelected(); }; delBtn.onClick = [this] { deleteSelected(); }; upBtn.onClick = [this] { moveSelected (1); }; downBtn.onClick = [this] { moveSelected (-1); }; grpBtn.onClick = [this] { groupSelected(); };
    startTimerHz (24);
}
void LayerTimeline::resized()
{
    auto r = getLocalBounds().removeFromTop (toolH).reduced (3, 2); const int w = r.getWidth() / 6;
    for (auto* b : { &addBtn, &dupBtn, &delBtn, &upBtn, &downBtn, &grpBtn }) b->setBounds (r.removeFromLeft (juce::jmin (w, 84)).reduced (1, 0));
}
const Layer* LayerTimeline::layerAtRow (int row) const
{
    auto& L = proc.store.get().layers; const int idx = (int) L.size() - 1 - row; return idx >= 0 && idx < (int) L.size() ? &L[(size_t) idx] : nullptr;
}
void LayerTimeline::duplicateSelected() { if (selected < 0) return; int nid = -1; proc.store.edit ("Duplicate layer", [&] (Project& p) { nid = p.duplicateLayer (selected); }); if (nid >= 0 && onSelect) onSelect (nid); }
void LayerTimeline::deleteSelected()
{
    if (selected < 0) return; const int id = selected; proc.store.edit ("Delete layer", [&] (Project& p) { p.removeLayer (id); });
    auto& L = proc.store.get().layers; if (onSelect) onSelect (L.empty() ? -1 : L.back().id);
}
void LayerTimeline::moveSelected (int delta)
{
    if (selected < 0) return; const int id = selected; proc.store.edit ("Reorder layer", [&] (Project& p) { int i = p.indexOf (id); if (i >= 0) p.moveLayer (id, i + delta); });
}
void LayerTimeline::groupSelected()
{
    if (selected < 0) return; const int id = selected;
    proc.store.edit ("Group layers", [&] (Project& p) { int i = p.indexOf (id); if (i < 0) return; auto& l = p.layers[(size_t) i];
        if (l.group != 0) { l.group = 0; return; } if (i + 1 < (int) p.layers.size()) { int g = p.layers[(size_t) i + 1].group ? p.layers[(size_t) i + 1].group : p.newId(); p.layers[(size_t) i + 1].group = g; l.group = g; } });
}
void LayerTimeline::layerMenu (int id)
{
    juce::PopupMenu m; m.addItem (1, "Rename"); m.addItem (2, "Duplicate"); m.addItem (3, "Delete"); m.addSeparator(); m.addItem (4, "Move up"); m.addItem (5, "Move down"); m.addItem (6, "Group / ungroup with layer above");
    m.addItem (7, "Show only this layer"); m.addItem (8, "Show all layers");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, id] (int r)
    {
        selected = id; if (onSelect) onSelect (id);
        switch (r)
        {
            case 1: { const int row = (int) proc.store.get().layers.size() - 1 - proc.store.get().indexOf (id); renamer = std::make_unique<juce::TextEditor>(); auto* l = proc.store.get().find (id); if (! l) break;
                      renamer->setText (juce::String (l->name), false); renamer->selectAll(); addAndMakeVisible (*renamer); renamer->setBounds (30, toolH + rulerH + row * rowH + 2, headW - 34, rowH - 4); renamer->grabKeyboardFocus();
                      renamer->onReturnKey = [this, id] { auto t = renamer->getText(); proc.store.edit ("Rename layer", [=] (Project& p) { if (auto* l2 = p.find (id)) l2->name = t.toStdString(); }); renamer.reset(); };
                      renamer->onEscapeKey = [this] { renamer.reset(); }; renamer->onFocusLost = [this] { renamer.reset(); }; break; }
            case 2: duplicateSelected(); break; case 3: deleteSelected(); break; case 4: moveSelected (1); break; case 5: moveSelected (-1); break; case 6: groupSelected(); break;
            case 7: proc.store.edit ("Solo layer", [=] (Project& p) { for (auto& l : p.layers) l.visible = (l.id == id); }); break;
            case 8: proc.store.edit ("Show all", [=] (Project& p) { for (auto& l : p.layers) l.visible = true; }); break;
        }
    });
}

void LayerTimeline::paint (juce::Graphics& g)
{
    auto& lf = static_cast<MonieLnF&> (getLookAndFeel()); const auto& P = proc.store.get(); const int W = getWidth(), H = getHeight();
    g.fillAll (lf.t.panel);
    g.setColour (lf.t.panel2); g.fillRect (0, toolH, headW, H - toolH); g.fillRect (headW, toolH, W - headW, rulerH);
    // ruler
    g.saveState(); g.reduceClipRegion (headW, toolH, W - headW, H - toolH);
    double step = 1; for (double s : { .1, .2, .5, 1., 2., 5., 10., 15., 30., 60., 120., 300. }) { step = s; if (s * pps >= 56) break; }
    g.setFont (10.f);
    for (double t = std::floor (scrollT / step) * step; timeToX (t) < W; t += step)
    {
        const float x = timeToX (t); g.setColour (lf.t.line); g.drawVerticalLine ((int) x, (float) toolH + rulerH - 6, (float) H);
        g.setColour (lf.t.dim); const int m = (int) t / 60; g.drawText (juce::String (m) + ":" + juce::String (t - m * 60, step < 1 ? 1 : 0).paddedLeft ('0', step < 1 ? 4 : 2), (int) x + 3, toolH, 56, rulerH, juce::Justification::centredLeft);
    }
    if (P.loopOn) { g.setColour (lf.t.accent.withAlpha (.18f)); g.fillRect (juce::Rectangle<float> (timeToX (P.loopA), (float) toolH, timeToX (P.loopB) - timeToX (P.loopA), (float) H - toolH)); g.setColour (lf.t.accent); g.fillRect (timeToX (P.loopA), (float) toolH, 2.f, (float) rulerH); g.fillRect (timeToX (P.loopB) - 2, (float) toolH, 2.f, (float) rulerH); }
    for (auto& m : P.markers) { const float x = timeToX (m.t); g.setColour (juce::Colour (0xffffc83d)); juce::Path f; f.addTriangle (x, (float) toolH + rulerH, x, (float) toolH + rulerH - 12, x + 9, (float) toolH + rulerH - 6); g.fillPath (f); g.drawVerticalLine ((int) x, (float) toolH + rulerH, (float) H); g.setFont (10.f); g.drawText (juce::String (m.name), (int) x + 11, toolH, 90, 12, juce::Justification::centredLeft); }
    g.restoreState();
    // rows
    const int n = (int) P.layers.size();
    for (int row = 0; row < n; ++row)
    {
        const auto& l = P.layers[(size_t) (n - 1 - row)]; const int y = toolH + rulerH + row * rowH; if (y > H) break; const bool sel = l.id == selected;
        g.setColour (sel ? lf.t.accent.withAlpha (.22f) : (row % 2 ? lf.t.panel : lf.t.panel2.withAlpha (.4f))); g.fillRect (0, y, W, rowH - 1);
        g.setColour (l.visible ? lf.t.accent : lf.t.dim); g.setFont (14.f); g.drawText (l.visible ? juce::String::fromUTF8 ("\xe2\x97\x89") : juce::String::fromUTF8 ("\xe2\x97\x8b"), 4, y, 20, rowH, juce::Justification::centred);
        g.setColour (l.locked ? lf.t.text : lf.t.dim); g.drawText (l.locked ? juce::String::fromUTF8 ("\xf0\x9f\x94\x92") : juce::String::fromUTF8 ("\xe2\x80\xa2"), 24, y, 18, rowH, juce::Justification::centred);
        const char* tn[] = { "IMG", "VID", "TXT", "CAP" }; g.setColour (lf.t.dim); g.setFont (9.f); g.drawText (tn[(int) l.type], 44, y, 26, rowH, juce::Justification::centred);
        g.setColour (l.visible ? lf.t.text : lf.t.dim); g.setFont (12.5f); g.drawText ((l.group ? juce::String (juce::CharPointer_UTF8 ("\xe2\x94\x94 ")) : juce::String()) + juce::String (l.name), 72, y, headW - 78, rowH, juce::Justification::centredLeft, true);
        // clip bar
        g.saveState(); g.reduceClipRegion (headW, y, W - headW, rowH);
        const double s = l.clipStart, e = l.clipLen < 0 ? juce::jmax (P.duration, s + 1) : s + l.clipLen;
        juce::Colour cc = l.type == LayerType::Text ? juce::Colour (0xff2f7d6d) : l.type == LayerType::Caption ? juce::Colour (0xff8a6d2f) : l.type == LayerType::Video ? juce::Colour (0xff6d3f8a) : juce::Colour (0xff3f5f9a);
        auto bar = juce::Rectangle<float> (timeToX (s), (float) y + 3, timeToX (e) - timeToX (s), (float) rowH - 7);
        g.setColour (cc.withAlpha (l.visible ? .85f : .35f)); g.fillRoundedRectangle (bar, 3.f); g.setColour (sel ? lf.t.text : cc.brighter (.4f)); g.drawRoundedRectangle (bar, 3.f, sel ? 1.6f : 1.f);
        for (auto& a : l.anims) { const double a0 = s + a.start; g.setColour (juce::Colours::white.withAlpha (.35f)); g.fillRect (timeToX (a0), bar.getBottom() - 4, juce::jmax (2.f, (float) (a.dur * pps)), 3.f); }
        std::set<double> kt; for (auto& kv : l.keys) for (auto& k : kv.second.all()) kt.insert (s + k.time);
        for (double t : kt) { const float x = timeToX (t); juce::Path d; d.addQuadrilateral (x, bar.getCentreY() - 5, x + 5, bar.getCentreY(), x, bar.getCentreY() + 5, x - 5, bar.getCentreY()); g.setColour (juce::Colour (0xffffe08a)); g.fillPath (d); }
        g.restoreState();
    }
    if (drag == Reorder && reorderTarget >= 0) { g.setColour (lf.t.accent); g.fillRect (0, toolH + rulerH + reorderTarget * rowH - 1, headW, 2); }
    g.setColour (lf.t.line); g.drawVerticalLine (headW, (float) toolH, (float) H);
    if (n == 0) { g.setColour (lf.t.dim); g.setFont (13.f); g.drawText ("No layers. Use + Layer to add an image, video, text or caption.", 0, toolH + rulerH, W, 40, juce::Justification::centred); }
    // playhead
    const double now = proc.clockNow(); const float px = timeToX (now); if (px >= headW && px < W) { g.setColour (juce::Colour (0xffff5c6c)); g.fillRect (px - 0.5f, (float) toolH, 2.f, (float) H - toolH); juce::Path tri; tri.addTriangle (px - 6, (float) toolH, px + 6, (float) toolH, px, (float) toolH + 9); g.fillPath (tri); }
    if (proc.hostControlled()) { g.setColour (lf.t.dim); g.setFont (10.f); g.drawText ("Host controls time", W - 130, 2, 126, 14, juce::Justification::centredRight); }
}

void LayerTimeline::mouseDown (const juce::MouseEvent& e)
{
    if (renamer) renamer.reset();
    const int y = e.y; drag = None; if (y < toolH) return;
    const auto& P = proc.store.get();
    if (y < toolH + rulerH)
    {
        if (e.x < headW) return; const double t = xToTime ((float) e.x);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m; m.addItem (1, "Add marker here"); m.addItem (2, "Set loop start here"); m.addItem (3, "Set loop end here"); m.addItem (4, "Clear loop"); m.addItem (5, "Clear markers");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, t] (int r) { switch (r) {
                case 1: proc.store.edit ("Add marker", [=] (Project& p) { p.markers.push_back ({ t, "Marker " + std::to_string (p.markers.size() + 1) }); }); break;
                case 2: proc.store.edit ("Loop start", [=] (Project& p) { p.loopA = t; if (p.loopB <= t) p.loopB = t + 4; p.loopOn = true; }); proc.setLoop (true, proc.store.get().loopA, proc.store.get().loopB); break;
                case 3: proc.store.edit ("Loop end", [=] (Project& p) { p.loopB = t; if (p.loopA >= t) p.loopA = juce::jmax (0.0, t - 4); p.loopOn = true; }); proc.setLoop (true, proc.store.get().loopA, proc.store.get().loopB); break;
                case 4: proc.store.edit ("Clear loop", [=] (Project& p) { p.loopOn = false; }); proc.setLoop (false, 0, 1); break;
                case 5: proc.store.edit ("Clear markers", [=] (Project& p) { p.markers.clear(); }); break; } });
            return;
        }
        for (auto& m : P.markers) if (std::fabs (timeToX (m.t) - (float) e.x) < 8) { proc.transportSeek (m.t); drag = Scrub; return; }
        drag = Scrub; proc.transportSeek (juce::jmax (0.0, t)); return;
    }
    const int row = rowAt (y); auto* l = layerAtRow (row); if (! l) { selected = -1; if (onSelect) onSelect (-1); return; }
    const int id = l->id; selected = id; if (onSelect) onSelect (id);
    if (e.mods.isPopupMenu()) { layerMenu (id); return; }
    if (e.x < headW)
    {
        if (e.x < 22) proc.store.edit ("Layer visibility", [=] (Project& p) { if (auto* x = p.find (id)) x->visible = ! x->visible; });
        else if (e.x < 44) proc.store.edit ("Lock layer", [=] (Project& p) { if (auto* x = p.find (id)) x->locked = ! x->locked; });
        else { drag = Reorder; dragLayer = id; reorderTarget = row; }
        return;
    }
    if (l->locked) return;
    const double s = l->clipStart, en = l->clipLen < 0 ? juce::jmax (P.duration, s + 1) : s + l->clipLen; const float x0 = timeToX (s), x1 = timeToX (en);
    dragLayer = id; dragGrab = xToTime ((float) e.x); dragOrigStart = s; dragOrigLen = l->clipLen < 0 ? en - s : l->clipLen;
    if (std::fabs ((float) e.x - x0) < 6) drag = TrimL; else if (std::fabs ((float) e.x - x1) < 6) drag = TrimR; else if (e.x > x0 && e.x < x1) drag = MoveClip;
    else for (auto& kv : l->keys) for (auto& k : kv.second.all()) if (std::fabs (timeToX (s + k.time) - (float) e.x) < 7) { proc.transportSeek (s + k.time); return; }
}
void LayerTimeline::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Scrub) { proc.transportSeek (juce::jmax (0.0, xToTime ((float) e.x))); return; }
    if (drag == Reorder) { reorderTarget = juce::jlimit (0, juce::jmax (0, (int) proc.store.get().layers.size() - 1), rowAt (e.y)); repaint(); return; }
    if (dragLayer < 0 || drag == None) return;
    const double dt = xToTime ((float) e.x) - dragGrab; const int id = dragLayer; const double os = dragOrigStart, ol = dragOrigLen; const Drag d = drag;
    proc.store.edit ("Edit clip", [=] (Project& p) { auto* l = p.find (id); if (! l) return;
        if (d == MoveClip) l->clipStart = juce::jmax (0.0, os + dt);
        else if (d == TrimL) { const double ns = juce::jlimit (0.0, os + ol - 0.05, os + dt); l->clipStart = ns; l->clipLen = os + ol - ns; }
        else if (d == TrimR) l->clipLen = juce::jmax (0.05, ol + dt); }, "clip" + juce::String (id));
}
void LayerTimeline::mouseUp (const juce::MouseEvent&)
{
    if (drag == Reorder && dragLayer >= 0 && reorderTarget >= 0) { const int n = (int) proc.store.get().layers.size(); const int newIdx = n - 1 - reorderTarget; const int id = dragLayer; proc.store.edit ("Reorder layer", [=] (Project& p) { p.moveLayer (id, newIdx); }); }
    drag = None; dragLayer = -1; reorderTarget = -1; repaint();
}
void LayerTimeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= toolH && e.y < toolH + rulerH && e.x > headW) { const double t = xToTime ((float) e.x); proc.store.edit ("Add marker", [=] (Project& p) { p.markers.push_back ({ t, "Marker " + std::to_string (p.markers.size() + 1) }); }); return; }
    if (e.x < headW && e.y > toolH + rulerH) if (auto* l = layerAtRow (rowAt (e.y))) layerMenu (l->id);
}
void LayerTimeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.x < headW) return;
    if (e.mods.isShiftDown()) { scrollT = juce::jmax (0.0, scrollT - w.deltaY * 120.0 / pps); return; }
    const double anchor = xToTime ((float) e.x); pps = juce::jlimit (4.0, 600.0, pps * (1.0 + w.deltaY * .6)); scrollT = juce::jmax (0.0, anchor - (double) (e.x - headW) / pps);
}
}
