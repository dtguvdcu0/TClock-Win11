# TCapture.ini Guide

`TCapture.ini` is used by all modes of `TCapture.exe`.
Load order:
1. executable directory
2. current working directory

## Profile sections
- Use `[default]` as base profile.
- Add extra sections such as `[work]`, `[display1]`.
- `--profile NAME` selects a section.

## Keys
- `output_dir` (default `.`)
- `format` (`png` or `jpg`)
- `compression` (PNG: `0-9`, JPG quality: `1-100`)
- `burst_fps` (`0` disables burst)
- `burst_seconds`
- `displays` (`all`, `active_display`, `active_window`, `1,3` etc.)
- `hotkey_capture` (used by `--agent`)
- `auto_capture` (agent timer)
- `auto_seconds`

## Example
```ini
[default]
output_dir=C:\captures
format=png
compression=6
burst_fps=0
burst_seconds=1
displays=all
hotkey_capture=Ctrl+Alt+S
auto_capture=false
auto_seconds=60
```

Note: language is not stored in `TCapture.ini`; when launched from TClock agent, language follows TClock (`EnglishMenu`).

## Override rule
CLI options override INI values for that run.
Supported overrides: `--display`, `--output`, `--format`, `--quality`, `--fps`, `--duration`, `--profile`.
