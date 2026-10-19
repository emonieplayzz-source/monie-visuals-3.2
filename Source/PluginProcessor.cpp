#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CaptionService.h"

using namespace monie;

// Analysis runs on its own thread: it drains the lock-free FIFO, runs FFT/beat detection and publishes atomics.
class MonieProcessor::AnalysisThread : public juce::Thread
{
public:
    AnalysisThread (MonieProcessor& p, double sr) : juce::Thread ("monie analysis"), proc (p) { analyzer.prepare (sr); }
    void run() override
    {
        std::vector<float> tmp (1024);
        while (! threadShouldExit())
        {
            const int ready = proc.fifo.getNumReady();
            if (ready <= 0) { wait (3); continue; }
            const int n = juce::jmin (ready, (int) tmp.size());
            int s1, n1, s2, n2;
            proc.fifo.prepareToRead (n, s1, n1, s2, n2);
            if (n1 > 0) std::copy (proc.fifoData.begin() + s1, proc.fifoData.begin() + s1 + n1, tmp.begin());
            if (n2 > 0) std::copy (proc.fifoData.begin() + s2, proc.fifoData.begin() + s2 + n2, tmp.begin() + n1);
            proc.fifo.finishedRead (n1 + n2);

            if (proc.beatSensParam != nullptr) analyzer.setBeatSensitivity (proc.beatSensParam->load());
            if (analyzer.push (tmp.data(), n1 + n2))
            {
                const auto& f = analyzer.getFrame();
                auto& a = proc.analysis;
                a.bass = f.bass; a.mid = f.mid; a.treble = f.treble; a.rms = f.rms; a.peak = f.peak; a.flux = f.flux;
                a.lowMid = f.lowMid; a.highMid = f.highMid; a.transient = f.transient;
                a.beatCount = f.beatCount;
                for (int i = 0; i < AnalysisFrame::numBands; ++i) a.bands[i] = f.bands[i];
                for (int i = 0; i < AnalysisFrame::numWave; ++i)  a.wave[i] = f.wave[i];
                a.framesReceived.fetch_add (1);
            }
        }
    }
private:
    MonieProcessor& proc;
    AudioAnalyzer analyzer;
};

MonieProcessor::MonieProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain (optional vocal stem)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "MonieVisuals", createParameterLayout()),
      fifoData ((size_t) fifoSize, 0.f), asrData ((size_t) (1 << 17), 0.f)
{
    beatSensParam = apvts.getRawParameterValue ("sensBeat");
    sceneParam = apvts.getRawParameterValue ("scene");
    for (int i = 0; i < 8; ++i) knobParams[i] = apvts.getRawParameterValue ("sk" + juce::String (i));
    for (auto& d : globalDefs()) globalParams.push_back (apvts.getRawParameterValue (d.id));
    apvts.addParameterListener ("scene", this);
    apvts.addParameterListener ("captions", this);
    startAnalysis (currentSampleRate);
    captions = std::make_unique<CaptionService> (*this);
}

MonieProcessor::~MonieProcessor()
{
    cancelPendingUpdate();
    captions.reset();
    apvts.removeParameterListener ("scene", this); apvts.removeParameterListener ("captions", this);
    stopAnalysis();
}

void MonieProcessor::startAnalysis (double sr)
{
    stopAnalysis(); fifo.reset();
    analysisThread = std::make_unique<AnalysisThread> (*this, sr);
    analysisThread->startThread (juce::Thread::Priority::normal);
}
void MonieProcessor::stopAnalysis() { if (analysisThread != nullptr) { analysisThread->stopThread (2000); analysisThread.reset(); } }

void MonieProcessor::prepareToPlay (double sr, int) { currentSampleRate = sr; sampleRateAtomic = sr; startAnalysis (sr); asrFifo.reset(); }

bool MonieProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet(); const auto& out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (! (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())) return false;
    const auto& sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::stereo() || sc == juce::AudioChannelSet::mono();
}

// AUDIO THREAD. The main buffer is left untouched (pure passthrough). We only COPY a mono mix into preallocated lock-free FIFOs and
// publish the host transport as plain atomics. No allocation, no locks, no I/O, no GPU access, no recognition work.
void MonieProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nIn = getMainBusNumInputChannels(), n = buffer.getNumSamples();
    if (n == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.valid.store (true, std::memory_order_relaxed); host.playing.store (pos->getIsPlaying(), std::memory_order_relaxed);
            double sec = 0; if (auto t = pos->getTimeInSeconds()) sec = *t; else if (auto s = pos->getTimeInSamples()) sec = (double) *s / currentSampleRate;
            host.seconds.store (sec, std::memory_order_relaxed); if (auto b = pos->getBpm()) host.bpm.store (*b, std::memory_order_relaxed);
            host.stamp.store (juce::Time::getMillisecondCounterHiRes() * 0.001, std::memory_order_relaxed); host.stampCount.fetch_add (1, std::memory_order_release);
        }

    if (nIn > 0)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = nIn > 1 ? buffer.getReadPointer (1) : l;
        int pos = 0;
        while (pos < n)
        {
            const int m = juce::jmin (n - pos, (int) scratch.size());
            for (int i = 0; i < m; ++i) scratch[(size_t) i] = 0.5f * (l[pos + i] + r[pos + i]);
            int s1, n1, s2, n2;
            fifo.prepareToWrite (m, s1, n1, s2, n2);          // if full, drop samples (analysis only)
            if (n1 > 0) std::copy (scratch.begin(), scratch.begin() + n1, fifoData.begin() + s1);
            if (n2 > 0) std::copy (scratch.begin() + n1, scratch.begin() + n1 + n2, fifoData.begin() + s2);
            fifo.finishedWrite (n1 + n2);
            pos += m;
        }
    }
    if (asrCapture.load (std::memory_order_relaxed))
    {   // recognition input: main mix, or the optional sidechain bus (e.g. a vocal stem) - copied only, processed on another thread
        const bool useSc = sidechainChoice.load (std::memory_order_relaxed) == 1 && getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
        auto bufSrc = useSc ? getBusBuffer (buffer, true, 1) : getBusBuffer (buffer, true, 0);
        const int nc = bufSrc.getNumChannels();
        if (nc > 0)
        {
            const float* l = bufSrc.getReadPointer (0); const float* r = nc > 1 ? bufSrc.getReadPointer (1) : l;
            int pos = 0;
            while (pos < n)
            {
                const int m = juce::jmin (n - pos, (int) scratch.size());
                for (int i = 0; i < m; ++i) scratch[(size_t) i] = 0.5f * (l[pos + i] + r[pos + i]);
                int s1, n1, s2, n2; asrFifo.prepareToWrite (m, s1, n1, s2, n2);
                if (n1 > 0) std::copy (scratch.begin(), scratch.begin() + n1, asrData.begin() + s1);
                if (n2 > 0) std::copy (scratch.begin() + n1, scratch.begin() + n1 + n2, asrData.begin() + s2);
                asrFifo.finishedWrite (n1 + n2); pos += m;
            }
        }
    }
}

juce::AudioProcessorEditor* MonieProcessor::createEditor() { return new MonieEditor (*this); }

// ---------------- helpers for the render side ----------------
void MonieProcessor::fillRaw (RawAudio& r) const
{
    r.bass = analysis.bass; r.lowMid = analysis.lowMid; r.mid = analysis.mid; r.highMid = analysis.highMid; r.treble = analysis.treble; r.rms = analysis.rms; r.peak = analysis.peak;
    r.flux = analysis.flux; r.transient = analysis.transient; r.beatCount = analysis.beatCount;
    for (int i = 0; i < 64; ++i) { r.bands[i] = analysis.bands[i]; r.wave[i] = analysis.wave[i]; }
}
DriveParams MonieProcessor::driveParams() const
{
    DriveParams p; auto L = [&] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    p.sensBass = L ("sensBass"); p.sensMid = L ("sensMid"); p.sensTreble = L ("sensTreble"); p.sensBeat = L ("sensBeat"); p.master = L ("master"); p.smooth = L ("smooth"); p.attack = L ("attack"); p.release = L ("release");
    return p;
}
void MonieProcessor::readGlobals (float* out) const { for (size_t i = 0; i < globalParams.size(); ++i) out[i] = globalParams[i]->load(); }
void MonieProcessor::readKnobs (float* out8) const { for (int i = 0; i < 8; ++i) out8[i] = knobParams[i]->load(); }

// ---------------- transport ----------------
double MonieProcessor::clockNow()
{
    const juce::SpinLock::ScopedLockType sl (clockLock);
    const double wall = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const int sc = host.stampCount.load (std::memory_order_acquire);
    if (host.valid.load() && sc != lastStampSeen) { lastStampSeen = sc; clock.hostUpdate (true, host.playing.load(), host.seconds.load(), host.stamp.load()); }
    else if (host.valid.load() && clock.mode == TransportClock::Mode::Host)
    {   // host stopped calling processBlock (transport stopped / plugin bypassed): treat as paused so the clock doesn't run away
        if (wall - host.stamp.load() > 0.35 && clock.playing()) clock.hostUpdate (true, false, host.seconds.load(), wall);
    }
    clock.normalise (wall);
    return clock.now (wall);
}
void MonieProcessor::transportPlay() { const juce::SpinLock::ScopedLockType sl (clockLock); if (clock.mode == TransportClock::Mode::Internal) clock.play (juce::Time::getMillisecondCounterHiRes() * 0.001); }
void MonieProcessor::transportPause() { const juce::SpinLock::ScopedLockType sl (clockLock); if (clock.mode == TransportClock::Mode::Internal) clock.pause (juce::Time::getMillisecondCounterHiRes() * 0.001); }
void MonieProcessor::transportStop() { const juce::SpinLock::ScopedLockType sl (clockLock); if (clock.mode == TransportClock::Mode::Internal) clock.stop (juce::Time::getMillisecondCounterHiRes() * 0.001); }
void MonieProcessor::transportSeek (double s) { const juce::SpinLock::ScopedLockType sl (clockLock); if (clock.mode == TransportClock::Mode::Internal) clock.seek (s, juce::Time::getMillisecondCounterHiRes() * 0.001); }
bool MonieProcessor::transportIsPlaying() { const juce::SpinLock::ScopedLockType sl (clockLock); return clock.playing(); }
bool MonieProcessor::hostControlled() { const juce::SpinLock::ScopedLockType sl (clockLock); return clock.mode == TransportClock::Mode::Host; }
int MonieProcessor::seekCounter() { const juce::SpinLock::ScopedLockType sl (clockLock); return clock.seekCount; }
void MonieProcessor::setLoop (bool on, double a, double b) { const juce::SpinLock::ScopedLockType sl (clockLock); clock.setLoop (on, a, b); }

// ---------------- scene bookkeeping ----------------
void MonieProcessor::parameterChanged (const juce::String& id, float) { if (id == "scene" || id == "captions") triggerAsyncUpdate(); }

void MonieProcessor::handleAsyncUpdate()
{
    if (restoring) return;
    const int newScene = currentScene();
    if (newScene != appliedScene)
    {
        const int old = appliedScene; appliedScene = newScene;
        float k[8]; readKnobs (k);
        store.edit ("Scene", [&] (Project& p)
        {
            std::array<float, 8> mem; for (int i = 0; i < 8; ++i) mem[(size_t) i] = k[i];
            p.sceneMem[old] = mem; p.scene = newScene;
            auto it = p.sceneMem.find (newScene);
            const auto& si = sceneCatalogue()[(size_t) newScene];
            for (int i = 0; i < 8; ++i) { const float v = it != p.sceneMem.end() ? it->second[(size_t) i] : si.defNorm (i); if (auto* par = apvts.getParameter ("sk" + juce::String (i))) par->setValueNotifyingHost (v); }
        }, {}, false);
    }
    const bool capHost = apvts.getRawParameterValue ("captions")->load() > .5f;
    if (capHost != store.get().captionsOn) store.edit ("Captions", [&] (Project& p) { p.captionsOn = capHost; }, {}, false);
}

void MonieProcessor::selectScene (int scene)
{
    scene = juce::jlimit (0, kNumScenes - 1, scene);
    if (auto* p = apvts.getParameter ("scene")) p->setValueNotifyingHost (p->convertTo0to1 ((float) scene));
    handleAsyncUpdate();
}
void MonieProcessor::resetSceneKnobs()
{
    const auto& si = sceneCatalogue()[(size_t) currentScene()];
    for (int i = 0; i < 8; ++i) if (auto* par = apvts.getParameter ("sk" + juce::String (i))) par->setValueNotifyingHost (si.defNorm (i));
}

// ---------------- state ----------------
juce::String MonieProcessor::projectStateString()
{
    Project p = *store.snapshot(); p.scene = currentScene();
    std::array<float, 8> mem; float k[8]; readKnobs (k); for (int i = 0; i < 8; ++i) mem[(size_t) i] = k[i]; p.sceneMem[p.scene] = mem;
    return juce::String::fromUTF8 (projectToJson (p).c_str());
}
void MonieProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = std::make_unique<juce::XmlElement> ("MonieVisualsState");
    xml->setAttribute ("version", 3);
    if (auto p = apvts.copyState().createXml()) xml->addChildElement (p.release());
    xml->createNewChildElement ("Project")->addTextElement (projectStateString());
    copyXmlToBinary (*xml, dest);
}
void MonieProcessor::setStateInformation (const void* data, int size)
{
    auto x = getXmlFromBinary (data, size);
    if (x == nullptr || ! x->hasTagName ("MonieVisualsState")) return;   // unknown / older-format state: leave the plugin at its defaults rather than guess
    if (x->getIntAttribute ("version", 0) < 3) return;       // pre-3.0 states used different parameter ids
    restoring = true;
    if (auto* p = x->getChildByName (apvts.state.getType().toString())) apvts.replaceState (juce::ValueTree::fromXml (*p));
    appliedScene = currentScene();
    if (auto* pj = x->getChildByName ("Project"))
    {
        Project p; if (projectFromJson (pj->getAllSubText().toStdString(), p)) { p.scene = appliedScene; auto s = std::make_shared<Project> (std::move (p)); juce::MessageManager::callAsync ([this, s] { store.replace (*s, false); }); }
    }
    restoring = false;
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MonieProcessor(); }
