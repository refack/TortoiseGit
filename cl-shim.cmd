@echo off
rem cl-shim.cmd - single-path entry point for cl-shim.ps1.
rem
rem CMAKE_<LANG>_COMPILER_LAUNCHER wants one executable path, and a .ps1 is not
rem executable. This supplies the path; the .ps1 beside it holds the logic.
rem %* forwards the compiler's argv verbatim, quoting intact.
rem
rem -NoProfile matters for more than startup time: a profile that writes to the
rem host would land in the middle of the compiler output this shim forwards.
pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0cl-shim.ps1" %*
exit /b %ERRORLEVEL%
