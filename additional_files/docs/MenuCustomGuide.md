# MenuCustom User Guide

Last updated: 2026-03-08  
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
- `command`
- `separator`
- `passive`
- `alarm`

## Quick Start Example

This example adds a simple Notepad launcher:

```ini
[MenuCustom]
MenuCustomEnabled=1
ItemCount=1

Item1Type=command
Item1Enabled=1
Item1Label=Notepad
Item1ExecType=shell
Item1Param=notepad.exe
```

## Common Ways To Use It

### Launch apps and folders

Use `ItemNType=command` with:
- `ItemNExecType=shell`
- `ItemNParam=<path or command>`

### Open a URL

Use a `command` item with:
- `ItemNExecType=shell`
- `ItemNParam=https://...`

### Add a display-only status row

Use `ItemNType=passive`.
This is useful for showing dynamic date/time text without running an action.

### Add a timer

Use `ItemNType=alarm`.
This creates a small countdown timer inside the menu.

## Global Keys

These keys are placed directly under `[MenuCustom]`:

- `MenuCustomEnabled=<int>`
- `ItemCount=<int>`
- `LabelFormatUpdateSec=<int>`

`MenuCustomEnabled=1` enables custom right-click menu behavior.

## Item Numbering Rules

`N` in keys such as `ItemNType` means the item index number.

- Numbers do not need to be contiguous.
- Missing numbers are skipped.
- If `ItemCount=3`, then `Item4+` is ignored.

## Item Types

### `ItemNType=command`

Runs an action.

Basic keys:
- `ItemNEnabled=0|1`
- `ItemNLabel=<menu_text>`
- `ItemNExecType=builtin|shell|commandline`
- `ItemNParam=<execution parameter>`

Optional keys:
- `ItemNAction=<action_name>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNArgs=<optional args>`
- `ItemNWorkDir=<optional working directory>`
- `ItemNShow=<SW_*>`

### `ItemNType=separator`

Adds a separator line.

### `ItemNType=passive`

Shows display-only text and keeps the menu open when clicked.

### `ItemNType=alarm`

Adds a countdown timer item.

## Examples

### Launch Notepad

```ini
Item1Type=command
Item1Enabled=1
Item1Action=launch_notepad
Item1Label=Notepad
Item1ExecType=shell
Item1Param=notepad.exe
```

### Open Date/Time settings with a dynamic label

```ini
Item2Type=command
Item2Enabled=1
Item2Action=clock_label
Item2Label=Clock
Item2LabelFormat=Date/Time yyyy/mm/dd ddd tt hh:nn:ss
Item2LabelUpdateSec=1
Item2ExecType=shell
Item2Param=ms-settings:dateandtime
```

### Add a separator

```ini
Item5Type=separator
Item5Enabled=1
```

### Add a passive clock row

```ini
Item7Type=passive
Item7Enabled=1
Item7Label=Now
Item7LabelFormat=yyyy/mm/dd ddd tt hh:nn:ss
Item7LabelUpdateSec=1
```

### Add a 5-minute timer

```ini
Item3Type=alarm
Item3Enabled=1
Item3Label=Timer
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

Examples for `ItemNExecType=builtin`:
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
