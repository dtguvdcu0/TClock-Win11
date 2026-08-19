# TCapture User Guide

Last updated: 2026-03-08  
TCapture is a Windows screenshot tool for one-click capture, hotkey capture, and automatic repeated capture.

## What You Can Do With TCapture

TCapture is useful when a normal screenshot tool is not enough and you want repeatable capture behavior.

Typical uses:
- capture all displays at once
- capture only specific displays
- keep different capture presets for work, gaming, or testing
- save screenshots with a hotkey while the tool stays in the background
- take screenshots automatically every few seconds or minutes

## Main Files

- `TCapture.exe`: the application
- `TCapture.ini`: profile-based settings file

`TCapture.ini` is loaded from:
1. the executable directory
2. the current working directory

## How It Works

TCapture supports profiles.
Each profile can define:
- output folder
- image format
- PNG or JPG compression settings
- target displays
- hotkey
- automatic capture interval

## Quick Start

### Capture Once

```bat
TCapture.exe --list
TCapture.exe -a -o C:\captures
```

### Run in the Background

```bat
TCapture.exe --agent
```

### Open Settings

```bat
TCapture.exe --settings
```

## Minimal Profile Example

```ini
[default]
output_dir=C:\captures
format=png
compression_png=6
compression_jpg=96
burst_fps=0
burst_seconds=1
displays=all
hotkey_capture=Ctrl+Alt+S
auto_capture=false
auto_seconds=60
```

## Common Ways To Use It

### Save screenshots with a hotkey

Set `hotkey_capture` and run `TCapture.exe --agent`.

### Capture only one monitor or selected monitors

Use `displays=1`, `displays=1,3`, `displays=active_display`, or `displays=active_window`.

### Keep multiple presets

Create sections such as `[default]`, `[work]`, and `[display1]`, then use `--profile NAME`.

### Capture automatically

Set `auto_capture=true` and choose `auto_seconds`.

## Common Options

- `--list`: show available displays
- `--profile NAME`: use a named INI profile
- `--output PATH`: override output folder
- `--format png|jpg`: override output format
- `--fps N`: burst capture frames per second
- `--duration N`: burst duration in seconds

## Tips

- Start with the `[default]` profile before creating additional profiles.
- Use PNG unless you specifically need smaller JPEG files.
- Use the background mode when you want hotkey or timer capture without reopening the app each time.
- Keep output folders on a writable local path.

## Related Docs

- Core implementation docs live under `source/tcap/docs/`
