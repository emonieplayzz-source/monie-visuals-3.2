#pragma once
#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace monie
{
// Everything that changes the *shape* of the text bitmap. Colours, gradients and animation are applied in the shader, so changing
// them never forces a re-rasterisation.
struct TextRequest
{
    std::string text, font; int style = 1;           // style: 0 regular 1 bold 2 italic 3 bold-italic
    float sizePx = 100, letterSp = 0, wordSp = 0, lineSp = 1.15f, boxWpx = 0, curve = 0; int align = 1; bool vertical = false;
    float o1w = 0, o2w = 0, shBlur = 6, shDist = 6, glowR = 18; bool bg = false; float padX = 28, padY = 14, radius = 12;
    int highlightWord = -1;                          // word index (0-based over the whole text) painted into the "active word" mask
    bool operator< (const TextRequest& o) const;
};
struct TextBitmap
{
    int w = 0, h = 0;
    std::vector<uint8_t> masksA;                     // RGBA: R fill, G outline 1, B outline 2, A background box
    std::vector<uint8_t> masksB;                     // RGBA: R shadow source, G glow source, B active word
    float contentW = 0, contentH = 0;                // tight text box in pixels (for the selection frame)
    bool fontFallback = false;                       // requested font is not installed -> a default font was used
    int wordCount = 0;
    float quadScale = 1.f;                           // >1 when the bitmap had to be rendered smaller than requested (4096 px texture limit)
};

// Font registry: installed fonts + user-imported TTF/OTF files (validated before use).
class FontRegistry
{
public:
    static FontRegistry& get();
    juce::StringArray installedFamilies();                                  // cached; message or any thread
    // Returns empty string on success, otherwise a human-readable reason (invalid file, no glyphs, ...). On success *familyOut is the usable name.
    juce::String importFont (const juce::File& file, juce::String* familyOut = nullptr);
    juce::Typeface::Ptr importedTypeface (const juce::String& family) const;
    bool isAvailable (const juce::String& family);
    juce::Font makeFont (const juce::String& family, int style, float heightPx, bool* fallbackUsed = nullptr);
private:
    juce::StringArray families; bool haveFamilies = false;
    std::map<juce::String, juce::Typeface::Ptr> imported; juce::CriticalSection lock;
};

TextBitmap rasteriseText (const TextRequest&);

// separable box-blur (3 passes ~ gaussian) on an 8-bit mask; exposed for tests
void blurMask (std::vector<uint8_t>& m, int w, int h, float radius);
} // namespace monie
