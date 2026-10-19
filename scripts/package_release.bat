@echo off
REM Collects the built binaries + docs into .\release
setlocal
cd /d "%~dp0\.."
set OUT=build\MonieVisuals_artefacts\Release
if not exist "%OUT%\VST3\monie visuals.vst3" (echo [ERROR] Build first & exit /b 1)
rmdir /s /q release 2>nul
mkdir release\VST3 release\Standalone release\docs release\assets
xcopy "%OUT%\VST3\monie visuals.vst3" "release\VST3\monie visuals.vst3\" /E /I /Y >nul
copy "%OUT%\Standalone\monie visuals.exe" release\Standalone\ >nul
copy README.md release\ >nul & copy docs\*.md release\docs\ >nul & xcopy assets release\assets /E /I /Y >nul
echo Release folder ready: %CD%\release
