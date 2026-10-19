#pragma once
#include "../PluginProcessor.h"
#include "../CaptionService.h"
#include "PreviewComponent.h"
#include "PropPanel.h"

namespace ui
{
struct UiCtx
{
    MonieProcessor& proc; PreviewComponent& preview;
    int selectedLayer = -1;                              // layer id, -1 = none
    int fxTarget = 0;                                    // 0 = selected layer, 1 = global stack
    juce::String filter;                                 // parameter search text (case-insensitive)
    std::function<void()> rebuild;                       // ask the host window to rebuild the current inspector
    std::function<void (int)> select;                    // change selection
    std::function<void()> relayout;
    bool match (const juce::String& label, const juce::String& section = {}) const { return filter.isEmpty() || label.containsIgnoreCase (filter) || section.containsIgnoreCase (filter); }
    const monie::Project& project() const { return proc.store.get(); }
    double layerTime (const monie::Layer& l) const { return juce::jmax (0.0, proc.clockNow() - l.clipStart); }
};

void buildLayerProps  (PropPanel&, UiCtx&);              // schema driven: every parameter of the selected layer
void buildLayerAnims  (PropPanel&, UiCtx&);              // animation presets on the selected layer
void buildFxStack     (PropPanel&, UiCtx&);              // effect stack (selected layer or global)
void buildScenePanel  (PropPanel&, UiCtx&);              // scene info + 8 knobs + randomise / reset / copy / paste / A-B
void buildGlobalsPanel(PropPanel&, UiCtx&, const juce::StringArray& sections);
void buildColorPanel  (PropPanel&, UiCtx&);              // gradient editor + grade
void buildAudioPanel  (PropPanel&, UiCtx&);              // sensitivity + mapping editor
void buildCaptionPanel(PropPanel&, UiCtx&);              // toggle, status, recognition setup, style
void buildProjectPanel(PropPanel&, UiCtx&);              // canvas, duration, loop, markers
void buildSettingsPanel(PropPanel&, UiCtx&);             // theme, tools, quality
void buildPresetPanel (PropPanel&, UiCtx&);              // A/B, randomise with locks, preset kinds

juce::File presetsDir();
juce::File userDataDir();
void setParamPlain (MonieProcessor&, const juce::String& id, float plain);
void paramGesture (MonieProcessor&, const juce::String& id, bool begin);
std::function<void (int, juce::Colour)> themeChangedHook();
void setThemeHook (std::function<void (int, juce::Colour)>);   // set by the editor
}

namespace ui
{
void chooseFile (const juce::String& title, const juce::File& start, const juce::String& wildcard, bool save, std::function<void (juce::File)> cb);
void chooseFolder (const juce::String& title, const juce::File& start, std::function<void (juce::File)> cb);
enum PresetKind { PresetProject = 0, PresetFx, PresetTextStyle, PresetPalette, PresetMapping, PresetCaptionStyle, NumPresetKinds };
const char* presetExt (int kind); const char* presetKindName (int kind);
void savePreset (UiCtx&, int kind);
bool loadPresetFile (UiCtx&, const juce::File&, juce::String* err = nullptr);
juce::PropertiesFile& userSettings();
void applyTextStyle (UiCtx&, const std::map<std::string, std::string>& vals);   // numeric values given as strings
extern int gSelectedStop;
}
