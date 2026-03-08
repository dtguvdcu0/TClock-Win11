# TCapture Configuration

## Primary File
- `TCapture.ini`

## Load Order
1. Executable directory
2. Current working directory

The executable directory is the preferred and stable location.

## Profile Model
- `[default]` acts as the base profile.
- Additional sections such as `[work]` and `[display1]` define alternate capture profiles.
- `--profile NAME` selects the active profile for a run.
- The current settings writer saves normalized profile values, not every accepted compatibility alias.

## Canonical Saved Keys
These keys are the normalized keys written by the current implementation.

- `output_dir`: output directory, empty is normalized to `.`
- `format`: `png` or `jpg`
- `compression_png`: PNG compression level
- `compression_jpg`: JPEG quality
- `burst_fps`: burst capture FPS, `0` disables burst
- `burst_seconds`: burst duration in seconds
- `auto_capture`: `true` or `false`
- `auto_seconds`: timer interval for agent mode
- `displays`: display selector text
- `hotkey_capture`: capture hotkey

## Accepted Compatibility Keys
These aliases are accepted while reading the INI.

### Output path
- `output_dir`
- `output`

### Format
- `format`
- accepted values: `png`, `jpg`, `jpeg`

### Compression and quality
- generic compatibility keys:
  - `compression`
  - `quality`
- PNG-specific keys:
  - `compression_png`
  - `png_compression`
- JPEG-specific keys:
  - `compression_jpg`
  - `jpg_compression`
  - `jpg_quality`

### Burst capture
- FPS keys:
  - `burst_fps`
  - `fps`
- duration keys:
  - `burst_seconds`
  - `burst_secs`
  - `seconds`
  - `duration`

### Auto capture
- enable keys:
  - `auto_capture`
  - `auto`
  - `auto_enable`
- interval keys:
  - `auto_seconds`
  - `auto_interval`
  - `interval`

### Display selection
- `displays`
- `display`

### Hotkey
- `hotkey_capture`
- `capture_hotkey`
- `hotkey`

### Language
- `language`
- `lang`

## Value Rules

### `format`
- `png` and `jpg` are the normalized values.
- `jpeg` is accepted and normalized to `jpg`.
- Invalid values fall back to the normalizer and current defaults.

### `compression_png`
- Intended range: `0..9`
- Lower values are faster; higher values are smaller.

### `compression_jpg`
- Intended range: `1..100`
- Higher values mean better quality and larger files.

### `compression`
- Compatibility key.
- Sets the generic compression value.
- Also seeds PNG compression and, when the format is JPEG and no JPG-specific key has been set yet, seeds JPEG quality.

### `displays`
Accepted selectors include:
- `all`
- `0`
- `active_display`
- `active_window`
- comma-separated monitor numbers such as `1,3`

### `auto_capture`
Accepted true values:
- `1`
- `true`
- `yes`
- `on`

Any other value is treated as false by the current parser.

## Integration Section
`TCapture.ini` may also contain:

```ini
[Integration]
TClockIniPath=..\tclock-win11.ini
```

This path is used when TCapture resolves the TClock INI for integration-related behavior.

## CLI Override Rule
CLI options override profile values for the current run.

Supported CLI overrides:
- `--display`
- `--output`
- `--format`
- `--quality`
- `--fps`
- `--duration`
- `--profile`

## Example
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

[Integration]
TClockIniPath=..\tclock-win11.ini
```
