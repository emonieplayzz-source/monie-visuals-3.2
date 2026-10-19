# Building on Windows 11 (untested by the author - expect to report a first error or two)

## Requirements
- Visual Studio 2022 with *Desktop development with C++* (MSVC, Windows SDK)
- CMake 3.22 or newer, Git (the first configure downloads JUCE 8.0.6 and stb)

## Steps
1. Open **x64 Native Tools Command Prompt for VS 2022**.
2. `cd` into this folder and run `scripts\build_windows.bat`.
   It configures, builds the VST3, the standalone app and the unit tests, and runs the tests.
3. Outputs: `build\MonieVisuals_artefacts\Release\VST3\monie visuals.vst3` and `...\Standalone\monie visuals.exe`.
4. Run `scripts\install_vst3.bat` from an **Administrator** prompt (copies to `C:\Program Files\Common Files\VST3`).
5. FL Studio 26: *Options > Manage plugins > Find installed plugins*. Add **monie visuals** to a mixer insert.

First thing to do: run the standalone `.exe` (it needs no DAW) and check that the picture appears. If the picture is black, see TROUBLESHOOTING.md.

## Without installing anything: GitHub Actions
Upload the folder to a GitHub repository, open *Actions > Build Windows > Run workflow*, and download the artifact. The log contains any compiler errors.

## If the build fails
Copy the first `error C....` lines from the output; they are almost always a one-line fix.
