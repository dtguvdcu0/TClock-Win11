@echo off
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set BUILD_DIR=%~2
if "%BUILD_DIR%"=="" set BUILD_DIR=build

set "SCRIPT_DIR=%~dp0"
set "DEPLOY_DIR=%SCRIPT_DIR%..\x64\%CONFIG%"

echo NOTE: build/verify commands are serial-only. Do not run build.bat, build_verify.bat, verify_only.bat in parallel.
call "%SCRIPT_DIR%build.bat" "%CONFIG%" "%BUILD_DIR%"
if errorlevel 1 (
    exit /b 1
)

echo Running TCycle runtime verification suite...
set "VERIFY_ARGS=-RuntimeDir ""%DEPLOY_DIR%"""
if /i "%TCYCLE_VERIFY_INCLUDE_APPLY%"=="1" (
    echo INFO: Including optional apply-no-launch verification.
    set "VERIFY_ARGS=%VERIFY_ARGS% -IncludeApplyNoLaunch"
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\verify_all.ps1" %VERIFY_ARGS%
if errorlevel 1 (
    echo ERROR: verify_all.ps1 failed.
    exit /b 1
)

echo Build + verification succeeded.
exit /b 0

