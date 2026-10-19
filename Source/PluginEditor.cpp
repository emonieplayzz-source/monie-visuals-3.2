#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "ExportJob.h"
#include "CaptionService.h"

using namespace monie;

namespace
{
// ============================================================================================ export dialog
class ExportDialog : public juce::Component, private juce::Timer
{
public:
    ExportDialog (MonieProcessor& p, PreviewComponent& pv) : proc (p), preview (pv)
    {
        auto lbl = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); addAndMakeVisible (l); };
        lbl (lOut, "Output video (.mp4)"); lbl (lAudio, "Audio file to react to"); lbl (lRes, "Height"); lbl (lFps, "Frame rate"); lbl (lStart, "Start (s)"); lbl (lEnd, "End (s)"); lbl (lCrf, "Quality (lower = better)");
        const auto& P = proc.store.get();
        out.setText (juce::File::getSpecialLocation (juce::File::userMoviesDirectory).getChildFile ("monie_visuals.mp4").getFullPathName(), false);
        for (auto* e : { &out, &audio, &startEd, &endEd }) addAndMakeVisible (e);
        audio.setTextToShowWhenEmpty ("Choose the song or stem to analyse", juce::Colours::grey);
        startEd.setText (P.loopOn ? juce::String (P.loopA, 2) : "0", false); endEd.setText (P.loopOn ? juce::String (P.loopB, 2) : juce::String (juce::jmin (30.0, P.duration), 2), false);
        res.addItemList ({ "720", "1080", "1440", "2160" }, 1); res.setSelectedItemIndex (1); fps.addItemList ({ "24", "30", "60" }, 1); fps.setSelectedItemIndex (1);
        addAndMakeVisible (res); addAndMakeVisible (fps);
        crf.setRange (14, 30, 1); crf.setValue (18); crf.setSliderStyle (juce::Slider::LinearHorizontal); crf.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20); addAndMakeVisible (crf);
        captions.setButtonText ("Burn captions into the video"); captions.setToggleState (true, juce::dontSendNotification); captions.setTooltip ("Turn off for a clean export (captions layers are hidden)."); addAndMakeVisible (captions);
        srt.setButtonText ("Also write an .srt caption file"); addAndMakeVisible (srt);
        browseOut.setButtonText ("..."); browseAudio.setButtonText ("..."); startBtn.setButtonText ("Start export"); cancelBtn.setButtonText ("Cancel"); cancelBtn.setEnabled (false);
        for (auto* b : { &browseOut, &browseAudio, &startBtn, &cancelBtn }) addAndMakeVisible (b);
        status.setJustificationType (juce::Justification::topLeft); addAndMakeVisible (status); addAndMakeVisible (bar);
        status.setText (FfTools::available() ? "Exports analyse the audio file you choose (the plugin cannot capture the host's audio offline), render every frame on the GPU and encode with FFmpeg." : "FFmpeg was not found. Set its folder in SETTINGS > Tools first.", juce::dontSendNotification);
        browseOut.onClick = [this] { ui::chooseFile ("Save video as", juce::File (out.getText()), "*.mp4", true, [this] (juce::File f) { out.setText (f.withFileExtension ("mp4").getFullPathName(), false); }); };
        browseAudio.onClick = [this] { ui::chooseFile ("Audio file", juce::File(), "*.wav;*.mp3;*.flac;*.ogg;*.m4a;*.aiff", false, [this] (juce::File f) { audio.setText (f.getFullPathName(), false);
            juce::AudioFormatManager fm; fm.registerBasicFormats(); if (std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); r && r->sampleRate > 0) endEd.setText (juce::String ((double) r->lengthInSamples / r->sampleRate, 2), false); }); };
        startBtn.onClick = [this] { start(); }; cancelBtn.onClick = [this] { if (job) job->cancel(); };
        setSize (560, 420);
    }
    ~ExportDialog() override { stopTimer(); job.reset(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12); auto row = [&] (juce::Label& l, juce::Component& c, int extra = 0) { auto a = r.removeFromTop (26); l.setBounds (a.removeFromLeft (170)); if (extra) { (extra == 1 ? browseOut : browseAudio).setBounds (a.removeFromRight (30)); a.removeFromRight (4); } c.setBounds (a); r.removeFromTop (5); };
        row (lOut, out, 1); row (lAudio, audio, 2); row (lRes, res); row (lFps, fps); row (lStart, startEd); row (lEnd, endEd); row (lCrf, crf);
        captions.setBounds (r.removeFromTop (24)); srt.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        auto b = r.removeFromTop (30); startBtn.setBounds (b.removeFromLeft (200)); b.removeFromLeft (8); cancelBtn.setBounds (b.removeFromLeft (120)); r.removeFromTop (8);
        bar.setBounds (r.removeFromTop (20)); r.removeFromTop (6); status.setBounds (r);
    }
private:
    void start()
    {
        monie::ExportSettings s; s.output = out.getText().toStdString(); s.audioFile = audio.getText().toStdString(); const int h = res.getText().getIntValue(); const float asp = monie::canvasAspectOf (proc.store.get());
        s.height = h; s.width = (int) std::lround (h * asp); s.fps = fps.getText().getIntValue(); s.start = startEd.getText().getDoubleValue(); s.end = endEd.getText().getDoubleValue(); s.crf = (int) crf.getValue();
        s.captions = captions.getToggleState(); s.writeSrt = srt.getToggleState(); monie::evenSize (s.width, s.height);
        if (auto why = monie::validateExport (s, FfTools::available()); ! why.empty()) { status.setText (juce::String (why), juce::dontSendNotification); return; }
        auto go = [this, s] { job = std::make_unique<ExportJob> (proc, preview, s); startBtn.setEnabled (false); cancelBtn.setEnabled (true); progress = 0; job->start(); startTimerHz (8); };
        if (juce::File (out.getText()).existsAsFile())
        {
            juce::Component::SafePointer<ExportDialog> sp (this);
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace file?", "The output file already exists. Replace it?", "Replace", "Cancel", this, juce::ModalCallbackFunction::create ([sp, go] (int r) { if (r == 1 && sp != nullptr) go(); }));
        }
        else go();
    }
    void timerCallback() override
    {
        if (! job) return; progress = job->progress(); const double eta = job->etaSeconds();
        status.setText (job->status() + (eta >= 0 && ! job->finished() ? "   (about " + juce::String ((int) eta) + " s left)" : juce::String()), juce::dontSendNotification);
        if (job->finished()) { stopTimer(); startBtn.setEnabled (true); cancelBtn.setEnabled (false); if (job->succeeded()) progress = 1.0; }
    }
    MonieProcessor& proc; PreviewComponent& preview; std::unique_ptr<ExportJob> job; double progress = 0;
    juce::Label lOut, lAudio, lRes, lFps, lStart, lEnd, lCrf, status; juce::TextEditor out, audio, startEd, endEd; juce::ComboBox res, fps; juce::Slider crf; juce::ToggleButton captions, srt;
    juce::TextButton browseOut, browseAudio, startBtn, cancelBtn; juce::ProgressBar bar { progress };
};
}

// ============================================================================================ editor
MonieEditor::MonieEditor (MonieProcessor& p) : juce::AudioProcessorEditor (p), proc (p), preview (p), layers (p), ctx { p, preview }
{
    auto& us = ui::userSettings();
    if (auto f = us.getValue ("ffmpegFolder"); f.isNotEmpty()) FfTools::setFolder (juce::File (f));
    if (int w = us.getIntValue ("proxyW", 0); w > 0) proc.media.setProxyWidth (w);
    lnf.setTheme (us.getIntValue ("theme", 0), juce::Colour::fromString (us.getValue ("accent", "ff7c5cff")));
    setLookAndFeel (&lnf);
    ui::setThemeHook ([this] (int id, juce::Colour c) { applyTheme (id, c); });

    addAndMakeVisible (preview); addAndMakeVisible (layers); addAndMakeVisible (inspector);
    ctx.rebuild = [this] { juce::Component::SafePointer<MonieEditor> sp (this); juce::MessageManager::callAsync ([sp] { if (sp) sp->rebuildInspector(); }); };
    ctx.select = [this] (int id) { selectLayer (id); };
    ctx.relayout = [this] { inspector.panel.refreshValues(); };

    brand.setText ("monie visuals", juce::dontSendNotification); brand.setFont (juce::FontOptions (17.f, juce::Font::bold)); addAndMakeVisible (brand);
    projectName.setTooltip ("Project name"); projectName.onTextChange = [this] { auto t = projectName.getText(); proc.store.edit ("Rename project", [=] (Project& pr) { pr.name = t.toStdString(); }, "pname"); }; addAndMakeVisible (projectName);
    for (auto* b : { &newBtn, &openBtn, &saveBtn, &undoBtn, &redoBtn, &playBtn, &stopBtn, &exportBtn, &focusBtn, &splitBtn, &gridBtn, &safeBtn, &fitBtn, &zoomBtn }) addAndMakeVisible (b);
    newBtn.setTooltip ("Start a new project"); openBtn.setTooltip ("Open a project file (Ctrl+O)"); saveBtn.setTooltip ("Save the project to a file (Ctrl+S). The project is also saved inside your DAW project automatically.");
    undoBtn.setTooltip ("Undo (Ctrl+Z)"); redoBtn.setTooltip ("Redo (Ctrl+Y)"); playBtn.setTooltip ("Play / pause the timeline (Space). In a DAW the host transport controls time."); stopBtn.setTooltip ("Back to the start (Home)");
    exportBtn.setTooltip ("Render a video file with FFmpeg"); focusBtn.setTooltip ("Hide the side panels to see the preview larger"); splitBtn.setTooltip ("Compare before/after the effects and colour grade (B)"); gridBtn.setTooltip ("Grid and centre guides (G)");
    safeBtn.setTooltip ("Title-safe area guide"); fitBtn.setTooltip ("Fit the whole picture in view (F)"); zoomBtn.setTooltip ("Zoom to 100%. Mouse wheel zooms, middle-drag pans.");
    newBtn.onClick = [this] { newProject(); }; openBtn.onClick = [this] { openProject(); }; saveBtn.onClick = [this] { saveProject (false); };
    undoBtn.onClick = [this] { proc.store.undo(); }; redoBtn.onClick = [this] { proc.store.redo(); };
    playBtn.onClick = [this] { if (proc.hostControlled()) return; if (proc.transportIsPlaying()) proc.transportPause(); else proc.transportPlay(); }; stopBtn.onClick = [this] { if (! proc.hostControlled()) proc.transportStop(); };
    exportBtn.onClick = [this] { showExportDialog(); }; focusBtn.onClick = [this] { focusMode = ! focusMode; focusBtn.setToggleState (focusMode, juce::dontSendNotification); layoutAll(); };
    splitBtn.setClickingTogglesState (true); splitBtn.onClick = [this] { preview.setSplit (splitBtn.getToggleState()); };
    gridBtn.setClickingTogglesState (true); safeBtn.setClickingTogglesState (true);
    auto guides = [this] { unsigned g = 0; if (gridBtn.getToggleState()) g |= PreviewComponent::Grid | PreviewComponent::Cross | PreviewComponent::Thirds; if (safeBtn.getToggleState()) g |= PreviewComponent::Safe; preview.setGuides (g); };
    gridBtn.onClick = guides; safeBtn.onClick = guides; fitBtn.onClick = [this] { preview.setZoomMode (0); }; zoomBtn.onClick = [this] { preview.setZoomMode (2, 1.f); };
    for (auto* b : { &splitBtn, &gridBtn, &safeBtn, &focusBtn }) b->setColour (juce::TextButton::buttonOnColourId, lnf.t.accent);
    aspectBox.addItemList (juce::StringArray::fromTokens (aspectNames(), "|", ""), 1); aspectBox.setTooltip ("Canvas shape");
    aspectBox.onChange = [this] { const int i = aspectBox.getSelectedItemIndex(); if (i != proc.store.get().canvasAspect) proc.store.edit ("Canvas shape", [=] (Project& pr) { pr.canvasAspect = i; }); }; addAndMakeVisible (aspectBox);
    qualityBox.addItemList (juce::StringArray::fromTokens (qualityNames(), "|", ""), 1); qualityBox.setTooltip ("Preview quality - lower it if your computer struggles while producing.");
    qualityBox.onChange = [this] { ui::setParamPlain (proc, "quality", (float) qualityBox.getSelectedItemIndex()); }; addAndMakeVisible (qualityBox);
    for (auto* l : { &timeLabel, &pill, &perf, &statusLabel }) addAndMakeVisible (l);
    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.f, juce::Font::plain)); pill.setJustificationType (juce::Justification::centred); perf.setJustificationType (juce::Justification::centredRight); statusLabel.setFont (12.f);
    const char* names[] = { "OVERVIEW", "SCENES", "MEDIA", "TEXT", "CAPTIONS", "EFFECTS", "COLORS", "AUDIO", "LAYERS", "TIMELINE", "PRESETS", "SETTINGS" };
    for (int i = 0; i < NumWorkspaces; ++i) { auto* b = tabButtons.add (new juce::TextButton (names[i])); b->setClickingTogglesState (false); b->onClick = [this, i] { setWorkspace (i); }; addAndMakeVisible (b); }
    paramSearch.setTextToShowWhenEmpty ("Search parameters (Ctrl+F)", juce::Colours::grey); paramSearch.onTextChange = [this] { rebuildInspector(); }; addAndMakeVisible (paramSearch);

    setupBrowsers();
    layers.onSelect = [this] (int id) { selectLayer (id); }; layers.onAddLayer = [this] (int k) { addLayer (k); };
    preview.onLayerPicked = [this] (int id) { selectLayer (id); };
    preview.onLayerDragged = [this] (int id, juce::Point<float> pos)
    {
        auto* l = proc.store.get().find (id); if (! l || l->locked) return; const float asp = preview.canvasAspect(); const auto& sch = schemaFor (l->type); const PDef* dx = findDef (sch, "x"); const PDef* dy = findDef (sch, "y"); if (! dx || ! dy) return;
        const float nx = PropBag::clampTo (*dx, pos.x / asp), ny = PropBag::clampTo (*dy, pos.y);
        proc.store.edit ("Move layer", [=] (Project& pr) { if (auto* x = pr.find (id)) { x->props.f["x"] = nx; x->props.f["y"] = ny; } }, "drag" + juce::String (id));
    };
    proc.store.addListener ([this] { storeChanged(); });

    // restore imported fonts
    for (auto& f : proc.store.get().importedFonts) FontRegistry::get().importFont (juce::File (juce::String (f)));
    if (auto& L = proc.store.get().layers; ! L.empty()) ctx.selectedLayer = L.back().id;
    layers.selected = ctx.selectedLayer; preview.setSelectedLayer (ctx.selectedLayer);

    setResizable (true, true); setResizeLimits (1000, 640, 4096, 2400);
    setSize (juce::jlimit (1000, 3000, us.getIntValue ("winW", 1500)), juce::jlimit (640, 2000, us.getIntValue ("winH", 900)));
    addKeyListener (this); setWantsKeyboardFocus (true);
    shotDir = juce::SystemStats::getEnvironmentVariable ("MONIE_SELFTEST", "");
    if (auto w = juce::SystemStats::getEnvironmentVariable ("MONIE_WS", ""); w.isNotEmpty()) workspace = w.getIntValue();
    setWorkspace (workspace); storeChanged();
    startTimerHz (10);
}
MonieEditor::~MonieEditor()
{
    stopTimer(); testJob.reset(); ui::setThemeHook ({}); exportWindow.reset();
    auto& us = ui::userSettings(); us.setValue ("winW", getWidth()); us.setValue ("winH", getHeight()); us.setValue ("workspace", workspace); us.saveIfNeeded();
    setLookAndFeel (nullptr);
}
void MonieEditor::parentHierarchyChanged() {}
void MonieEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.t.bg); g.setColour (lnf.t.panel); g.fillRect (rTop); g.fillRect (rTabs); g.setColour (lnf.t.line); g.drawHorizontalLine (rTop.getBottom() - 1, 0, (float) getWidth()); g.drawHorizontalLine (rTabs.getBottom() - 1, 0, (float) getWidth());
    if (! rRight.isEmpty()) { g.setColour (lnf.t.panel); g.fillRect (rRight); g.setColour (lnf.t.line); g.drawVerticalLine (rRight.getX(), (float) rRight.getY(), (float) rRight.getBottom()); }
    if (! rLeft.isEmpty()) { g.setColour (lnf.t.panel); g.fillRect (rLeft); g.setColour (lnf.t.line); g.drawVerticalLine (rLeft.getRight() - 1, (float) rLeft.getY(), (float) rLeft.getBottom()); }
    g.setColour (lnf.t.panel); g.fillRect (rStatus);
}
void MonieEditor::applyTheme (int id, juce::Colour accent)
{
    lnf.setTheme (id, accent); for (auto* b : { &splitBtn, &gridBtn, &safeBtn, &focusBtn }) b->setColour (juce::TextButton::buttonOnColourId, lnf.t.accent);
    sendLookAndFeelChange(); repaint(); rebuildInspector();
}

void MonieEditor::layoutAll()
{
    auto r = getLocalBounds(); const int W = r.getWidth();
    rTop = r.removeFromTop (44); rTabs = r.removeFromTop (30);
    const int bh = focusMode ? 0 : (workspace == Timeline ? juce::jmin (380, r.getHeight() / 2) : juce::jlimit (160, 260, r.getHeight() / 4));
    rBottom = r.removeFromBottom (bh); rStatus = r.removeFromBottom (24);
    rRight = focusMode ? juce::Rectangle<int>() : r.removeFromRight (juce::jlimit (300, 460, W * 27 / 100));
    rLeft = (focusMode || leftShown == nullptr) ? juce::Rectangle<int>() : r.removeFromLeft (juce::jlimit (240, 380, W * 21 / 100));
    // top bar
    {
        auto t = rTop.reduced (8, 7); brand.setBounds (t.removeFromLeft (132)); projectName.setBounds (t.removeFromLeft (150)); t.removeFromLeft (8);
        for (auto* b : { &newBtn, &openBtn, &saveBtn }) { b->setBounds (t.removeFromLeft (46)); t.removeFromLeft (3); } t.removeFromLeft (6);
        undoBtn.setBounds (t.removeFromLeft (46)); t.removeFromLeft (3); redoBtn.setBounds (t.removeFromLeft (46)); t.removeFromLeft (14);
        playBtn.setBounds (t.removeFromLeft (50)); t.removeFromLeft (3); stopBtn.setBounds (t.removeFromLeft (46)); t.removeFromLeft (6); timeLabel.setBounds (t.removeFromLeft (150)); t.removeFromLeft (6);
        exportBtn.setBounds (t.removeFromRight (110)); t.removeFromRight (8); focusBtn.setBounds (t.removeFromRight (54)); t.removeFromRight (8); perf.setBounds (t.removeFromRight (150)); t.removeFromRight (8);
        pill.setBounds (t.removeFromRight (170)); t.removeFromRight (8); qualityBox.setBounds (t.removeFromRight (120)); t.removeFromRight (4); aspectBox.setBounds (t.removeFromRight (70));
    }
    { auto t = rTabs.reduced (6, 3); const int w = t.getWidth() / NumWorkspaces; for (auto* b : tabButtons) b->setBounds (t.removeFromLeft (w).reduced (1, 0)); }
    // status strip with view buttons
    { auto t = rStatus.reduced (6, 2); for (auto* b : { &splitBtn, &gridBtn, &safeBtn, &fitBtn, &zoomBtn }) { b->setBounds (t.removeFromRight (84)); t.removeFromRight (3); } statusLabel.setBounds (t); }
    preview.setBounds (r); layers.setBounds (rBottom); layers.setVisible (bh > 0); statusLabel.setVisible (true);
    for (auto* b : { &splitBtn, &gridBtn, &safeBtn, &fitBtn, &zoomBtn }) b->setVisible (true);
    if (! rRight.isEmpty()) { auto t = rRight.reduced (0); auto s = t.removeFromTop (30).reduced (6, 3); paramSearch.setBounds (s); inspector.setBounds (t); } inspector.setVisible (! rRight.isEmpty()); paramSearch.setVisible (! rRight.isEmpty());
    if (leftShown) leftShown->setBounds (rLeft); for (auto* c : { (juce::Component*) &scenesList, (juce::Component*) &fontsList, (juce::Component*) &fxList, (juce::Component*) &palList, (juce::Component*) &presetList, (juce::Component*) &mediaList }) c->setVisible (c == leftShown && ! rLeft.isEmpty());
    repaint();
}
void MonieEditor::resized() { layoutAll(); }

void MonieEditor::showLeft (juce::Component* c) { leftShown = c; }
void MonieEditor::setWorkspace (int ws)
{
    workspace = juce::jlimit (0, NumWorkspaces - 1, ws);
    for (int i = 0; i < tabButtons.size(); ++i) { tabButtons[i]->setToggleState (i == workspace, juce::dontSendNotification); tabButtons[i]->setColour (juce::TextButton::buttonColourId, i == workspace ? lnf.t.accent : lnf.t.panel2); tabButtons[i]->setColour (juce::TextButton::textColourOffId, i == workspace ? lnf.t.accentText : lnf.t.text); }
    switch (workspace)
    {
        case Overview: case Scenes: case Audio: showLeft (&scenesList); break;
        case Media: case Layers: showLeft (&mediaList); break;
        case Text: case Captions: showLeft (&fontsList); break;
        case Effects: showLeft (&fxList); break;
        case Colors: showLeft (&palList); break;
        case Presets: showLeft (&presetList); break;
        default: showLeft (nullptr); break;
    }
    // pick a sensible layer for the workspace
    if (workspace == Captions) { for (auto& l : proc.store.get().layers) if (l.type == LayerType::Caption) { selectLayer (l.id); break; } }
    if (workspace == Text) { auto* cur = proc.store.get().find (ctx.selectedLayer); if (! cur || (cur->type != LayerType::Text && cur->type != LayerType::Caption)) for (auto& l : proc.store.get().layers) if (l.type == LayerType::Text) { selectLayer (l.id); break; } }
    if (workspace == Media) { auto* cur = proc.store.get().find (ctx.selectedLayer); if (! cur || (cur->type != LayerType::Image && cur->type != LayerType::Video)) for (auto& l : proc.store.get().layers) if (l.type == LayerType::Image || l.type == LayerType::Video) { selectLayer (l.id); break; } }
    scenesList.refresh(); fontsList.refresh(); fxList.refresh(); palList.refresh(); presetList.refresh(); mediaList.refresh();
    layoutAll(); rebuildInspector();
}
void MonieEditor::selectLayer (int id)
{
    ctx.selectedLayer = id; layers.selected = id; preview.setSelectedLayer (id);
    layers.repaint(); mediaList.repaintList(); fontsList.repaintList(); rebuildInspector();
}

void MonieEditor::rebuildInspector()
{
    auto& pp = inspector.panel; ctx.filter = paramSearch.getText().trim(); pp.clear(); const auto* L = proc.store.get().find (ctx.selectedLayer);
    const bool isText = L && (L->type == LayerType::Text || L->type == LayerType::Caption);
    switch (workspace)
    {
        case Overview: ui::buildScenePanel (pp, ctx); ui::buildGlobalsPanel (pp, ctx, { "Motion", "Transitions", "Stylize" }); break;
        case Scenes: ui::buildScenePanel (pp, ctx); ui::buildGlobalsPanel (pp, ctx, { "Motion", "Transitions" }); break;
        case Media: ui::buildLayerProps (pp, ctx); if (L) ui::buildLayerAnims (pp, ctx); break;
        case Text:
            pp.section ("Quick styles", false, "Q:styles");
            pp.buttons ({ { "Neon", [this] { ui::applyTextStyle (ctx, { { "fill", "#ffffff" }, { "glow", "1.2" }, { "glowC", "#ff2a6d" }, { "o1w", "0" }, { "shOp", "0" } }); } }, { "Outline", [this] { ui::applyTextStyle (ctx, { { "fill", "#ffffff" }, { "o1w", "6" }, { "o1c", "#000000" }, { "glow", "0" } }); } },
                          { "Gradient", [this] { ui::applyTextStyle (ctx, { { "gradient", "1" }, { "fill", "#ffd27a" }, { "fill2", "#e8564a" }, { "gradAngle", "90" } }); } } });
            pp.buttons ({ { "Boxed", [this] { ui::applyTextStyle (ctx, { { "bg", "1" }, { "bgC", "#000000" }, { "bgOp", "0.7" }, { "fill", "#ffffff" } }); } }, { "Retro", [this] { ui::applyTextStyle (ctx, { { "fill", "#ffd9a0" }, { "o1w", "3" }, { "o1c", "#5c1a0b" }, { "shOp", "0.7" }, { "shDist", "8" }, { "rgb", "0.15" } }); } },
                          { "Glitchy", [this] { ui::applyTextStyle (ctx, { { "fill", "#ffffff" }, { "rgb", "0.5" }, { "noise", "0.3" }, { "distortion", "0.2" } }); } } });
            ui::buildLayerProps (pp, ctx); if (L) ui::buildLayerAnims (pp, ctx); if (! isText) pp.info ("Tip: add a text layer with + Layer in the layer panel."); break;
        case Captions: ui::buildCaptionPanel (pp, ctx); if (L && L->type == LayerType::Caption) ui::buildLayerProps (pp, ctx); break;
        case Effects:
            pp.buttons ({ { ctx.fxTarget == 0 ? "[ Selected layer ]" : "Selected layer", [this] { ctx.fxTarget = 0; fxList.repaintList(); rebuildInspector(); } }, { ctx.fxTarget == 1 ? "[ Global ]" : "Global", [this] { ctx.fxTarget = 1; rebuildInspector(); } } }, "Which effect stack the effect browser and this panel edit.");
            ui::buildFxStack (pp, ctx); break;
        case Colors: ui::buildColorPanel (pp, ctx); break;
        case Audio: ui::buildAudioPanel (pp, ctx); break;
        case Layers: ui::buildLayerProps (pp, ctx); if (L) { ui::buildLayerAnims (pp, ctx); ctx.fxTarget = 0; ui::buildFxStack (pp, ctx); } break;
        case Timeline: ui::buildProjectPanel (pp, ctx); if (L) { pp.section ("Keyframes on this layer", false, "TL:keys"); pp.info ("Click the diamond next to any slider to add a keyframe at the playhead. Click a diamond in the timeline to jump to it."); int n = 0; for (auto& kv : L->keys) n += (int) kv.second.all().size(); pp.info (juce::String ((int) L->keys.size()) + " animated parameters, " + juce::String (n) + " keyframes."); } break;
        case Presets: ui::buildPresetPanel (pp, ctx); break;
        case Settings: ui::buildSettingsPanel (pp, ctx); break;
    }
    pp.finish(); inspector.fit(); lastKey = structureKey();
}

juce::String MonieEditor::structureKey() const
{
    const auto& P = proc.store.get(); juce::String k; k << workspace << "|" << ctx.selectedLayer << "|" << proc.currentScene() << "|" << (int) P.layers.size() << "|" << ctx.fxTarget << "|" << paramSearch.getText() << "|";
    k << (int) P.fx.size() << "|" << (int) P.gradient.stops.size() << "|" << ui::gSelectedStop << "|" << (int) P.mapping.maps.size() << "|" << (int) P.markers.size() << "|" << (int) P.captionsOn << (int) P.asr.consent << (int) P.asr.enabled << "|" << (int) P.lyrics.lines.size() << "," << (int) P.autoLyrics.lines.size() << "|" << P.canvasAspect << "|";
    if (auto* l = P.find (ctx.selectedLayer)) { k << (int) l->type << "|" << (int) l->anims.size() << "|" << (int) l->fx.size() << "|" << l->media << "|" << (int) l->keys.size() << "|"; for (auto& a : l->anims) k << a.id << ":" << a.preset << ","; for (auto& f : l->fx) k << f.id << ":" << (int) f.enabled << ","; for (auto& f : P.fx) k << f.id << ":" << (int) f.enabled << ","; if (! l->media.empty()) k << (int) proc.media.info (l->media).missing; }
    if (proc.captions) k << proc.captions->stateName() << proc.captions->message().length();
    return k;
}
void MonieEditor::storeChanged()
{
    const auto& P = proc.store.get();
    if (aspectBox.getSelectedItemIndex() != P.canvasAspect) aspectBox.setSelectedItemIndex (P.canvasAspect, juce::dontSendNotification);
    if (! projectName.hasKeyboardFocus (true) && projectName.getText() != juce::String (P.name)) projectName.setText (juce::String (P.name), false);
    undoBtn.setEnabled (proc.store.canUndo()); redoBtn.setEnabled (proc.store.canRedo()); undoBtn.setTooltip (proc.store.canUndo() ? "Undo: " + proc.store.undoLabel() : "Nothing to undo"); redoBtn.setTooltip (proc.store.canRedo() ? "Redo: " + proc.store.redoLabel() : "Nothing to redo");
    if (ctx.selectedLayer >= 0 && ! P.find (ctx.selectedLayer)) { ctx.selectedLayer = P.layers.empty() ? -1 : P.layers.back().id; layers.selected = ctx.selectedLayer; preview.setSelectedLayer (ctx.selectedLayer); }
    mediaList.refresh(); presetList.repaintList(); scenesList.repaintList(); fontsList.repaintList(); fxList.repaintList(); palList.repaintList(); layers.repaint();
    if (structureKey() != lastKey) ctx.rebuild(); else inspector.panel.refreshValues();
}

void MonieEditor::timerCallback()
{
    ++tick;
    if (const int th = preview.comp().thumbsReady(); th != lastThumbs) { lastThumbs = th; scenesList.repaintList(); }
    const double t = proc.clockNow(); const int m = (int) t / 60; timeLabel.setText (juce::String (m) + ":" + juce::String (t - m * 60, 2).paddedLeft ('0', 5) + " / " + juce::String ((int) proc.store.get().duration / 60) + ":" + juce::String ((int) proc.store.get().duration % 60).paddedLeft ('0', 2), juce::dontSendNotification);
    const bool host = proc.hostControlled(); playBtn.setButtonText (host ? "HOST" : proc.transportIsPlaying() ? "Pause" : "Play"); playBtn.setEnabled (! host); stopBtn.setEnabled (! host);
    const auto st = preview.status(); const auto& P = proc.store.get();
    juce::String ps = proc.captions ? proc.captions->stateName() : "OFF"; if (! P.asr.enabled) ps = P.captionsOn ? "CAPTIONS ON" : "CAPTIONS OFF";
    pill.setText (ps, juce::dontSendNotification); const bool err = ps == "ERROR"; const bool on = ps != "CAPTIONS OFF" && ps != "OFF";
    pill.setColour (juce::Label::backgroundColourId, err ? juce::Colour (0xffa31b2b) : on ? lnf.t.accent.withAlpha (.35f) : lnf.t.panel2); pill.setColour (juce::Label::textColourId, lnf.t.text);
    perf.setText (juce::String (st.fps, 0) + " fps  " + juce::String (st.frameMs, 1) + " ms  " + juce::String (st.canvasW) + "x" + juce::String (st.canvasH) + (proc.audioActive() ? "  audio ok" : "  no audio"), juce::dontSendNotification);
    perf.setColour (juce::Label::textColourId, proc.audioActive() ? lnf.t.dim : juce::Colour (0xffffb454));
    juce::String s; juce::Colour sc = lnf.t.dim;
    if (st.shaderError.isNotEmpty()) { s = "SHADER ERROR: " + st.shaderError.upToFirstOccurrenceOf ("\n", false, false).substring (0, 140); sc = juce::Colour (0xffff5c6c); }
    else if (st.missingMedia > 0) { s = "MISSING MEDIA: " + juce::String (st.missingMedia) + " file(s) not found - open MEDIA and use Relink"; sc = juce::Colour (0xffffb454); }
    else if (! st.fontFallbacks.empty()) { s = "FONT FALLBACK: " + st.fontFallbacks[0] + " is not installed - using a default font"; sc = juce::Colour (0xffffb454); }
    else s = "GPU: " + st.gpu + "   |   Click a layer in the preview to select it, drag to move it";
    statusLabel.setText (s, juce::dontSendNotification); statusLabel.setColour (juce::Label::textColourId, sc);
    if (tick % 3 == 0 && ! inspector.panel.isMouseButtonDownAnywhere()) { if (structureKey() != lastKey) ctx.rebuild(); else inspector.panel.refreshValues(); }
    if (int q = (int) proc.apvts.getRawParameterValue ("quality")->load(); qualityBox.getSelectedItemIndex() != q) qualityBox.setSelectedItemIndex (q, juce::dontSendNotification);

    if (shotDir.isNotEmpty() && preview.comp().thumbsReady() >= kNumScenes && shotStage == 0)
    {
        shotStage = 1; const int cols = 6, rows = 5, tw = 256, th = 144;
        juce::Image sheet (juce::Image::ARGB, cols * tw, rows * th, true); juce::Graphics g (sheet);
        for (int i = 0; i < kNumScenes; ++i) { auto img = preview.comp().sceneThumb (i); const int x = (i % cols) * tw, y = (i / cols) * th; g.drawImageAt (img, x, y); g.setColour (juce::Colours::black.withAlpha (.6f)); g.fillRect (x, y, tw, 16); g.setColour (juce::Colours::white); g.setFont (12.f); g.drawText (juce::String (i) + " " + sceneCatalogue()[(size_t) i].name, x + 3, y, tw - 6, 16, juce::Justification::centredLeft); }
        juce::File out (shotDir + "/contact.png"); out.deleteFile(); juce::FileOutputStream fo (out); juce::PNGImageFormat().writeImageToStream (sheet, fo);
        juce::File (shotDir + "/status.txt").replaceWithText ("shaderError:\n" + st.shaderError + "\ngpu: " + st.gpu + "\n"); std::fprintf (stderr, "SELFTEST: contact sheet written\n");
    }
    if (shotDir.isNotEmpty() && shotStage == 1 && ++shotWait > 8)
    {   // offline frame of the current project (exercises the same path as video export)
        shotStage = 2; const int fw = juce::SystemStats::getEnvironmentVariable ("MONIE_FW", "1280").getIntValue(), fh = juce::SystemStats::getEnvironmentVariable ("MONIE_FH", "720").getIntValue(); std::vector<float> gl (globalDefs().size()); proc.readGlobals (gl.data()); FrameInputs in; in.project = proc.store.snapshot(); in.globals = gl.data(); in.scene = proc.currentScene(); proc.readKnobs (in.knobs);
        Drive d; for (int i = 0; i < 64; ++i) { const float x = (float) i / 63.f; d.bands[i] = 0.6f * std::exp (-x * 3.f); d.wave[i] = 0.5f + 0.3f * std::sin (x * 25.f); } d.bass = .6f; d.mid = .4f; d.treb = .3f; d.rms = .5f; d.beat = .6f; d.beatAge = .1f; in.drive = d;
        in.clock = 1.5; in.dt = 1.f / 30.f; in.overlay = false; std::vector<uint8_t> rgba; bool ok = false; preview.requestOfflineBegin (true);
        preview.runOnGL ([&] { for (int k = 0; k < 3; ++k) { in.clock = 1.2 + 0.1 * k; ok = preview.comp().renderOffline (in, fw, fh, rgba); } }); preview.requestOfflineBegin (false);
        if (ok) { juce::Image img (juce::Image::ARGB, fw, fh, false); juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly); for (int y = 0; y < fh; ++y) for (int x = 0; x < fw; ++x) { auto* q = &rgba[((size_t) y * (size_t) fw + (size_t) x) * 4]; bd.setPixelColour (x, y, juce::Colour (q[0], q[1], q[2], (juce::uint8) 255)); }
            juce::File out (shotDir + "/frame.png"); out.deleteFile(); juce::FileOutputStream fo (out); juce::PNGImageFormat().writeImageToStream (img, fo); std::fprintf (stderr, "SELFTEST: offline frame written\n"); }
        else std::fprintf (stderr, "SELFTEST: offline render FAILED\n");
    }
    if (shotDir.isNotEmpty() && shotStage == 2 && juce::SystemStats::getEnvironmentVariable ("MONIE_EXPORT_TEST", "").isNotEmpty() && ++shotWait > 40)
    {   // end-to-end export test: edit the project through the same API the UI uses, then run the real ExportJob
        shotStage = 3;
        proc.store.edit ("selftest", [&] (Project& p)
        {
            int t = p.addLayer (LayerType::Text, "Sub"); auto* l = p.find (t); l->props.s["text"] = "Hello export"; l->props.f["y"] = -0.4f; AnimInst a; a.id = p.newId(); a.preset = 1; a.dur = .5; l->anims.push_back (a);
            if (auto ip = juce::SystemStats::getEnvironmentVariable ("MONIE_TEST_IMAGE", ""); ip.isNotEmpty()) { int i = p.addLayer (LayerType::Image, "TestImage"); auto* li = p.find (i); li->media = ip.toStdString(); li->props.f["x"] = -0.55f; li->props.f["y"] = 0.3f; li->props.f["scale"] = 0.45f; li->props.f["distortion"] = 0.4f; proc.media.request (li->media); }
            if (auto vp = juce::SystemStats::getEnvironmentVariable ("MONIE_TEST_VIDEO", ""); vp.isNotEmpty()) { int i = p.addLayer (LayerType::Video, "TestVideo"); auto* lv = p.find (i); lv->media = vp.toStdString(); lv->props.f["x"] = 0.55f; lv->props.f["y"] = 0.3f; lv->props.f["scale"] = 0.45f; proc.media.request (lv->media); }
            p.addFx (p.fx, 14); Mapping m; m.target = "global:glow"; m.source = Source::Bass; m.outMax = .8f; p.mapping.add (m);
        });
        ExportSettings s; s.output = (shotDir + "/export_test.mp4").toStdString(); s.audioFile = juce::SystemStats::getEnvironmentVariable ("MONIE_EXPORT_TEST", "").toStdString(); s.width = 640; s.height = 360; s.fps = 30; s.start = 0; s.end = 3; s.captions = true; s.writeSrt = false;
        testJob = std::make_unique<ExportJob> (proc, preview, s); testJob->start(); std::fprintf (stderr, "SELFTEST: export started\n");
    }
    if (testJob && testJob->finished())
    {
        juce::File (shotDir + "/export_result.txt").replaceWithText (juce::String (testJob->succeeded() ? "OK" : "FAILED") + "\n" + testJob->status() + "\n"); std::fprintf (stderr, "SELFTEST: export finished: %s\n", testJob->status().toRawUTF8()); testJob.reset();
    }
}

void MonieEditor::addLayer (int kind)
{
    auto finish = [this] (int id) { if (id >= 0) selectLayer (id); };
    if (kind == 0 || kind == 1)
    {
        ui::chooseFile (kind == 1 ? "Choose a video" : "Choose an image", juce::File(), kind == 1 ? "*.mp4;*.mov;*.mkv;*.webm;*.avi;*.m4v;*.gif" : "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.webp", false, [this, kind, finish] (juce::File f)
        {
            auto why = MediaPool::unsupportedReason (f); if (why.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot use this file", why); return; }
            proc.media.request (f.getFullPathName().toStdString()); int id = -1;
            proc.store.edit ("Add layer", [&] (Project& p) { id = p.addLayer (kind == 1 ? LayerType::Video : LayerType::Image, f.getFileNameWithoutExtension().toStdString()); p.find (id)->media = f.getFullPathName().toStdString(); });
            finish (id);
        });
        return;
    }
    int id = -1; const int n = (int) proc.store.get().layers.size();
    proc.store.edit ("Add layer", [&] (Project& p) { id = p.addLayer (kind == 2 ? LayerType::Text : LayerType::Caption, kind == 2 ? "Text" : "Captions"); auto* l = p.find (id); if (kind == 2) { l->props.s["text"] = "Your text"; l->props.f["y"] = juce::jlimit (-.8f, .8f, -0.25f * (float) (n % 4)); } });
    finish (id);
}

// ============================================================================================ browsers
void MonieEditor::setupBrowsers()
{
    for (auto* l : { &scenesList, &fontsList, &fxList, &palList, &presetList, &mediaList }) addChildComponent (l);

    // ---- scenes ----
    scenesList.thumbW = 80; scenesList.setRowHeight (54);
    scenesList.source = [] { std::vector<ui::BrowserItem> v; for (auto& s : sceneCatalogue()) v.push_back ({ s.index, juce::String (s.index + 1) + ". " + juce::String (s.name), juce::String (s.desc), juce::String (s.category), juce::String (s.tags) }); return v; };
    scenesList.paintThumb = [this] (juce::Graphics& g, juce::Rectangle<int> r, int id) { auto img = preview.comp().sceneThumb (id); if (img.isValid()) g.drawImage (img, r.toFloat(), juce::RectanglePlacement::fillDestination); else { g.setColour (juce::Colours::black); g.fillRect (r); } };
    scenesList.isFav = [this] (int id) { return proc.store.get().favScenes.count (id) > 0; };
    scenesList.toggleFav = [this] (int id) { proc.store.edit ("Favourite scene", [=] (Project& p) { if (p.favScenes.count (id)) p.favScenes.erase (id); else p.favScenes.insert (id); }); };
    scenesList.onPick = [this] (int id) { proc.selectScene (id); scenesList.repaintList(); rebuildInspector(); };
    scenesList.isSelected = [this] (int id) { return id == proc.currentScene(); };
    scenesList.tooltipFor = [] (int id) { return juce::String (sceneCatalogue()[(size_t) id].desc); };

    // ---- fonts ----
    fontsList.thumbW = 120; fontsList.setRowHeight (40);
    fontsList.source = [this]
    {
        fontNames.clear(); std::vector<ui::BrowserItem> v; juce::StringArray all; for (auto& f : proc.store.get().favFonts) all.addIfNotAlreadyThere (juce::String (f)); for (auto& f : FontRegistry::get().installedFamilies()) all.addIfNotAlreadyThere (f);
        for (auto& f : all) { v.push_back ({ (int) fontNames.size(), f, proc.store.get().favFonts.count (f.toStdString()) ? "favourite" : "", "", "" }); fontNames.push_back (f); } return v;
    };
    fontsList.paintThumb = [this] (juce::Graphics& g, juce::Rectangle<int> r, int id) { g.setColour (lnf.t.bg); g.fillRect (r); g.setColour (lnf.t.text); if (id < (int) fontNames.size()) g.setFont (FontRegistry::get().makeFont (fontNames[(size_t) id], 1, 22.f)); g.drawText ("Aa Monie", r, juce::Justification::centred); };
    fontsList.isFav = [this] (int id) { return id < (int) fontNames.size() && proc.store.get().favFonts.count (fontNames[(size_t) id].toStdString()) > 0; };
    fontsList.toggleFav = [this] (int id) { if (id >= (int) fontNames.size()) return; auto n = fontNames[(size_t) id].toStdString(); proc.store.edit ("Favourite font", [=] (Project& p) { if (p.favFonts.count (n)) p.favFonts.erase (n); else p.favFonts.insert (n); }); fontsList.refresh(); };
    fontsList.onPick = [this] (int id) { if (id >= (int) fontNames.size()) return; const int lid = ctx.selectedLayer; auto* l = proc.store.get().find (lid); if (! l || (l->type != LayerType::Text && l->type != LayerType::Caption)) { statusLabel.setText ("Select a text or caption layer first, then click a font.", juce::dontSendNotification); return; }
        auto n = fontNames[(size_t) id].toStdString(); proc.store.edit ("Change font", [=] (Project& p) { if (auto* x = p.find (lid)) x->props.s["font"] = n; }); fontsList.repaintList(); };
    fontsList.isSelected = [this] (int id) { auto* l = proc.store.get().find (ctx.selectedLayer); return l && id < (int) fontNames.size() && l->props.getS ("font", "") == fontNames[(size_t) id].toStdString(); };
    fontHeader.add ("Import font (.ttf/.otf)...", [this] { ui::chooseFile ("Choose a font file", juce::File(), "*.ttf;*.otf", false, [this] (juce::File f)
        { juce::String fam; auto why = FontRegistry::get().importFont (f, &fam); if (why.isNotEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Font not usable", why); return; }
          proc.store.edit ("Import font", [=] (Project& p) { auto path = f.getFullPathName().toStdString(); if (std::find (p.importedFonts.begin(), p.importedFonts.end(), path) == p.importedFonts.end()) p.importedFonts.push_back (path); p.favFonts.insert (fam.toStdString()); }); fontsList.refresh(); }); }, "Imported fonts are validated first and remembered in the project.");
    fontsList.addHeaderComponent (&fontHeader, 26);

    // ---- effects ----
    fxList.setRowHeight (46);
    fxList.source = [] { std::vector<ui::BrowserItem> v; for (auto& d : fxCatalogue()) v.push_back ({ d.type, juce::String (d.name), juce::String (d.desc), juce::String (d.category), {} }); return v; };
    fxList.isFav = [this] (int id) { return proc.store.get().favFx.count (id) > 0; };
    fxList.toggleFav = [this] (int id) { proc.store.edit ("Favourite effect", [=] (Project& p) { if (p.favFx.count (id)) p.favFx.erase (id); else p.favFx.insert (id); }); };
    fxList.onActivate = [this] (int id) { const bool g = ctx.fxTarget == 1 || ctx.selectedLayer < 0; const int lid = ctx.selectedLayer;
        proc.store.edit ("Add effect", [=] (Project& p) { if (g) p.addFx (p.fx, id); else if (auto* l = p.find (lid)) p.addFx (l->fx, id); }); };
    fxList.onPick = [this] (int id) { if (auto* d = fxDef (id)) statusLabel.setText (juce::String (d->name) + ": " + d->desc + "   (double-click to add to the " + (ctx.fxTarget == 1 || ctx.selectedLayer < 0 ? "global stack)" : "selected layer)"), juce::dontSendNotification); };
    fxList.tooltipFor = [] (int id) { auto* d = fxDef (id); return d ? juce::String (d->desc) + "\nDouble-click to add." : juce::String(); };

    // ---- palettes ----
    palList.thumbW = 110; palList.setRowHeight (38);
    palList.source = [this] { std::vector<ui::BrowserItem> v; int i = 0; for (auto& g : builtinPalettes()) v.push_back ({ i++, juce::String (g.name), "built-in", "Built-in", {} }); i = 1000; for (auto& g : proc.store.get().userPalettes) v.push_back ({ i++, juce::String (g.name), "yours", "Yours", {} }); return v; };
    palList.paintThumb = [this] (juce::Graphics& g, juce::Rectangle<int> r, int id)
    {
        const Gradient* gr = id < 1000 ? &builtinPalettes()[(size_t) id] : (id - 1000 < (int) proc.store.get().userPalettes.size() ? &proc.store.get().userPalettes[(size_t) id - 1000] : nullptr); if (! gr) return;
        for (int x = 0; x < r.getWidth(); ++x) { auto c = gr->at ((float) x / (float) juce::jmax (1, r.getWidth() - 1)); g.setColour (juce::Colour::fromFloatRGBA (c.r, c.g, c.b, 1.f)); g.fillRect (r.getX() + x, r.getY(), 1, r.getHeight()); }
    };
    palList.isFav = [this] (int id) { const Gradient* gr = id < 1000 ? &builtinPalettes()[(size_t) id] : nullptr; return gr && proc.store.get().favPalettes.count (gr->name) > 0; };
    palList.toggleFav = [this] (int id) { if (id >= 1000) return; auto n = builtinPalettes()[(size_t) id].name; proc.store.edit ("Favourite palette", [=] (Project& p) { if (p.favPalettes.count (n)) p.favPalettes.erase (n); else p.favPalettes.insert (n); }); };
    palList.onPick = [this] (int id) { Gradient g; if (id < 1000) g = builtinPalettes()[(size_t) id]; else if (id - 1000 < (int) proc.store.get().userPalettes.size()) g = proc.store.get().userPalettes[(size_t) id - 1000]; else return;
        proc.store.edit ("Apply palette", [=] (Project& p) { p.gradient = g; }); ui::gSelectedStop = 0; };
    palList.contextMenu = [] (int id, juce::PopupMenu& m) { if (id >= 1000) m.addItem (1, "Delete palette"); };
    palList.onContextChoice = [this] (int id, int) { proc.store.edit ("Delete palette", [=] (Project& p) { if (id - 1000 < (int) p.userPalettes.size()) p.userPalettes.erase (p.userPalettes.begin() + (id - 1000)); }); };
    palList.isSelected = [this] (int id) { const Gradient* gr = id < 1000 ? &builtinPalettes()[(size_t) id] : nullptr; return gr && *gr == proc.store.get().gradient; };

    // ---- presets ----
    presetList.setRowHeight (40);
    presetList.source = [this]
    {
        presetFiles.clear(); std::vector<ui::BrowserItem> v;
        for (auto& f : ui::presetsDir().findChildFiles (juce::File::findFiles, true))
            for (int k = 0; k < ui::NumPresetKinds; ++k) if (f.hasFileExtension (juce::String (ui::presetExt (k)).trimCharactersAtStart ("."))) { v.push_back ({ (int) presetFiles.size(), f.getFileNameWithoutExtension(), ui::presetKindName (k), ui::presetKindName (k), {} }); presetFiles.push_back (f); }
        return v;
    };
    presetList.onActivate = [this] (int id) { if (id >= (int) presetFiles.size()) return; juce::String err; if (! ui::loadPresetFile (ctx, presetFiles[(size_t) id], &err)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load preset", err); };
    presetList.onPick = [this] (int id) { if (id < (int) presetFiles.size()) statusLabel.setText ("Double-click to load " + presetFiles[(size_t) id].getFileName(), juce::dontSendNotification); };
    presetList.contextMenu = [] (int, juce::PopupMenu& m) { m.addItem (1, "Load"); m.addItem (2, "Delete file"); m.addItem (3, "Show in folder"); };
    presetList.onContextChoice = [this] (int id, int r) { if (id >= (int) presetFiles.size()) return; auto f = presetFiles[(size_t) id]; if (r == 1) presetList.onActivate (id); else if (r == 2) { f.moveToTrash(); presetList.refresh(); } else if (r == 3) f.revealToUser(); };
    presetHeader.add ("Refresh", [this] { presetList.refresh(); }); presetHeader.add ("Open folder", [] { ui::presetsDir().revealToUser(); });
    presetList.addHeaderComponent (&presetHeader, 26);

    // ---- media ----
    mediaList.thumbW = 70; mediaList.setRowHeight (48);
    mediaList.source = [this]
    {
        mediaEntries.clear(); std::vector<ui::BrowserItem> v; const auto& P = proc.store.get();
        auto add = [&] (int lid, const std::string& path, const juce::String& what) { if (path.empty()) return; auto info = proc.media.info (path); v.push_back ({ (int) mediaEntries.size(), juce::File (juce::String (path)).getFileName(), what + (info.missing ? "  -  MISSING" : info.loading ? "  -  loading" : info.error.isNotEmpty() ? "  -  " + info.error : "  -  " + juce::String (info.w) + "x" + juce::String (info.h)), what, {} }); mediaEntries.push_back ({ lid, path }); };
        add (-1, P.sceneImage, "Scene image"); for (auto& c : P.collageImages) add (-1, c, "Collage image"); for (auto& l : P.layers) if (l.type == LayerType::Image || l.type == LayerType::Video) add (l.id, l.media, l.type == LayerType::Video ? "Video layer" : "Image layer");
        return v;
    };
    mediaList.paintThumb = [this] (juce::Graphics& g, juce::Rectangle<int> r, int id) { g.setColour (juce::Colours::black); g.fillRect (r); if (id < (int) mediaEntries.size()) { auto info = proc.media.info (mediaEntries[(size_t) id].second); if (info.thumb.isValid()) g.drawImage (info.thumb, r.toFloat(), juce::RectanglePlacement::centred); else if (info.missing) { g.setColour (juce::Colour (0xffff5c6c)); g.drawText ("missing", r, juce::Justification::centred); } } };
    mediaList.onPick = [this] (int id) { if (id < (int) mediaEntries.size() && mediaEntries[(size_t) id].first >= 0) selectLayer (mediaEntries[(size_t) id].first); };
    mediaList.isSelected = [this] (int id) { return id < (int) mediaEntries.size() && mediaEntries[(size_t) id].first == ctx.selectedLayer && ctx.selectedLayer >= 0; };
    mediaList.contextMenu = [] (int, juce::PopupMenu& m) { m.addItem (1, "Relink..."); m.addItem (2, "Show in folder"); };
    mediaList.onContextChoice = [this] (int id, int r) { if (id >= (int) mediaEntries.size()) return; auto entry = mediaEntries[(size_t) id];
        if (r == 2) juce::File (juce::String (entry.second)).revealToUser();
        else ui::chooseFile ("Locate the file", juce::File (juce::String (entry.second)).getParentDirectory(), "*", false, [this, entry] (juce::File f) { proc.media.relink (entry.second, f.getFullPathName().toStdString());
            proc.store.edit ("Relink media", [=] (Project& p) { auto np = f.getFullPathName().toStdString(); if (entry.first >= 0) { if (auto* l = p.find (entry.first)) l->media = np; } else { if (p.sceneImage == entry.second) p.sceneImage = np; for (auto& c : p.collageImages) if (c == entry.second) c = np; } }); }); };
    mediaHeader.add ("+ Image", [this] { addLayer (0); }, "Add an image layer"); mediaHeader.add ("+ Video", [this] { addLayer (1); }, "Add a video layer (needs FFmpeg)"); mediaHeader.add ("Scene image...", [this] { ui::chooseFile ("Choose the scene image", juce::File(), "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.webp", false, [this] (juce::File f) { proc.media.request (f.getFullPathName().toStdString()); proc.store.edit ("Scene image", [=] (Project& p) { p.sceneImage = f.getFullPathName().toStdString(); }); }); }, "Picture used by the image-based scenes");
    mediaList.addHeaderComponent (&mediaHeader, 26);
    sceneHeader.add ("Random scene", [this] { proc.selectScene ((int) (juce::Random::getSystemRandom().nextFloat() * kNumScenes) % kNumScenes); scenesList.repaintList(); rebuildInspector(); }); scenesList.addHeaderComponent (&sceneHeader, 26);
    mediaList.refresh();
}

// ============================================================================================ project files, shortcuts, dialogs
void MonieEditor::newProject()
{
    juce::Component::SafePointer<MonieEditor> sp (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New project", "Discard the current project and start a new one?", "Discard", "Cancel", this, juce::ModalCallbackFunction::create ([sp] (int r)
    {
        if (r != 1 || sp == nullptr) return; auto& e = *sp;
        e.proc.store.replace (defaultProject(), true); e.currentFile = juce::File(); e.proc.selectScene (0); e.proc.resetSceneKnobs(); if (auto& L = e.proc.store.get().layers; ! L.empty()) e.selectLayer (L.back().id);
    }));
}
void MonieEditor::openProject()
{
    ui::chooseFile ("Open project", ui::presetsDir(), "*.monie", false, [this] (juce::File f) { juce::String err; if (ui::loadPresetFile (ctx, f, &err)) { currentFile = f; if (auto& L = proc.store.get().layers; ! L.empty()) selectLayer (L.back().id); } else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not open", err); });
}
void MonieEditor::saveProject (bool saveAs)
{
    auto doSave = [this] (juce::File f) { Project p = proc.store.get(); p.scene = proc.currentScene(); std::array<float, 8> mem; float k[8]; proc.readKnobs (k); for (int i = 0; i < 8; ++i) mem[(size_t) i] = k[i]; p.sceneMem[p.scene] = mem;
        if (f.replaceWithText (juce::String::fromUTF8 (projectToJson (p).c_str()))) { currentFile = f; statusLabel.setText ("Saved " + f.getFullPathName(), juce::dontSendNotification); } else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save", "The file could not be written."); };
    if (! saveAs && currentFile != juce::File()) { doSave (currentFile); return; }
    ui::chooseFile ("Save project", ui::presetsDir().getChildFile (juce::String (proc.store.get().name) + ".monie"), "*.monie", true, [doSave] (juce::File f) { doSave (f.withFileExtension ("monie")); });
}
void MonieEditor::showExportDialog()
{
    juce::DialogWindow::LaunchOptions o; o.content.setOwned (new ExportDialog (proc, preview)); o.dialogTitle = "Export video"; o.dialogBackgroundColour = lnf.t.panel2; o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false;
    o.launchAsync();
}
bool MonieEditor::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    const bool ctrl = k.getModifiers().isCommandDown(); const int c = k.getKeyCode();
    if (c == juce::KeyPress::spaceKey) { playBtn.onClick(); return true; }
    if (c == juce::KeyPress::homeKey) { stopBtn.onClick(); return true; }
    if (ctrl && (c == 'z' || c == 'Z')) { k.getModifiers().isShiftDown() ? proc.store.redo() : proc.store.undo(); return true; }
    if (ctrl && (c == 'y' || c == 'Y')) { proc.store.redo(); return true; }
    if (ctrl && (c == 's' || c == 'S')) { saveProject (k.getModifiers().isShiftDown()); return true; }
    if (ctrl && (c == 'o' || c == 'O')) { openProject(); return true; }
    if (ctrl && (c == 'd' || c == 'D')) { layers.duplicateSelected(); return true; }
    if (ctrl && (c == 'f' || c == 'F')) { paramSearch.grabKeyboardFocus(); return true; }
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) { layers.deleteSelected(); return true; }
    if (c == 'f' || c == 'F') { fitBtn.onClick(); return true; }
    if (c == 'g' || c == 'G') { gridBtn.setToggleState (! gridBtn.getToggleState(), juce::dontSendNotification); gridBtn.onClick(); return true; }
    if (c == 'b' || c == 'B') { splitBtn.setToggleState (! splitBtn.getToggleState(), juce::dontSendNotification); splitBtn.onClick(); return true; }
    return false;
}
