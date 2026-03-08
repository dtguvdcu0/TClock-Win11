# TCycle User Guide

Last updated: 2026-03-08  
TCycle is a Windows task launcher that runs in the background and starts apps, scripts, or commands for you.

## What You Can Do With TCycle

Use TCycle when you want Windows to launch something automatically without opening Task Scheduler every time.

Typical uses:
- open a tool when Windows starts
- run an app every 10 minutes or every hour
- launch something at a specific time on specific weekdays
- assign a hotkey to a program or script
- restart a program automatically if it stops running

## Main Files

- `TCycle.exe`: the scheduler
- `TCycle.ini`: the main settings file
- `tcycle.state.ini`: optional runtime state file when state saving is enabled

By default, `TCycle.ini` is expected next to `TCycle.exe`.

## How It Works

You create one or more task sections in `TCycle.ini`.
Each task defines:
- when it should run
- what it should launch
- whether duplicate launches should be prevented

## Quick Start Example

This example opens Notepad every 10 minutes:

```ini
[TCycle]
PollSec=1
GraceSec=60

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

## Common Ways To Use It

### Run a program at startup

Use `TriggerTypes=startup`.

### Run a program repeatedly

Use `TriggerTypes=interval` and set `IntervalSec`.

### Run something at a fixed day and time

Use `TriggerTypes=weekly_time` with weekday and time settings.

### Launch something with a hotkey

Use `TriggerTypes=hotkey_only` and set `Hotkey`.

### Keep another app running

Use `TriggerTypes=non_running` together with watchdog settings.

## Useful Command

- `TCycle.exe --settings`: open the settings window

## Tips

- Use full paths for `ActionPath` and `ActionCwd` when possible.
- Put command arguments in `ActionArgs`, not inside `ActionPath`.
- If a task should not run twice, set `SingleInstance=1`.
- Start with one simple task first, then add more after it works.

## Related Docs

- Core implementation docs live under `source/tcyc/docs/`
