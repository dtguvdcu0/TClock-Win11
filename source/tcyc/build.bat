@echo off
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set BUILD_DIR=%~2
if "%BUILD_DIR%"=="" set BUILD_DIR=build

if not "%3"=="" (
    echo ERROR: build.bat no longer accepts a verify argument.
    echo Use build_verify.bat for build + runtime verification.
    exit /b 1
)

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
set "DEPLOY_DIR=%SCRIPT_DIR%..\x64\%CONFIG%"

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

set "TCYC_BUILD_OUT=%BUILD_DIR%\%CONFIG%"
set "TCYC_EXE=%TCYC_BUILD_OUT%\TCycle.exe"

if not exist "%TCYC_EXE%" (
    echo ERROR: TCycle artifact not found: %TCYC_EXE%
    popd
    exit /b 1
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
call :copy_with_retry "%TCYC_EXE%" "%DEPLOY_DIR%\TCycle.exe" "TCycle.exe"
if errorlevel 1 (
    popd
    exit /b 1
)

if exist "%SCRIPT_DIR%TCycle.ini" (
    copy /y "%SCRIPT_DIR%TCycle.ini" "%DEPLOY_DIR%\TCycle.ini" >nul
    call :assert_debug_off "%DEPLOY_DIR%\TCycle.ini"
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

if exist "%SCRIPT_DIR%lang" (
    if not exist "%DEPLOY_DIR%\tcyc\lang" mkdir "%DEPLOY_DIR%\tcyc\lang"
    xcopy /e /i /y "%SCRIPT_DIR%lang" "%DEPLOY_DIR%\tcyc\lang" >nul
)

echo Build succeeded. Deployed artifact: %DEPLOY_DIR%\TCycle.exe.
popd
exit /b 0

:assert_debug_off
set "INI_FILE=%~1"
if not exist "%INI_FILE%" exit /b 0

for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$v=(Get-Content -Path '%INI_FILE%' | Where-Object { $_ -match '^\s*ForceCmdlineReadFail\s*=' } | Select-Object -First 1); if($null -eq $v){'0'} else { ($v -split '=',2)[1].Trim() }"`) do set "FORCE_VAL=%%V"
if "%FORCE_VAL%"=="" set "FORCE_VAL=0"

if /i not "%FORCE_VAL%"=="0" (
    echo ERROR: Debug hook must be disabled for deploy. ForceCmdlineReadFail=%FORCE_VAL%
    exit /b 1
)
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
