#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include "Theme.h"

namespace ui
{
// A vertical list of labelled controls (sliders, toggles, choices, colours, text, buttons) with collapsible sections.
// Rows are plain data + lambdas, so every inspector in the plugin is built the same way and gets reset / tooltip / keyframe support.
class PropPanel : public juce::Component
{
public:
    struct Row : public juce::Component
    {
        juce::String sectionKey; int h = 26; std::function<void()> refresh; bool isHeader = false;
    };

    PropPanel() { setOpaque (false); }
    void clear() { rows.clear(); removeAllChildren(); curSection.clear(); curCollapsed = false; }
    void finish() { relayout(); }
    void refreshValues() { for (auto& r : rows) if (r->refresh) r->refresh(); }
    int  contentHeight() const { return totalH; }
    std::function<void()> onHeightChanged;

    // ---- structure ----
    void section (const juce::String& title, bool defaultCollapsed = false, const juce::String& keyOverride = {})
    {
        auto key = keyOverride.isNotEmpty() ? keyOverride : title; auto it = collapsed().find (key.toStdString()); if (it == collapsed().end()) collapsed()[key.toStdString()] = defaultCollapsed;
        auto* r = new HeaderRow (*this, title, key); r->isHeader = true; r->h = 24; add (r); curSection = key; curCollapsed = collapsed()[key.toStdString()];
    }
    void info (const juce::String& text)
    {
        auto* r = new InfoRow (text); r->h = 20; add (r);
    }
    // ---- controls ----
    void slider (const juce::String& label, const juce::String& tip, double mn, double mx, double def, double step,
                 std::function<double()> get, std::function<void (double)> set, std::function<void()> onKey = {}, std::function<bool()> keyed = {}, std::function<void (bool)> gesture = {})
    {
        auto* r = new SliderRow (label, tip, mn, mx, def, step, std::move (get), std::move (set), std::move (onKey), std::move (keyed)); if (gesture) { r->sl.onDragStart = [gesture] { gesture (true); }; r->sl.onDragEnd = [gesture] { gesture (false); }; } add (r);
    }
    void toggle (const juce::String& label, const juce::String& tip, std::function<bool()> get, std::function<void (bool)> set) { add (new ToggleRow (label, tip, std::move (get), std::move (set))); }
    void choice (const juce::String& label, const juce::String& tip, const juce::StringArray& items, std::function<int()> get, std::function<void (int)> set, int def = 0)
    { add (new ChoiceRow (label, tip, items, std::move (get), std::move (set), def)); }
    void colour (const juce::String& label, const juce::String& tip, std::function<juce::String()> get, std::function<void (juce::String)> set, juce::String def = "#ffffff")
    { add (new ColourRow (label, tip, std::move (get), std::move (set), def)); }
    void text (const juce::String& label, const juce::String& tip, std::function<juce::String()> get, std::function<void (juce::String)> set, bool multiline = false, int height = 0)
    { auto* r = new TextRow (label, tip, std::move (get), std::move (set), multiline); if (height > 0) r->h = height; else if (multiline) r->h = 72; add (r); }
    void buttons (const std::vector<std::pair<juce::String, std::function<void()>>>& bs, const juce::String& tip = {}) { add (new ButtonRow (bs, tip)); }
    void custom (juce::Component* c, int height) { auto* r = new CustomRow (c); r->h = height; add (r); }

    void resized() override { relayout(); }
    void paint (juce::Graphics&) override {}

    static std::map<std::string, bool>& collapsed() { static std::map<std::string, bool> m; return m; }

private:
    // ---------------------------------------------------------------- row implementations
    struct HeaderRow : Row
    {
        HeaderRow (PropPanel& p, const juce::String& t, const juce::String& k) : owner (p), title (t), key (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics& g) override
        {
            auto& l = static_cast<MonieLnF&> (getLookAndFeel()); const bool c = PropPanel::collapsed()[key.toStdString()];
            g.setColour (l.t.panel2); g.fillRect (getLocalBounds().withTrimmedTop (2));
            g.setColour (l.t.accent); juce::Path tri; if (c) tri.addTriangle (8, 8, 8, 18, 14, 13); else tri.addTriangle (7, 9, 17, 9, 12, 15); g.fillPath (tri);
            g.setColour (l.t.text); g.setFont (juce::FontOptions (13.f, juce::Font::bold)); g.drawText (title.toUpperCase(), 24, 2, getWidth() - 28, h - 2, juce::Justification::centredLeft);
        }
        void mouseUp (const juce::MouseEvent&) override { auto& c = PropPanel::collapsed()[key.toStdString()]; c = ! c; owner.relayout(); owner.repaint(); }
        PropPanel& owner; juce::String title, key;
    };
    struct InfoRow : Row
    {
        explicit InfoRow (const juce::String& t) : text (t) {}
        void paint (juce::Graphics& g) override { auto& l = static_cast<MonieLnF&> (getLookAndFeel()); g.setColour (l.t.dim); g.setFont (12.f); g.drawFittedText (text, getLocalBounds().reduced (6, 0), juce::Justification::centredLeft, 3); }
        juce::String text;
    };
    struct LabelBase : Row
    {
        LabelBase (const juce::String& l, const juce::String& tip) : label (l), tooltip (tip) { setTooltip (tip); }
        void paintLabel (juce::Graphics& g, int w)
        {
            auto& lf = static_cast<MonieLnF&> (getLookAndFeel()); g.setColour (lf.t.text); g.setFont (12.5f); g.drawText (label, 8, 0, w - 10, h, juce::Justification::centredLeft, true);
        }
        void setTooltip (const juce::String& t) { tooltipText = t; }
        juce::String label, tooltip, tooltipText;
    };
    struct SliderRow : LabelBase
    {
        SliderRow (const juce::String& l, const juce::String& tip, double mn, double mx, double d, double step, std::function<double()> g, std::function<void (double)> s, std::function<void()> k, std::function<bool()> kd)
            : LabelBase (l, tip), def (d), get (std::move (g)), set (std::move (s)), keyCb (std::move (k)), keyedCb (std::move (kd))
        {
            sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setRange (mn, mx, step > 0 ? step : 0.0); sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            sl.setDoubleClickReturnValue (true, d); sl.setNumDecimalPlacesToDisplay (mx - mn >= 100 ? 0 : mx - mn >= 10 ? 1 : 2);
            sl.setTooltip (tip.isNotEmpty() ? tip + "\n(double-click to reset)" : "Double-click to reset"); addAndMakeVisible (sl);
            sl.onValueChange = [this] { if (! updating) set (sl.getValue()); };
            reset.setButtonText (juce::String::fromUTF8 ("\xe2\x86\xba")); reset.setTooltip ("Reset to default"); reset.onClick = [this] { sl.setValue (def, juce::sendNotificationSync); }; addAndMakeVisible (reset);
            if (keyCb) { key.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x86")); key.setTooltip ("Add / remove a keyframe at the playhead"); key.onClick = [this] { keyCb(); }; addAndMakeVisible (key); }
            refresh = [this] { updating = true; sl.setValue (get(), juce::dontSendNotification); updating = false; key.setToggleState (keyedCb && keyedCb(), juce::dontSendNotification); key.setColour (juce::TextButton::textColourOffId, key.getToggleState() ? static_cast<MonieLnF&> (getLookAndFeel()).t.accent : static_cast<MonieLnF&> (getLookAndFeel()).t.dim); };
            refresh();
        }
        void resized() override
        {
            auto r = getLocalBounds(); const int lw = juce::jmax (80, r.getWidth() * 36 / 100);
            auto right = r.withTrimmedLeft (lw).reduced (0, 3); reset.setBounds (right.removeFromRight (22)); if (keyCb) key.setBounds (right.removeFromRight (22)); sl.setBounds (right);
        }
        void paint (juce::Graphics& g) override { paintLabel (g, juce::jmax (80, getWidth() * 36 / 100)); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { if (e.x < juce::jmax (80, getWidth() * 36 / 100)) sl.setValue (def, juce::sendNotificationSync); }
        juce::Slider sl; juce::TextButton reset, key; double def; std::function<double()> get; std::function<void (double)> set; std::function<void()> keyCb; std::function<bool()> keyedCb; bool updating = false;
    };
    struct ToggleRow : LabelBase
    {
        ToggleRow (const juce::String& l, const juce::String& tip, std::function<bool()> g, std::function<void (bool)> s) : LabelBase (l, tip), get (std::move (g)), set (std::move (s))
        { tb.setButtonText (l); tb.setTooltip (tip); addAndMakeVisible (tb); tb.onClick = [this] { if (! updating) set (tb.getToggleState()); }; refresh = [this] { updating = true; tb.setToggleState (get(), juce::dontSendNotification); updating = false; }; refresh(); }
        void resized() override { tb.setBounds (getLocalBounds().reduced (6, 2)); }
        juce::ToggleButton tb; std::function<bool()> get; std::function<void (bool)> set; bool updating = false;
    };
    struct ChoiceRow : LabelBase
    {
        ChoiceRow (const juce::String& l, const juce::String& tip, const juce::StringArray& items, std::function<int()> g, std::function<void (int)> s, int d) : LabelBase (l, tip), get (std::move (g)), set (std::move (s))
        {
            cb.addItemList (items, 1); cb.setTooltip (tip); addAndMakeVisible (cb); (void) d;
            cb.onChange = [this] { if (! updating) set (cb.getSelectedItemIndex()); }; refresh = [this] { updating = true; cb.setSelectedItemIndex (get(), juce::dontSendNotification); updating = false; }; refresh();
        }
        void resized() override { auto r = getLocalBounds(); const int lw = juce::jmax (80, r.getWidth() * 36 / 100); cb.setBounds (r.withTrimmedLeft (lw).reduced (0, 2)); }
        void paint (juce::Graphics& g) override { paintLabel (g, juce::jmax (80, getWidth() * 36 / 100)); }
        juce::ComboBox cb; std::function<int()> get; std::function<void (int)> set; bool updating = false;
    };
    struct ColourRow : LabelBase
    {
        ColourRow (const juce::String& l, const juce::String& tip, std::function<juce::String()> g, std::function<void (juce::String)> s, juce::String d) : LabelBase (l, tip), get (std::move (g)), set (std::move (s)), def (d)
        {
            hex.setTooltip (tip + (tip.isEmpty() ? "" : "\n") + "Type a hex colour such as #ff8800"); hex.setInputRestrictions (9, "#0123456789abcdefABCDEF"); addAndMakeVisible (hex);
            swatch.setTooltip ("Pick a colour"); swatch.onClick = [this] { pick(); }; addAndMakeVisible (swatch);
            hex.onReturnKey = hex.onFocusLost = [this] { commitHex(); };
            refresh = [this] { current = get(); hex.setText (current, false); swatch.setColour (juce::TextButton::buttonColourId, juce::Colour::fromString ("ff" + current.trimCharactersAtStart ("#"))); };
            refresh();
        }
        void commitHex()
        {
            auto t = hex.getText().trim(); if (! t.startsWith ("#")) t = "#" + t; if (t.length() == 4) t = juce::String ("#") + t[1] + t[1] + t[2] + t[2] + t[3] + t[3];
            if (t.length() != 7) { hex.setText (current, false); return; } current = t.toLowerCase(); set (current); refresh();
        }
        void pick()
        {
            struct CS : public juce::ColourSelector, juce::ChangeListener { CS (std::function<void (juce::Colour)> f) : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders | juce::ColourSelector::editableColour), cb (std::move (f)) { addChangeListener (this); setSize (260, 300); }
                ~CS() override { removeChangeListener (this); } void changeListenerCallback (juce::ChangeBroadcaster*) override { cb (getCurrentColour()); } std::function<void (juce::Colour)> cb; };
            auto* c = new CS ([this] (juce::Colour col) { current = "#" + col.toDisplayString (false).toLowerCase(); set (current); hex.setText (current, false); swatch.setColour (juce::TextButton::buttonColourId, col); });
            c->setCurrentColour (juce::Colour::fromString ("ff" + current.trimCharactersAtStart ("#")), juce::dontSendNotification);
            juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (c), swatch.getScreenBounds(), nullptr);
        }
        void resized() override { auto r = getLocalBounds(); const int lw = juce::jmax (80, r.getWidth() * 36 / 100); auto right = r.withTrimmedLeft (lw).reduced (0, 3); swatch.setBounds (right.removeFromLeft (36)); right.removeFromLeft (6); hex.setBounds (right.removeFromLeft (84)); }
        void paint (juce::Graphics& g) override { paintLabel (g, juce::jmax (80, getWidth() * 36 / 100)); }
        juce::TextButton swatch; juce::TextEditor hex; std::function<juce::String()> get; std::function<void (juce::String)> set; juce::String def, current;
    };
    struct TextRow : LabelBase
    {
        TextRow (const juce::String& l, const juce::String& tip, std::function<juce::String()> g, std::function<void (juce::String)> s, bool multi) : LabelBase (l, tip), get (std::move (g)), set (std::move (s)), multiline (multi)
        {
            ed.setMultiLine (multi, true); ed.setReturnKeyStartsNewLine (multi); ed.setTooltip (tip); ed.setFont (juce::FontOptions (13.f)); addAndMakeVisible (ed);
            ed.onTextChange = [this] { if (! updating) set (ed.getText()); }; refresh = [this] { if (ed.hasKeyboardFocus (true)) return; updating = true; ed.setText (get(), false); updating = false; }; refresh();
        }
        void resized() override { auto r = getLocalBounds(); if (multiline) { r.removeFromTop (16); ed.setBounds (r.reduced (4, 2)); } else { const int lw = juce::jmax (80, r.getWidth() * 36 / 100); ed.setBounds (r.withTrimmedLeft (lw).reduced (0, 2)); } }
        void paint (juce::Graphics& g) override { auto& lf = static_cast<MonieLnF&> (getLookAndFeel()); g.setColour (lf.t.text); g.setFont (12.5f); if (multiline) g.drawText (label, 8, 0, getWidth() - 10, 16, juce::Justification::centredLeft); else paintLabel (g, juce::jmax (80, getWidth() * 36 / 100)); }
        juce::TextEditor ed; std::function<juce::String()> get; std::function<void (juce::String)> set; bool multiline, updating = false;
    };
    struct ButtonRow : Row
    {
        ButtonRow (const std::vector<std::pair<juce::String, std::function<void()>>>& bs, const juce::String& tip)
        { for (auto& b : bs) { auto* t = btns.add (new juce::TextButton (b.first)); t->setTooltip (tip); t->onClick = b.second; addAndMakeVisible (t); } h = 28; }
        void resized() override { auto r = getLocalBounds().reduced (4, 2); const int n = btns.size(); if (n == 0) return; const int w = r.getWidth() / n; for (auto* b : btns) b->setBounds (r.removeFromLeft (w).reduced (2, 0)); }
        juce::OwnedArray<juce::TextButton> btns;
    };
    struct CustomRow : Row { explicit CustomRow (juce::Component* c) : comp (c) { addAndMakeVisible (c); } void resized() override { comp->setBounds (getLocalBounds()); } juce::Component* comp; };

    void add (Row* r)
    {
        r->sectionKey = curSection; rows.add (r); addAndMakeVisible (r);
        if (! r->isHeader) r->setVisible (! curCollapsed);
    }
    void relayout()
    {
        int y = 0; const int w = getWidth();
        for (auto* r : rows)
        {
            bool vis = true; if (! r->isHeader && r->sectionKey.isNotEmpty()) vis = ! collapsed()[r->sectionKey.toStdString()];
            r->setVisible (vis); if (! vis) continue;
            r->setBounds (0, y, w, r->h); y += r->h + (r->isHeader ? 2 : 1);
        }
        const int newH = y + 8; if (newH != totalH) { totalH = newH; if (onHeightChanged) onHeightChanged(); }
    }
    juce::OwnedArray<Row> rows; juce::String curSection; bool curCollapsed = false; int totalH = 0;
};

// Scrollable wrapper that keeps the PropPanel as wide as the viewport.
class PropView : public juce::Viewport
{
public:
    PropView() { setViewedComponent (&panel, false); setScrollBarsShown (true, false); panel.onHeightChanged = [this] { fit(); }; }
    void resized() override { juce::Viewport::resized(); fit(); }
    void fit() { const int w = getMaximumVisibleWidth(); panel.setSize (w, juce::jmax (panel.contentHeight(), 10)); }
    PropPanel panel;
};
}
