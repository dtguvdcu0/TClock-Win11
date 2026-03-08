# TCapture Overview

## Purpose
TCapture is a single-binary Windows screenshot tool that combines one-shot capture, resident agent behavior, and a settings UI.

## Main Modules
- `gui_main.cpp`: process entry and argument-based mode dispatch
- `screenshot.cpp`: monitor capture flow, image encode path, and output writing
- `settings.cpp` / `settings.h`: settings model and INI-backed profile handling
- `resources.rc` / `resource.h`: native resources and UI assets

## Launch Modes
- default or `--capture`: execute one capture run
- `--agent`: run the resident agent loop
- `--settings`: open the settings window
- `--lang ja|en`: optional language hint for agent/settings mode

## Capture Pipeline
1. Enumerate monitors with `EnumDisplayMonitors`.
2. Copy pixels with GDI (`BitBlt`, `GetDIBits`).
3. Encode output through WIC for PNG or JPEG.
4. Write images to the configured output directory.

## Resident Behavior
- A single-instance mutex blocks duplicate agent/settings processes.
- Existing agent instances receive a show-settings signal for `--settings`.
- Hotkey and timer configuration are reloaded from profile settings.

## Output Model
- Output location comes from the active profile unless overridden by CLI.
- Relative paths are interpreted from the active working context, with the executable directory preferred by configuration loading.

## TClock Integration
- TClock reads `[TCapture] Enable` and `[TCapture] Path` from its own INI.
- `Path` may be relative or absolute.
- Relative paths are resolved from the TClock executable directory.
- TClock may pass `--lang ja` or `--lang en` to the same binary.

## Build Boundary
- `CMakeLists.txt` builds one target: `TCapture`.
- Runtime behavior is selected by arguments, not by separate binaries.
