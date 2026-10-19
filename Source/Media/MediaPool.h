#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace monie
{
struct PixelBuf { int w = 0, h = 0; std::vector<uint8_t> rgba; uint64_t version = 0; };
using PixelPtr = std::shared_ptr<const PixelBuf>;

struct MediaInfo
{
    std::string path; bool isVideo = false, known = false, loading = false, missing = false; int w = 0, h = 0; double duration = 0, fps = 0;
    juce::String error; juce::Image thumb;
};

// Locates ffmpeg/ffprobe (PATH, next to the plugin, or a user-configured folder). Cached. Empty string = not found.
struct FfTools
{
    static juce::String ffmpeg();
    static juce::String ffprobe();
    static void setFolder (const juce::File& folder);      // user override from Settings
    static void rescan();
    static bool available() { return ffmpeg().isNotEmpty(); }
};

class VideoSource;

// Image + video decode with caching. All heavy work (stb decode, ffmpeg child processes) happens on worker threads, never on the
// audio thread or the render thread. Video is decoded by an *external* ffmpeg process (not linked), so no FFmpeg code is
// embedded in the plugin binary (see docs/LICENSES.md).
class MediaPool : public juce::ChangeBroadcaster
{
public:
    MediaPool();
    ~MediaPool() override;
    static bool isImageFile (const juce::File&);
    static bool isVideoFile (const juce::File&);
    static juce::String unsupportedReason (const juce::File&);      // empty = ok

    void request (const std::string& path);                          // message thread: start loading (idempotent)
    MediaInfo info (const std::string& path) const;
    void relink (const std::string& oldPath, const std::string& newPath);   // forget old entry, load new (caller edits the project)
    void forget (const std::string& path);

    // render thread
    PixelPtr image (const std::string& path);                        // null until decoded
    PixelPtr videoFrame (const std::string& path, double mediaTime, bool exact);   // nearest decoded frame; restarts decoding when seeking
    void setProxyWidth (int w) { proxyWidth = w; }                   // preview decodes video at this width (export uses 0 = native up to 1920)
    int  getProxyWidth() const { return proxyWidth; }

private:
    struct Entry { MediaInfo info; PixelPtr pixels; std::unique_ptr<VideoSource> video; };
    void loadImageAsync (const std::string& path);
    void probeVideoAsync (const std::string& path);
    mutable std::mutex mtx; std::map<std::string, Entry> entries; juce::ThreadPool pool { 2 }; std::atomic<int> proxyWidth { 960 };
    JUCE_DECLARE_WEAK_REFERENCEABLE (MediaPool)
};
} // namespace monie
