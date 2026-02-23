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

set "TCAL_EXE=%~dp0tcal\build-vs\Release\TCalendar.exe"
set "TCAL_DST=%~dp0x64\Release\TCalendar.exe"
if not exist "%TCAL_EXE%" (
    echo ERROR: TCalendar artifact not found: %TCAL_EXE%
    exit /b 1
)

copy /y "%TCAL_EXE%" "%TCAL_DST%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy TCalendar artifact to %TCAL_DST%
    exit /b 1
)

set "TCAL_BUILD_OUT=%~dp0tcal\build-vs\Release"
set "TCAL_DST_DIR=%~dp0x64\Release"

copy /y "%TCAL_BUILD_OUT%\WebView2Loader.dll" "%TCAL_DST_DIR%\WebView2Loader.dll" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy WebView2Loader.dll to %TCAL_DST_DIR%
    exit /b 1
)

if not exist "%TCAL_DST_DIR%\web" mkdir "%TCAL_DST_DIR%\web"
if not exist "%TCAL_DST_DIR%\data" mkdir "%TCAL_DST_DIR%\data"

xcopy /e /i /y "%TCAL_BUILD_OUT%\web" "%TCAL_DST_DIR%\web" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy web assets to %TCAL_DST_DIR%\web
    exit /b 1
)

echo TCalendar deployed: %TCAL_DST%
exit /b 0
