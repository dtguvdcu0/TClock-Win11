# MenuCustom Specification

Last updated: 2026-02-17  
Target: TClock-Win11 right-click menu (`[MenuCustom]`)

## Overview
`[MenuCustom]` controls non-fixed entries in the right-click menu.

Always-fixed entries (not removed by `[MenuCustom]`):
- Language (submenu)
- TClock properties
- Open TClock folder
- Restart TClock
- Exit TClock

## Quick Start
```ini
[MenuCustom]
Version=1
ItemCount=1
LabelFormatUpdateSec=1

Item1Type=command
Item1Enabled=1
Item1Action=clock_label
Item1Label=Clock
Item1LabelFormat=yyyy/mm/dd ddd tt hh:nn:ss
Item1LabelUpdateSec=1
Item1ExecType=shell
Item1Param=ms-settings:dateandtime
```

## Key Reference
Global keys:
- `Version=<int>`
- `ItemCount=<int>`
- `LabelFormatUpdateSec=<int>`: default refresh interval (seconds) for formatted labels

Per-item keys (`N` starts from `1`):
- `ItemNType=command|separator`
- `ItemNEnabled=0|1`
- `ItemNAction=<action_name>`
- `ItemNLabel=<menu_text>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNExecType=builtin|shell|commandline`
- `ItemNParam=<execution parameter>`
- `ItemNArgs=<optional args for shell target>`
- `ItemNWorkDir=<optional working directory>`
- `ItemNShow=<SW_*>` (default `1` = `SW_SHOWNORMAL`)

## Per-item Keys Explained (Top to Bottom)
- `ItemNType=command|separator`
  - `command`: clickable menu row.
  - `separator`: divider line. Other `ItemN*` execution keys are ignored.

- `ItemNEnabled=0|1`
  - `1`: row is active and shown.
  - `0`: row is skipped (hidden).

- `ItemNAction=<action_name>`
  - Logical identifier for the item.
  - For `builtin`, this must match a known built-in action name.
  - For `shell`/`commandline`, it is still useful as readable metadata.

- `ItemNLabel=<menu_text>`
  - Static display text.
  - Used directly when `ItemNLabelFormat` is empty.

- `ItemNLabelFormat=<format_text>`
  - Dynamic display text generated from format.
  - Uses TClock `MakeFormat` behavior.
  - If set, it overrides `ItemNLabel` for display.
  - Aliases supported:
    - `@CLOCK`
    - `@MAIN_FORMAT`
    - `@TCLOCK_FORMAT`
  - These aliases use the current main clock format from `Format/CustomFormat` (or `Format`).

- `ItemNLabelUpdateSec=<int>`
  - Per-item refresh interval in seconds.
  - `0`: refresh every timer tick while menu is open.
  - `1+`: refresh at that interval.
  - If omitted, `LabelFormatUpdateSec` is used.

- `ItemNExecType=builtin|shell|commandline`
  - `builtin`: executes internal TClock command handler.
  - `shell`: executes external target via ShellExecute-style route.
  - `commandline`: executes `cmd.exe` with `ItemNParam`.

- `ItemNParam=<execution parameter>`
  - Main execution payload.
  - `builtin`: optional (action usually decides behavior).
  - `shell`: target/URI or command string.
  - `commandline`: arguments passed to `cmd.exe` (for example `/c ...`).

- `ItemNArgs=<optional args for shell target>`
  - Extra arguments for `shell` execution.
  - Usually empty unless you want to split executable and arguments explicitly.

- `ItemNWorkDir=<optional working directory>`
  - Working directory for process launch.
  - Mainly useful for `shell` and `commandline`.

- `ItemNShow=<SW_*>`
  - Initial window show state for launched process.
  - Typical values:
    - `0`: hidden (`SW_HIDE`)
    - `1`: normal (`SW_SHOWNORMAL`)
    - `3`: maximized (`SW_SHOWMAXIMIZED`)
    - `7`: minimized (`SW_SHOWMINNOACTIVE`)

## Label Format Behavior
- `ItemNLabelFormat` uses TClock's `MakeFormat` engine.
- Token support follows TClock format support.
- `ItemNLabelFormat` overrides displayed `ItemNLabel` text.
- Refresh interval priority:
  - `ItemNLabelUpdateSec`
  - fallback to `LabelFormatUpdateSec`
- `0` means refresh on each menu open.

## Samples
Different refresh intervals per item:
```ini
[MenuCustom]
Version=1
ItemCount=3
LabelFormatUpdateSec=5

Item1Type=command
Item1Enabled=1
Item1Action=clock_fast
Item1Label=Clock Fast
Item1LabelFormat=hh:nn:ss
Item1LabelUpdateSec=1
Item1ExecType=shell
Item1Param=ms-settings:dateandtime


Item3Type=command
Item3Enabled=1
Item3Action=date_default
Item3Label=Date Default
Item3LabelFormat=yyyy/mm/dd
Item3ExecType=shell
Item3Param=ms-settings:dateandtime
```

Use full TClock format directly:
```ini
Item10Type=command
Item10Enabled=1
Item10Action=clock_full_format
Item10Label=Clock
Item10LabelFormat=<your TClock format string>
Item10LabelUpdateSec=1
Item10ExecType=shell
Item10Param=ms-settings:dateandtime
```

Launch Chrome:
```ini
Item23Type=command
Item23Enabled=1
Item23Action=launch_chrome
Item23Label=Google Chrome
Item23ExecType=shell
Item23Param="C:\Program Files\Google\Chrome\Application\chrome.exe"
```


Restart Explorer:
```ini
Item24Type=command
Item24Enabled=1
Item24Action=restart_explorer
Item24Label=Restart Explorer
Item24ExecType=commandline
Item24Param=/c taskkill /f /im explorer.exe & start explorer.exe
```

Turn off monitor:
```ini
Item25Type=command
Item25Enabled=1
Item25Action=monitor_off
Item25Label=Turn Off Monitor
Item25ExecType=commandline
Item25Param=/c powershell -NoProfile -Command "(Add-Type '[DllImport("user32.dll")]public static extern int SendMessage(int hWnd,int hMsg,int wParam,int lParam);' -Name Native -Namespace Win32 -PassThru)::SendMessage(-1,0x0112,0xF170,2)"
```

## Built-in Action Names
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

## Notes
- `ItemNType=separator` ignores action/exec keys.
- malformed rows are skipped safely.
- duplicate/leading/trailing separators are normalized.
- keep `pullback` and `remove_drive_dynamic` as `builtin`.
