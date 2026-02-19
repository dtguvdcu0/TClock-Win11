# CustomVars Specification

Last updated: 2026-02-19  
Guide for configuring custom variable display using `[CustomVars]` in `tclock-win11.ini`.

Relative paths (for example `custom\weather_tokyo.json`) are resolved from the folder that contains the active INI file.

## Recommended Approach (Text First)

For normal use, start with `line` mode (plain text first-line read).  
`json` mode is more flexible, but intended for advanced users.

- Default usage: `CustomNMode=line`
- Advanced usage: `CustomNMode=json`

## Minimal Working Example

```ini
[CustomVars]
Custom1Path=custom\custom_test.txt
Custom1Mode=line
Custom1FailValue=N/A
Custom1RefreshSec=10
Custom1MaxChars=20
```

`CUSTOM1` will display the first line of `custom\custom_test.txt`.

## Using CUSTOM Variables in Format Strings

```ini
[Format]
Custom=1
CustomFormat=yyyy/mm/dd(ddd) hh:nn:ss CUSTOM1
```

## Key Summary

`N` means `1..32` (for example: `Custom4Path`).

- `CustomNPath=<path>`: source file path
- `CustomNMode=line|json`: read mode
- `CustomNFailValue=<text>`: fallback text on failure
- `CustomNRefreshSec=<int>`: refresh interval (seconds)
- `CustomNMaxChars=<int>`: max output length

Global keys under `[CustomVars]`:
- `PreloadOnStartup=<0|1>`
- `RefreshSec=<int>`
- `MaxChars=<int>`

## JSON Mode (Advanced)

Use JSON mode only when you need structured extraction or templated output.

Main keys:
- `CustomNJsonPath`
- `CustomNJsonType`
- `CustomNJsonDefault`
- `CustomNJsonValue`

Example:

```ini
[CustomVars]
Custom4Path=custom\weather_tokyo.json
Custom4Mode=json
Custom4JsonValue=Tokyo {$.weather.desc} {$.weather.temp_c}C {$.weather.humidity_pct}% Wind{$.weather.wind_mps}
Custom4JsonType=auto
Custom4FailValue=N/A
Custom4RefreshSec=60
Custom4MaxChars=20
```

## Troubleshooting Checklist

- Does `CustomNPath` point to an existing file?
- Did you validate behavior in `line` mode first?
- Is `CustomNFailValue` readable and intentional?
- Are refresh values reasonable (`RefreshSec` / `CustomNRefreshSec`)?
- If using JSON mode, are JSON paths and template placeholders correct?
