@echo off
setlocal

set "TCAL_BUILD=%~dp0tcal\build.bat"
if not exist "%TCAL_BUILD%" (
    echo ERROR: TCalendar build script not found: %TCAL_BUILD%
    exit /b 1
)

echo Building TCalendar...
call "%TCAL_BUILD%" Release build-vs
if errorlevel 1 (
    echo ERROR: TCalendar build failed.
    exit /b 1
)

set "TCAL_EXE=%~dp0tcal\build-vs\Release\TCalendar.exe"
set "TCAL_DST=%~dp0x64\Release\TCalendar.exe"
if not exist "%TCAL_EXE%" (
    echo ERROR: TCalendar artifact not found: %TCAL_EXE%
    exit /b 1
)

set "_copy_ok=0"
for /l %%I in (1,1,3) do (
    copy /y "%TCAL_EXE%" "%TCAL_DST%" >nul
    if not errorlevel 1 (
        set "_copy_ok=1"
        goto :copy_done
    )

    if %%I==1 (
        tasklist /fi "imagename eq TCalendar.exe" | find /i "TCalendar.exe" >nul
        if not errorlevel 1 (
            echo INFO: TCalendar.exe is running. Close it and rerun if copy keeps failing.
        )
    )

    if %%I lss 3 (
        echo WARN: Copy retry %%I/3 failed. Retrying...
        timeout /t 1 /nobreak >nul
    )
)

:copy_done
if not "%_copy_ok%"=="1" (
    echo ERROR: Failed to copy TCalendar artifact to %TCAL_DST%
    exit /b 1
)

set "TCAL_BUILD_OUT=%~dp0tcal\build-vs\Release"
set "TCAL_DST_DIR=%~dp0x64\Release"

copy /y "%TCAL_BUILD_OUT%\WebView2Loader.dll" "%TCAL_DST_DIR%\WebView2Loader.dll" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy WebView2Loader.dll to %TCAL_DST_DIR%
    exit /b 1
)

if not exist "%TCAL_DST_DIR%\tcalendar\template" mkdir "%TCAL_DST_DIR%\tcalendar\template"
if not exist "%TCAL_DST_DIR%\tcalendar\data" mkdir "%TCAL_DST_DIR%\tcalendar\data"

xcopy /e /i /y "%TCAL_BUILD_OUT%\tcalendar\template" "%TCAL_DST_DIR%\tcalendar\template" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy template assets to %TCAL_DST_DIR%\tcalendar\template
    exit /b 1
)

echo TCalendar deployed: %TCAL_DST%
exit /b 0
