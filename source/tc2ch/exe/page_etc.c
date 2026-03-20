/*-------------------------------------------
  pagedataplan.c
     「データ利用状況設定」
     by TTTT
---------------------------------------------*/

#include "tclock.h"

#define IDC_ETC_EXIT_EXTENSIONS_ON_EXIT 49001

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static void OnUpdate(HWND hDlg);
static void LaunchTCycleRuntimeFromEtcIfEnabled(HWND hDlg);
static int MapDluY(HWND hDlg, int dluY);

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern char g_mydir[];


//BOOL b_exe_UseSubClks = TRUE;

extern BOOL b_AutoRestart;

extern BOOL b_EnglishMenu;
extern int Language_Offset;

int selectedThermalZone = 0;

static HFONT hfontb;

BOOL b_TempAvailable = TRUE;

static int MapDluY(HWND hDlg, int dluY)
{
	RECT rc;
	rc.left = 0;
	rc.top = 0;
	rc.right = 0;
	rc.bottom = dluY;
	MapDialogRect(hDlg, &rc);
	return rc.bottom;
}

static void EnsureExitExtensionsOnExitControl(HWND hDlg)
{
	HWND hCtrl = GetDlgItem(hDlg, IDC_ETC_EXIT_EXTENSIONS_ON_EXIT);
	if (!hCtrl)
	{
		const wchar_t* text = (Language_Offset == LANGUAGE_OFFSET_JAPANESE)
			? L"TClock終了時に拡張機能も終了させる"
			: L"Exit extensions when TClock exits";
		hCtrl = CreateWindowW(L"BUTTON", text,
			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
			9, 188, 210, 11, hDlg,
			(HMENU)(INT_PTR)IDC_ETC_EXIT_EXTENSIONS_ON_EXIT, g_hInst, NULL);
	}
	if (hCtrl)
	{
		RECT rcBase = { 0 }, rc2 = { 0 }, rc3 = { 0 }, rcClient = { 0 };
		HWND h1 = GetDlgItem(hDlg, IDC_ETC_TCYCLE_INTEGRATION);
		HWND h2 = GetDlgItem(hDlg, IDC_ETC_TCALENDAR_INTEGRATION);
		HWND h3 = GetDlgItem(hDlg, IDC_ETC_TCAPTURE_INTEGRATION);
		int rowPitch = MapDluY(hDlg, 16);
		int x = 9, y = 188, w = 210;
		GetClientRect(hDlg, &rcClient);
		if (h1 && GetWindowRect(h1, &rcBase))
		{
			MapWindowPoints(NULL, hDlg, (POINT*)&rcBase, 2);
			x = rcBase.left;
			if (h2 && GetWindowRect(h2, &rc2)) {
				MapWindowPoints(NULL, hDlg, (POINT*)&rc2, 2);
				if (rc2.bottom > rcBase.bottom) rcBase.bottom = rc2.bottom;
			}
			if (h3 && GetWindowRect(h3, &rc3)) {
				MapWindowPoints(NULL, hDlg, (POINT*)&rc3, 2);
				if (rc3.bottom > rcBase.bottom) rcBase.bottom = rc3.bottom;
			}
			y = rcBase.top + rowPitch;
			w = rcClient.right - x - 6;
			if (w < 120) w = 120;
		}
		SetWindowPos(hCtrl, NULL, x, y, w, 11, SWP_NOZORDER | SWP_NOACTIVATE);
		HFONT hFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
		if (hFont) SendMessage(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
	}
}

/*------------------------------------------------
　「バージョン情報」ページ用ダイアログプロシージャ
--------------------------------------------------*/

INT_PTR CALLBACK PageEtcProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{

		case WM_INITDIALOG:
			OnInit(hDlg);
			return TRUE;
		case WM_TIMER:		//WM_TIMERに対する処理
			if (wParam == IDTIMER_UPDATE_TEMP)
			{
				OnUpdate(hDlg);
			}
			return TRUE;
		case WM_COMMAND:
		{
			WORD id, code;
			id = LOWORD(wParam); code = HIWORD(wParam);
			switch (id)
			{
			case IDC_THERMALZONE:
			case IDC_SPIN_THERMALZONE:
				if (code == EN_CHANGE)
				{
					SendPSChanged(hDlg);
					OnUpdate(hDlg);
					break;
				}
			}
			SendPSChanged(hDlg);
			return TRUE;
			case IDC_GET_TEMP:
				OnUpdate(hDlg);
				break;
		}
		case WM_NOTIFY:
			switch (((NMHDR *)lParam)->code)
			{
			case PSN_APPLY: 
				OnApply(hDlg); 
				break;
			}
			return TRUE;
		case WM_DESTROY:
			KillTimer(hDlg, IDTIMER_UPDATE_TEMP);
			DeleteObject(hfontb);
			return TRUE;
	}
	return FALSE;
}

static void LaunchTCycleRuntimeFromEtcIfEnabled(HWND hDlg)
{
	char tcycPathCfg[MAX_PATH];
	char exePath[MAX_PATH];

	if (!IsDlgButtonChecked(hDlg, IDC_ETC_TCYCLE_INTEGRATION)) return;

	GetMyRegStr("TCycle", "Path", tcycPathCfg, MAX_PATH, "");
	if (tcycPathCfg[0] == '\0') strcpy(tcycPathCfg, "plugins\\TCycle.exe");

	if ((tcycPathCfg[1] == ':') || (tcycPathCfg[0] == '\\') || (tcycPathCfg[0] == '/')) {
		strcpy(exePath, tcycPathCfg);
	}
	else {
		strcpy(exePath, g_mydir);
		add_title(exePath, tcycPathCfg);
	}

	if (!PathFileExistsUtf8Strict(exePath)) return;
	ShellExecuteUtf8Strict(g_hwndMain, "open", exePath, NULL, g_mydir, SW_HIDE);
}

/*------------------------------------------------
  initialize
--------------------------------------------------*/
static void OnInit(HWND hDlg)
{
	int tempInt = 0, tempNumThermalZone = 0;

	CheckDlgButton(hDlg, IDC_ETC_AUTORESTART, GetMyRegLong(NULL, "AutoRestart", TRUE));
	
	CheckDlgButton(hDlg, IDC_USE_SUBCLKS, GetMyRegLong(NULL, "EnableOnSubDisplay", TRUE));

	CheckDlgButton(hDlg, IDC_ETC_SHOWTRAYICON, GetMyRegLong(NULL, "ShowTrayIcon", TRUE));
	CheckDlgButton(hDlg, IDC_ETC_TCYCLE_INTEGRATION, GetMyRegLong("TCycle", "Enable", 0));
	CheckDlgButton(hDlg, IDC_ETC_TCALENDAR_INTEGRATION, GetMyRegLong("TCalendar", "Enable", 0));
	CheckDlgButton(hDlg, IDC_ETC_TCAPTURE_INTEGRATION, GetMyRegLong("TCapture", "Enable", 0));
	EnsureExitExtensionsOnExitControl(hDlg);
	CheckDlgButton(hDlg, IDC_ETC_EXIT_EXTENSIONS_ON_EXIT, GetMyRegLong("ETC", "ExitExtensionsOnTClockExit", 0));
	//CheckDlgButton(hDlg, IDC_ETC_SHOWTRAYICON, TRUE);
	//EnableDlgItem(hDlg, IDC_ETC_SHOWTRAYICON, FALSE);

	tempInt = (int)SendMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_REQUEST_TEMPCOUNTERINFO, selectedThermalZone);
	tempNumThermalZone = tempInt / 200;

	if (tempNumThermalZone > 0)
	{
		b_TempAvailable = TRUE;

		SendDlgItemMessage(hDlg, IDC_SPIN_THERMALZONE, UDM_SETRANGE, 0,
			MAKELONG(tempNumThermalZone - 1, 0));

		selectedThermalZone = GetMyRegLong("ETC", "SelectedThermalZone", 0);
		SendDlgItemMessage(hDlg, IDC_SPIN_THERMALZONE, UDM_SETPOS, 0, selectedThermalZone);

		if (tempNumThermalZone == 1)
		{
			EnableDlgItem(hDlg, IDC_THERMALZONE, FALSE);
			EnableDlgItem(hDlg, IDC_SPIN_THERMALZONE, FALSE);
		}

		if (Language_Offset == LANGUAGE_OFFSET_JAPANESE) {
			wchar_t tempStr[64];
		wsprintfW(tempStr, L"\x73FE\x5728\x306E\x30C7\x30FC\x30BF: %d \x2103", tempInt % 200);
			SetDlgItemTextW(hDlg, IDC_LABEL_CURRENT_TEMP, tempStr);
		}
		else {
			char tempStr[64];
			wsprintf(tempStr, "Current Value: %d deg.", tempInt % 200);
			SetWindowTextUTF8Strict(GetDlgItem(hDlg, IDC_LABEL_CURRENT_TEMP), tempStr);
		}

	}
	else {
		b_TempAvailable = FALSE;

		EnableDlgItem(hDlg, IDC_THERMALZONE, FALSE);
		EnableDlgItem(hDlg, IDC_SPIN_THERMALZONE, FALSE);

		if (Language_Offset == LANGUAGE_OFFSET_JAPANESE) {
			wchar_t tempStr[64];
			wsprintfW(tempStr, L"\x53D6\x5F97\x4E0D\x53EF");
			SetDlgItemTextW(hDlg, IDC_LABEL_CURRENT_TEMP, tempStr);
		}
		else {
			char tempStr[64];
			wsprintf(tempStr, "Not Available");
			SetWindowTextUTF8Strict(GetDlgItem(hDlg, IDC_LABEL_CURRENT_TEMP), tempStr);
		}
	}







	LOGFONT logfont;
	hfontb = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
	GetObject(hfontb, sizeof(LOGFONT), &logfont);
	logfont.lfWeight = FW_BOLD;
	hfontb = CreateFontIndirect(&logfont);
	SendDlgItemMessage(hDlg, IDC_LABEL_CURRENT_TEMP, WM_SETFONT, (WPARAM)hfontb, 0);	

	SetTimer(hDlg, IDTIMER_UPDATE_TEMP, 1000, NULL);
}


static void OnUpdate(HWND hDlg)
{
	int tempInt = 0;

	if (!b_TempAvailable)return;

	selectedThermalZone = (int)SendDlgItemMessage(hDlg, IDC_SPIN_THERMALZONE, UDM_GETPOS, 0, 0);

	tempInt = (int)SendMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_REQUEST_TEMPCOUNTERINFO, selectedThermalZone);

	if (Language_Offset == LANGUAGE_OFFSET_JAPANESE) {
		wchar_t tempStr[64];
		wsprintfW(tempStr, L"\x73FE\x5728\x306E\x30C7\x30FC\x30BF: %d \x2103", tempInt % 200);
		SetDlgItemTextW(hDlg, IDC_LABEL_CURRENT_TEMP, tempStr);
	}
	else {
		char tempStr[64];
		wsprintf(tempStr, "Current Value: %d deg.", tempInt % 200);
		SetWindowTextUTF8Strict(GetDlgItem(hDlg, IDC_LABEL_CURRENT_TEMP), tempStr);
	}

}

/*------------------------------------------------
  "Apply" button
--------------------------------------------------*/
static void OnApply(HWND hDlg)
{
	BOOL bTemp = FALSE;
	extern BOOL b_ShowTrayIcon;

	SetMyRegLong(NULL, "EnableOnSubDisplay", IsDlgButtonChecked(hDlg, IDC_USE_SUBCLKS));

	b_AutoRestart = IsDlgButtonChecked(hDlg, IDC_ETC_AUTORESTART);
	SetMyRegLong(NULL, "AutoRestart", IsDlgButtonChecked(hDlg, IDC_ETC_AUTORESTART));

	bTemp = IsDlgButtonChecked(hDlg, IDC_ETC_SHOWTRAYICON);
	SetMyRegLong(NULL, "ShowTrayIcon", bTemp);
	CreateTClockTrayIcon(bTemp);

	SetMyRegLong("TCycle", "Enable", IsDlgButtonChecked(hDlg, IDC_ETC_TCYCLE_INTEGRATION));
	LaunchTCycleRuntimeFromEtcIfEnabled(hDlg);

	SetMyRegLong("TCalendar", "Enable", IsDlgButtonChecked(hDlg, IDC_ETC_TCALENDAR_INTEGRATION));
	SetMyRegLong("TCapture", "Enable", IsDlgButtonChecked(hDlg, IDC_ETC_TCAPTURE_INTEGRATION));
	SetMyRegLong("ETC", "ExitExtensionsOnTClockExit", IsDlgButtonChecked(hDlg, IDC_ETC_EXIT_EXTENSIONS_ON_EXIT));

	SetMyRegLongDef("ETC", "SelectedThermalZone", selectedThermalZone);

}

