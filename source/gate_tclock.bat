@echo off
setlocal

echo [gate_tclock] quick gate (touched, skip build)
python source/scripts/migration_gate.py --mode touched --skip-build
if errorlevel 1 exit /b 1

echo [gate_tclock] full gate (touched)
python source/scripts/migration_gate.py --mode touched
if errorlevel 1 exit /b 1

echo [gate_tclock] passed.
exit /b 0
