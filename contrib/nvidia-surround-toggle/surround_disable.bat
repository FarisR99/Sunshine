@echo off
setlocal enableextensions enabledelayedexpansion
rem ============================================================================
rem surround_disable.bat - Safely tear down NVIDIA Surround.
rem
rem   1. If ANY Sunshine instance is currently streaming, do NOTHING (leave the
rem      Surround span up) and exit 0.
rem   2. Otherwise: stop the game (--game <exe>), then disable Surround (--apply).
rem
rem "Streaming" is detected from the per-session UDP ports. Sunshine binds its
rem video/control/audio UDP ports (base+9 / base+10 / base+11) ONLY while a
rem stream is active -- the broadcast context is a ref-counted resource held for
rem the duration of a session (see src/stream.cpp). On Windows, mDNS is handled
rem by the OS (DnsServiceRegister), so an idle Sunshine binds none of these
rem ports. A bound streaming port therefore means that instance is streaming.
rem
rem Usage (standalone cleanup, or a prep-cmd Undo):
rem   surround_disable.bat --game "Stardew Valley.exe" [--ports 47989,48989] ^
rem                        -- --tv-id 0x80061083 [--dummy-id 0x80061082]
rem
rem   --game <exe>     Image name to taskkill when nothing is streaming (optional).
rem   --ports <list>   Comma-separated Sunshine BASE ports to probe (default 47989).
rem                    For each base B, ports B+9/B+10/B+11 are checked.
rem   --              Everything after this is forwarded to
rem                    `nvidia-surround-toggle disable ... --apply`.
rem
rem Override the helper exe path with the SURROUND_TOGGLE_EXE environment variable.
rem ============================================================================

set "GAME="
set "PORTS=47989"
set "DISABLE_ARGS="

:parse
if "%~1"=="" goto after
if /I "%~1"=="--game"  ( set "GAME=%~2" & shift & shift & goto parse )
if /I "%~1"=="--ports" ( set "PORTS=%~2" & shift & shift & goto parse )
if "%~1"=="--" ( shift & goto collect )
rem any stray token before `--` is forwarded to the toggle too
set "DISABLE_ARGS=%DISABLE_ARGS% %1"
shift
goto parse

:collect
if "%~1"=="" goto after
set "DISABLE_ARGS=%DISABLE_ARGS% %1"
shift
goto collect

:after
call :find_toggle
if not defined TOGGLE (
  echo [surround_disable] ERROR: nvidia-surround-toggle.exe not found. Set SURROUND_TOGGLE_EXE.>&2
  exit /b 1
)

rem --- Is any Sunshine instance streaming? (any base+9/+10/+11 UDP port bound) ---
set "STREAMING="
for %%B in (%PORTS:,= %) do (
  set /a V=%%B+9, C=%%B+10, A=%%B+11
  for %%P in (!V! !C! !A!) do (
    netstat -ano | findstr /R /C:":%%P " >nul && set "STREAMING=1"
  )
)

if defined STREAMING (
  echo [surround_disable] A Sunshine stream is active; leaving Surround enabled.
  exit /b 0
)

if defined GAME (
  echo [surround_disable] No active stream; stopping game "%GAME%"...
  taskkill /F /IM "%GAME%" >nul 2>&1
)

echo [surround_disable] Disabling Surround...
"%TOGGLE%" disable%DISABLE_ARGS% --apply
exit /b !errorlevel!

:find_toggle
set "TOGGLE="
if defined SURROUND_TOGGLE_EXE if exist "%SURROUND_TOGGLE_EXE%" set "TOGGLE=%SURROUND_TOGGLE_EXE%"
if not defined TOGGLE if exist "%~dp0build\nvidia-surround-toggle.exe" set "TOGGLE=%~dp0build\nvidia-surround-toggle.exe"
if not defined TOGGLE if exist "%~dp0contrib\nvidia-surround-toggle\build\nvidia-surround-toggle.exe" set "TOGGLE=%~dp0contrib\nvidia-surround-toggle\build\nvidia-surround-toggle.exe"
goto :eof
