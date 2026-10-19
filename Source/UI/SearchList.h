#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"

namespace ui
{
// A row of buttons that fills its width (used above browser lists).
class ButtonStrip : public juce::Component
{
public:
    void add (const juce::String& text, std::function<void()> fn, const juce::String& tip = {}) { auto* b = buttons.add (new juce::TextButton (text)); b->setTooltip (tip); b->onClick = std::move (fn); addAndMakeVisible (b); resized(); }
    void resized() override { auto r = getLocalBounds(); const int n = buttons.size(); if (n == 0) return; const int w = r.getWidth() / n; for (auto* b : buttons) b->setBounds (r.removeFromLeft (w).reduced (1, 0)); }
    juce::OwnedArray<juce::TextButton> buttons;
};
struct BrowserItem { int id = 0; juce::String title, sub, category, searchText; };

// Searchable, filterable list with optional thumbnails, favourite stars and extra header components. Used by every browser tab.
class SearchList : public juce::Component, private juce::ListBoxModel
{
public:
    SearchList()
    {
        search.setTextToShowWhenEmpty ("Search...", juce::Colours::grey); search.onTextChange = [this] { refresh(); }; addAndMakeVisible (search);
        category.onChange = [this] { refresh(); }; addAndMakeVisible (category);
        favOnly.setButtonText (juce::String::fromUTF8 ("\xe2\x98\x85 only")); favOnly.setTooltip ("Show only favourites"); favOnly.onClick = [this] { refresh(); }; addAndMakeVisible (favOnly);
        list.setModel (this); list.setRowHeight (rowH); addAndMakeVisible (list);
    }
    std::function<std::vector<BrowserItem>()> source;
    std::function<bool (int)> isFav;  std::function<void (int)> toggleFav;
    std::function<void (int)> onPick, onActivate; std::function<void (int, juce::PopupMenu&)> contextMenu; std::function<void (int, int)> onContextChoice;
    std::function<void (juce::Graphics&, juce::Rectangle<int>, int)> paintThumb; std::function<juce::String (int)> tooltipFor;
    std::function<bool (int)> isSelected;
    int thumbW = 0;                                                  // 0 = no thumbnail column
    void setRowHeight (int h) { rowH = h; list.setRowHeight (h); }
    void addHeaderComponent (juce::Component* c, int height) { headers.add (c); headerH.add (height); addAndMakeVisible (c); resized(); }

    void refresh()
    {
        all = source ? source() : std::vector<BrowserItem>();
        juce::StringArray cats; cats.add ("All categories"); for (auto& i : all) if (i.category.isNotEmpty()) cats.addIfNotAlreadyThere (i.category);
        const juce::String cur = category.getText();
        if (cats != lastCats) { category.clear (juce::dontSendNotification); category.addItemList (cats, 1); category.setText (cats.contains (cur) ? cur : cats[0], juce::dontSendNotification); lastCats = cats; }
        shown.clear(); const juce::String q = search.getText().trim(); const bool all_ = category.getSelectedItemIndex() <= 0; const juce::String cat = category.getText();
        for (auto& i : all)
        {
            if (! all_ && i.category != cat) continue; if (favOnly.getToggleState() && isFav && ! isFav (i.id)) continue;
            if (q.isNotEmpty()) { bool ok = true; for (auto& w : juce::StringArray::fromTokens (q, " ", "")) if (! (i.title + " " + i.sub + " " + i.category + " " + i.searchText).containsIgnoreCase (w)) ok = false; if (! ok) continue; }
            shown.push_back (i);
        }
        list.updateContent(); list.repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4); auto top = r.removeFromTop (26); favOnly.setBounds (top.removeFromRight (62)); top.removeFromRight (4); search.setBounds (top);
        r.removeFromTop (4); category.setBounds (r.removeFromTop (24)); r.removeFromTop (4);
        for (int i = 0; i < headers.size(); ++i) { headers[i]->setBounds (r.removeFromTop (headerH[i])); r.removeFromTop (3); }
        list.setBounds (r);
    }
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= (int) shown.size()) return; auto& it = shown[(size_t) row]; auto& lf = static_cast<MonieLnF&> (getLookAndFeel());
        const bool picked = isSelected ? isSelected (it.id) : sel;
        g.setColour (picked ? lf.t.accent.withAlpha (.28f) : (row % 2 ? lf.t.panel : lf.t.panel2.withAlpha (.5f))); g.fillRect (0, 0, w, h - 1);
        int x = 4; if (thumbW > 0 && paintThumb) { auto tr = juce::Rectangle<int> (4, 3, thumbW, h - 7); g.saveState(); g.reduceClipRegion (tr); paintThumb (g, tr, it.id); g.restoreState(); g.setColour (lf.t.line); g.drawRect (tr); x = thumbW + 10; }
        g.setColour (lf.t.text); g.setFont (juce::FontOptions (13.f, juce::Font::bold)); g.drawText (it.title, x, 4, w - x - 30, 18, juce::Justification::centredLeft, true);
        g.setColour (lf.t.dim); g.setFont (11.f); g.drawText (it.sub, x, 22, w - x - 30, h - 24, juce::Justification::topLeft, true);
        if (isFav) { const bool f = isFav (it.id); g.setColour (f ? juce::Colour (0xffffc83d) : lf.t.dim); g.setFont (16.f); g.drawText (juce::String::fromUTF8 (f ? "\xe2\x98\x85" : "\xe2\x98\x86"), w - 26, 2, 22, 22, juce::Justification::centred); }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) shown.size()) return; const int id = shown[(size_t) row].id;
        if (isFav && e.x > getWidth() - 34 && toggleFav) { toggleFav (id); list.repaint(); return; }
        if (e.mods.isPopupMenu() && contextMenu) { juce::PopupMenu m; contextMenu (id, m); if (m.getNumItems() > 0) m.showMenuAsync (juce::PopupMenu::Options(), [this, id] (int r) { if (r > 0 && onContextChoice) onContextChoice (id, r); }); return; }
        if (onPick) onPick (id);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (row >= 0 && row < (int) shown.size() && onActivate) onActivate (shown[(size_t) row].id); }
    juce::String getTooltipForRow (int row) override { return row >= 0 && row < (int) shown.size() && tooltipFor ? tooltipFor (shown[(size_t) row].id) : juce::String(); }
    void repaintList() { list.repaint(); }
    juce::ListBox list { {}, nullptr }; juce::TextEditor search; juce::ComboBox category; juce::ToggleButton favOnly;
private:
    int rowH = 48; std::vector<BrowserItem> all, shown; juce::StringArray lastCats; juce::Array<juce::Component*> headers; juce::Array<int> headerH;
};
}
