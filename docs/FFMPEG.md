# FFmpeg (needed for video export and video layers)

monie visuals does not contain FFmpeg. It starts an `ffmpeg` / `ffprobe` program that you install yourself.

1. Download a Windows build (for example from https://www.gyan.dev/ffmpeg/builds/ - "release essentials") and unzip it somewhere permanent, e.g. `C:\Tools\ffmpeg`.
2. Either add `C:\Tools\ffmpeg\bin` to your PATH, or in the plugin open **SETTINGS > Tools > Choose FFmpeg folder** and pick the `bin` folder.
3. SETTINGS shows "FFmpeg found: <path>" when it works.

How export works: **Export video...** asks for an audio file (the plugin cannot capture your DAW's audio offline - render your song or a stem to WAV first),
a resolution, frame rate and time range. Every frame is rendered on the GPU at exactly its time, reacts to the analysis of that audio file, and is piped to FFmpeg
which encodes H.264 (yuv420p) + AAC into an MP4. Progress, ETA and Cancel are shown; a cancelled export deletes the partial file; FFmpeg's error text is shown if it fails.
Tick *Also write an .srt* for a caption file, untick *Burn captions* for a clean picture.

Licensing: FFmpeg builds are LGPL/GPL depending on the build; because it is a separate program, nothing is linked into the plugin.
