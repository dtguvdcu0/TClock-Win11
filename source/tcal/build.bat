@echo off
setlocal

:: Windows build script for MSVC with CMake.
:: Usage: build.bat [Release|Debug] [build_dir]

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set BUILD_DIR=%2
if "%BUILD_DIR%"=="" set BUILD_DIR=build-vs

set GENERATOR=
if not "%GENERATOR_OVERRIDE%"=="" set GENERATOR=%GENERATOR_OVERRIDE%
if "%GENERATOR%"=="" (
    for /f "delims=" %%G in ('cmake --help ^| findstr /r /c:"Visual Studio 18 2026"') do set GENERATOR=Visual Studio 18 2026
)
if "%GENERATOR%"=="" (
    for /f "delims=" %%G in ('cmake --help ^| findstr /r /c:"Visual Studio 17 2022"') do set GENERATOR=Visual Studio 17 2022
)
if "%GENERATOR%"=="" (
    echo No supported Visual Studio generator found. Set GENERATOR_OVERRIDE to override.
    exit /b 1
)

set ARCH=x64
set "SCRIPT_DIR=%~dp0"
set "WV2_ROOT=%SCRIPT_DIR%third_party\webview2"
set "DEPLOY_DIR=%SCRIPT_DIR%..\x64\%CONFIG%"

if not exist "%WV2_ROOT%\include\WebView2.h" (
    echo ERROR: Bundled WebView2 header missing: %WV2_ROOT%\include\WebView2.h
    exit /b 1
)
if not exist "%WV2_ROOT%\lib\x64\WebView2LoaderStatic.lib" (
    echo ERROR: Bundled WebView2 static lib missing: %WV2_ROOT%\lib\x64\WebView2LoaderStatic.lib
    exit /b 1
)
if not exist "%WV2_ROOT%\bin\x64\WebView2Loader.dll" (
    echo ERROR: Bundled WebView2 loader DLL missing: %WV2_ROOT%\bin\x64\WebView2Loader.dll
    exit /b 1
)

echo Building TCalendar with configuration: %CONFIG%
echo Build directory: %BUILD_DIR%

pushd "%SCRIPT_DIR%"
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH%
if errorlevel 1 (
    echo CMake configuration failed.
    popd
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo Build failed.
    popd
    exit /b 1
)

set "TCAL_BUILD_OUT=%BUILD_DIR%\%CONFIG%"
set "TCAL_EXE=%TCAL_BUILD_OUT%\TCalendar.exe"

if not exist "%TCAL_EXE%" (
    echo ERROR: TCalendar artifact not found: %TCAL_EXE%
    popd
    exit /b 1
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
call :copy_with_retry "%TCAL_EXE%" "%DEPLOY_DIR%\TCalendar.exe" "TCalendar.exe"
if errorlevel 1 (
    popd
    exit /b 1
)

call :copy_with_retry "%TCAL_BUILD_OUT%\WebView2Loader.dll" "%DEPLOY_DIR%\WebView2Loader.dll" "TCalendar.exe"
if errorlevel 1 (
    popd
    exit /b 1
)

if not exist "%DEPLOY_DIR%\tcalendar\template" mkdir "%DEPLOY_DIR%\tcalendar\template"
if not exist "%DEPLOY_DIR%\tcalendar\data" mkdir "%DEPLOY_DIR%\tcalendar\data"

xcopy /e /i /y "%TCAL_BUILD_OUT%\tcalendar\template" "%DEPLOY_DIR%\tcalendar\template" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy template assets to %DEPLOY_DIR%\tcalendar\template
    popd
    exit /b 1
)


echo Build succeeded. Deployed artifact: %DEPLOY_DIR%\TCalendar.exe.
popd
exit /b 0

:copy_with_retry
set "COPY_SRC=%~1"
set "COPY_DST=%~2"
set "LOCK_PROC=%~3"

copy /y "%COPY_SRC%" "%COPY_DST%" >nul
if not errorlevel 1 exit /b 0

echo WARN: Initial copy failed: %COPY_DST%
tasklist /fi "IMAGENAME eq %LOCK_PROC%" 2>nul | find /i "%LOCK_PROC%" >nul
if errorlevel 1 (
    echo ERROR: Copy failed and locking process was not detected: %LOCK_PROC%
    exit /b 1
)

echo INFO: Stopping process to unlock file: %LOCK_PROC%
taskkill /f /im "%LOCK_PROC%" >nul 2>&1
timeout /t 1 /nobreak >nul

copy /y "%COPY_SRC%" "%COPY_DST%" >nul
if errorlevel 1 (
    echo ERROR: Copy failed after stopping process: %COPY_DST%
    exit /b 1
)

echo INFO: Copy succeeded after stopping process: %LOCK_PROC%
exit /b 0
