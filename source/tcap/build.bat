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

echo Building with configuration: %CONFIG%
echo Build directory: %BUILD_DIR%

cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH%
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded. Artifact is under %BUILD_DIR%\%CONFIG%\TCapture.exe.
exit /b 0
