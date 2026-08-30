# MenuCustom User Guide

Last updated: 2026-08-30
MenuCustom lets you turn the TClock right-click menu into a customizable launcher and utility menu.

## What You Can Do With MenuCustom

Use MenuCustom when you want the TClock right-click menu to work as a personal quick-access menu.

Typical uses:
- launch your favorite apps
- open folders, URLs, and settings pages
- add dynamic labels that show date, time, or other values
- add separators and display-only rows
- place a small timer directly in the menu

## Fixed Items

These items are always present and are not replaced by MenuCustom:
- Language switch submenu
- TClock properties
- Open TClock folder
- Restart TClock
- Exit TClock

## How It Works

MenuCustom is configured under `[MenuCustom]` in `tclock-win11.ini`.
You add items by numbering them:
- `Item1...`
- `Item2...`
- `Item3...`

Each item can be one of these types:
- `builtin`
- `shell`
- `commandline`
- `separator`
- `passive`
- `alarm`

## Quick Start Example

This example adds a simple Notepad launcher:

```ini
[MenuCustom]
MenuCustomEnabled=1
ItemCount=1

Item1Mode=shell
Item1Enabled=1
Item1Param=notepad.exe
Item1LabelFormat=Notepad
```

## Common Ways To Use It

### Launch apps and folders

Use `ItemNMode=shell` with:
- `ItemNParam=<path, URL, or URI>`
- `ItemNArgs=<optional arguments>`
- `ItemNWorkDir=<optional working directory>`

### Open a URL

Use a `shell` item with:
- `ItemNParam=https://...`

### Add a display-only status row

Use `ItemNMode=passive`.
This is useful for showing dynamic date/time text without running an action.

### Add a timer

Use `ItemNMode=alarm`.
This creates a small countdown timer inside the menu.

## Global Keys

These keys are placed directly under `[MenuCustom]`:

- `MenuCustomEnabled=<int>`
- `ItemCount=<int>`
- `LabelFormatUpdateSec=<int>`

`MenuCustomEnabled=1` enables custom right-click menu behavior.

## Item Numbering Rules

`N` in keys such as `ItemNMode` means the item index number.

- Numbers do not need to be contiguous.
- Missing numbers are skipped.
- If `ItemCount=3`, only `Item1` through `Item3` are processed. Higher-numbered items are ignored.
- The property page accepts item counts from `0` through `64`.

## Item Types

### `ItemNMode=builtin`

Runs one of TClock's built-in actions. Use `ItemNAction` to select the action.

Basic keys:
- `ItemNEnabled=0|1`
- `ItemNAction=<action_name>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`

Optional keys:
- `ItemNShow=<integer>` (a Windows `SW_*` show value)

### `ItemNMode=shell`

Opens a file, folder, URL, or URI through the Windows shell.

Basic keys:
- `ItemNEnabled=0|1`
- `ItemNParam=<target>`

Optional keys:
- `ItemNArgs=<optional arguments>`
- `ItemNWorkDir=<optional working directory>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNShow=<SW_*>`

### `ItemNMode=commandline`

Runs `ItemNParam` as a command line. `ItemNArgs` is not appended in this mode.

Basic keys:
- `ItemNEnabled=0|1`
- `ItemNParam=<command line>`

Optional keys:
- `ItemNWorkDir=<optional working directory>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNShow=<SW_*>`

### `ItemNMode=separator`

Adds a separator line.

### `ItemNMode=passive`

Shows display-only text and keeps the menu open when clicked.

Keys:
- `ItemNEnabled=0|1`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`

### `ItemNMode=alarm`

Adds a countdown timer item.

Keys:
- `ItemNEnabled=0|1`
- `ItemNLabelFormat=<base label>`
- `ItemNAlarmInitialSec=1..86400`
- `ItemNAlarmUpdateSec=<int>`
- `ItemNAlarmKeepMenuOpen=0|1`
- `ItemNAlarmNotifyFlags=0..3`
- `ItemNAlarmSoundFile=<WAV path>`
- `ItemNAlarmSoundVolume=0..100`
- `ItemNAlarmSoundLoop=0|1`
- `ItemNAlarmLabelIdle=<format>`
- `ItemNAlarmLabelRun=<format>`
- `ItemNAlarmLabelPause=<format>`
- `ItemNAlarmLabelDone=<format>`
- `ItemNAlarmMessage=<message>`

Alarm labels support `%REMAIN_HHMMSS%`, `%REMAIN_MMSS%`, `%REMAIN_SEC%`, and `%STATE%`.

## Examples

### Launch Notepad

```ini
Item1Mode=shell
Item1Enabled=1
Item1Param=notepad.exe
Item1LabelFormat=Notepad
```

### Open Date/Time settings with a dynamic label

```ini
Item2Mode=shell
Item2Enabled=1
Item2LabelFormat=Date/Time yyyy/mm/dd ddd tt hh:nn:ss
Item2LabelUpdateSec=1
Item2Param=ms-settings:dateandtime
```

### Add a separator

```ini
Item5Mode=separator
Item5Enabled=1
```

### Add a passive clock row

```ini
Item7Mode=passive
Item7Enabled=1
Item7LabelFormat=yyyy/mm/dd ddd tt hh:nn:ss
Item7LabelUpdateSec=1
```

### Add a 5-minute timer

```ini
Item3Mode=alarm
Item3Enabled=1
Item3LabelFormat=%REMAIN_MMSS% Timer
Item3AlarmInitialSec=300
Item3AlarmLabelIdle=%REMAIN_MMSS% Timer
Item3AlarmLabelRun=%REMAIN_MMSS% Running
Item3AlarmLabelPause=%REMAIN_MMSS% Paused
Item3AlarmLabelDone=%REMAIN_MMSS% Done
Item3AlarmUpdateSec=1
Item3AlarmKeepMenuOpen=1
Item3AlarmNotifyFlags=3
Item3AlarmMessage=Timer finished
Item3AlarmSoundFile=C:\Windows\Media\notify.wav
Item3AlarmSoundVolume=70
Item3AlarmSoundLoop=0
```

## Builtin Actions

Examples for `ItemNMode=builtin` with `ItemNAction`:
- `taskmgr`
- `cmd`
- `alarm_clock`
- `pullback`
- `control_panel`
- `power_options`
- `network_connections`
- `settings_home`
- `settings_network`
- `settings_datetime`
- `remove_drive_dynamic`
