# monie visuals 3.0

An original GPU audio-reactive visualizer for **FL Studio 26 on Windows 11 x64** (VST3 + standalone app), written in C++17 with JUCE 8 and OpenGL/GLSL.
30 scenes, a layer system (images, videos, text, captions), a 30-effect stack, animation presets, a gradient/colour workspace, audio-to-parameter mapping,
a timeline with keyframes, optional offline lyric captions, and MP4 export through FFmpeg.

> ## Honest status - read this first
> * **The Windows build has not been compiled or run by me.** This project was developed and tested in a Linux sandbox (GCC + Mesa software OpenGL).
>   It is standard JUCE/CMake code and is set up to build with Visual Studio 2022, but expect that a first Windows build may need small fixes.
> * **It has not been loaded in FL Studio.** The VST3 wrapper, host-transport sync and FL's handling of the OpenGL editor are untested.
> * **There is no compiled Windows binary in this package.** See *Getting a Windows build* below (GitHub Actions can build it for you without installing anything).
> * What *was* verified (Linux): the whole plugin compiles and runs as a standalone app; all shaders compile; 30 scenes render distinct pictures;
>   the full UI opens and every workspace draws; text/animation/effects/audio-mapping render; a real H.264+AAC MP4 was exported end-to-end through FFmpeg.
>   Pure-logic engines have 8 unit-test programs. Details and every unfinished item are in **docs/V3_STATUS.md**.
> * Automatic caption *recognition* has **not been run** (no Whisper model was available). The manual lyrics / LRC / SRT path is tested; the recognition glue is not.

## Getting a Windows build

**Option A - GitHub Actions (nothing to install).** Create a free GitHub repository, upload this folder, open the *Actions* tab, run **Build Windows**,
then download the `monie-visuals-windows-x64` artifact (contains `monie visuals.vst3` and the standalone `.exe`). If the build fails, the log shows the exact compiler error.

**Option B - local build.** Install Visual Studio 2022 (workload *Desktop development with C++*), CMake and Git, then run `scripts\build_windows.bat`.
Then run `scripts\install_vst3.bat` from an *Administrator* prompt. Full steps: **docs/BUILD_WINDOWS.md**.

In FL Studio: *Options > Manage plugins > Find plugins*, then add **monie visuals** as an effect on a mixer insert (or the master) so it receives the audio you want to visualise.

## What you get

| Area | Summary |
|---|---|
| Scenes | 30 original GLSL scenes in 6 categories, each with its own 8 controls; thumbnails, search, favourites, randomise / vary, copy / paste, A/B compare, crossfade between scenes |
| Layers | Image, video, text and caption layers: transform, crop, colour, blend mode, opacity, audio-driven morph/displacement; reorder, duplicate, group, lock, hide, rename |
| Text | Any installed font plus imported TTF/OTF (validated), size/spacing/alignment/wrap, curve, skew/tilt, gradient or image fill, 2 outlines, shadow, glow, background box, blur / distortion / noise / RGB split, 36 animation presets (stackable, with easing, direction, audio reactivity), drag in the preview |
| Effects | 30 non-destructive effects, global stack and per-layer stacks: reorder, bypass, duplicate, audio-driven amount, effect parameters |
| Colour | Gradient editor with stops (hex / RGB / HSV), duotone / tritone, 24 palettes (not all neon), user palettes, palette import/export, colour grade, audio colour drift |
| Audio | Bass / low-mid / mid / high-mid / treble / RMS / peak / transient / beat / flux; per-parameter mappings with curve, smoothing, attack/release, threshold, beat sample-and-hold |
| Timeline | Playhead, markers, loop region, clips you can move and trim, keyframes with interpolation, zoom/scroll |
| Captions | Manual lyrics (LRC / SRT / plain text, editable, exportable), caption styles, word highlight, optional offline Whisper recognition (see docs/CAPTIONS_SETUP.md) |
| Export | MP4 (H.264 + AAC) via an external FFmpeg: reacts to an audio file you choose, exact frame timing, progress / ETA / cancel, optional burned-in captions and an `.srt` |
| Presets | Project, effect chain, text style, caption style, palette, mapping presets as plain files; randomise with locks |
| FL Studio safety | The audio is passed through untouched; the audio thread only copies samples into a lock-free buffer; all analysis, rendering, recognition and export happen on other threads |

The 12 workspaces (OVERVIEW, SCENES, MEDIA, TEXT, CAPTIONS, EFFECTS, COLORS, AUDIO, LAYERS, TIMELINE, PRESETS, SETTINGS) switch the browser on the left and the inspector on the right.
Four themes (Dark, Extra dark, Monochrome, High contrast) with a custom accent colour. Hover any control for a tooltip; double-click a slider to reset it.

## Shortcuts
`Space` play/pause (standalone) - `Home` start - `Ctrl+Z / Ctrl+Y` undo/redo - `Ctrl+S / Ctrl+O` save/open - `Ctrl+D` duplicate layer - `Delete` delete layer - `F` fit - `G` grid - `B` before/after - `Ctrl+F` search parameters.

## Project layout
`Source/Core` pure C++ engines (no JUCE; unit tested) - `Source/Render` OpenGL compositor and text rasteriser - `Source/Media` image/video decode - `Source/UI` editor panels -
`Shaders` GLSL - `tests` unit tests - `docs` documentation - `examples` example project and lyrics - `scripts` Windows build/install helpers.

## Licence note
JUCE is AGPLv3/commercial; see **docs/LICENSES.md**. FFmpeg and whisper.cpp are *not* bundled: they are separate programs you install yourself.
