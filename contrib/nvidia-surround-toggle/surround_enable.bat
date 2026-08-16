@echo off
setlocal enableextensions enabledelayedexpansion
rem ============================================================================
rem surround_enable.bat - Enable NVIDIA Surround only if it is not already on.
rem
rem Idempotent wrapper around nvidia-surround-toggle.exe, meant for a Sunshine
rem per-app prep-cmd "Do" (elevated). If a Surround span is already active it
rem does NOTHING and exits 0 (so the app still launches); otherwise it enables
rem the span and propagates the toggle's exit code (non-zero aborts the launch).
rem
rem "Already on" = a Mosaic grid with 2+ displays is present (the toggle's
rem `list` prints e.g. "grid[0]: 1x2, 2 display(s), ..."; a torn-down state is
rem "1x1, 1 display(s)" or no grid at all).
rem
rem All arguments are forwarded verbatim to `nvidia-surround-toggle enable`,
rem with --apply appended. Typical prep-cmd Do:
rem   surround_enable.bat --tv-id 0x80061083 --dummy-id 0x80061082 [--skip-validate]
rem
rem The helper exe is auto-located next to / under this script; override with
rem the SURROUND_TOGGLE_EXE environment variable.
rem ============================================================================

call :find_toggle
if not defined TOGGLE (
  echo [surround_enable] ERROR: nvidia-surround-toggle.exe not found. Set SURROUND_TOGGLE_EXE.>&2
  exit /b 1
)

rem --- Already enabled? A Mosaic grid with 2+ displays means a span is active. ---
"%TOGGLE%" list 2>nul | findstr /R /C:"[2-9] display(s)" >nul
if not errorlevel 1 (
  echo [surround_enable] Surround span already active; nothing to do.
  exit /b 0
)

echo [surround_enable] No span active; enabling Surround...
"%TOGGLE%" enable %* --apply
exit /b !errorlevel!

:find_toggle
set "TOGGLE="
if defined SURROUND_TOGGLE_EXE if exist "%SURROUND_TOGGLE_EXE%" set "TOGGLE=%SURROUND_TOGGLE_EXE%"
if not defined TOGGLE if exist "%~dp0build\nvidia-surround-toggle.exe" set "TOGGLE=%~dp0build\nvidia-surround-toggle.exe"
if not defined TOGGLE if exist "%~dp0contrib\nvidia-surround-toggle\build\nvidia-surround-toggle.exe" set "TOGGLE=%~dp0contrib\nvidia-surround-toggle\build\nvidia-surround-toggle.exe"
goto :eof
