#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "MediaPool.h"
#include <stb_image.h>
#include <condition_variable>
#include <deque>
#include "Core/MiniJson.h"

namespace monie
{
// ------------------------------------------------------------------ ffmpeg discovery
static juce::CriticalSection ffLock; static juce::String ffmpegPath, ffprobePath; static bool ffScanned = false; static juce::File ffFolder;

static juce::String tryExe (const juce::String& exe)
{
    juce::ChildProcess p; if (! p.start (juce::StringArray { exe, "-version" })) return {};
    auto out = p.readAllProcessOutput(); p.waitForProcessToFinish (3000);
    return out.containsIgnoreCase ("version") ? exe : juce::String();
}
static void scanFf()
{
    const juce::ScopedLock sl (ffLock); if (ffScanned) return; ffScanned = true; ffmpegPath = ffprobePath = {};
    juce::StringArray bases;
    if (ffFolder != juce::File()) bases.add (ffFolder.getFullPathName());
    auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    bases.add (self.getFullPathName()); bases.add (self.getChildFile ("ffmpeg").getFullPathName()); bases.add (self.getChildFile ("bin").getFullPathName());
   #if JUCE_WINDOWS
    const char* e1 = "ffmpeg.exe"; const char* e2 = "ffprobe.exe";
   #else
    const char* e1 = "ffmpeg"; const char* e2 = "ffprobe";
   #endif
    for (auto& b : bases) { auto f = juce::File (b).getChildFile (e1); if (f.existsAsFile()) { ffmpegPath = tryExe (f.getFullPathName()); if (ffmpegPath.isNotEmpty()) { auto pr = juce::File (b).getChildFile (e2); if (pr.existsAsFile()) ffprobePath = pr.getFullPathName(); return; } } }
    ffmpegPath = tryExe ("ffmpeg"); if (ffmpegPath.isNotEmpty()) ffprobePath = tryExe ("ffprobe");
}
juce::String FfTools::ffmpeg() { scanFf(); const juce::ScopedLock sl (ffLock); return ffmpegPath; }
juce::String FfTools::ffprobe() { scanFf(); const juce::ScopedLock sl (ffLock); return ffprobePath; }
void FfTools::setFolder (const juce::File& f) { { const juce::ScopedLock sl (ffLock); ffFolder = f; } rescan(); }
void FfTools::rescan() { { const juce::ScopedLock sl (ffLock); ffScanned = false; } scanFf(); }

// ------------------------------------------------------------------ video source (ffmpeg raw-frame pipe)
class VideoSource : public juce::Thread
{
public:
    VideoSource (std::string p, int srcW, int srcH, double fps_, double dur) : juce::Thread ("monie video"), path (std::move (p)), sw (srcW), sh (srcH), fps (fps_ > 1 ? fps_ : 30.0), duration (dur) {}
    ~VideoSource() override { stopThread (3000); }

    PixelPtr get (double t, bool exact, int targetWidth)
    {
        t = std::max (0.0, t);
        {
            std::unique_lock<std::mutex> lk (m);
            reqWidth = targetWidth;
            const bool havePlan = running && gen == wantGen && ! frames.empty();
            const double lo = frames.empty() ? 1e9 : frames.front().t, hi = frames.empty() ? -1e9 : frames.back().t;
            // decoding continues forward on its own; restart only on seeks (backwards, or far ahead) or resolution change
            if (! running || widthChanged || t < lo - 0.25 || t > hi + 2.0) { requestRestart (t); }
            (void) havePlan;
            if (exact)
            {
                const auto deadline = juce::Time::getMillisecondCounter() + 8000;
                while (juce::Time::getMillisecondCounter() < deadline && ! threadShouldExit() && (frames.empty() || frames.back().t + 0.5 / fps < t) && ! eof && ! failed)
                    cv.wait_for (lk, std::chrono::milliseconds (20));
            }
            // pick the newest frame with time <= t, drop older ones
            while (frames.size() > 1 && frames[1].t <= t + 0.5 / fps) frames.pop_front();
            if (! frames.empty()) { last = frames.front().px; }
            cv.notify_all();
        }
        return last;
    }
    bool hasFailed() const { return failed; }
    juce::String lastError() { std::lock_guard<std::mutex> g (m); return err; }

    void run() override
    {
        while (! threadShouldExit())
        {
            double startT; int width; unsigned myGen;
            {
                std::unique_lock<std::mutex> lk (m);
                while (! threadShouldExit() && ! restartPending) cv.wait_for (lk, std::chrono::milliseconds (50));
                if (threadShouldExit()) break;
                restartPending = false; startT = wantStart; width = reqWidth; myGen = ++gen; frames.clear(); eof = false; widthChanged = false; running = true; curWidth = width;
            }
            decode (startT, width, myGen);
            { std::lock_guard<std::mutex> g (m); if (gen == myGen && ! restartPending) running = false; }
        }
    }
private:
    struct Frame { double t; PixelPtr px; };
    void requestRestart (double t) { wantStart = t; restartPending = true; wantGen = gen + 1; if (reqWidth != curWidth && running) widthChanged = false; cv.notify_all(); running = true; }

    void decode (double startT, int reqW, unsigned myGen)
    {
        const juce::String ff = FfTools::ffmpeg(); if (ff.isEmpty()) { std::lock_guard<std::mutex> g (m); failed = true; err = "ffmpeg not found"; return; }
        int w = sw > 0 ? sw : 1280, h = sh > 0 ? sh : 720;
        const int cap = reqW > 0 ? reqW : 1920;
        if (w > cap) { h = std::max (2, (int) std::lround ((double) h * cap / w)); w = cap; }
        w &= ~1; h &= ~1; w = std::max (w, 2); h = std::max (h, 2);
        juce::StringArray a { ff, "-v", "error", "-ss", juce::String (startT, 3), "-i", juce::String (path), "-an", "-vf", "scale=" + juce::String (w) + ":" + juce::String (h), "-f", "rawvideo", "-pix_fmt", "rgba", "-" };
        juce::ChildProcess proc; if (! proc.start (a)) { std::lock_guard<std::mutex> g (m); failed = true; err = "could not start ffmpeg"; return; }
        const size_t fsz = (size_t) w * (size_t) h * 4; std::vector<uint8_t> buf (fsz); size_t got = 0; int n = 0; uint64_t ver = 1;
        while (! threadShouldExit())
        {
            { std::unique_lock<std::mutex> lk (m); if (restartPending || gen != myGen) break;
              while (frames.size() >= 6 && ! restartPending && ! threadShouldExit()) cv.wait_for (lk, std::chrono::milliseconds (10));
              if (restartPending || threadShouldExit()) break; }
            const int r = proc.readProcessOutput (reinterpret_cast<char*> (buf.data()) + got, (int) std::min<size_t> (fsz - got, 1 << 20));
            if (r <= 0) { std::lock_guard<std::mutex> g (m); if (gen == myGen) { eof = true; if (n == 0) { failed = true; err = proc.readAllProcessOutput(); if (err.isEmpty()) err = "no frames decoded"; } } cv.notify_all(); break; }
            got += (size_t) r;
            if (got == fsz)
            {
                auto pb = std::make_shared<PixelBuf>(); pb->w = w; pb->h = h; pb->rgba = buf; pb->version = ++ver + (uint64_t) myGen * 1000000ull; got = 0;
                std::lock_guard<std::mutex> g (m); if (gen != myGen) break;
                frames.push_back ({ startT + (double) n / fps, pb }); ++n; cv.notify_all();
            }
        }
        proc.kill();
    }
    std::string path; int sw, sh; double fps, duration;
    std::mutex m; std::condition_variable cv; std::deque<Frame> frames; PixelPtr last;
    bool restartPending = true, running = false, eof = false, failed = false, widthChanged = false; double wantStart = 0; unsigned gen = 0, wantGen = 0; int reqWidth = 960, curWidth = 0; juce::String err;
};

// ------------------------------------------------------------------ pool
static std::string lowerExt (const juce::File& f) { return f.getFileExtension().trimCharactersAtStart (".").toLowerCase().toStdString(); }
bool MediaPool::isImageFile (const juce::File& f) { auto e = lowerExt (f); return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "gif" || e == "tga" || e == "webp" || e == "tif" || e == "tiff"; }
bool MediaPool::isVideoFile (const juce::File& f) { auto e = lowerExt (f); return e == "mp4" || e == "mov" || e == "mkv" || e == "webm" || e == "avi" || e == "m4v"; }
juce::String MediaPool::unsupportedReason (const juce::File& f)
{
    auto e = lowerExt (f);
    if (e == "webp") return "WebP images are not decoded by this build - convert to PNG/JPG.";
    if (e == "tif" || e == "tiff") return "TIFF images are not decoded by this build - convert to PNG/JPG.";
    if (isImageFile (f)) return {};
    if (isVideoFile (f)) return FfTools::available() ? juce::String() : juce::String ("Video needs ffmpeg (not found). See Settings > Tools or docs/FFMPEG.md.");
    return "Unsupported file type: ." + juce::String (e);
}

MediaPool::MediaPool() {}
MediaPool::~MediaPool() { pool.removeAllJobs (true, 4000); std::lock_guard<std::mutex> g (mtx); entries.clear(); }

void MediaPool::request (const std::string& path)
{
    juce::File f (juce::String::fromUTF8 (path.c_str()));
    {
        std::lock_guard<std::mutex> g (mtx);
        auto it = entries.find (path); if (it != entries.end()) return;
        Entry e; e.info.path = path; e.info.isVideo = isVideoFile (f); e.info.loading = true; e.info.missing = ! f.existsAsFile();
        if (e.info.missing) { e.info.loading = false; e.info.error = "File not found - relink it from the Media panel."; }
        else if (auto why = unsupportedReason (f); why.isNotEmpty()) { e.info.loading = false; e.info.error = why; }
        entries.emplace (path, std::move (e));
        auto& ref = entries[path]; if (! ref.info.loading) { sendChangeMessage(); return; }
    }
    if (isVideoFile (f)) probeVideoAsync (path); else loadImageAsync (path);
}
MediaInfo MediaPool::info (const std::string& path) const { std::lock_guard<std::mutex> g (mtx); auto it = entries.find (path); if (it == entries.end()) { MediaInfo m; m.path = path; return m; } return it->second.info; }
void MediaPool::forget (const std::string& path) { std::lock_guard<std::mutex> g (mtx); entries.erase (path); }
void MediaPool::relink (const std::string& oldPath, const std::string& newPath) { forget (oldPath); request (newPath); }

PixelPtr MediaPool::image (const std::string& path) { std::lock_guard<std::mutex> g (mtx); auto it = entries.find (path); return it == entries.end() ? nullptr : it->second.pixels; }

PixelPtr MediaPool::videoFrame (const std::string& path, double t, bool exact)
{
    VideoSource* vs = nullptr;
    { std::lock_guard<std::mutex> g (mtx); auto it = entries.find (path); if (it == entries.end() || ! it->second.info.isVideo || ! it->second.info.known) return nullptr;
      auto& e = it->second; if (! e.video) { e.video = std::make_unique<VideoSource> (path, e.info.w, e.info.h, e.info.fps, e.info.duration); e.video->startThread(); } vs = e.video.get(); }
    return vs->get (t, exact, proxyWidth.load());
}

void MediaPool::loadImageAsync (const std::string& path)
{
    juce::WeakReference<MediaPool> weak (this);
    pool.addJob ([weak, path]
    {
        juce::File f (juce::String::fromUTF8 (path.c_str())); juce::MemoryBlock mb; MediaInfo mi; mi.path = path; mi.known = true; PixelPtr px;
        if (! f.loadFileAsData (mb) || mb.getSize() == 0) mi.error = "Could not read the file.";
        else
        {
            int w = 0, h = 0, n = 0; stbi_uc* data = stbi_load_from_memory (static_cast<const stbi_uc*> (mb.getData()), (int) mb.getSize(), &w, &h, &n, 4);
            if (data == nullptr) mi.error = juce::String ("Could not decode image: ") + stbi_failure_reason();
            else
            {
                // cap at 4096 px on the long edge (GPU memory + upload time); simple box downscale on the worker thread
                int tw = w, th = h; const int mx = std::max (w, h); if (mx > 4096) { const double k = 4096.0 / mx; tw = std::max (1, (int) (w * k)); th = std::max (1, (int) (h * k)); }
                auto pb = std::make_shared<PixelBuf>(); pb->w = tw; pb->h = th; pb->version = 1; pb->rgba.resize ((size_t) tw * (size_t) th * 4);
                if (tw == w && th == h) std::copy (data, data + pb->rgba.size(), pb->rgba.begin());
                else for (int y = 0; y < th; ++y) for (int x = 0; x < tw; ++x) { const int sx = std::min (w - 1, (int) ((x + .5) * w / tw)), sy = std::min (h - 1, (int) ((y + .5) * h / th)); for (int c = 0; c < 4; ++c) pb->rgba[((size_t) y * tw + x) * 4 + c] = data[((size_t) sy * w + sx) * 4 + c]; }
                stbi_image_free (data); mi.w = tw; mi.h = th;
                juce::Image thumb (juce::Image::ARGB, 192, 108, true); juce::Image src (juce::Image::ARGB, tw, th, false);
                { juce::Image::BitmapData bd (src, juce::Image::BitmapData::writeOnly); for (int y = 0; y < th; ++y) for (int x = 0; x < tw; ++x) { const uint8_t* p = &pb->rgba[((size_t) y * tw + x) * 4]; bd.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2], p[3])); } }
                { juce::Graphics g (thumb); g.fillAll (juce::Colour (0xff101014)); g.drawImage (src, juce::Rectangle<float> (0, 0, 192, 108), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize); }
                mi.thumb = thumb; px = pb;
            }
        }
        juce::MessageManager::callAsync ([weak, path, mi, px]
        {
            if (auto* self = weak.get()) { { std::lock_guard<std::mutex> g (self->mtx); auto it = self->entries.find (path); if (it == self->entries.end()) return; auto old = it->second.info; it->second.info = mi; it->second.info.loading = false; it->second.pixels = px; } self->sendChangeMessage(); }
        });
    });
}

void MediaPool::probeVideoAsync (const std::string& path)
{
    juce::WeakReference<MediaPool> weak (this);
    pool.addJob ([weak, path]
    {
        MediaInfo mi; mi.path = path; mi.isVideo = true; mi.known = true;
        const auto probe = FfTools::ffprobe(), ff = FfTools::ffmpeg();
        if (probe.isEmpty() || ff.isEmpty()) { mi.known = false; mi.error = "ffprobe/ffmpeg not found. Install FFmpeg and set its folder in Settings > Tools."; }
        else
        {
            juce::ChildProcess p;
            if (! p.start (juce::StringArray { probe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height,r_frame_rate,duration:format=duration", "-of", "json", juce::String::fromUTF8 (path.c_str()) })) { mi.known = false; mi.error = "could not run ffprobe"; }
            else
            {
                auto out = p.readAllProcessOutput(); p.waitForProcessToFinish (8000); JVal root;
                if (! JsonParser::parse (out.toStdString(), root)) { mi.known = false; mi.error = "ffprobe returned no usable data (is this a video file?)"; }
                else
                {
                    const JVal* st = root.get ("streams"); if (! st || st->a.empty()) { mi.known = false; mi.error = "no video stream found"; }
                    else
                    {
                        auto& s = st->a[0]; mi.w = (int) s.num ("width"); mi.h = (int) s.num ("height"); std::string fr = s.str ("r_frame_rate", "30/1"); auto slash = fr.find ('/');
                        double n = std::atof (fr.c_str()), d = slash == std::string::npos ? 1.0 : std::atof (fr.c_str() + slash + 1); mi.fps = d > 0 ? n / d : 30.0;
                        mi.duration = s.num ("duration", 0); if (mi.duration <= 0) if (auto* f = root.get ("format")) mi.duration = f->num ("duration", 0);
                        if (mi.w <= 0 || mi.h <= 0) { mi.known = false; mi.error = "could not read video size"; }
                        else
                        {   // thumbnail: one frame as PNG through a pipe
                            juce::ChildProcess t; if (t.start (juce::StringArray { ff, "-v", "error", "-ss", "0.5", "-i", juce::String::fromUTF8 (path.c_str()), "-frames:v", "1", "-vf", "scale=192:108:force_original_aspect_ratio=decrease", "-f", "image2pipe", "-vcodec", "png", "-" }))
                            { juce::MemoryBlock mb; char buf[8192]; for (;;) { int r = t.readProcessOutput (buf, sizeof buf); if (r <= 0) break; mb.append (buf, (size_t) r); } mi.thumb = juce::ImageFileFormat::loadFrom (mb.getData(), mb.getSize()); }
                        }
                    }
                }
            }
        }
        juce::MessageManager::callAsync ([weak, path, mi]
        { if (auto* self = weak.get()) { { std::lock_guard<std::mutex> g (self->mtx); auto it = self->entries.find (path); if (it == self->entries.end()) return; it->second.info = mi; it->second.info.loading = false; } self->sendChangeMessage(); } });
    });
}
} // namespace monie
