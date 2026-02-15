/*-------------------------------------------
  page_win11.c
  Win11 settings page
  by TTTT
---------------------------------------------*/

#include "tclock.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static DWORD ReadPolicyDword(const char* subkey, const char* valueName, DWORD defval);
static void WritePolicyDword(const char* subkey, const char* valueName, DWORD value);
static BOOL ApplyHideClockActionElevated(HWND hDlg, DWORD hideClock);
static void EnsureHideClockActionButtons(HWND hDlg);

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
#ifndef IDC_WIN11_AUTOBACK_CLOCK_OFFSET
#define IDC_WIN11_AUTOBACK_CLOCK_OFFSET 1910
#define IDC_WIN11_AUTOBACK_CLOCK_OFFSET_SPIN 1911
#define IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET 1912
#define IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET_SPIN 1913
#endif
#ifndef IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT
#define IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT 1904
#endif
#ifndef IDC_WIN11_HIDE_NATIVE_CLOCK
#define IDC_WIN11_HIDE_NATIVE_CLOCK 1930
#define IDC_WIN11_SHOW_NATIVE_CLOCK 1931
#endif

static BOOL ApplyHideClockActionElevated(HWND hDlg, DWORD hideClock)
{
	wchar_t params[640];
	HINSTANCE hRet;
	wsprintfW(params,
		L"/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\" /v HideClock /t REG_DWORD /d %lu /f && taskkill /F /IM explorer.exe && start \"\" explorer.exe",
		(unsigned long)hideClock);
	hRet = ShellExecuteW(hDlg, L"runas", L"cmd.exe", params, NULL, SW_HIDE);
	return ((INT_PTR)hRet > 32) ? TRUE : FALSE;
}

static void EnsureHideClockActionButtons(HWND hDlg)
{
	HWND hCheck;
	HWND hAlign;
	HWND hSave;
	RECT rcCheck;
	RECT rcAlign;
	RECT rcSave;
	HWND hBtnHide;
	HWND hBtnShow;
	HFONT hFont;
	int gap;
	int vGap;
	int btnWidth;
	int rowLeft;
	int rowWidth;
	int btnHeight;
	int btnTop;
	const wchar_t* hideLabel;
	const wchar_t* showLabel;

	hCheck = GetDlgItem(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR);
	hAlign = GetDlgItem(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT);
	hSave = GetDlgItem(hDlg, IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT);
	if (!hCheck || !hAlign || !hSave) return;
	if (GetDlgItem(hDlg, IDC_WIN11_HIDE_NATIVE_CLOCK) && GetDlgItem(hDlg, IDC_WIN11_SHOW_NATIVE_CLOCK)) return;
	if (!GetWindowRect(hCheck, &rcCheck)) return;
	if (!GetWindowRect(hAlign, &rcAlign)) return;
	if (!GetWindowRect(hSave, &rcSave)) return;
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcCheck, 2);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcAlign, 2);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcSave, 2);
	ShowWindow(hCheck, SW_HIDE);
	rowLeft = rcSave.left;
	rowWidth = rcSave.right - rcSave.left;
	if (rowWidth <= 0) rowWidth = rcAlign.right - rcAlign.left;
	vGap = 5;
	gap = 6;
	btnWidth = (rowWidth - gap) / 2;
	if (btnWidth < 80) btnWidth = 80;
	btnHeight = rcSave.bottom - rcSave.top;
	if (btnHeight <= 0) btnHeight = 14;
	btnTop = rcSave.bottom + vGap;
	hideLabel = b_EnglishMenu ? L"Hide native clock" : L"純正時計を非表示にする";
	showLabel = b_EnglishMenu ? L"Show native clock" : L"純正時計を表示する";

	hBtnHide = CreateWindowExW(0, L"BUTTON",
		hideLabel,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		rowLeft, btnTop, btnWidth, btnHeight,
		hDlg, (HMENU)(INT_PTR)IDC_WIN11_HIDE_NATIVE_CLOCK, g_hInst, NULL);
	hBtnShow = CreateWindowExW(0, L"BUTTON",
		showLabel,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		rowLeft + btnWidth + gap, btnTop, rowWidth - btnWidth - gap, btnHeight,
		hDlg, (HMENU)(INT_PTR)IDC_WIN11_SHOW_NATIVE_CLOCK, g_hInst, NULL);
	SetWindowPos(hAlign, NULL, rowLeft, btnTop + btnHeight + vGap, rowWidth, rcAlign.bottom - rcAlign.top, SWP_NOZORDER);

	hFont = (HFONT)SendMessage(hCheck, WM_GETFONT, 0, 0);
	if (hFont) {
		if (hBtnHide) SendMessage(hBtnHide, WM_SETFONT, (WPARAM)hFont, TRUE);
		if (hBtnShow) SendMessage(hBtnShow, WM_SETFONT, (WPARAM)hFont, TRUE);
	}
}
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
	HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
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
  Win11 property page dialog procedure
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
			if (LOWORD(wParam) == IDC_WIN11_HIDE_NATIVE_CLOCK || LOWORD(wParam) == IDC_WIN11_SHOW_NATIVE_CLOCK) {
				DWORD hideClock = (LOWORD(wParam) == IDC_WIN11_HIDE_NATIVE_CLOCK) ? 1 : 0;
				WritePolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", hideClock);
				CheckDlgButton(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, hideClock ? BST_CHECKED : BST_UNCHECKED);
				if (!ApplyHideClockActionElevated(hDlg, hideClock)) {
					MyMessageBox(hDlg,
						"Failed to run elevated action.\nPlease approve UAC prompt and try again.",
						"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
				}
				return TRUE;
			}
			if (LOWORD(wParam) == IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT) {
				LRESULT saved = 0;
				if (IsWindow(g_hwndClock)) {
					saved = SendMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_SNAPSHOT_AUTOBACK_SAVE, 0);
				}
				if (saved) {
					MyMessageBox(hDlg,
						"現在の自動取得背景色を保存しました。\n固定値として使うには「背景色をタスクバーに自動一致」をOFFにしてください。\n\nSaved current auto-matched background colors.\nTurn off \"Auto match taskbar background\" to use fixed values.",
						"TClock-Win11", MB_OK, MB_ICONINFORMATION);
				}
				else {
					MyMessageBox(hDlg,
						"背景色の保存に失敗しました。\nタスクバー色の取得状態を確認して再実行してください。\n\nFailed to save background colors.\nPlease retry after taskbar color sampling is available.",
						"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
				}
				return TRUE;
			}
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
	int autoBackClockOffset;
	int autoBackShowDesktopOffset;
	DWORD hideClock;
	DWORD transparency;
	BOOL alignLeft;

	b_exe_Win11Main = GetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 9);

	autoBackAlpha = (int)GetMyRegLong("Color_Font", "AutoBackAlpha", 96);
	if (autoBackAlpha < 0) autoBackAlpha = 0;
	if (autoBackAlpha > 255) autoBackAlpha = 255;

	autoBackBlendRatio = (int)GetMyRegLong("Color_Font", "AutoBackBlendRatio", 50);
	if (autoBackBlendRatio < 0) autoBackBlendRatio = 0;
	if (autoBackBlendRatio > 100) autoBackBlendRatio = 100;

	autoBackRefreshSec = (int)GetMyRegLong("Color_Font", "AutoBackRefreshSec", 3);
	if (autoBackRefreshSec < 1) autoBackRefreshSec = 1;
	if (autoBackRefreshSec > 120) autoBackRefreshSec = 120;

	autoBackClockOffset = (int)GetMyRegLong("Color_Font", "AutoBackSampleClockOffset", 0);
	if (autoBackClockOffset < -200) autoBackClockOffset = -200;
	if (autoBackClockOffset > 200) autoBackClockOffset = 200;
	autoBackShowDesktopOffset = (int)GetMyRegLong("Color_Font", "AutoBackSampleShowDesktopOffset", 0);
	if (autoBackShowDesktopOffset < -200) autoBackShowDesktopOffset = -200;
	if (autoBackShowDesktopOffset > 200) autoBackShowDesktopOffset = 200;

	hideClock = ReadPolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", 0);
	transparency = ReadPolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", 1);
	alignLeft = (BOOL)GetMyRegLong("Win11", "AlignTaskbarLeft", 1);

	CheckDlgButton(hDlg, IDC_ETC_USE_WIN11NOTIFY, (BOOL)GetMyRegLong("Color_Font", "AutoBackMatchTaskbar", 1));
	CheckDlgButton(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, hideClock ? BST_CHECKED : BST_UNCHECKED);
	EnsureHideClockActionButtons(hDlg);
	CheckDlgButton(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, transparency ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, alignLeft ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUTPOSITION, UDM_SETRANGE, 0, MAKELONG(255, 0));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUTPOSITION, UDM_SETPOS, 0, autoBackAlpha);
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, UDM_SETRANGE, 0, MAKELONG(100, 0));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, UDM_SETPOS, 0, autoBackBlendRatio);
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETRANGE, 0, MAKELONG(120, 1));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETPOS, 0, autoBackRefreshSec);
	SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_CLOCK_OFFSET_SPIN, UDM_SETRANGE, 0, MAKELONG(200, -200));
	SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_CLOCK_OFFSET_SPIN, UDM_SETPOS, 0, autoBackClockOffset);
	SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET_SPIN, UDM_SETRANGE, 0, MAKELONG(200, -200));
	SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET_SPIN, UDM_SETPOS, 0, autoBackShowDesktopOffset);

	if (!b_exe_Win11Main) {
		EnableDlgItem(hDlg, IDC_ETC_USE_WIN11NOTIFY, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_HIDE_NATIVE_CLOCK, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_SHOW_NATIVE_CLOCK, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_CUTPOSITION, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_CUTPOSITION, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_NOTIFY_DETECTPOS, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_NOTIFY_DETECTPOS, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_AUTOBACK_CLOCK_OFFSET, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_AUTOBACK_CLOCK_OFFSET_SPIN, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET_SPIN, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT, FALSE);
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
	int autoBackClockOffset;
	int autoBackShowDesktopOffset;
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

	autoBackClockOffset = (int)(short)SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_CLOCK_OFFSET_SPIN, UDM_GETPOS, 0, 0);
	if (autoBackClockOffset < -200) autoBackClockOffset = -200;
	if (autoBackClockOffset > 200) autoBackClockOffset = 200;
	SetMyRegLong("Color_Font", "AutoBackSampleClockOffset", autoBackClockOffset);

	autoBackShowDesktopOffset = (int)(short)SendDlgItemMessage(hDlg, IDC_WIN11_AUTOBACK_SHOWDESK_OFFSET_SPIN, UDM_GETPOS, 0, 0);
	if (autoBackShowDesktopOffset < -200) autoBackShowDesktopOffset = -200;
	if (autoBackShowDesktopOffset > 200) autoBackShowDesktopOffset = 200;
	SetMyRegLong("Color_Font", "AutoBackSampleShowDesktopOffset", autoBackShowDesktopOffset);

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
