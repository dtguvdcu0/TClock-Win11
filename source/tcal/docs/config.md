# TCalendar Configuration

## Primary Files
- Runtime INI: `tcalendar.ini`
- Integration INI: `tclock-win11.ini` for `[TCalendar] Alart`

## Load Rules
- `tcalendar.ini` is resolved beside `TCalendar.exe`.
- The application creates missing keys with default values during startup.
- Most relative paths are resolved from the executable directory.
- Smoke mode still reads `tcalendar.ini`, but forces the storage database path to the smoke database.

## Section
All documented runtime keys are under `[TCalendar]`.

## Canonical Keys

### Template and storage
- `Skin`: skin name used to derive the default template path when `TemplateDefault` is empty, default `default`
- `TemplateDefault`: optional explicit template path
- `TemplateUser`: user template path, default `tcalendar\template\user\index.html`
- `StorageDbPath`: runtime database path, default `tcalendar\data\tasks.db`

### WebView and navigation
- `BlockExternalNavigation`: `0` or `1`, default `1`
- `EnableWebView2Bootstrap`: `0` or `1`, default `1`
- `StartupLogEnabled`: `0` or `1`, default `0`. When enabled, startup timing marks are appended to `tcalendar\logs\startup.log`.

### Window state
- `WindowWidth`: window width, default `960`, valid load range `320..3840`
- `WindowHeight`: window height, default `640`, valid load range `240..2160`

### Default view behavior
- `DefaultViewMode`: `list` or `timeline`, default `list`
- `DefaultRangePresetDays`: allowed values `1`, `7`, `14`, `30`, default `1`
- `DefaultCustomRangeDays`: clamped to `1..365`, default `7`
- `DefaultUseCustomRange`: `0` or `1`, default `0`

### UI layout
- `UiFontFamily`: default `Segoe UI`
- `UiBaseFontSize`: clamped to `9..28`, default `14`
- `UiCalendarDateFontSize`: clamped to `9..28`, default `13`
- `UiTaskFontSize`: clamped to `9..28`, default `14`
- `UiPanelRightWidth`: clamped to `320..1600`, default `420`
- `UiCalendarHeight`: clamped to `280..1200`, default `420`
- `UiShowTaskPanel`: `0` or `1`, default `1`

### Alert behavior
- `AlertScanWindowMinutes`: default `60`
- `AlertDispatchTickSeconds`: clamped to `30..3600`, default `60`
- `AlertRefreshMinutes`: clamped to `1..1440`, default `10`
- `AlertGraceMinutes`: clamped to `0..5`, default `1`
- `AlertSoundEnabled`: `0` or `1`, default `1`
- `AlertSoundPath`: default `C:\Windows\Media\notify.wav`

### Holiday subscription
- `HolidaySubscriptionFiles`: optional multi-entry subscription list
- `HolidaySubscriptionCatalog`: optional saved checklist state for the Settings file list
- Storage format: a single string with `|` separators between subscription entries
- UI editing format:
  - provider checklist in Settings
  - only providers from the fixed `tcalendar\providers\` directory are shown
  - checked rows contribute to `HolidaySubscriptionFiles`
  - free-form add/remove path editing is not part of the Settings UI
- Provider aliases:
  - `jp-public-holiday`
- `jp-public-holiday` resolves to:
  - `tcalendar\providers\jp-public-holiday.ini`
  - runtime then loads the same-basename script `tcalendar\providers\jp-public-holiday.js`
- File subscription loading scope:
  - current provider reads UTF-8 text files
  - one record per line
  - format: `YYYY-MM-DD|Name|Kind`
  - `Kind` is optional
  - blank lines and lines starting with `#` or `;` are ignored
- Merge rule:
  - subscription entries are applied in listed order
  - later entries override earlier entries for the same date
- Relative path rule:
  - relative file paths are resolved from the directory containing `tcalendar.ini`
- Important:
  - `jp-public-holiday` is not enabled unless it is listed here
  - the runtime loads `jp-public-holiday` from the manifest/script pair, not from an always-on built-in holiday set
  - `HolidaySubscriptionCatalog` keeps unchecked files in the saved Settings list without activating them
  - the shipped `jp-public-holiday` provider currently mirrors the workbook historical holiday table from `1873+`, including workbook year-end rows and historical pre-1948 observances

## Integration-Owned Key
The alert startup toggle is not owned by `tcalendar.ini`.

The current runtime reads:

```ini
[TCalendar]
Alart=0
```

from `tclock-win11.ini`.

`tcalendar.ini` startup also removes legacy local `Enable` and `Alart` keys from its own `[TCalendar]` section.

## Path Rules
- `TemplateDefault`, `TemplateUser`, and `StorageDbPath` are resolved from the executable directory when they are relative paths.
- If `TemplateDefault` is empty, the runtime derives it from `Skin` as `tcalendar\template\<Skin>\index.html`.

## Example
```ini
[TCalendar]
Skin=default
WindowWidth=960
WindowHeight=640
DefaultViewMode=list
DefaultRangePresetDays=7
DefaultCustomRangeDays=14
DefaultUseCustomRange=0
UiFontFamily=Segoe UI
UiBaseFontSize=14
UiCalendarDateFontSize=13
UiTaskFontSize=14
UiPanelRightWidth=420
UiCalendarHeight=420
UiShowTaskPanel=1
BlockExternalNavigation=1
EnableWebView2Bootstrap=1
StartupLogEnabled=0
TemplateUser=tcalendar\template\user\index.html
StorageDbPath=tcalendar\data\tasks.db
AlertDispatchTickSeconds=60
AlertRefreshMinutes=10
AlertGraceMinutes=1
AlertSoundEnabled=1
AlertSoundPath=C:\Windows\Media\notify.wav
HolidaySubscriptionFiles=
```
