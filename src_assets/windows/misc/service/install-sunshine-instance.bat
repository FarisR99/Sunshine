@echo off
setlocal enableextensions enabledelayedexpansion
rem ============================================================================
rem install-sunshine-instance.bat
rem
rem Register an ADDITIONAL Sunshine service instance that runs from the same
rem executable as the primary install, pointed at its own config directory and
rem port. Relies on the sunshinesvc arg-forwarding patch (sunshinesvc forwards
rem --config-dir / port= to Sunshine.exe), so one exe can back many instances.
rem
rem Usage (run from an ELEVATED / Administrator prompt):
rem   install-sunshine-instance.bat --name <ServiceName> --config-dir <dir>
rem                                 --port <basePort> [options]
rem
rem Required:
rem   --name <ServiceName>   Windows service name, e.g. SunshineService2
rem   --config-dir <dir>     Per-instance config dir, e.g. "C:\Program Files\Sunshine\config-2"
rem   --port <basePort>      Base port, e.g. 48989 (web UI = basePort+1)
rem
rem Options:
rem   --start <type>         auto (default) | demand | delayed-auto | disabled
rem   --display "<text>"     Service display name (default: "Sunshine Service (<name>)")
rem   --svc "<path>"         Path to the PATCHED sunshinesvc.exe (auto-located otherwise)
rem   --firewall             Also add inbound firewall rules for this port range
rem   --no-start             Create the service but do not start it now
rem   --force                Allow operating on the primary "SunshineService"
rem
rem Add more instances later by re-running with a different --name/--config-dir/--port.
rem ============================================================================

set "NAME="
set "CONFIG_DIR="
set "PORT="
set "START=auto"
set "DISPLAY="
set "SVC="
set "FIREWALL="
set "NOSTART="
set "FORCE="

:parse
if "%~1"=="" goto validate
if /I "%~1"=="--name"       ( set "NAME=%~2" & shift & shift & goto parse )
if /I "%~1"=="--config-dir" ( set "CONFIG_DIR=%~2" & shift & shift & goto parse )
if /I "%~1"=="--port"       ( set "PORT=%~2" & shift & shift & goto parse )
if /I "%~1"=="--start"      ( set "START=%~2" & shift & shift & goto parse )
if /I "%~1"=="--display"    ( set "DISPLAY=%~2" & shift & shift & goto parse )
if /I "%~1"=="--svc"        ( set "SVC=%~2" & shift & shift & goto parse )
if /I "%~1"=="--firewall"   ( set "FIREWALL=1" & shift & goto parse )
if /I "%~1"=="--no-start"   ( set "NOSTART=1" & shift & goto parse )
if /I "%~1"=="--force"      ( set "FORCE=1" & shift & goto parse )
echo [install] Unknown argument: %~1>&2
exit /b 2

:validate
call :require_admin || exit /b 1
if not defined NAME       ( echo [install] --name is required.>&2 & exit /b 2 )
if not defined CONFIG_DIR ( echo [install] --config-dir is required.>&2 & exit /b 2 )
if not defined PORT       ( echo [install] --port is required.>&2 & exit /b 2 )
if /I "%NAME%"=="SunshineService" if not defined FORCE (
  echo [install] Refusing to touch the primary "SunshineService" without --force.>&2
  exit /b 2
)
if not defined DISPLAY set "DISPLAY=Sunshine Service (%NAME%)"

call :find_svc
if not defined SVC (
  echo [install] sunshinesvc.exe not found; pass --svc or set SUNSHINE_SVC_EXE.>&2
  exit /b 1
)

set /a UIPORT=%PORT%+1
echo [install] Service host : "%SVC%"
echo [install] Instance name: %NAME%
echo [install] Config dir   : %CONFIG_DIR%
echo [install] Base port    : %PORT%   (web UI: https://localhost:!UIPORT!)
echo [install] Start type   : %START%

rem --- create if new, otherwise reconfigure the existing service ---
sc qc "%NAME%" >nul 2>&1
if %ERRORLEVEL%==0 (
  echo [install] Service already exists; reconfiguring...
  net stop "%NAME%" >nul 2>&1
  set "SC_CMD=config"
) else (
  set "SC_CMD=create"
)

sc !SC_CMD! "%NAME%" binPath= "\"%SVC%\" --config-dir \"%CONFIG_DIR%\" port=%PORT%" start= %START% DisplayName= "%DISPLAY%"
if errorlevel 1 ( echo [install] sc !SC_CMD! failed.>&2 & exit /b 1 )
sc description "%NAME%" "Sunshine game stream host instance (config-dir %CONFIG_DIR%, port %PORT%)." >nul

if defined FIREWALL (
  set /a LO=%PORT%-5, HI=%PORT%+21
  echo [install] Adding firewall rules for ports !LO!-!HI! (TCP+UDP)...
  netsh advfirewall firewall delete rule name="Sunshine %NAME% TCP" >nul 2>&1
  netsh advfirewall firewall delete rule name="Sunshine %NAME% UDP" >nul 2>&1
  netsh advfirewall firewall add rule name="Sunshine %NAME% TCP" dir=in action=allow protocol=TCP localport=!LO!-!HI! >nul
  netsh advfirewall firewall add rule name="Sunshine %NAME% UDP" dir=in action=allow protocol=UDP localport=!LO!-!HI! >nul
)

if not defined NOSTART (
  echo [install] Starting "%NAME%"...
  net start "%NAME%"
)
echo [install] Done.
exit /b 0

:require_admin
net session >nul 2>&1
if errorlevel 1 ( echo [install] Please run this from an elevated (Administrator) prompt.>&2 & exit /b 1 )
exit /b 0

rem Locate the PATCHED sunshinesvc.exe. Its folder also decides which Sunshine.exe
rem runs (the exe two folders up from sunshinesvc.exe). A real INSTALL is preferred
rem over a dev build: an installed Sunshine (found next to this script, via the
rem primary SunshineService registration, or at the default path) wins over any
rem C:\dev\...\cmake-build-release build. Force a specific one with --svc.
:find_svc
if defined SVC (
  if exist "%SVC%" ( for %%I in ("%SVC%") do set "SVC=%%~fI" & goto :eof )
  echo [install] --svc path not found: "%SVC%" (falling back to auto-detect)>&2
  set "SVC="
)
if defined SUNSHINE_SVC_EXE if exist "%SUNSHINE_SVC_EXE%" set "SVC=%SUNSHINE_SVC_EXE%"
rem Installed layouts (preferred):
rem  1) tools\ sitting next to this script's folder (installed scripts copy; any dir).
if not defined SVC if exist "%~dp0..\tools\sunshinesvc.exe" set "SVC=%~dp0..\tools\sunshinesvc.exe"
rem  2) The primary SunshineService's registered exe (covers a custom install dir).
if not defined SVC call :svc_from_service SunshineService
rem  3) The default install location.
if not defined SVC if exist "C:\Program Files\Sunshine\tools\sunshinesvc.exe" set "SVC=C:\Program Files\Sunshine\tools\sunshinesvc.exe"
rem Development builds (fallback only when no install is found):
if not defined SVC if exist "%~dp0tools\sunshinesvc.exe" set "SVC=%~dp0tools\sunshinesvc.exe"
if not defined SVC if exist "%~dp0cmake-build-release\tools\sunshinesvc.exe" set "SVC=%~dp0cmake-build-release\tools\sunshinesvc.exe"
if not defined SVC if exist "C:\dev\Sunshine\cmake-build-release\tools\sunshinesvc.exe" set "SVC=C:\dev\Sunshine\cmake-build-release\tools\sunshinesvc.exe"
if defined SVC for %%I in ("%SVC%") do set "SVC=%%~fI"
goto :eof

rem Resolve a service's host exe from its `sc qc` BINARY_PATH_NAME (%1 = service name).
rem Handles a quoted path with spaces and ignores any trailing arguments.
:svc_from_service
set "IMG="
for /f "tokens=2,*" %%A in ('sc qc "%~1" 2^>nul ^| findstr /I "BINARY_PATH_NAME"') do set "IMG=%%B"
if not defined IMG goto :eof
set "GOTFIRST="
for %%I in (%IMG%) do if not defined GOTFIRST ( set "SVC=%%~fI" & set "GOTFIRST=1" )
if defined SVC if not exist "%SVC%" set "SVC="
set "IMG="
set "GOTFIRST="
goto :eof
