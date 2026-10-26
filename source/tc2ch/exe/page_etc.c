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



/*------------------------------------------------
  initialize
--------------------------------------------------*/
static void OnInit(HWND hDlg)
{
	int tempInt = 0, tempNumThermalZone = 0;

	CheckDlgButton(hDlg, IDC_ETC_AUTORESTART, GetMyRegLong(NULL, "AutoRestart", TRUE));
	
	CheckDlgButton(hDlg, IDC_USE_SUBCLKS, GetMyRegLong(NULL, "EnableOnSubDisplay", TRUE));

	CheckDlgButton(hDlg, IDC_ETC_SHOWTRAYICON, GetMyRegLong(NULL, "ShowTrayIcon", TRUE));
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

	SetMyRegLongDef("ETC", "SelectedThermalZone", selectedThermalZone);

}

