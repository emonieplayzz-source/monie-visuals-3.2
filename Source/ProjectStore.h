#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <mutex>
#include "Core/ProjectModel.h"

namespace monie
{
// Owns the editable Project. Edits happen on the message thread only; every edit publishes an immutable snapshot that the
// render / worker threads read without blocking the UI. Undo/redo are snapshot-based (project JSON), with coalescing for slider drags.
class ProjectStore
{
public:
    ProjectStore() { publish(); }

    std::shared_ptr<const Project> snapshot() const { std::lock_guard<std::mutex> g (m); return snap; }
    const Project& get() const { return proj; }                       // message thread only
    uint64_t revision() const { return rev; }

    void edit (const juce::String& label, const std::function<void (Project&)>& fn, const juce::String& coalesceKey = {}, bool undoable = true)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (undoable)
        {
            const bool coalesce = coalesceKey.isNotEmpty() && coalesceKey == lastKey && now - lastEditMs < 800.0;
            if (! coalesce) { pushUndo (label); }
            lastKey = coalesceKey; lastEditMs = now;
        }
        fn (proj); ++rev; publish(); notify();
    }
    void replace (Project p, bool undoable)
    {
        if (undoable) pushUndo ("Load"); else { undoStack.clear(); redoStack.clear(); }
        proj = std::move (p); ++rev; publish(); notify();
    }
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    juce::String undoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    juce::String redoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().label; }
    bool undo() { return step (undoStack, redoStack); }
    bool redo() { return step (redoStack, undoStack); }
    void addListener (std::function<void()> f) { listeners.push_back (std::move (f)); }

private:
    struct Entry { juce::String label; std::string json; };
    void pushUndo (const juce::String& label) { undoStack.push_back ({ label, projectToJson (proj) }); if (undoStack.size() > 120) undoStack.erase (undoStack.begin()); redoStack.clear(); }
    bool step (std::vector<Entry>& from, std::vector<Entry>& to)
    {
        if (from.empty()) return false;
        Entry e = std::move (from.back()); from.pop_back();
        Project p; if (! projectFromJson (e.json, p)) return false;
        to.push_back ({ e.label, projectToJson (proj) });
        // Things that are host/automation state or session-only must not be rolled back by undo
        p.scene = proj.scene;
        proj = std::move (p); ++rev; lastKey = {}; publish(); notify(); return true;
    }
    void publish() { auto s = std::make_shared<const Project> (proj); std::lock_guard<std::mutex> g (m); snap = std::move (s); }
    void notify() { for (auto& f : listeners) f(); }

    Project proj = defaultProject();
    mutable std::mutex m; std::shared_ptr<const Project> snap;
    std::vector<Entry> undoStack, redoStack; juce::String lastKey; double lastEditMs = 0; uint64_t rev = 1;
    std::vector<std::function<void()>> listeners;
};
} // namespace monie
