@echo off
rem ninja-sarif.cmd - ninja, then collate SARIF, whatever ninja's outcome was.
rem
rem Scaffolding, not architecture. A build graph only runs an edge when every
rem input succeeded, and diagnostics matter most when inputs failed; the only
rem process that always runs to completion is the build tool itself, so this
rem wraps it. The real fix is a SARIF Viewer that imports a glob, at which
rem point this file and collate-sarif.ps1 both go away.
rem
rem Installed through CMAKE_MAKE_PROGRAM in CMakePresets.json. The build
rem directory is the working directory: cmake --build sets it before invoking
rem the make program, and every CLion build goes through cmake --build.
rem
rem Two invocations must pass through untouched: CMake runs "ninja --version"
rem at configure and parses the stdout, and "-t restat/cleandead/recompact" on
rem regenerate. Neither is a build, so neither gets a collate pass.

ninja %*
set "NINJA_EXIT=%ERRORLEVEL%"

echo( %* | findstr /C:"--version" /C:" -t " >nul
if not errorlevel 1 exit /b %NINJA_EXIT%

pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0collate-sarif.ps1" "%CD%" "%CD%\diagnostics.sarif"

rem ninja's exit code is the build's; the collator's is never a build failure.
exit /b %NINJA_EXIT%
