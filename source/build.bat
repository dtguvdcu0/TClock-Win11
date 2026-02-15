@echo off
setlocal

set "SLN=%~dp0tc2ch.sln"

if not defined VSINSTALL (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\18\Community"
)

if defined VSINSTALL goto SKIP_VSWHERE
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto NO_VSWHERE

set "TMPFILE=%TEMP%\vswhere_install.txt"
"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath > "%TMPFILE%"
if errorlevel 1 goto VSWHERE_FAIL

set /p VSINSTALL=<"%TMPFILE%"
del /q "%TMPFILE%" >nul 2>&1

if not defined VSINSTALL goto NO_VS

:SKIP_VSWHERE

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto NO_VCVARS
call "%VCVARS%"

set "MSBUILD=%VSINSTALL%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" goto NO_MSBUILD

echo Using MSBuild: %MSBUILD%
echo Building: %SLN%

"%MSBUILD%" "%SLN%" /m /t:Build /p:Configuration=Release;Platform=x64
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" goto BUILD_FAIL

echo Build succeeded.
exit /b 0

:NO_VSWHERE
echo ERROR: vswhere not found: %VSWHERE%
exit /b 1

:VSWHERE_FAIL
echo ERROR: vswhere failed.
exit /b 1

:NO_VS
echo ERROR: Visual Studio installation not found.
exit /b 1

:NO_VCVARS
echo ERROR: vcvars64.bat not found: %VCVARS%
exit /b 1

:NO_MSBUILD
echo ERROR: MSBuild not found: %MSBUILD%
exit /b 1

:BUILD_FAIL
echo Build failed with error %ERR%.
exit /b %ERR%
