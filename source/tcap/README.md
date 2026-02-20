# TCapture (Windows)

TCapture is a Windows multi-monitor screenshot tool.
A single executable supports three modes:
- capture mode (default CLI behavior)
- `--agent` (background hotkey/timer agent)
- `--settings` (open settings window)

## Component
- `TCapture.exe`: unified binary for capture + background agent + settings UI.

## Build (Windows, MSVC)
```bat
build.bat Release
```

Or manual CMake:
```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

## Quick usage
Capture:
```bat
TCapture.exe --list
TCapture.exe -a -o C:\tmp
TCapture.exe -d 1,3 -f jpg -q 90
TCapture.exe --fps 10 --duration 3
TCapture.exe --profile default
```

Capture options: `--list`, `--all`, `--display`, `--output`, `--format`, `--quality`, `--fps`, `--duration`, `--profile`.
Background agent:
```bat
TCapture.exe --agent
```

Language override (used by TClock integration):
```bat
TCapture.exe --agent --lang ja
TCapture.exe --agent --lang en
```

Open settings UI (from menu or command):
```bat
TCapture.exe --settings
```

## Configuration
`TCapture.ini` is loaded from:
1. executable directory (preferred)
2. current working directory (fallback)

Profiles use INI sections like `[default]`, `[work]`, `[display1]`.
See `SETTINGS.md` for keys.

## TClock integration
`TClock-Win11` reads these INI keys under `[TCapture]`:
- `Enable=0|1`
- `Path=TCapture.exe`

`Path` accepts relative or absolute paths. Relative paths are resolved from TClock executable directory.
