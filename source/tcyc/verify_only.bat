@echo off
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set "SCRIPT_DIR=%~dp0"
set "DEPLOY_DIR=%SCRIPT_DIR%..\x64\%CONFIG%"

if not exist "%DEPLOY_DIR%\TCycle.exe" (
    echo ERROR: TCycle.exe not found in %DEPLOY_DIR%
    echo Build first with: build.bat %CONFIG%
    exit /b 1
)

echo Running TCycle runtime verification suite only...
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

echo Verification succeeded.
exit /b 0

