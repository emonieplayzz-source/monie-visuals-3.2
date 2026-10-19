# Status of every feature (honest)

Legend: **Verified** = run in the Linux sandbox (GCC, Mesa llvmpipe software OpenGL, Xvfb) and seen working. **Unit-tested** = pure logic with a test program.
**Written, not run** = code exists and compiles but no run produced evidence. **Prototype** = works in a limited way, described. **Not implemented**.

Nothing below has been compiled on Windows or loaded in FL Studio.

## Verified in the sandbox
| Item | Evidence |
|---|---|
| Whole plugin compiles and links (VST3 sources, standalone target) on Linux | build succeeded; standalone binary runs |
| All 7 shader programs compile | `status.txt` self-test: no shader errors |
| 30 scenes each render a distinct picture | contact sheet of all 30 (docs/screenshots/scene_contact_sheet_software_gl.png), inspected |
| UI opens; all 12 workspaces draw their browser + inspector | screenshots of every workspace |
| Text layer + animation + effect + audio-mapping render | exported video frames |
| **MP4 export end to end** (offline audio analysis, GPU frames, FFmpeg pipe, AAC audio) | exported 3 s 640x360 30 fps H.264+AAC; `ffprobe` confirmed codec/size/duration; frames inspected |
| Live preview with text layer selection frame | screenshot |

## Unit-tested pure logic (`tests/`, 8 programs, all pass under g++)
audio analyser (FFT, bands, beat), audio drive smoothing, audio mapping engine, gradients + global parameter table, text-animation presets (bounded outputs),
project JSON save/load (round-trip, rejects bad input), lyrics (LRC/SRT parse + export), caption timing, whisper JSON parsing, ASR chunker (seek/pause/silence),
WAV writer, export planning (validation, frame timing, FFmpeg command line quoting), timeline/keyframes/undo, randomiser, transport clock.

## Written, compiled, but not exercised by a run
- Interactive behaviour of buttons, drag and drop, popup menus, file dialogs, context menus (no mouse automation was available; panels only *rendered*).
- Image layers and video layers (decode + draw). Video needs FFmpeg/ffprobe on PATH or set in SETTINGS.
- Font import validation UI, relink, palette import/export dialogs, preset save/load.
- Keyframe editing from the inspector and timeline clip trimming/moving.
- Saving/restoring plugin state inside a host project (code path exists; no host was available).
- Host transport following in a DAW (standalone uses its own transport).
- Sidechain bus input routing.
- Captions: the **recognition glue** (CaptionService launching `whisper-cli`, chunking live audio, merging results) is written; the chunker and the JSON parser are unit-tested but a real recognition run has **never happened** (no model/binary available).

## Prototype / limited (by design, not hidden)
- "Per-letter" animations (Typewriter, Letter Wave) are driven by position inside the text box in the shader, not by true per-glyph transforms.
- Pixel sorting is an approximation (directional bright-pixel smear), not a true sort.
- Video decode uses an external FFmpeg process (preview at reduced width; export at full width). No FFmpeg code is linked.
- Live caption recognition is chunked (default 12 s): captions appear several seconds after the singing. For accurate lyrics use *Transcribe an audio file* once, then correct the text.
- Preview panels have fixed proportions that scale with the window; there are no draggable splitters yet. Window size and theme are remembered.
- Draggable text in the preview moves layers; resizing/rotating by handles is not implemented (use the sliders).
- Shapes (rectangles/circles as layers) are **not implemented**.
- Proxy files for video: preview decodes at a reduced width on the fly; no persistent proxy files are created.
- Scene "duplicate" is provided as Copy/Paste of the scene controls plus Vary / Randomise, not as separate scene instances.
- Several layers can use the same scene only through layering; the scene itself is a single background.
- FL Studio's own automation of the 35+ global parameters and 8 scene knobs is wired through JUCE parameters (stable ids) but untested in FL.

## Not implemented
Shapes layers - lyric word-level manual timing editor (word timing exists only from recognition/enhanced LRC) - persistent video proxies - draggable splitters - project templates gallery - Windows installer.

## Interaction test (Linux, Xvfb, real X11 mouse events via XTest)
Clicking a scene in the list, switching to the COLORS workspace and pressing Play all worked
(docs/screenshots/ui_interaction_click_test_linux_software_gl.png). Drags, popup menus and file dialogs were NOT exercised.
