#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/GlobalDefs.h"
#include "Core/SceneRegistry.h"

// Host-automatable parameters. IDs are PERMANENT: renaming one breaks saved FL Studio projects and automation clips.
//   scene            - choice, 30 entries (index = Core/SceneRegistry.h order; append new scenes at the END only)
//   sk0 .. sk7       - the 8 knobs of the CURRENT scene, normalised 0..1 (their meaning/labels follow the selected scene)
//   <global ids>     - see Core/GlobalDefs.h (master, speed, zoom, ... edge)
//   quality, fps     - render settings
//   captions         - lyric captions on/off (same switch as the CAPTIONS toggle in the UI)
namespace monie
{
inline juce::StringArray sceneChoiceNames()
{ juce::StringArray a; for (auto& s : sceneCatalogue()) a.add (s.name); return a; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scene", 2 }, "Scene", sceneChoiceNames(), 0));
    for (int i = 0; i < 8; ++i)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sk" + juce::String (i), 2 }, "Scene Knob " + juce::String (i + 1),
                                                           juce::NormalisableRange<float> (0.f, 1.f, 0.f), sceneCatalogue()[0].defNorm (i)));
    for (auto& d : globalDefs())
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { d.id, 2 }, d.name, juce::NormalisableRange<float> (d.minV, d.maxV, 0.f), d.defV));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "quality", 2 }, "Render Quality", juce::StringArray::fromTokens (qualityNames(), "|", ""), 2));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "fps", 2 }, "Frame Rate", juce::StringArray::fromTokens (fpsNames(), "|", ""), 1));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "captions", 2 }, "Captions On", false));
    return l;
}
} // namespace monie
