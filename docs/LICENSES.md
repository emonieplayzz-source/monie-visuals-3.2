# Dependencies & licensing

| Component | Use | License |
|---|---|---|
| JUCE 8.0.6 (fetched at configure time) | framework, VST3 wrapper, OpenGL glue | **AGPLv3 or commercial.** If you distribute binaries built with JUCE under AGPL, you must offer the full source under AGPLv3. For private use (making your own beats/videos) that's fine; for selling/closed-source distribution buy a JUCE license. |
| VST3 SDK (bundled in JUCE) | plugin format | Steinberg VST3 SDK is MIT/GPLv3 dual-licensed as of VST 3.8; check `JUCE/modules/juce_audio_processors/format_types/VST3_SDK` for the version shipped. "VST" is a trademark of Steinberg Media Technologies. |
| stb_image (fetched from github.com/nothings/stb) | PNG/JPG/BMP/GIF/TGA decoding | Public domain / MIT |
| monie visuals source, shaders, example images in `assets/examples` | - | Written for this project; the example PNGs are procedurally generated and free to redistribute. Choose your own license for the project code (note JUCE's AGPL terms above). |

External programs the user may install (NOT bundled, NOT linked): **FFmpeg** (video decode and MP4 export; LGPL/GPL depending on the build) and **whisper.cpp** (optional caption recognition; MIT, with models under their own licences). Because they run as separate processes, their licences do not apply to this plugin's code, but you must follow them if you redistribute them.

Fonts: installed system fonts are used as-is; fonts you import remain under their own licences - do not redistribute them without permission.
