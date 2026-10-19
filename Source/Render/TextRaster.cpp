#include "TextRaster.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace monie
{
bool TextRequest::operator< (const TextRequest& o) const
{
    return std::tie (text, font, style, sizePx, letterSp, wordSp, lineSp, boxWpx, curve, align, vertical, o1w, o2w, shBlur, shDist, glowR, bg, padX, padY, radius, highlightWord)
         < std::tie (o.text, o.font, o.style, o.sizePx, o.letterSp, o.wordSp, o.lineSp, o.boxWpx, o.curve, o.align, o.vertical, o.o1w, o.o2w, o.shBlur, o.shDist, o.glowR, o.bg, o.padX, o.padY, o.radius, o.highlightWord);
}

// ---------------------------------------------------------------- fonts
FontRegistry& FontRegistry::get() { static FontRegistry r; return r; }

juce::StringArray FontRegistry::installedFamilies()
{
    const juce::ScopedLock sl (lock);
    if (! haveFamilies)
    {
        families = juce::Font::findAllTypefaceNames(); families.sort (true); families.removeDuplicates (true); haveFamilies = true;
        for (auto& kv : imported) families.addIfNotAlreadyThere (kv.first);
    }
    return families;
}

juce::String FontRegistry::importFont (const juce::File& file, juce::String* familyOut)
{
    if (! file.existsAsFile()) return "File not found.";
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext != ".ttf" && ext != ".otf") return "Only .ttf and .otf fonts can be imported (got " + ext + ").";
    if (file.getSize() > 40 * 1024 * 1024) return "Font file is unreasonably large (>40 MB).";
    juce::MemoryBlock mb; if (! file.loadFileAsData (mb) || mb.getSize() < 64) return "Could not read the font file.";
    // sanity: sfnt magic (0x00010000, 'OTTO', 'true')
    const auto* b = static_cast<const unsigned char*> (mb.getData());
    const bool sfnt = (b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) || (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O') || (b[0] == 't' && b[1] == 'r' && b[2] == 'u' && b[3] == 'e');
    if (! sfnt) return "This does not look like a valid TrueType/OpenType font (bad header).";
    auto tf = juce::Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize());
    if (tf == nullptr) return "The system could not load this font (corrupt or unsupported).";
    juce::Font f { juce::FontOptions (tf) }; f.setHeight (40.f);
    juce::GlyphArrangement ga; ga.addLineOfText (f, "Aa", 0, 0);
    if (ga.getNumGlyphs() < 2 || ga.getBoundingBox (0, -1, true).getWidth() < 1.f) return "The font contains no usable glyphs.";
    juce::String family = tf->getName(); if (family.isEmpty()) family = file.getFileNameWithoutExtension();
    const juce::ScopedLock sl (lock);
    imported[family] = tf; haveFamilies = false;
    if (familyOut != nullptr) *familyOut = family;
    return {};
}
juce::Typeface::Ptr FontRegistry::importedTypeface (const juce::String& family) const
{
    const juce::ScopedLock sl (lock);
    auto it = imported.find (family); return it == imported.end() ? nullptr : it->second;
}
bool FontRegistry::isAvailable (const juce::String& family)
{
    if (family.isEmpty()) return true;
    { const juce::ScopedLock sl (lock); if (imported.count (family)) return true; }
    return installedFamilies().contains (family, true);
}
juce::Font FontRegistry::makeFont (const juce::String& family, int style, float heightPx, bool* fallbackUsed)
{
    const int flags = (style == 1 || style == 3 ? juce::Font::bold : 0) | (style >= 2 ? juce::Font::italic : 0);
    if (fallbackUsed != nullptr) *fallbackUsed = false;
    if (auto tf = importedTypeface (family)) { juce::Font f { juce::FontOptions (tf) }; f.setHeight (heightPx); return f; }
    if (family.isNotEmpty() && ! isAvailable (family)) { if (fallbackUsed != nullptr) *fallbackUsed = true; }
    const juce::String use = (family.isEmpty() || (fallbackUsed != nullptr && *fallbackUsed)) ? juce::Font::getDefaultSansSerifFontName() : family;
    return juce::Font (juce::FontOptions (use, heightPx, flags));
}

// ---------------------------------------------------------------- blur
static void boxPass (std::vector<float>& a, std::vector<float>& tmp, int w, int h, int r, bool horizontal)
{
    if (r <= 0) return;
    const int len = horizontal ? w : h, lines = horizontal ? h : w;
    const float inv = 1.f / (2.f * (float) r + 1.f);
    for (int l = 0; l < lines; ++l)
    {
        auto at = [&] (int i) -> float& { return horizontal ? a[(size_t) l * (size_t) w + (size_t) i] : a[(size_t) i * (size_t) w + (size_t) l]; };
        auto out = [&] (int i) -> float& { return horizontal ? tmp[(size_t) l * (size_t) w + (size_t) i] : tmp[(size_t) i * (size_t) w + (size_t) l]; };
        float sum = 0.f;
        for (int i = -r; i <= r; ++i) sum += (i >= 0 && i < len) ? at (i) : 0.f;
        for (int i = 0; i < len; ++i)
        {
            out (i) = sum * inv;
            const int add = i + r + 1, sub = i - r;
            sum += (add < len ? at (add) : 0.f) - (sub >= 0 ? at (sub) : 0.f);
        }
    }
    a.swap (tmp);
}
void blurMask (std::vector<uint8_t>& m, int w, int h, float radius)
{
    if (radius < 0.5f || w <= 0 || h <= 0) return;
    std::vector<float> a (m.begin(), m.end()), tmp (a.size());
    const float sigma = radius * 0.5f; const int r = std::max (1, (int) std::lround (std::sqrt (12.f * sigma * sigma / 3.f + 1.f) * 0.5f));
    for (int pass = 0; pass < 3; ++pass) { boxPass (a, tmp, w, h, r, true); boxPass (a, tmp, w, h, r, false); }
    for (size_t i = 0; i < m.size(); ++i) m[i] = (uint8_t) std::clamp ((int) std::lround (a[i]), 0, 255);
}

// ---------------------------------------------------------------- layout + raster
namespace
{
struct WordPath { juce::Path path; int line = 0; };

float measure (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0);
    return ga.getNumGlyphs() ? ga.getBoundingBox (0, -1, true).getRight() : 0.f;     // advance width incl. kerning (starts at x=0)
}
juce::Path warpPath (const juce::Path& in, float cx, float yRef, float R)
{
    juce::Path out; out.setUsingNonZeroWinding (in.isUsingNonZeroWinding());
    juce::PathFlatteningIterator it (in, juce::AffineTransform(), 0.2f);
    auto W = [&] (float x, float y) { const float th = (x - cx) / R, hgt = yRef - y; return juce::Point<float> (cx + (R + hgt) * std::sin (th), yRef + R - (R + hgt) * std::cos (th)); };
    while (it.next())
    {
        if (it.subPathIndex == 0) out.startNewSubPath (W (it.x1, it.y1));
        out.lineTo (W (it.x2, it.y2));
        if (it.closesSubPath) out.closeSubPath();
    }
    return out;
}
void drawMask (juce::Image& img, const std::function<void (juce::Graphics&)>& fn)
{
    juce::Graphics g (img); g.setColour (juce::Colours::white); fn (g);
}
std::vector<uint8_t> toBytes (const juce::Image& img)
{
    std::vector<uint8_t> v ((size_t) img.getWidth() * (size_t) img.getHeight());
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y) for (int x = 0; x < img.getWidth(); ++x) v[(size_t) y * (size_t) img.getWidth() + (size_t) x] = bd.getPixelColour (x, y).getAlpha();
    return v;
}
}

TextBitmap rasteriseText (const TextRequest& rq)
{
    TextBitmap out;
    const float sizePx = std::clamp (rq.sizePx, 4.f, 3000.f);
    bool fallback = false;
    juce::Font font = FontRegistry::get().makeFont (rq.font, rq.style, sizePx, &fallback);
    out.fontFallback = fallback;
    font = font.withExtraKerningFactor (rq.letterSp);
    const float spaceW = std::max (sizePx * 0.15f, measure (font, "n n") - measure (font, "nn"));
    const float wordGap = spaceW * (1.f + rq.wordSp);
    const float lineH = font.getHeight() * rq.lineSp;
    const float ascent = font.getAscent();

    // ---- split into words (keeping manual line breaks) ----
    struct W { juce::String s; float w; int para; };
    std::vector<W> words; int para = 0;
    {
        juce::String t = juce::String::fromUTF8 (rq.text.c_str()).replace ("\r", "");
        for (auto& line : juce::StringArray::fromLines (t))
        {
            if (rq.vertical)
            {
                for (auto c : line) if (! juce::CharacterFunctions::isWhitespace (c)) words.push_back ({ juce::String::charToString (c), measure (font, juce::String::charToString (c)), para });
            }
            else
                for (auto& w : juce::StringArray::fromTokens (line, " \t", ""))
                    if (w.isNotEmpty()) words.push_back ({ w, measure (font, w), para });
            ++para;
        }
    }
    out.wordCount = (int) words.size();
    if (words.empty()) { out.w = out.h = 2; out.masksA.assign (16, 0); out.masksB.assign (16, 0); return out; }

    // ---- line breaking ----
    std::vector<std::vector<size_t>> lines;
    {
        std::vector<size_t> cur; float curW = 0; int curPara = words[0].para;
        for (size_t i = 0; i < words.size(); ++i)
        {
            const bool newPara = words[i].para != curPara || rq.vertical;
            const float add = (cur.empty() ? 0.f : wordGap) + words[i].w;
            if (! cur.empty() && (newPara || (rq.boxWpx > 0 && curW + add > rq.boxWpx))) { lines.push_back (cur); cur.clear(); curW = 0; }
            curPara = words[i].para;
            curW += (cur.empty() ? 0.f : wordGap) + words[i].w; cur.push_back (i);
        }
        if (! cur.empty()) lines.push_back (cur);
    }
    std::vector<float> lineW (lines.size(), 0.f); float maxW = 0;
    for (size_t l = 0; l < lines.size(); ++l)
    {
        float w = 0; for (size_t k = 0; k < lines[l].size(); ++k) w += (k ? wordGap : 0.f) + words[lines[l][k]].w;
        lineW[l] = w; maxW = std::max (maxW, w);
    }
    const float blockW = rq.boxWpx > 0 ? std::max (rq.boxWpx, maxW) : maxW;

    // ---- build glyph paths per word ----
    std::vector<WordPath> wp (words.size());
    for (size_t l = 0; l < lines.size(); ++l)
    {
        float x = rq.align == 0 ? 0.f : rq.align == 1 ? (blockW - lineW[l]) * 0.5f : blockW - lineW[l];
        if (rq.vertical) x = (blockW - words[lines[l][0]].w) * 0.5f;
        const float baseline = ascent + (float) l * lineH;
        for (size_t k = 0; k < lines[l].size(); ++k)
        {
            const size_t wi = lines[l][k]; juce::GlyphArrangement ga; ga.addLineOfText (font, words[wi].s, x, baseline);
            ga.createPath (wp[wi].path); wp[wi].line = (int) l; x += words[wi].w + wordGap;
        }
    }
    if (std::fabs (rq.curve) > 0.01f && blockW > 1.f)
    {
        const float R = blockW / (rq.curve * 3.14159265f), cx = blockW * 0.5f, yRef = ascent + (float) (lines.size() - 1) * lineH * 0.5f;
        for (auto& w : wp) w.path = warpPath (w.path, cx, yRef, R);
    }
    juce::Path all; for (auto& w : wp) all.addPath (w.path);
    juce::Rectangle<float> tb = all.getBounds();
    if (tb.isEmpty()) tb = { 0, 0, 2, 2 };
    out.contentW = tb.getWidth(); out.contentH = tb.getHeight();
    juce::Rectangle<float> region = tb;
    if (rq.boxWpx > 0) region = region.getUnion ({ 0.f, tb.getY(), blockW, tb.getHeight() });

    // ---- margins ----
    const float shR = std::max (0.f, rq.shBlur), glR = std::max (0.f, rq.glowR);
    float margin = std::max ({ rq.o1w + rq.o2w, rq.shDist + shR * 1.7f, glR * 1.9f, 0.f }) + 4.f;
    if (rq.bg) margin = std::max (margin, std::max (rq.padX, rq.padY) + 4.f);
    int W = (int) std::ceil (region.getWidth() + 2 * margin), H = (int) std::ceil (region.getHeight() + 2 * margin);
    if (std::max (W, H) > 4096 && rq.sizePx > 8.f)
    {   // too large for a texture: render smaller and let the compositor scale the quad up
        TextRequest smaller = rq; const float k = 4000.f / (float) std::max (W, H);
        smaller.sizePx *= k; smaller.boxWpx *= k; smaller.o1w *= k; smaller.o2w *= k; smaller.shBlur *= k; smaller.shDist *= k; smaller.glowR *= k; smaller.padX *= k; smaller.padY *= k; smaller.radius *= k;
        auto r = rasteriseText (smaller); r.quadScale = 1.f / k; r.contentW /= k; r.contentH /= k; r.contentW *= 1.f; return r;
    }
    W = std::max (W, 2); H = std::max (H, 2);
    const auto xf = juce::AffineTransform::translation (margin - region.getX(), margin - region.getY());

    juce::Path P = all; P.applyTransform (xf);
    juce::Image iFill (juce::Image::SingleChannel, W, H, true), iO1 (juce::Image::SingleChannel, W, H, true), iO2 (juce::Image::SingleChannel, W, H, true),
                iBox (juce::Image::SingleChannel, W, H, true), iUnion (juce::Image::SingleChannel, W, H, true), iWord (juce::Image::SingleChannel, W, H, true);
    drawMask (iFill, [&] (juce::Graphics& g) { g.fillPath (P); });
    if (rq.o1w > 0.05f) drawMask (iO1, [&] (juce::Graphics& g) { g.strokePath (P, juce::PathStrokeType (rq.o1w * 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); });
    if (rq.o2w > 0.05f) drawMask (iO2, [&] (juce::Graphics& g) { g.strokePath (P, juce::PathStrokeType ((rq.o1w + rq.o2w) * 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); });
    drawMask (iUnion, [&] (juce::Graphics& g)
    {
        g.fillPath (P);
        const float ow = rq.o1w + rq.o2w; if (ow > 0.05f) g.strokePath (P, juce::PathStrokeType (ow * 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    });
    if (rq.bg)
    {
        auto r = juce::Rectangle<float> (region.getWidth() + 2 * rq.padX, region.getHeight() + 2 * rq.padY).withCentre ({ (float) W * 0.5f, (float) H * 0.5f });
        drawMask (iBox, [&] (juce::Graphics& g) { g.fillRoundedRectangle (r, rq.radius); });
    }
    if (rq.highlightWord >= 0 && rq.highlightWord < (int) wp.size())
    {
        juce::Path hw = wp[(size_t) rq.highlightWord].path; hw.applyTransform (xf);
        drawMask (iWord, [&] (juce::Graphics& g) { g.fillPath (hw); });
    }
    auto bFill = toBytes (iFill), bO1 = toBytes (iO1), bO2 = toBytes (iO2), bBox = toBytes (iBox), bUnion = toBytes (iUnion), bWord = toBytes (iWord);
    auto bShadow = bUnion, bGlow = bUnion;
    blurMask (bShadow, W, H, shR); blurMask (bGlow, W, H, glR);
    out.w = W; out.h = H; out.masksA.resize ((size_t) W * (size_t) H * 4); out.masksB.resize (out.masksA.size());
    // GL texture rows run bottom-to-top; the shader flips v for text just like for media, so keep image order (top row first).
    for (size_t i = 0; i < bFill.size(); ++i)
    {
        out.masksA[i * 4] = bFill[i]; out.masksA[i * 4 + 1] = bO1[i]; out.masksA[i * 4 + 2] = bO2[i]; out.masksA[i * 4 + 3] = bBox[i];
        out.masksB[i * 4] = bShadow[i]; out.masksB[i * 4 + 1] = bGlow[i]; out.masksB[i * 4 + 2] = bWord[i]; out.masksB[i * 4 + 3] = 255;
    }
    return out;
}
} // namespace monie
