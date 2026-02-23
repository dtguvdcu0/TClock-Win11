@echo off
setlocal

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=%BUILD_TARGET%"
if /I "%TARGET%"=="tcalendar" goto BUILD_TCAL
if /I "%TARGET%"=="tclock" goto BUILD_TCLOCK
if "%TARGET%"=="" goto BUILD_TCLOCK

echo Usage:
echo   build.bat tclock
echo   build.bat tcalendar
exit /b 1

:BUILD_TCLOCK
call "%~dp0build_tclock.bat"
exit /b %ERRORLEVEL%

:BUILD_TCAL
call "%~dp0build_tcalendar.bat"
exit /b %ERRORLEVEL%
