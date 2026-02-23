@echo off
setlocal

echo [gate_tcalendar] quick gate (touched, skip build)
python source/scripts/migration_gate.py --mode touched --skip-build
if errorlevel 1 exit /b 1

echo [gate_tcalendar] build TCalendar
call source\build_tcalendar.bat
if errorlevel 1 exit /b 1

echo [gate_tcalendar] smoke (normal)
source\x64\Release\TCalendar.exe --smoke
if errorlevel 1 exit /b 1

echo [gate_tcalendar] smoke (storage-error strict)
source\x64\Release\TCalendar.exe --smoke-storage-error-strict
if errorlevel 1 exit /b 1

echo [gate_tcalendar] full gate (touched, target=tcalendar)
set "BUILD_TARGET=tcalendar"
python source/scripts/migration_gate.py --mode touched
set "BUILD_TARGET="
if errorlevel 1 exit /b 1

echo [gate_tcalendar] passed.
exit /b 0
