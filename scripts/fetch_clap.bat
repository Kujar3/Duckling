@echo off
rem Usage: scripts\fetch_clap.bat [destDir]   (destDir defaults to third_party\clap-juce-extensions)
rem Clones clap-juce-extensions (pinned commit, JUCE 8 compatible) together with its clap and
rem clap-helpers submodules into third_party\clap-juce-extensions. Once it exists, CMake adds the
rem Duckling_CLAP target (option DUCKLING_CLAP, ON by default):
rem   scripts\build.bat build-clap Duckling_CLAP
rem If the build dir was configured before the checkout existed, delete it (or re-run cmake) first.
setlocal
set "ROOT=%~dp0.."
set "DEST=%ROOT%\third_party\clap-juce-extensions"
if not "%~1"=="" set "DEST=%~f1"
set "CJE_URL=https://github.com/free-audio/clap-juce-extensions.git"
rem main @ 2026-10-06 ("guard guiRequestResize with canUseGui check"), clap 1.2.7
set "CJE_COMMIT=7adee3a1bd4684d4caa5601100e364abccff4b4d"

if not exist "%DEST%\.git" (
  git clone "%CJE_URL%" "%DEST%" || exit /b 1
)

for /f %%H in ('git -C "%DEST%" rev-parse HEAD') do set "HEAD_NOW=%%H"
if /i not "%HEAD_NOW%"=="%CJE_COMMIT%" (
  git -C "%DEST%" cat-file -e %CJE_COMMIT% 2>nul || git -C "%DEST%" fetch origin || exit /b 1
  git -C "%DEST%" -c advice.detachedHead=false checkout %CJE_COMMIT% || exit /b 1
)
git -C "%DEST%" submodule update --init --recursive || exit /b 1

rem Local fix: request a CLAP param value rescan after state load (CLAP params spec; clap-validator
rem state-reproducibility-* tests). Applied once; re-running the script is harmless.
set "PATCH=%~dp0clap-juce-extensions-state-rescan.patch"
git -C "%DEST%" apply --reverse --check "%PATCH%" >nul 2>&1
if errorlevel 1 (
  git -C "%DEST%" apply "%PATCH%" || (echo failed to apply %PATCH% & exit /b 1)
  echo applied %PATCH%
)
echo clap-juce-extensions ready at %CJE_COMMIT%
exit /b 0
