@echo off
rem Usage: scripts\build.bat [buildDir] [target ...]
rem   buildDir defaults to "build". Targets default to "all".
rem   Common targets: Duckling_VST3  Duckling_CLAP  CoreTests  DucklingDev
rem Configures (once) with Ninja + MSVC 2022 x64 Release and builds the given targets.
setlocal
set "ROOT=%~dp0.."
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build"
shift
set "TARGETS="
:collect
if "%~1"=="" goto done_collect
set "TARGETS=%TARGETS% %~1"
shift
goto collect
:done_collect
if "%TARGETS%"=="" set "TARGETS=all"

if not defined VSCMD_VER (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

pushd "%ROOT%"
if not exist "%BUILD_DIR%\build.ninja" (
  cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release || (popd & exit /b 1)
)
cmake --build "%BUILD_DIR%" --target %TARGETS%
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
