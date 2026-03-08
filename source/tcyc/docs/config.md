# TCycle Configuration

## Primary Files
- Runtime INI: `TCycle.ini`
- Optional state INI: `tcycle.state.ini`
- Integration INI: path resolved from `[Integration] TClockIniPath`

## Load Rules
- Runtime INI is resolved as `exeDir\\TCycle.ini`.
- If `TCycle.ini` does not exist on first launch, the program creates a default file.
- Relative paths are resolved from the executable directory unless stated otherwise.

## Global Sections
### `[TCycle]`
- `PollSec`: polling interval in seconds, clamped to `1..60`, default `1`
- `GraceSec`: late-fire grace window, normalized to `1..300`, default `60`
- `LogLevel`: log verbosity, range `0..3`, default `0`
- `LogFile`: log path, relative paths resolve from `exeDir`, default `tcycle.log`
- `StateEnabled`: enable runtime state persistence, default `0`
- `StateFile`: state file path, relative paths resolve from `exeDir`, default `tcycle.state.ini`

### `[Debug]`
- `ForceCmdlineReadFail`: test-only switch for single-instance command-line read failure simulation, default `0`

### `[Integration]`
- `TClockIniPath`: path to the TClock INI file used for the global enable gate, default `..\\tclock-win11.ini`

## Task Sections
Task sections are named `[Task.N]` where `N=1..128`.

## Core Runtime Keys
- `Enabled`: `0` or `1`
- `Name`: free-form task name
- `TriggerType`: single trigger key
- `TriggerTypes`: comma-separated trigger list
- `IntervalSec`: integer seconds
- `ActionMode`: `program`, `command`, or `shell`
- `ActionPath`: executable path or command text depending on mode
- `ActionArgs`: additional arguments
- `ActionCwd`: working directory
- `SingleInstance`: `0` or `1`
- `WatchdogEnabled`: `0` or `1`
- `WatchdogRetrySec`: clamped to `10..3600`
- `WatchdogMaxRetry`: retry cap, `<0` means unlimited
- `WatchdogRequireArgsMatch`: `0` or `1`
- `StartDateTime`: `YYYY-MM-DD HH:MM[:SS]`
- `RepeatEverySec`: integer seconds, expected `0..86400`
- `RepeatCount`: integer count, expected `0..1000000`
- `DateEnabled`: `0` or `1`
- `Date`: `YYYY-MM-DD`
- `WeekdayEnabled`: `0` or `1`
- `EveryDay`: `0` or `1`
- `Weekdays`: comma-separated day names such as `mon,wed,fri` or `everyday`
- `Weekday`: single compatibility day name `sun..sat`
- `TimeEnabled`: `0` or `1`
- `TimeOfDay`: `HH:MM[:SS]`
- `Hotkey`: hotkey text such as `Ctrl+Alt+9`

## Trigger Values
Valid trigger names:
- `interval`
- `datetime_interval_limited`
- `weekly_time`
- `startup`
- `hotkey_only`
- `non_running`

## Hotkey Format
Hotkey strings are case-insensitive in practice and are built from:
- modifiers: `Ctrl`, `Shift`, `Alt`, `Win`
- one primary key: `A-Z`, `0-9`, `F1-F24`, `PrintScreen`, `Insert`, `Delete`, `Home`, `End`, `PgUp`, `PgDn`, `Space`, `Enter`

Examples:
- `Ctrl+Alt+9`
- `Alt+PrintScreen`
- `Ctrl+Shift+F12`

## Fallback and Clamp Rules
- Invalid `ActionMode` falls back to `program`.
- If `TriggerTypes` is empty, the runtime falls back to `TriggerType`.
- If `TriggerTypes` is valid and `TriggerType` is unknown, the runtime derives the primary trigger from the mask.
- If `WatchdogEnabled=1`, the runtime forces the `non_running` trigger bit.
- If `EveryDay=1` or `Weekdays=everyday`, per-day masks are ignored.
- Invalid or incomplete task sections are skipped instead of partially executed.

## Path Rules
- `ActionPath` is executable-relative only in `program` mode.
- `ActionPath` stays raw command text in `command` and `shell` modes.
- `ActionCwd` resolves relative to `exeDir` when it is not empty.

## Writing Examples

### Interval task
```ini
[Task.1]
Enabled=1
Name=Open_Notepad
TriggerTypes=interval
IntervalSec=600
ActionMode=program
ActionPath=C:\Windows\System32\notepad.exe
ActionArgs=
ActionCwd=C:\Windows\System32
SingleInstance=1
```

### Weekly time task
```ini
[Task.2]
Enabled=1
Name=Weekday_0900
TriggerTypes=weekly_time
DateEnabled=0
WeekdayEnabled=1
Weekdays=mon,tue,wed,thu,fri
TimeEnabled=1
TimeOfDay=09:00
ActionMode=program
ActionPath=C:\Windows\System32\calc.exe
ActionArgs=
ActionCwd=C:\Windows\System32
SingleInstance=1
```

### Hotkey task
```ini
[Task.3]
Enabled=1
Name=Hotkey_OpenExplorer
TriggerTypes=hotkey_only
Hotkey=Ctrl+Alt+9
ActionMode=program
ActionPath=C:\Windows\explorer.exe
ActionArgs=
ActionCwd=C:\Windows
SingleInstance=0
```

### Watchdog task
```ini
[Task.4]
Enabled=1
Name=Watch_MyApp
TriggerTypes=non_running
ActionMode=program
ActionPath=C:\tools\MyApp\MyApp.exe
ActionArgs=--minimized
ActionCwd=C:\tools\MyApp
SingleInstance=1
WatchdogEnabled=1
WatchdogRetrySec=10
WatchdogMaxRetry=5
WatchdogRequireArgsMatch=1
```
