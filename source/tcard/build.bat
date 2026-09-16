@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=x64"
set "TEST_ARG="
set "DEPLOY_ARG="
if /I "%~3"=="-Test" set "TEST_ARG=-Test"
if /I "%~3"=="-Deploy" set "DEPLOY_ARG=-Deploy"
if /I "%~4"=="-Test" set "TEST_ARG=-Test"
if /I "%~4"=="-Deploy" set "DEPLOY_ARG=-Deploy"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build\build.ps1" -Configuration "%CONFIG%" -Platform "%PLATFORM%" %TEST_ARG% %DEPLOY_ARG%
exit /b %errorlevel%
