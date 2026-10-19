#pragma once
#include <juce_opengl/juce_opengl.h>
#include <string>
#include <unordered_map>

namespace monie
{
// Thin wrapper around juce::OpenGLShaderProgram with a cached uniform-location table. GL thread only.
struct GlProgram
{
    std::unique_ptr<juce::OpenGLShaderProgram> p;
    std::unordered_map<std::string, int> loc;
    int pos = -1;

    bool build (juce::OpenGLContext& c, const juce::String& vs, const juce::String& fs, juce::String& err)
    {
        using namespace juce::gl;
        loc.clear(); p = std::make_unique<juce::OpenGLShaderProgram> (c);
        if (p->addVertexShader (vs) && p->addFragmentShader (fs) && p->link()) { pos = glGetAttribLocation (p->getProgramID(), "aPos"); return pos >= 0; }
        err = p->getLastError(); p.reset(); return false;
    }
    int L (const char* n)
    {
        using namespace juce::gl;
        auto it = loc.find (n); if (it != loc.end()) return it->second;
        const int l = glGetUniformLocation (p->getProgramID(), n); loc.emplace (n, l); return l;
    }
    void f1 (const char* n, float v) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform1f (l, v); }
    void f2 (const char* n, float a, float b) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform2f (l, a, b); }
    void f3 (const char* n, float a, float b, float c) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform3f (l, a, b, c); }
    void f4 (const char* n, float a, float b, float c, float d) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform4f (l, a, b, c, d); }
    void i1 (const char* n, int v) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform1i (l, v); }
    void fv (const char* n, const float* v, int count) { using namespace juce::gl; const int l = L (n); if (l >= 0) glUniform1fv (l, count, v); }
    bool ok() const { return p != nullptr; }
    void use() { p->use(); }
};
} // namespace monie
