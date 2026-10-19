#pragma once
#include "PluginProcessor.h"
#include "UI/Inspectors.h"
#include "UI/LayerTimeline.h"
#include "UI/PreviewComponent.h"
#include "UI/PropPanel.h"
#include "UI/SearchList.h"
#include "UI/Theme.h"

class ExportJob;
#include <memory>

// The full editor window: top bar, workspace tabs, browser (left), preview (centre), inspector (right), layers + timeline (bottom).
class MonieEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::KeyListener
{
public:
    explicit MonieEditor (MonieProcessor&);
    ~MonieEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void parentHierarchyChanged() override;
    enum Workspace { Overview, Scenes, Media, Text, Captions, Effects, Colors, Audio, Layers, Timeline, Presets, Settings, NumWorkspaces };
    void setWorkspace (int ws);
    void applyTheme (int id, juce::Colour accent);
    void rebuildInspector();
    void selectLayer (int id);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void showExportDialog();
    void newProject(); void openProject(); void saveProject (bool saveAs);

private:
    void timerCallback() override;
    void storeChanged();
    juce::String structureKey() const;
    void addLayer (int kind);
    void setupBrowsers();
    void showLeft (juce::Component* c);
    void layoutAll();

    MonieProcessor& proc;
    ui::MonieLnF lnf;
    PreviewComponent preview;
    ui::LayerTimeline layers;
    ui::UiCtx ctx;
    juce::TooltipWindow tooltip { this, 500 };

    // top bar
    juce::Label brand; juce::TextEditor projectName; juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, openBtn { "Open" }, saveBtn { "Save" }, newBtn { "New" };
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" }, exportBtn { "Export video..." }, focusBtn { "Focus" };
    juce::Label timeLabel, pill, perf; juce::ComboBox aspectBox, qualityBox;
    juce::TextButton splitBtn { "Before/After" }, gridBtn { "Grid" }, safeBtn { "Safe" }, fitBtn { "Fit" }, zoomBtn { "100%" };
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextEditor paramSearch;
    juce::Label statusLabel;

    // browsers
    ui::SearchList scenesList, fontsList, fxList, palList, presetList, mediaList;
    juce::Component* leftShown = nullptr;
    std::vector<std::pair<int, std::string>> mediaEntries; std::vector<juce::String> fontNames; std::vector<juce::File> presetFiles;
    ui::ButtonStrip fontHeader, fxHeader, mediaHeader, presetHeader, palHeader, sceneHeader;

    ui::PropView inspector;
    int workspace = Overview; bool focusMode = false; juce::String lastKey; uint64_t lastRev = 0; int tick = 0;
    juce::File currentFile; std::unique_ptr<juce::DialogWindow> exportWindow;
    juce::String shotDir; int shotStage = 0, shotWait = 0, lastThumbs = 0; std::unique_ptr<ExportJob> testJob; juce::String exportTestDir; juce::Rectangle<int> rTop, rTabs, rLeft, rRight, rBottom, rStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonieEditor)
};
