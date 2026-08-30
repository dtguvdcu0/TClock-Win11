# CustomVars User Guide

Last updated: 2026-08-30

CustomVars reads text or JSON from external files and makes the result available as `CUSTOM1` through `CUSTOM32` in a TClock format.

## Quick start

1. Create a text file next to `tclock-win11.ini`, for example `custom\status.txt`.
2. Add one CustomVars entry:

```ini
[CustomVars]
Custom1Mode=line
Custom1Path=custom\status.txt
Custom1FailValue=N/A
Custom1RefreshSec=10
Custom1MaxChars=20
```

3. Add `CUSTOM1` to the clock format:

```ini
[Format]
Custom=1
CustomFormat=yyyy/mm/dd(ddd) hh:nn:ss CUSTOM1
```

If the file contains `Online` on its first line, the clock displays `Online` in place of `CUSTOM1`.

## How the settings work

Each variable has an index from `1` through `32`. The index is part of every key name:

```ini
CustomNMode=...
CustomNPath=...
```

Replace `N` with the variable number, such as `Custom4Mode`.

Relative paths are resolved relative to the directory containing `tclock-win11.ini`. Absolute Windows paths are also accepted.

The source file must be non-empty and no larger than 64 KiB. UTF-8, UTF-16, and legacy Shift-JIS text input are supported.

## Read modes

### Line mode

Use `CustomNMode=line` to read only the first line of a text file. This is the simplest mode and is suitable for status values written by scripts.

```ini
[CustomVars]
Custom1Mode=line
Custom1Path=custom\status.txt
Custom1RefreshSec=10
Custom1MaxChars=20
Custom1FailValue=offline
Custom1Whitespace=trim_edges
```

### JSON mode

Use `CustomNMode=json` to read values from a JSON file. The output is a template in which each `{...}` expression is a JSON path:

```ini
[CustomVars]
Custom2Mode=json
Custom2Path=custom\weather.json
Custom2JsonValue=Tokyo {$.weather.desc} {$.weather.temp_c}C
Custom2RefreshSec=60
Custom2MaxChars=48
Custom2FailValue=N/A
```

JSON paths use dot-separated object names and zero-based array indexes, for example `$.weather.temp_c` or `$.items.0.name`.

The following JSON options are available:

- `CustomNJsonStringify=0|1`: allow JSON objects and arrays to be emitted as JSON text.
- `CustomNJsonNullAsEmpty=0|1`: emit an empty string for JSON `null`; otherwise use the failure value.

Use `{{` and `}}` when a literal brace is needed in the template. If a JSON path is missing or has an incompatible value, the variable uses `CustomNFailValue`.

JSON refresh intervals below 5 seconds are raised to 5 seconds.

## Common item keys

These keys are available for each `CustomN` entry:

| Key | Meaning |
| --- | --- |
| `CustomNPath` | Source file path. Required for a value to be read. |
| `CustomNMode` | `line` or `json`; defaults to `line`. |
| `CustomNRefreshSec` | File refresh interval, from 1 to 86400 seconds. |
| `CustomNMaxChars` | Maximum emitted length, from 1 to 4096 characters. |
| `CustomNFailValue` | Text used when the file cannot be read or extraction fails. |
| `CustomNWhitespace` | `trim_edges` or `keep`. |

`trim_edges` removes leading and trailing spaces, tabs, and full-width spaces. `keep` preserves them.

## Global defaults

These keys are placed directly under `[CustomVars]` and apply when the corresponding per-item key is absent:

```ini
[CustomVars]
RefreshSec=60
MaxChars=20
FailValue=N/A
Whitespace=trim_edges
PreloadOnStartup=1
```

- `RefreshSec`: default file refresh interval, from 1 to 86400 seconds.
- `MaxChars`: default maximum output length, from 1 to 4096 characters.
- `FailValue`: default failure text.
- `Whitespace`: default `trim_edges` or `keep` behavior.
- `PreloadOnStartup=0|1`: read configured files during startup before the first normal refresh.

Per-item values override the global defaults.

## Optional script refresh

CustomVars can run a command to update a source file. This is useful for weather, exchange-rate, or other externally generated data.

```ini
[CustomVars]
Custom3Mode=json
Custom3Path=custom\rates.json
Custom3JsonValue=USD {$.usd_jpy}
Custom3ExecEnable=1
Custom3ExecType=shell
Custom3ExecStart=both
Custom3ExecIntervalSec=600
Custom3ExecCommand=custom\fetch_rates.bat
Custom3ExecCwd=custom
```

Script keys:

- `CustomNExecEnable=0|1`: enable or disable script execution.
- `CustomNExecType=command|shell`: run through `cmd.exe` or PowerShell.
- `CustomNExecStart=startup|interval|both|time`: when to run the command.
- `CustomNExecIntervalSec`: interval for `interval` or `both`, from 1 to 86400 seconds.
- `CustomNExecTime=HH:MM`: daily run time for `time`.
- `CustomNExecCommand`: command text to execute.
- `CustomNExecCwd`: optional working directory; relative paths use the INI directory.

Use `both` to run once at startup and then at the interval. Script execution is hidden and does not wait for the process to finish before continuing.

## Complete JSON example

```ini
[CustomVars]
PreloadOnStartup=1
RefreshSec=60
MaxChars=48
FailValue=N/A
Whitespace=trim_edges

Custom4Mode=json
Custom4Path=custom\weather_tokyo.json
Custom4JsonValue=Tokyo {$.weather.desc} {$.weather.temp_c}C humidity {$.weather.humidity_pct}%
Custom4RefreshSec=60
Custom4MaxChars=48
Custom4FailValue=N/A
Custom4JsonStringify=0
Custom4JsonNullAsEmpty=0
Custom4ExecEnable=1
Custom4ExecType=shell
Custom4ExecStart=both
Custom4ExecIntervalSec=600
Custom4ExecCommand=custom\fetch_weather.bat
```

## Troubleshooting

- If `CUSTOMn` shows the failure value, check the path, file existence, file size, and JSON path.
- If only part of a text file is displayed, check `CustomNMaxChars`.
- If spaces disappear, set `CustomNWhitespace=keep`.
- If JSON output is not shown, verify that `CustomNJsonValue` contains valid `{JSON path}` expressions.
- If a script does not update the file, check `CustomNExecEnable`, `CustomNExecCommand`, `CustomNExecType`, and `CustomNExecCwd`.
