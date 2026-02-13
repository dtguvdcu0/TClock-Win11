/*-------------------------------------------
  pagedataplan.c
     「データ利用状況設定」
     by TTTT
---------------------------------------------*/

#include "tclock.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static DWORD ReadPolicyDword(const char* subkey, const char* valueName, DWORD defval);
static void WritePolicyDword(const char* subkey, const char* valueName, DWORD value);

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern char g_mydir[];

BOOL b_exe_Win11Main = FALSE;
int exe_AdjustTrayCutPosition = 0;
int exe_AdjustWin11ClockWidth = 0;
int exe_AdjutDetectNotify = 0;
BOOL b_exe_AdjustTrayWin11SmallTaskbar = TRUE;

extern BOOL b_EnglishMenu;
extern int Language_Offset;

#ifndef IDC_WIN11_ENABLE_TRANSPARENCY
#define IDC_WIN11_ENABLE_TRANSPARENCY 1900
#endif

static DWORD ReadPolicyDword(const char* subkey, const char* valueName, DWORD defval)
{
	HKEY hkey;
	DWORD value = defval;
	DWORD regtype = REG_DWORD;
	DWORD size = sizeof(DWORD);

	if (RegOpenKeyEx(HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS) {
		return defval;
	}
	if (RegQueryValueEx(hkey, valueName, 0, &regtype, (LPBYTE)&value, &size) != ERROR_SUCCESS || regtype != REG_DWORD) {
		value = defval;
	}
	RegCloseKey(hkey);
	return value;
}

static void WritePolicyDword(const char* subkey, const char* valueName, DWORD value)
{
	HKEY hkey;
	DWORD disposition = 0;

	if (RegCreateKeyEx(HKEY_CURRENT_USER, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, valueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}
static void WriteTaskbarAlignLeftQuiet(void)
{
	HKEY hkey;
	DWORD disposition = 0;
	DWORD value = 0;
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

	if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, "TaskbarAl", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}


static void WriteTaskbarAlignCenterQuiet(void)
{
	HKEY hkey;
	DWORD disposition = 0;
	DWORD value = 1;
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

	if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, "TaskbarAl", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}


static void NotifyExplorerAdvancedChanged(void)
{
	DWORD_PTR dw = 0;
	HWND hwndTaskbar = FindWindow("Shell_TrayWnd", NULL);
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)regPath, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"TraySettings", SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	if (hwndTaskbar) {
		SendMessageTimeout(hwndTaskbar, WM_SETTINGCHANGE, 0, (LPARAM)regPath, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
		SendMessageTimeout(hwndTaskbar, WM_SETTINGCHANGE, 0, (LPARAM)"TraySettings", SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}


/*------------------------------------------------
　「バージョン情報」ページ用ダイアログプロシージャ
--------------------------------------------------*/

INT_PTR CALLBACK PageWin11Proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	switch(message)
	{
		case WM_INITDIALOG:
			OnInit(hDlg);
			return TRUE;
		case WM_COMMAND:
			SendPSChanged(hDlg);
			return TRUE;
		case WM_NOTIFY:
			switch (((NMHDR *)lParam)->code)
			{
			case PSN_APPLY:
				OnApply(hDlg);
				break;
			case PSN_HELP:
				My2chHelp(GetParent(hDlg));
				break;
			}
			return TRUE;
	}
	return FALSE;
}

/*------------------------------------------------
  initialize
--------------------------------------------------*/
static void OnInit(HWND hDlg)
{
	int autoBackAlpha;
	int autoBackBlendRatio;
	int autoBackRefreshSec;
	DWORD hideClock;
	DWORD transparency;
	BOOL alignLeft;

	b_exe_Win11Main = GetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 9);

	autoBackAlpha = (int)GetMyRegLong("Color_Font", "AutoBackAlpha", 96);
	if (autoBackAlpha < 0) autoBackAlpha = 0;
	if (autoBackAlpha > 255) autoBackAlpha = 255;

	autoBackBlendRatio = (int)GetMyRegLong("Color_Font", "AutoBackBlendRatio", 65);
	if (autoBackBlendRatio < 0) autoBackBlendRatio = 0;
	if (autoBackBlendRatio > 100) autoBackBlendRatio = 100;

	autoBackRefreshSec = (int)GetMyRegLong("Color_Font", "AutoBackRefreshSec", 3);
	if (autoBackRefreshSec < 1) autoBackRefreshSec = 1;
	if (autoBackRefreshSec > 120) autoBackRefreshSec = 120;

	hideClock = ReadPolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", 0);
	transparency = ReadPolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", 1);
	alignLeft = (BOOL)GetMyRegLong("Win11", "AlignTaskbarLeft", 1);

	CheckDlgButton(hDlg, IDC_ETC_USE_WIN11NOTIFY, (BOOL)GetMyRegLong("Color_Font", "AutoBackMatchTaskbar", 1));
	CheckDlgButton(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, hideClock ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, transparency ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, alignLeft ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUTPOSITION, UDM_SETRANGE, 0, MAKELONG(255, 0));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUTPOSITION, UDM_SETPOS, 0, autoBackAlpha);
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, UDM_SETRANGE, 0, MAKELONG(100, 0));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, UDM_SETPOS, 0, autoBackBlendRatio);
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETRANGE, 0, MAKELONG(120, 1));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETPOS, 0, autoBackRefreshSec);

	if (!b_exe_Win11Main) {
		EnableDlgItem(hDlg, IDC_ETC_USE_WIN11NOTIFY, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_CUTPOSITION, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_CUTPOSITION, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_NOTIFY_DETECTPOS, FALSE);
	}
}

/*------------------------------------------------
  "Apply" button
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	int autoBackAlpha;
	int autoBackBlendRatio;
	int autoBackRefreshSec;
	DWORD hideClock;
	DWORD transparency;
	BOOL alignLeft;

	SetMyRegLong("Color_Font", "AutoBackMatchTaskbar", IsDlgButtonChecked(hDlg, IDC_ETC_USE_WIN11NOTIFY));
	if (IsDlgButtonChecked(hDlg, IDC_ETC_USE_WIN11NOTIFY)) {
		SetMyRegLong("Color_Font", "UseBackColor", 0);
	}

	autoBackAlpha = (int)(short)SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUTPOSITION, UDM_GETPOS, 0, 0);
	if (autoBackAlpha < 0) autoBackAlpha = 0;
	if (autoBackAlpha > 255) autoBackAlpha = 255;
	SetMyRegLong("Color_Font", "AutoBackAlpha", autoBackAlpha);

	autoBackBlendRatio = (int)(short)SendDlgItemMessage(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, UDM_GETPOS, 0, 0);
	if (autoBackBlendRatio < 0) autoBackBlendRatio = 0;
	if (autoBackBlendRatio > 100) autoBackBlendRatio = 100;
	SetMyRegLong("Color_Font", "AutoBackBlendRatio", autoBackBlendRatio);

	autoBackRefreshSec = (int)(short)SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_GETPOS, 0, 0);
	if (autoBackRefreshSec < 1) autoBackRefreshSec = 1;
	if (autoBackRefreshSec > 120) autoBackRefreshSec = 120;
	SetMyRegLong("Color_Font", "AutoBackRefreshSec", autoBackRefreshSec);

	hideClock = IsDlgButtonChecked(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR) ? 1 : 0;
	WritePolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", hideClock);

	transparency = IsDlgButtonChecked(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY) ? 1 : 0;
	WritePolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", transparency);

	alignLeft = IsDlgButtonChecked(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT) ? TRUE : FALSE;
	SetMyRegLong("Win11", "AlignTaskbarLeft", alignLeft);
	if (alignLeft) {
		WriteTaskbarAlignLeftQuiet();
	} else {
		WriteTaskbarAlignCenterQuiet();
	}
	NotifyExplorerAdvancedChanged();
}