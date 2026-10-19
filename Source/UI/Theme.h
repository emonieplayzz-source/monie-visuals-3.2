#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
struct Theme { juce::Colour bg, panel, panel2, line, text, dim, accent, accentText; };
inline const char* themeNames() { return "Dark|Extra dark|Monochrome|High contrast"; }
inline Theme makeTheme (int id, juce::Colour accent)
{
    using C = juce::Colour;
    switch (id)
    {
        case 1:  return { C (0xff000000), C (0xff07070a), C (0xff0d0d12), C (0xff1b1b22), C (0xffe6e6ec), C (0xff7d7d89), accent, C (0xff000000) };
        case 2:  return { C (0xff141414), C (0xff1c1c1c), C (0xff242424), C (0xff333333), C (0xffe8e8e8), C (0xff8a8a8a), C (0xffd0d0d0), C (0xff000000) };
        case 3:  return { C (0xff000000), C (0xff000000), C (0xff0a0a0a), C (0xffffffff), C (0xffffffff), C (0xffcfcfcf), C (0xffffd400), C (0xff000000) };
        default: return { C (0xff0f0f14), C (0xff16161d), C (0xff1d1d26), C (0xff2a2a35), C (0xffe9e9f0), C (0xff8b8b9a), accent, C (0xff0a0a0f) };
    }
}

class MonieLnF : public juce::LookAndFeel_V4
{
public:
    MonieLnF() { setTheme (0, juce::Colour (0xff7c5cff)); }
    Theme t;
    void setTheme (int id, juce::Colour accent)
    {
        t = makeTheme (id, accent);
        setColour (juce::ResizableWindow::backgroundColourId, t.bg);
        setColour (juce::Label::textColourId, t.text); setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, t.panel2); setColour (juce::TextButton::buttonOnColourId, t.accent);
        setColour (juce::TextButton::textColourOffId, t.text); setColour (juce::TextButton::textColourOnId, t.accentText);
        setColour (juce::ToggleButton::textColourId, t.text); setColour (juce::ToggleButton::tickColourId, t.accent); setColour (juce::ToggleButton::tickDisabledColourId, t.dim);
        setColour (juce::ComboBox::backgroundColourId, t.panel2); setColour (juce::ComboBox::textColourId, t.text); setColour (juce::ComboBox::outlineColourId, t.line); setColour (juce::ComboBox::arrowColourId, t.dim);
        setColour (juce::PopupMenu::backgroundColourId, t.panel2); setColour (juce::PopupMenu::textColourId, t.text); setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent); setColour (juce::PopupMenu::highlightedTextColourId, t.accentText);
        setColour (juce::TextEditor::backgroundColourId, t.bg); setColour (juce::TextEditor::textColourId, t.text); setColour (juce::TextEditor::outlineColourId, t.line); setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (.4f)); setColour (juce::CaretComponent::caretColourId, t.accent);
        setColour (juce::Slider::thumbColourId, t.accent); setColour (juce::Slider::trackColourId, t.accent); setColour (juce::Slider::backgroundColourId, t.line);
        setColour (juce::Slider::textBoxTextColourId, t.text); setColour (juce::Slider::textBoxBackgroundColourId, t.bg); setColour (juce::Slider::textBoxOutlineColourId, t.line);
        setColour (juce::ListBox::backgroundColourId, t.panel); setColour (juce::ListBox::textColourId, t.text); setColour (juce::ListBox::outlineColourId, t.line);
        setColour (juce::ScrollBar::thumbColourId, t.line.brighter (.3f));
        setColour (juce::TooltipWindow::backgroundColourId, t.panel2); setColour (juce::TooltipWindow::textColourId, t.text); setColour (juce::TooltipWindow::outlineColourId, t.accent);
        setColour (juce::AlertWindow::backgroundColourId, t.panel2); setColour (juce::AlertWindow::textColourId, t.text); setColour (juce::AlertWindow::outlineColourId, t.accent);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, t.line); setColour (juce::TabbedComponent::backgroundColourId, t.panel);
    }
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
        const float cy = (float) y + (float) h * .5f, th = 4.f;
        g.setColour (t.line); g.fillRoundedRectangle ((float) x, cy - th / 2, (float) w, th, 2.f);
        g.setColour (s.isEnabled() ? t.accent : t.dim); g.fillRoundedRectangle ((float) x, cy - th / 2, juce::jmax (0.f, pos - (float) x), th, 2.f);
        g.setColour (t.text); g.fillEllipse (pos - 5.f, cy - 5.f, 10.f, 10.f);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (.5f); auto c = bg;
        if (down) c = c.brighter (.2f); else if (over) c = c.brighter (.1f);
        g.setColour (c); g.fillRoundedRectangle (r, 4.f); g.setColour (t.line); g.drawRoundedRectangle (r, 4.f, 1.f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::Font (juce::FontOptions ((float) juce::jmin (13, h - 8))); }
};
}
