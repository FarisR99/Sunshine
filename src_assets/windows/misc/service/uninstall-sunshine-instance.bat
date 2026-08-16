@echo off
setlocal enableextensions
rem ============================================================================
rem uninstall-sunshine-instance.bat
rem
rem Stop and delete an additional Sunshine service instance created with
rem install-sunshine-instance.bat (and, optionally, its firewall rules).
rem
rem Usage (run from an ELEVATED / Administrator prompt):
rem   uninstall-sunshine-instance.bat --name <ServiceName> [--firewall] [--force]
rem
rem   --name <ServiceName>   Service to remove, e.g. SunshineService2
rem   --firewall             Also remove the "Sunshine <name> TCP/UDP" rules
rem   --force                Allow removing the primary "SunshineService"
rem ============================================================================

set "NAME="
set "FIREWALL="
set "FORCE="

:parse
if "%~1"=="" goto run
if /I "%~1"=="--name"     ( set "NAME=%~2" & shift & shift & goto parse )
if /I "%~1"=="--firewall" ( set "FIREWALL=1" & shift & goto parse )
if /I "%~1"=="--force"    ( set "FORCE=1" & shift & goto parse )
echo [uninstall] Unknown argument: %~1>&2
exit /b 2

:run
net session >nul 2>&1
if errorlevel 1 ( echo [uninstall] Please run this from an elevated (Administrator) prompt.>&2 & exit /b 1 )
if not defined NAME ( echo [uninstall] --name is required.>&2 & exit /b 2 )
if /I "%NAME%"=="SunshineService" if not defined FORCE (
  echo [uninstall] Refusing to remove the primary "SunshineService" without --force.>&2
  exit /b 2
)

echo [uninstall] Stopping and deleting "%NAME%"...
net stop "%NAME%" >nul 2>&1
sc delete "%NAME%"
if errorlevel 1 ( echo [uninstall] sc delete failed (does the service exist?).>&2 & exit /b 1 )

if defined FIREWALL (
  echo [uninstall] Removing firewall rules for "%NAME%"...
  netsh advfirewall firewall delete rule name="Sunshine %NAME% TCP" >nul 2>&1
  netsh advfirewall firewall delete rule name="Sunshine %NAME% UDP" >nul 2>&1
)
echo [uninstall] Done.
exit /b 0
