# Lyric captions

## Manual captions (works with nothing extra installed)
CAPTIONS workspace: type or paste lyrics as `[mm:ss.xx] line`, or **Import...** an `.lrc`, `.srt` or plain `.txt` (plain text is spread evenly over a time range as a starting point).
Export back out with **Export SRT / Export LRC**. Turn **CAPTIONS ON**; add a caption layer and style it (presets: Karaoke, Clean box, Big words, Warm italic).
Captions follow the timeline clock, so pause, seek and loop never desynchronise them.

## Automatic recognition (optional, OFF by default, untested by the author)
Recognition runs in a separate program, **whisper.cpp**, on your own computer. Nothing is uploaded anywhere.

1. Get `whisper-cli.exe` (build or download from https://github.com/ggml-org/whisper.cpp) and a model such as `ggml-base.en.bin` (multilingual: `ggml-base.bin`).
2. CAPTIONS > *Automatic recognition (optional)*: tick **I understand and consent**, set the program and model paths, language (or `auto`).
3. Easiest and most accurate: **Transcribe an audio file...** (a vocal stem or full song). The result becomes automatic captions; use *Auto lines -> manual* then correct the text.
4. Live mode: tick *Recognise lyrics automatically from the host audio*, then **CAPTIONS ON**. While your DAW plays, audio is cut into chunks (default 12 s) and recognised in the background; captions appear a few seconds late.
   A routed vocal stem on the plugin's sidechain input works far better than a full mix.

Status pill (top bar): OFF / ON / LISTENING / PROCESSING / CAPTIONS ACTIVE / ERROR. If recognition is slower than real time, the oldest queued chunks are dropped (counter shown) rather than letting memory grow.

## Privacy
- Off unless you consent *and* turn it on. When off, no audio is copied for recognition and no process is started.
- The plugin only reads the audio your DAW sends it. It never opens your microphone and never captures system audio.
- Audio chunks are written to a temporary folder, passed to whisper.cpp on this machine, and deleted afterwards.
- Singing is much harder than speech: expect mistakes. Words below the *Min Confidence* setting are hidden. Always review automatic lyrics.
