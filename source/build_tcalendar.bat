@echo off
setlocal

set "TCAL_BUILD=%~dp0tcal\build.bat"
if not exist "%TCAL_BUILD%" (
    echo ERROR: TCalendar build script not found: %TCAL_BUILD%
    exit /b 1
)

echo Building TCalendar...
call "%TCAL_BUILD%" Release build-vs
if errorlevel 1 (
    echo ERROR: TCalendar build failed.
    exit /b 1
)

set "TCAL_DST=%~dp0x64\Release\TCalendar.exe"
if not exist "%TCAL_DST%" (
    echo ERROR: Deployed TCalendar artifact not found: %TCAL_DST%
    exit /b 1
)

echo TCalendar deployed: %TCAL_DST%
exit /b 0
