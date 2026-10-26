@echo off
setlocal

if "%~1"=="" goto USAGE
if not "%~3"=="" goto USAGE
set "PROJECT=%~f1"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Release"
if /I "%CONFIG%"=="Release" goto CHECK_PROJECT
if /I "%CONFIG%"=="Debug" goto CHECK_PROJECT
goto USAGE

:CHECK_PROJECT
if not exist "%PROJECT%" (
    echo ERROR: Plugin project not found: "%PROJECT%"
    exit /b 1
)
set "PLUGIN_MSBUILD="
if defined VSINSTALL if exist "%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe" set "PLUGIN_MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" set "PLUGIN_MSBUILD=%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe"
if not defined PLUGIN_MSBUILD call :FIND_MSBUILD
if not defined PLUGIN_MSBUILD (
    echo ERROR: Visual Studio with MSBuild and C++ build tools was not found.
    exit /b 1
)
"%PLUGIN_MSBUILD%" "%PROJECT%" /nologo /m /t:Build "/p:Configuration=%CONFIG%;Platform=x64"
exit /b %ERRORLEVEL%

:FIND_MSBUILD
set "PLUGIN_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%PLUGIN_VSWHERE%" exit /b 1
set "PLUGIN_VSROOT="
for /f "usebackq delims=" %%I in (`"%PLUGIN_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PLUGIN_VSROOT=%%I"
if not defined PLUGIN_VSROOT exit /b 1
if exist "%PLUGIN_VSROOT%\MSBuild\Current\Bin\MSBuild.exe" set "PLUGIN_MSBUILD=%PLUGIN_VSROOT%\MSBuild\Current\Bin\MSBuild.exe"
exit /b 0

:USAGE
echo Usage: build_plugin.bat project.vcxproj [Release^|Debug]
exit /b 2
