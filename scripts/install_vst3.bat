@echo off
REM Installs the built VST3 into the standard Windows location. Needs an Administrator prompt.
setlocal
cd /d "%~dp0\.."
set SRC=build\MonieVisuals_artefacts\Release\VST3\monie visuals.vst3
if not exist "%SRC%" set SRC=release\VST3\monie visuals.vst3
if not exist "%SRC%" (echo [ERROR] VST3 not found. Build first with scripts\build_windows.bat & exit /b 1)
set DST=%CommonProgramFiles%\VST3\monie visuals.vst3
echo Installing to "%DST%"
xcopy "%SRC%" "%DST%\" /E /I /Y >nul || (echo [ERROR] Copy failed - run this prompt as Administrator & exit /b 1)
echo Installed. In FL Studio: Options > Manage plugins > Find plugins (scan), then add "monie visuals" from the Plugin database > Installed > Effects.
