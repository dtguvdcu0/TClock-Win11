***********************************************
***  tclock-win11 Ver 0.2.5.2 (2026/03/21)  ***
***       by ID:DTgUvdcU0                   ***
***********************************************

********
Introduction
********
This software is a TClock build for Windows 11 25H2.

This software is a revised version built with reference to:
TClock (by Kazubon)
TClock2ch (by 2ch volunteers)
TClockLight (by K. Takata)
TClock-Win10 (by Mantis Mountain Mobile)
so that it runs on Windows 11 25H2.

History of the TClock family:
TClock archive site (archive.org)
https://web.archive.org/web/20260000000000*/http://tclock2ch.no.land.to/

This program exists thanks to the work of Kazubon, K. Takata, Mantis Mountain Mobile, and many developers on the Internet.

This software is free software and may be redistributed or modified under the GNU GPL.

************
Terms of Use
************
As with other TClock-family software, the developer assumes no responsibility for any damage caused by using this software.

The base of this software is TClock2ch, which is a different line from the TClockLight series (currently TClockLight-kt). Please note that some behavior and formatting differ from TClockLight.

This software is intended to be placed in a folder that can be operated by user privileges (for example, inside a user account).
Please use it in a personal folder rather than under Windows-managed folders such as Program Files.
(It may also work in Program Files, but that is not verified.)

This software may crash and loop together with Explorer because of configuration problems or environment compatibility.
In that case, you may need to terminate the process from Task Manager, but a Safe Mode is provided to make recovery easier.
If the program starts within 20 seconds of the previous start, unstable features are suppressed and you may be able to open the TClock right-click menu and exit cleanly.
In that case, "[SafeMode]" appears at the beginning of the task tray format.

If Windows display scaling is set to anything other than 100%, text in the right-click menu and settings dialogs may appear blurred.
In that case, open the properties of tclock-win11.exe in Explorer and adjust the compatibility DPI settings so scaling is handled by the application.

About the VC++ runtime package:
This software requires the VC++ runtime package.
If tclock-win11.exe reports that VCRUNTIME140.dll or MSVCP140.dll is missing, install:
Visual Studio 2015 Visual C++ Redistributable Package
https://www.microsoft.com/ja-jp/download/details.aspx?id=48145
(use vc_redist.x64.exe for the 64-bit build)

****************
How to install
****************
Put the following files in the same folder and run TClock-Win11.exe.

[Required files (3)]
01. TClock-Win11.exe: Main program
02. tcdll-win11.dll: Taskbar modification module
03. tclang-win11.dll: Language module

[Optional files]
04. tclock-win11.ini: Configuration file. If not present, a new one will be created with default settings (recommended). Some configuration items still require direct editing of this file.
05. readme_en.txt: This file.
06. tclock_tooltip.txt: Tooltip content file. Sample files are included in the package. If you use Japanese, use Shift-JIS encoding.
07. tclock.bmp: Analog clock background file. A sample file is included.
08. UnplugDrive.exe: Executable file of the free software UnplugDrive Portable. It is not included in the distribution zip. Put it directly under the TClock folder to use removable-drive removal.

[Auto-generated files]
09. TClock-Win11.log: Normal operation log. If you do not need it, open tclock-win11.ini and set NormalLog=0.
10. TCLOCK-WIN11-DEBUG.LOG: Debug log. It is not recommended to keep debug logging enabled during normal use because it increases disk access.

********************
How to Uninstall
********************
Delete the folder and its files.

The registry is not used.

The Delete Registry button in Right-click Menu => TClock Properties => Other can still remove registry entries left by older TClock-family software.

**********
Overview and Features
**********
The following is a list of features of TClock2ch that have been added after removing features that can be replaced by other software.

The graph display function, which was a feature of TClock2ch-analog, can be used.

Configuration management is now centralized in an ini file (tclock-win11.ini). The registry is no longer used.
The existing registry information deletion function is still available.

If the configuration file does not exist at the first startup, it will be created in the program folder with default settings.
If it fails to create the file, it will terminate the operation.

This application is built with Visual Studio 2015.

This application is designed to be used in a Japanese environment, but you can change the display language to English from the right-click menu.

Windows 10 is designed to connect to Ethernet, WiFi, and LTE (WAN) at the same time, and use Ethernet > WiFi > LTE for Internet connection in that order.

The SSID for WiFi connections and the access point name for WAN (LTE) connections can be displayed.
It also has a function to automatically switch the display according to the communication status (see NMX1 and NMX2 formats).

It can display the BL (battery level in %), VL&VM (volume and mute status), CU (CPU load), and communication speed bars in the background.

You can remove removable drives from the right-click menu. (UnplugDrive.exe required)

The indicator can be displayed for some VPN connections (VPN connection identification keyword setting is required in the configuration file).

**********************
How to use and change settings
**********************
[Functionality derived from TClock2ch].
For basic usage, please refer to the TClock attached site
https://web.archive.org/web/20260000000000*/http://tclock2ch.no.land.to/

For basic information on how to use TClock2ch, please refer to "Help for TClock2ch" at
https://web.archive.org/web/20260000000000*/http://tclock2ch.no.land.to/help2ch/

In particular, the old custom formatting is explained below. Some of them are no longer available.
https://web.archive.org/web/20260000000000*/http://tclock2ch.no.land.to/help2ch/template.html

At least the following points are different from the old version
TClock2ch was not able to detect the amount of WiFi network transmissions correctly, but it is now supported. TClock2ch was not able to detect the amount of WiFi network transmission and reception correctly.

The TTBase integration function and the desktop calendar integration function have been removed. It is no longer available.

In addition, the following functions have been removed	Window arrangement and operation functions
	Timer function
	Calendar function
	Web display function on tooltip
	Modification of the start button and start menu
	Multiple settings saving function
	Volume control function
	Time adjustment function
	Desktop icon modification function

Ini file
https://web.archive.org/web/20260000000000*/http://tclock2ch.no.land.to/help2ch/ini.html
TClock-Win11 is completely migrated to ini file (tclock-win11.ini), and ini file is created automatically.
TClock-Win11 will automatically create the ini file. If it fails to create the file, it will not start.

[The ini file will be created automatically.]
For the newly added functions that are unlikely to be changed, the configuration dialog is not implemented at this time.
Please edit tclock-win11.ini directly, and restart TClock.

If a VPN connection is not detected, please register a part of the network adapter name for VPN in "TClock Properties" => "Others" tab => "VPN Keywords" item.

You can switch the language of the right-click menu itself between Japanese and English by clicking "Language" at the top of the right-click menu.

If you can't identify the Ethernet connection, please register a part of the name of the Eithernet network adapter in "TClock Properties" => "Others" tab => Ethernet keyword.

**********************************
********** Safe Mode **********
**********************************
If the system starts up within 20 seconds of the last startup, it will assume that it is in a loop due to startup failure and enter safe mode.

This may avoid a situation where Explorer keeps crashing and TClock cannot be stopped except from Task Manager.
This may prevent Explorer from continuing to crash and TClock from being stopped except from Task Manager.

****************************************
TClock-Win11 modified and new features: formatting
****************************************

Some of the formats were added for debugging purposes, but I thought they could be useful.

[Version].
VerTC TClock-Win11 version string.

[Connection status/communication status indicator]
LTE When LTE connection is established, the string "LTE" is displayed*.
Communication status indicator with an asterisk (which becomes "LTE*") (default: 4 characters wide).
The string displayed when connecting can be changed in the LTEString in the ini file.

WiFi When the WiFi connection is established, the string "WiFi" will be displayed.
An asterisk ("WiFi*") will be displayed to indicate the communication status (5 characters wide).

EthS When Ethernet connection is established, the string "Eth" will be displayed.
Communication status will be displayed with an asterisk (becomes "Eth*").

EthL The string "Ethernet" will be displayed when the Ethernet connection is established.
Display the communication status with an asterisk (becomes "Ethernet*").

If you cannot identify the Ethernet connection, please register a part of the name of the Eithernet network adapter in "TClock Properties" => "Others" tab => Ethernet keyword item.

NMX1(NMX2) (* 10 characters wide) (The display is the same for both NMX1 and NMX2)
	Displays information about the currently active network communication path by switching automatically.
	When an Ethernet connection is established, the string "Ethernet" is displayed.
	Communication status is displayed with an asterisk ("Ethernet*").
	During WiFi Internet connection, SSID is displayed.
	During LTE(WAN) connection, the access point name is displayed.
The string width can be set in [ETC] NetMIX_Length in tclock-win11.ini.

SSID, APN (* 10 characters wide)
	Displays the SSID or access point name APN when a WiFi or LTE (WAN) connection has been established (InternetConnection).
	The SSID and access point name APN will be displayed when a connection is established (InternetConnection) not only on the currently active route but also in the background.
The string width can be set in [ETC] SSID_AP_Length in the tclock-win11.ini file.

When VPNS VPN is connected, "VPN" is displayed. When not connected, " " (3 characters wide)

EWLS Display Ethernet / WiFi / LTE connection status ("E*W*L*" with no space)(6 characters wide)

EWLL Display Ethernet / WiFi / LTE connection status (with space like "E* W* L*")(8 characters wide)

ICP Display the following information (1 character width)
Displayed characters
   - No Internet connection path
   E Internet connection path: Ethernet
   W Internet connection path: WiFi
   L Internet Connection Path: LTE(WAN)* M Internet Connection Path: Metered WiFi
   M Internet Connection Path: Metered WiFi

The display characters for LTE(WAN) in EWLS, EWLL and ICP format can be changed in LTEChar in the tclock-win11.ini file (only the first character is valid).

[Communication Profile Information]
WANP (WAN Profile) LTE (WAN) profile number in the connection profile (estimated if not connected).
DPRP (Data Plan Retrieved Profile) Profile number of the data communication usage to be retrieved in the connection profile.
AIPF (Active Internet ProFile) The profile number in the connection profile that is currently being used for Internet connection.

[IP Address (15 characters)
IPE Ethernet
IPW WiFi
IPL LTE (WAN)
IPV VPN
IPA Of the above, currently active

[Battery Charge Status]
"BCS" Battery Charge Status
Display "*" when battery is charging and " " when battery is not charging.

Mute Status Display Function] [Mute Status Display Function
VM In the mute state, the string (default: "*") set by MuteString in the ini file will be displayed.
When unmuted, the same number of spaces are displayed.

[FTA Flag Timer Adjustment Indicator]
FTA Flag Timer Adjustment Displays a "*" (usually " ") when the timer is reset (for one second only) to make the seconds display (somewhat) accurate.
Algorithmically, there is a limit to the accuracy of TClock's seconds display, so in many cases it is not worth worrying about.

[Accumulated traffic (extended)].
In addition to the conventional NSAK(KB), NSAM(MB), NRAK(KB), and NSRAM(MB) notations, the following formats have been added.
# NSAG = total amount sent (in GB)
# NRAG = total received (in GB)
For these two formats, the method of specifying digits is the same as before (minority display is possible).

In addition, the following automatic unit displays have been added
NSAA (NetSendAllAutodigit) Total amount sent from Tclock startup (in auto units)
NRAA (NetRecieveAllAutodigit) Total amount of data received since Tclock started (automatic unit)
The unit (KB=>MB=>GB) of four characters including the decimal point plus two half-width characters is attached.

[Memory Information (Extended)
For conventional displays such as MTPK, GB units can be displayed by setting the last character to "G".
In GB notation, the format is "#" type, which allows you to specify digits after the decimal point.
*In order to match the notation of Windows Task Manager, etc., these are calculated with 1GB=1024MB, regardless of the value of MegabytesInGigaByte in tclock.ini.

# MS = amount of physical memory (in GB, integer value, calculated as 1GB = 1000MB, available as a system installed memory notation)
# MG = amount of free physical memory (in GB) < same as MAPG >
# MTPG = amount of physical memory (in GB)
# MAPG = amount of free physical memory (in GB) < same as MG >
# MUPG = amount of physical memory used (in GB)
# MTFG = Amount of page file memory in GB
# MAFG = Amount of free pagefile memory (in GB)
# MUFG = Amount of memory used by page file (in GB)
# MTVG = virtual memory in GB
# MAVG = virtual free memory in GB
MAVG = virtual free memory (in GB) # MUVG = virtual used memory (in GB)

[Time difference setting]
Implemented a time difference format that mimics TClock Light.
It is written in the form of td±h:min. The "±" character must be a single-byte "+" or "-", and the hour and minute must be two single-byte digits, and time differences longer than 24 hours are invalid.
The time difference of more than 24 hours is invalid. The time difference is valid from one declaration to the next, and can be restored by writing "td+00:00" or "td-00:00".
The following is also implemented on a trial basis. Please note that this has not been fully tested.
tu±h:min with North American Daylight Saving Time correction. The hour:minute should be the standard time difference.
te±h:min with European Daylight Saving Time correction. The hour:minute should be the standard time difference.
StU, StE Daylight saving time indicator ("*" for daylight saving time, " " for normal). Reflects the time difference setting of the nearest tu or te before this format.

CUeXX, CCeXX: Load and clock for logical processor (thread) number XX (two-digit number). This function is the same as CCX and CUX, but now supports two-digit logical processor numbers.
It is not that accurate, so please consider it as a reference. The clock also does not match between two logical processors that share a physical core.
The description of the traditional format says core number, but it is actually a logical processor (thread). It is not possible to display each physical core.

PCORE: Number of physical cores
LPROC: Number of logical processors (threads)

***************
Version history
***************

2026/03/21 Main changes (v0.2.5.2)
Fixes for the INI Recovery feature

2026/03/20 Main changes (v0.2.5.1)
Reviewed and adjusted SafeMode behavior
Added an INI Recovery feature available only while SafeMode is active
Reviewed the items emitted into the initial INI

2026/03/15 Main changes (v0.2.5.0)
Added a feature-limited MinimalMode that runs with [ETC]MinimalMode=1

2026/03/14 Main changes (v0.2.4.2)
Revived and corrected network-status-related format tokens

2026/03/10 Main changes (v0.2.4.0)
Added format tokens for storage transfer read, write, and total throughput
Adjusted INI-related behavior, limited the temporary Utf8Hex handling, and cleaned up items emitted at first run
Other minor fixes

2026/03/08 Main changes (v0.2.3.1)
Added a guide button to the format property page so the list of available formats can be reviewed
Added dedicated handling for loading ASCII art files (..AA.txt) in tooltips
Extended format support for CPU GHz and storage TB units
Added an option to retrieve the global IP address [GIP]
Other minor fixes

2026/03/07 Main changes (v0.2.3.0)
Experimental WinUI introduction

2026/03/05 Main changes (v0.2.2.0)
Added the task scheduler tool

2026/03/01 Main changes (v0.2.1.1)
Fixed a problem where interval launch did not work in custom format

2026/02/28 Main changes (v0.2.1.0)
Adjusted the custom-format property settings menu
Added a property settings menu to the right-click menu

2026/02/25 Main changes (v0.2.0.0)
Added support for missing formats such as Japanese calendar era formats
Added a property settings menu for custom format

2026/02/24 Main changes (v0.1.9.0)
Completed the internal Unicode migration and added support for special characters such as the sun symbol
Added the calendar feature (beta)

2026/02/21 Main changes (v0.1.8.0)
Added two-byte character support for tooltips, unified the related internal handling, and added support for <%...%> format syntax

2026/02/20 Main changes (v0.1.7.1)
Added a multi-display capture tool

2026/02/19 Main changes (v0.1.7.0)
Added custom formats that can read text and JSON
- When linked with external scripts, weather and exchange-rate information can be displayed dynamically

2026/02/18 Main changes (v0.1.6.0)
Added right-click menu customization for TClock

2026/02/16 Main changes (v0.1.5.0)
Handled Windows 11 25H2 start-menu left alignment and native clock hiding through registry changes
Stabilized the TClock format-compatibility area inherited from the original source
Added automatic taskbar-background color sampling for the TClock display area (Windows 11 property support)
Improved the gradual UTF-8 migration of INI files
Changed source files from Shift-JIS to UTF-8
Partially migrated from A APIs to W APIs
