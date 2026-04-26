@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%tcalendar\data\webview2\EBWebView"

if not exist "%ROOT%" (
    echo missing root: "%ROOT%"
    exit /b 1
)

tasklist /FI "IMAGENAME eq TCalendar.exe" | find /I "TCalendar.exe" >NUL
if not errorlevel 1 (
    echo TCalendar.exe is running. Close it before cleanup.
    exit /b 2
)

echo Resetting WebView2 user data for distribution:
echo   %ROOT%
echo.

rmdir /s /q "%ROOT%"
if exist "%ROOT%" (
    echo FAILED  could not remove "%ROOT%"
    exit /b 3
)

mkdir "%ROOT%"
if not exist "%ROOT%" (
    echo FAILED  could not recreate "%ROOT%"
    exit /b 4
)

echo DELETED all regenerable WebView2 user data.
echo RECREATED empty root:
echo   %ROOT%
echo.
echo Done.
exit /b 0
