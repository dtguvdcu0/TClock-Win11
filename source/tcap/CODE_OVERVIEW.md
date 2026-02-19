# TCapture Code Overview

## Entry model
Single executable:
- `wWinMain` in `gui_main.cpp` dispatches by arguments.
- Capture path calls `RunCaptureMain(...)` in `screenshot.cpp`.

Launch modes:
- default (or `--capture`): run capture once.
- `--agent`: run background agent loop (hotkey/timer/tray policy).
- `--settings`: show settings window (or signal existing agent instance).
- `--lang ja|en`: optional hint for agent/settings mode default language (used by TClock launcher).

## Settings flow
- `loadSettingsProfiles()` reads `TCapture.ini`.
- Sections map to capture profiles.
- Save path uses executable directory preference.

## Capture flow
- Monitor enum: `EnumDisplayMonitors`.
- Screen copy: GDI `BitBlt` + `GetDIBits`.
- Encode/write: WIC PNG/JPEG.

## Agent/UI flow
- Single-instance mutex prevents duplicate agent/settings process.
- `--settings` posts a show message to existing instance when already running.
- Hotkeys and auto-capture timers are reloaded from profile settings.

## Build
- `CMakeLists.txt` builds one target: `TCapture`.

## TClock launch contract
- TClock startup gate uses `[ETC] TCaptureEnable`.
- Executable path comes from `[ETC] TCapturePath` (default `TCapture.exe`, relative to TClock dir).
