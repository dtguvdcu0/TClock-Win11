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
- `capture_action`: `save` (default), `open`, or `save_open`
- `open_app`: Windows Shell handler identity in UTF-8; empty uses the Windows default image app
- `open_executable`: optional executable path in UTF-8; takes priority over `open_app`
- `format`: `png` or `jpg`
- `compression_png`: PNG compression level
- `compression_jpg`: JPEG quality
- `burst_fps`: burst capture FPS, `0` disables burst
- `burst_seconds`: burst duration in seconds
- `auto_capture`: `true` or `false`
- `auto_seconds`: timer interval for agent mode
- `displays`: display selector text

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

### `capture_action` and `open_app`
- Missing or unrecognized `capture_action` values use the existing save behavior.
- `save_open` saves in `output_dir`, then opens that same file for viewing or editing.
- `open` writes to `%TEMP%\TCapture`, then opens each image using the selected Shell handler.
- The GUI lists recommended apps registered for the selected PNG or JPEG format, including packaged apps.
- Choose `Choose executable...` in the app list to select an unregistered desktop tool.
- A custom executable receives one quoted image path argument and runs with its own directory as the working directory. A new process is requested for each image; tools may manage their own windows/tabs.
- App selection is per profile. Changing format refreshes the list without silently replacing the saved choice.
- An unavailable app reports an error and retains the image for recovery.
- Temporary images remain available after launch for asynchronous loading and editing; they can be removed later through Windows temporary-file cleanup.
- Open mode disables burst and automatic capture. Switching back to save leaves those features off until explicitly enabled.
- All requested displays are captured before any application is opened.

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

This reserved section is not a capture profile. The settings writer preserves its
contents, including the destination path, when rewriting profiles. Relative paths
are resolved against the TCapture.ini directory.

The GUI saves shortcut assignments in the resolved TClock INI under `[TCapture]`,
using `HotkeyCount`, `HotkeyNProfile`, and `HotkeyNValue`. These mappings override
legacy per-profile hotkey keys when loading. The normalized TCapture.ini writer
does not write `hotkey_capture`; it remains a compatibility input.

If no integration path is configured, the resolver uses `tclock-win11.ini` beside
the executable when present, otherwise in its parent directory. Preserving an
explicit path is required to reload assignments from the same destination.

TClock owns shortcut registration. The current GUI save does not notify TClock to
re-register changed assignments; saving the file and activating the new shortcut
are separate operations.

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
