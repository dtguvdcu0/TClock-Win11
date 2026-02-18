# MenuCustom Specification

Last updated: 2026-02-18  
Guide for customizing the right-click menu in TClock-Win11 using `[MenuCustom]`.

## What You Can Do

You can configure the right-click menu by editing settings under `[MenuCustom]` in `tclock-win11.ini`.

Typical usage is a customizable launcher-style menu.

Note: the following items are fixed and always present:

- Language switch submenu
- TClock properties
- Open TClock folder
- Restart TClock
- Exit TClock

## Global Keys

These keys are placed directly under `[MenuCustom]`:

- `MenuCustomEnabled=<int>`
- `ItemCount=<int>`
- `LabelFormatUpdateSec=<int>`

`MenuCustomEnabled=1` enables custom menu behavior on right-click.

`ItemCount` is the upper bound of item index numbers. Items above that index are not loaded (see below).

`LabelFormatUpdateSec` is used for items with TClock-style dynamic labels. If `ItemNLabelUpdateSec` is not set for an item, this global value is used.

## Item Numbering Rules

In this document, `N` in keys like `ItemN****` means the item index number (`Item1Type`, `Item2Label`, etc.).
Settings are grouped by matching item index.

- Numbers do not need to be contiguous. Missing numbers are skipped.

Examples:

- Defining only `Item1` and `Item3` is valid.
- If `ItemCount=3`, then `Item4+` is ignored.

## Per-Item Settings

### ItemType

- `ItemNType=command` run an action
- `ItemNType=separator` add a separator line
- `ItemNType=alarm` alarm/timer item

### command Item

Keys used when `ItemNType=command`.

Required/basic keys:

- `ItemNType=command`
- `ItemNEnabled=0|1`
- `ItemNLabel=<menu_text>`
- `ItemNExecType=builtin|shell|commandline`
- `ItemNParam=<execution parameter>`

Optional keys:

- `ItemNAction=<action_name>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNArgs=<optional args for shell target>`
- `ItemNWorkDir=<optional working directory>`
- `ItemNShow=<SW_*>` (default `1` = `SW_SHOWNORMAL`)

### command Key Details

- `ItemNEnabled`
  - `1`: visible
  - `0`: hidden
- `ItemNExecType`
  - `builtin`: internal TClock command (requires valid builtin action name)
  - `shell`: executed by ShellExecute (`ItemNParam`)
  - `commandline`: executed via `cmd.exe` (`ItemNParam`, usually starts with `/c`)
- `ItemNAction`
  - effectively required for `builtin`
  - optional metadata for `shell` / `commandline`
- `ItemNLabel`
  - plain menu text label
  - use this when no dynamic TClock format is needed
- `ItemNLabelFormat`
  - dynamic label text with TClock format
  - if set, it takes priority over `ItemNLabel`
- `ItemNLabelUpdateSec`
  - update interval for dynamic label (only while right-click menu is open)
  - `0`: fixed display (startup-time resolved)
  - `1+`: update every N seconds
- `ItemNShow`
  - startup window state
  - common values:
    - `0` = `SW_HIDE`
    - `1` = `SW_SHOWNORMAL`
    - `3` = `SW_SHOWMAXIMIZED`
    - `7` = `SW_SHOWMINNOACTIVE`

- Example: launch Notepad

```ini
Item1Type=command
Item1Enabled=1
Item1Action=launch_notepad
Item1Label=Notepad
Item1ExecType=shell
Item1Param=notepad.exe
```

- Example: dynamic label using TClock format

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

- Example: restart Explorer

```ini
Item24Type=command
Item24Enabled=1
Item24Action=Restart Explorer
Item24Label=Restart Explorer
Item24ExecType=commandline
Item24Param=/c taskkill /f /im explorer.exe & start explorer.exe
Item24Show=0
```

- Example: Command Prompt (Run as Administrator)

```ini
Item4Type=command
Item4Enabled=1
Item4Action=cmd_admin
Item4Label=Commandline (Admin)
Item4ExecType=commandline
Item4Param=/c powershell -NoProfile -Command "Start-Process cmd.exe -Verb RunAs -ArgumentList '/k cd /d C:\'"
```

- Example: Open folder (path)

```ini
Item11Type=command
Item11Enabled=1
Item11Label=Open TClock Folder
Item11ExecType=shell
Item11Param=C:\TClock-Win11
```

- Example: Open URL

```ini
Item13Type=command
Item13Enabled=1
Item13Label=Open GitHub
Item13ExecType=shell
Item13Param=https://github.com/
```

### Builtin Actions for `ExecType=builtin` (`ItemNAction`)

- `taskmgr`: open Task Manager
- `cmd`: open Command Prompt
- `alarm_clock`: open Windows Alarms & Clock
- `pullback`: collapse/restore custom area
- `control_panel`: open Control Panel
- `power_options`: open Power Options
- `network_connections`: open Network Connections
- `settings_home`: open Settings home
- `settings_network`: open Network settings
- `settings_datetime`: open Date/Time settings
- `remove_drive_dynamic`: removable-drive menu item (dynamic)

### separator Item

Use `ItemNType=separator` to add a separator line.

Notes:

- Leading/trailing/consecutive separators are normalized automatically.
- Example:

```ini
Item5Type=separator
Item5Enabled=1
```

### passive Item

Use `ItemNType=passive` for display-only text/TClock-formatted labels.
Clicking this row keeps the right-click menu open and runs no action.

```ini
Item7Type=passive
Item7Enabled=1
Item7Label=Now
Item7LabelFormat=yyyy/mm/dd ddd tt hh:nn:ss
Item7LabelUpdateSec=1
```

## `ItemNType=alarm`

Use `ItemNType=alarm` to configure a timer/alarm item.

It counts down from the configured initial seconds. After timeout, it can play a sound and/or show a message dialog.
Behavior: first left-click starts, next left-click pauses/resumes, right-click resets.

### alarm States

- `idle`: waiting at initial time
- `running`: counting down
- `paused`: paused
- `finished`: reached zero

### alarm Click Behavior

- Left-click transitions:
  - `idle -> running`
  - `running -> paused`
  - `paused -> running`
  - `finished -> running` (restart)
- Right-click: always reset to `idle` (initial time)

### alarm Keys

Required:

- `ItemNType=alarm`
- `ItemNEnabled=0|1`
- `ItemNAlarmInitialSec=<int>`

Recommended:

- `ItemNLabel=<text>`
- `ItemNAlarmLabelIdle=<text>`
- `ItemNAlarmLabelRun=<text>`
- `ItemNAlarmLabelPause=<text>`
- `ItemNAlarmLabelDone=<text>`

Optional:

- `ItemNAlarmUpdateSec=<int>`
- `ItemNAlarmKeepMenuOpen=0|1`
- `ItemNAlarmNotifyFlags=<0..3>`
- `ItemNAlarmMessage=<text>`
- `ItemNAlarmSoundFile=<path>`
- `ItemNAlarmSoundVolume=<0..100>`
- `ItemNAlarmSoundLoop=0|1`

### alarm Key Notes

`ItemNAlarmNotifyFlags`:

- `0`: no notification
- `1`: message only
- `2`: sound only
- `3`: message + sound

`ItemNAlarmKeepMenuOpen`:

- `0`: close menu after click
- `1`: keep menu visible after click

### alarm Label Placeholders

- `%REMAIN_SEC%`: remaining seconds
- `%REMAIN_MMSS%`: remaining min:sec
- `%REMAIN_HHMMSS%`: remaining hour:min:sec
- `%STATE%` (`idle`, `running`, `paused`, `finished`): current state

- Example

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
