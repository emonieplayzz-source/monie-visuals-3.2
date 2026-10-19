# Troubleshooting

**Black picture / "SHADER ERROR" in the status line** - your GPU driver rejected a shader. Update the graphics driver; if it persists, note the message shown and the GPU name (shown in the status line). The plugin needs OpenGL 2.1 or newer.

**Plugin not listed in FL Studio** - run `scripts\install_vst3.bat` as Administrator, then *Options > Manage plugins > Find installed plugins*. The folder must be `C:\Program Files\Common Files\VST3\monie visuals.vst3`.

**No reaction to music** - the status bar shows "no audio" until samples arrive. Put the plugin on the mixer insert that carries the music, and press play. For a sidechain feed choose *Sidechain bus* in AUDIO > Analysis source.

**Preview is slow** - lower the quality box in the top bar (Low = 50% resolution) or the frame rate in SETTINGS; turn off expensive effects (Bloom, Radial Blur, Pixel Sort, Feedback Trails).

**Video layer is black / "FFmpeg not found"** - see docs/FFMPEG.md. Only video needs FFmpeg; images do not.

**"MISSING MEDIA"** - a file moved. MEDIA workspace > right-click the entry > *Relink...*.

**Font shows "not installed"** - the project uses a font this PC lacks; a default font is used until you pick another or import the .ttf/.otf (TEXT > *Import font*).

**Export fails** - the message includes FFmpeg's own error text. Common causes: output folder not writable, audio file unreadable, FFmpeg missing.

**Captions never appear** - check CAPTIONS ON is highlighted, a caption layer exists and is visible, and the playhead is inside the lyric times. For automatic recognition see docs/CAPTIONS_SETUP.md.

**Projects saved with older versions** - 3.0 changed parameter IDs; states from 0.x/2.x are ignored (the plugin opens at defaults) rather than loaded wrongly.

**Reporting a bug** - include the Windows version, GPU, FL Studio version and the text in the status line.
