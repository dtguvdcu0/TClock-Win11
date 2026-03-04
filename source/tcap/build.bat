@echo off
setlocal

:: Windows build script for MSVC with CMake.
:: Usage: build.bat [Release|Debug] [build_dir]

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set BUILD_DIR=%2
if "%BUILD_DIR%"=="" set BUILD_DIR=build

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
set "DEPLOY_DIR=%SCRIPT_DIR%..\x64\%CONFIG%\plugins"

echo Building with configuration: %CONFIG%
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

set "TCAP_BUILD_OUT=%BUILD_DIR%\%CONFIG%"
set "TCAP_EXE=%TCAP_BUILD_OUT%\TCapture.exe"

if not exist "%TCAP_EXE%" (
    echo ERROR: TCapture artifact not found: %TCAP_EXE%
    popd
    exit /b 1
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
call :copy_with_retry "%TCAP_EXE%" "%DEPLOY_DIR%\TCapture.exe" "TCapture.exe"
if errorlevel 1 (
    popd
    exit /b 1
)

if exist "%SCRIPT_DIR%TCapture.ini" (
    copy /y "%SCRIPT_DIR%TCapture.ini" "%DEPLOY_DIR%\TCapture.ini" >nul
)
if exist "%SCRIPT_DIR%lang" (
    if not exist "%DEPLOY_DIR%\tcapture" mkdir "%DEPLOY_DIR%\tcapture"
    if not exist "%DEPLOY_DIR%\tcapture\lang" mkdir "%DEPLOY_DIR%\tcapture\lang"
    xcopy /e /i /y "%SCRIPT_DIR%lang" "%DEPLOY_DIR%\tcapture\lang" >nul
)


echo Build succeeded. Deployed artifact: %DEPLOY_DIR%\TCapture.exe.
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
