# TCycle Overview

## Purpose
TCycle is a resident Windows task runner that evaluates INI-defined triggers and launches actions with minimal runtime dependencies.

## Main Modules
- `main.cpp`: process entry, command-line dispatch, resident loop, and reload handling.
- `config.cpp` / `config.h`: INI parsing, defaults, value clamps, and path resolution.
- `scheduler.cpp` / `scheduler.h`: trigger evaluation for interval, weekly-time, startup, and bounded datetime patterns.
- `runner.cpp` / `runner.h`: action launch paths for `program`, `command`, and `shell`.
- `hotkey.cpp` / `hotkey.h`: global hotkey registration and dispatch.
- `watchdog.cpp` / `watchdog.h`: non-running retry logic and retry-window state.
- `state.cpp` / `state.h`: optional runtime-state persistence.
- `log.cpp` / `log.h`: UTF-8 log output and rotation.
- `gui_main.cpp` / `gui_main.h`: settings window entry path.

## Runtime Model
1. Bootstrap:
   - parse command line
   - resolve executable-relative paths
   - load `TCycle.ini`
2. Gate evaluation:
   - check the TClock integration gate
   - suppress launches when `[TCycle] Enabled=0` in the integration INI
3. Trigger evaluation:
   - compute due tasks
   - process startup, interval, weekly-time, hotkey, and watchdog paths
4. Action dispatch:
   - perform single-instance checks where applicable
   - launch a process or command wrapper
5. Optional persistence:
   - save runtime counters and timestamps when `StateEnabled=1`

## Command-Line Modes
- `--validate-config`: load configuration and gate state, then exit
- `--settings`: open only the settings window
- `--lang=ja` / `--lang=en`
- `--lang ja` / `--lang en`

Unknown arguments are ignored by the current implementation.

## Trigger Types
- `startup`
- `interval`
- `datetime_interval_limited`
- `weekly_time`
- `hotkey_only`
- `non_running`

## Trigger Semantics
- `startup`: fires once when the state path marks the task as not yet handled
- `interval`: evaluated every `PollSec` and uses `GraceSec` to tolerate delayed polls
- `datetime_interval_limited`: starts from `StartDateTime`, repeats every `RepeatEverySec`, and stops after `RepeatCount`
- `weekly_time`: combines date, weekday, and time selectors with a same-day cache
- `hotkey_only`: registers a global hotkey when `Hotkey` is valid
- `non_running`: uses watchdog retry rules and treats `WatchdogMaxRetry < 0` as unlimited

## Action Modes
- `program`: launches the target directly through `CreateProcessW` and supports single-instance checks
- `command`: wraps command text with `cmd.exe /C`
- `shell`: wraps command text with `powershell.exe -NoProfile -Command`

## Integration
- TCycle reads the INI path from `[Integration] TClockIniPath`.
- The referenced file is expected to provide `[TCycle] Enabled`.
- When `Enabled=0`, TCycle keeps running but suppresses due-task, watchdog, and hotkey launches.
- `TCycle.ini` is expected beside `TCycle.exe`.

## State and Logs
- `StateEnabled=1` enables state load/save for runtime counters and timestamps.
- Log output is UTF-8 with BOM.
- Log rotation starts above 1 MB and keeps up to five generations.
- `Local\\TCycle_Reload_Config_Event` triggers immediate configuration reload.

## Notes
- `program` mode is the only path that participates in single-instance process checks.
- `command` and `shell` treat `ActionPath` as command text, not as an executable path.
- `StartupDelaySec` is not used by the current `source/tcyc` runtime.
