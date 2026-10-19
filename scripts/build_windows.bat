@echo off
REM monie visuals - Windows 11 x64 build. Run from "x64 Native Tools Command Prompt for VS 2022" or any prompt with CMake on PATH.
REM Requirements: Visual Studio 2022 (Desktop development with C++), CMake >= 3.22, Git, internet (first configure downloads JUCE + stb).
setlocal
cd /d "%~dp0\.."
where cmake >nul 2>nul || (echo [ERROR] CMake not found. Install from https://cmake.org/download/ & exit /b 1)
where git   >nul 2>nul || (echo [ERROR] Git not found. Install from https://git-scm.com/ & exit /b 1)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || (echo [ERROR] CMake configure failed & exit /b 1)
cmake --build build --config Release --parallel || (echo [ERROR] Build failed & exit /b 1)

echo.
echo === Running unit tests ===
ctest --test-dir build -C Release --output-on-failure || (echo [ERROR] unit tests failed & exit /b 1)

set OUT=build\MonieVisuals_artefacts\Release
echo.
echo Build OK.
echo   VST3:       %OUT%\VST3\monie visuals.vst3
echo   Standalone: %OUT%\Standalone\monie visuals.exe
echo Next: run scripts\install_vst3.bat (as Administrator) to install the VST3.
