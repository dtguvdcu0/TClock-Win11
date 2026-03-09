/*-------------------------------------------
  pagemouse.c
　　「マウス操作」プロパティページ
　　KAZUBON 1997-1998
---------------------------------------------*/

#include "tclock.h"

#include "..\\common\\text_codec.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static void OnDestroy(HWND hDlg);
static void OnDropFilesChange(HWND hDlg);
static void OnMouseButton(HWND hDlg);
static void OnMouseClickTime(HWND hDlg, int id);
static void OnMouseFunc(HWND hDlg);
static void OnMouseFileChange(HWND hDlg);

static void OnSansho(HWND hDlg, WORD id);
static void InitMouseFuncList(HWND hDlg);
static LONG GetTCaptureEnableForMousePage(void);
static LONG GetTCalendarEnableForMousePage(void);

static char reg_section[] = "Mouse";

typedef struct {
	BOOL disable;
	int func[4];
	WORD hotkey[4];
	char format[4][256];
	char fname[4][256];
	int volume[4];
} CLICKDATA;
static CLICKDATA *pData = NULL;

//#define SendPSChanged(hDlg) SendMessage(GetParent(hDlg),PSM_CHANGED,(WPARAM)(hDlg),0)

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern BOOL b_EnglishMenu;
extern int Language_Offset;

static LONG GetTCaptureEnableForMousePage(void)
{
	LONG v = GetMyRegLong("TCapture", "Enable", -1);
	if (v != -1) return (v != 0) ? 1 : 0;
	v = GetMyRegLong("ETC", "TCaptureEnable", 0);
	SetMyRegLong("TCapture", "Enable", (v != 0) ? 1 : 0);
	DelMyReg("ETC", "TCaptureEnable");
	return (v != 0) ? 1 : 0;
}

static LONG GetTCalendarEnableForMousePage(void)
{
	LONG v = GetMyRegLong("TCalendar", "Enable", -1);
	if (v == -1) {
		SetMyRegLong("TCalendar", "Enable", 0);
		return 0;
	}
	return (v != 0) ? 1 : 0;
}

/*------------------------------------------------
　「マウス操作」ページ用ダイアログプロシージャ
--------------------------------------------------*/
BOOL CALLBACK PageMouseProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_INITDIALOG:
			OnInit(hDlg);
			return TRUE;
		case WM_COMMAND:
		{
			WORD id, code;
			id = LOWORD(wParam); code = HIWORD(wParam);
			switch(id)
			{
			// "Drop files"
			case IDC_DROPFILES:
				if(code == CBN_SELCHANGE)
				{
					OnDropFilesChange(hDlg);
					g_bApplyClock = TRUE;
				}
				break;
			case IDC_DROPFILESAPP:
				if(code == EN_CHANGE)
					SendPSChanged(hDlg);
				break;
			// "..."
			case IDC_DROPFILESAPPSANSHO:
			case IDC_MOUSEFILESANSHO:
				OnSansho(hDlg, id);
				break;
			//  "Button"
			case IDC_MOUSEBUTTON:
				if(code == CBN_SELCHANGE)
					OnMouseButton(hDlg);
				break;
			// single .... quadruple
			case IDC_RADSINGLE:
			case IDC_RADDOUBLE:
			case IDC_RADTRIPLE:
			case IDC_RADQUADRUPLE:
				OnMouseClickTime(hDlg, id);
				break;
			//  Mouse Function
			case IDC_MOUSEFUNC:
				if(code == CBN_SELCHANGE)
				{
					OnMouseFunc(hDlg);
					SendPSChanged(hDlg);
				}
				break;
			// Mouse Function - File
			case IDC_MOUSEFILE:
				if(code == EN_CHANGE)
				{
					OnMouseFileChange(hDlg);
					SendPSChanged(hDlg);
				}
				break;
			case IDC_RCLICKMENU:
				g_bApplyClock = TRUE;
				SendPSChanged(hDlg);
				break;
			case IDC_HOTKEY:
				OnMouseFunc(hDlg);
				SendPSChanged(hDlg);
				break;
			}
			return TRUE;
		}
		case WM_NOTIFY:
			switch(((NMHDR *)lParam)->code)
			{
				case PSN_APPLY: OnApply(hDlg); break;
			}
			return TRUE;
		case WM_DESTROY:
			OnDestroy(hDlg);
			return TRUE;
	}
	return FALSE;
}

/*------------------------------------------------
　ページの初期化
--------------------------------------------------*/
static void NormalizeUtf8InPlaceNoWriteback(char* value, int valueBytes)
{
	WCHAR wbuf[MAX_PATH];
	char utf8[MAX_PATH];
	if (!value || valueBytes <= 0 || value[0] == '\0') return;
	if (tc_utf8_to_utf16(value, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
	if (tc_utf16_to_utf8(wbuf, utf8, (int)sizeof(utf8)) <= 0) return;
	lstrcpyn(value, utf8, valueBytes);
}

void OnInit(HWND hDlg)
{
	char s[256];
	char entry[20];
	int i, j;
	HFONT hfont;

	hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	if(hfont)
	{
		SendDlgItemMessage(hDlg, IDC_DROPFILESAPP,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_MOUSEFILE,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_TOOLTIP,
			WM_SETFONT, (WPARAM)hfont, 0);
	}

	for(i = IDS_NONE; i <= IDS_MOVETO; i++)
		CBAddStringUTF8Compat(hDlg, IDC_DROPFILES, MyStringUTF8(i));
	CBSetCurSel(hDlg, IDC_DROPFILES,
		GetMyRegLong(reg_section, "DropFiles", 0));
	GetMyRegStr(reg_section, "DropFilesApp", s, 256, "");
	NormalizeUtf8InPlaceNoWriteback(s, (int)sizeof(s));
	SetDlgItemTextUTF8Strict(hDlg, IDC_DROPFILESAPP, s);

	pData = malloc(sizeof(CLICKDATA) * 28);

	for(i = 0; i < 28; i++)
	{
		for(j = 0; j < 4; j++)
		{
			wsprintf(entry, "%d%d", i, j+1);
			pData[i].disable = FALSE;
			pData[i].func[j] =
				GetMyRegLong(reg_section, entry, MOUSEFUNC_NONE);
			pData[i].format[j][0] = 0;
			pData[i].fname[j][0] = 0;
			pData[i].hotkey[j] = 0;
			if(i == IDS_HOTKEY - IDS_LEFTBUTTON)
			{
				wsprintf(entry, "%d%dHotkey", i, j+1);
				pData[i].hotkey[j] = (WORD)GetMyRegLong(reg_section, entry, 0);
			}

			else if(pData[i].func[j] == MOUSEFUNC_OPENFILE || pData[i].func[j] == MOUSEFUNC_FILELIST)
			{
				wsprintf(entry, "%d%dFile", i, j+1);
				GetMyRegStr(reg_section, entry, pData[i].fname[j], 256, "");
				NormalizeUtf8InPlaceNoWriteback(pData[i].fname[j], (int)sizeof(pData[i].fname[j]));
			}

		}
	}


	for(i = IDS_LEFTBUTTON; i <= IDS_SWHEEL2; i++)
		CBAddStringUTF8Compat(hDlg, IDC_MOUSEBUTTON, MyStringUTF8(i));
	AdjustDlgConboBoxDropDown(hDlg, IDC_MOUSEBUTTON, 22);

	CheckDlgButton(hDlg, IDC_RCLICKMENU,
		GetMyRegLong("Mouse", "RightClickMenu", TRUE));





	// set mouse functions to combo box
	InitMouseFuncList(hDlg);

	OnDropFilesChange(hDlg);
	CBSetCurSel(hDlg, IDC_MOUSEBUTTON, 0);
	OnMouseButton(hDlg);
}

/*------------------------------------------------
　更新
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	char s[256], entry[20];
	int n;
	int i, j;

	n = CBGetCurSel(hDlg, IDC_DROPFILES);
	SetMyRegLong(reg_section, "DropFiles", n);
	GetDlgItemTextUTF8(hDlg, IDC_DROPFILESAPP, s, 256);
	SetMyRegStr(reg_section, "DropFilesApp", s);

	SetMyRegLong("Mouse", "RightClickMenu",
		IsDlgButtonChecked(hDlg, IDC_RCLICKMENU));

	for(i = 0; i < 28; i++)
	{
		//if(i == 1) continue;
		for(j = 0; j < 4; j++)
		{
			wsprintf(entry, "%d%d", i, j+1);
			if(pData[i].func[j] >= 0)
				SetMyRegLong(reg_section, entry, pData[i].func[j]);
			else DelMyReg(reg_section, entry);
			if(i == IDS_HOTKEY - IDS_LEFTBUTTON)
			{
				wsprintf(entry, "%d%dHotkey", i, j+1);
				if (pData[i].hotkey[j])
					SetMyRegLong(reg_section, entry, pData[i].hotkey[j]);
				else DelMyReg(reg_section, entry);
			}
			if(pData[i].func[j] == MOUSEFUNC_OPENFILE || pData[i].func[j] == MOUSEFUNC_FILELIST)
			{
				wsprintf(entry, "%d%dFile", i, j+1);
				SetMyRegStr(reg_section, entry, pData[i].fname[j]);
			}

		}
	}
	ResetHotkey(g_hwndMain);
}

/*------------------------------------------------

--------------------------------------------------*/
void OnDestroy(HWND hDlg)
{
	UNREFERENCED_PARAMETER(hDlg);
	if(pData) free(pData);
}

/*------------------------------------------------
　「ファイルのドロップ」
--------------------------------------------------*/
void OnDropFilesChange(HWND hDlg)
{
	int i, n;
	n = CBGetCurSel(hDlg, IDC_DROPFILES);
	SetDlgItemTextUTF8Strict(hDlg, IDC_LABDROPFILESAPP,
		MyStringUTF8(n >= 3?IDS_LABFOLDER:IDS_LABPROGRAM));
	for(i = IDC_LABDROPFILESAPP; i <= IDC_DROPFILESAPPSANSHO; i++)
		ShowDlgItem(hDlg, i, (2 <= n && n <= 4));

	SendPSChanged(hDlg);
}

/*------------------------------------------------
  "Button"
--------------------------------------------------*/
void OnMouseButton(HWND hDlg)
{
	int n, button, j;

	n = CBGetCurSel(hDlg, IDC_MOUSEBUTTON);
	button = n;
	if (!pData || button < 0 || button >= 28) return;
	//if(n > 0) button = n + 1;

	for(j = 0; j < 4; j++)
	{
		if(pData[button].func[j] >= 0) break;
	}
	if(j == 4) j = 0;
	CheckRadioButton(hDlg, IDC_RADSINGLE, IDC_RADQUADRUPLE,
		IDC_RADSINGLE + j);

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
	{
		ShowDlgItem(hDlg, IDC_HOTKEY, TRUE);
	}
	else
	{
		ShowDlgItem(hDlg, IDC_HOTKEY, FALSE);
	}

	if (button == (IDS_WHEEL1  - IDS_LEFTBUTTON) || button == (IDS_WHEEL2  - IDS_LEFTBUTTON)||
		button == (IDS_CWHEEL1 - IDS_LEFTBUTTON) || button == (IDS_CWHEEL2 - IDS_LEFTBUTTON)||
		button == (IDS_SWHEEL1 - IDS_LEFTBUTTON) || button == (IDS_SWHEEL2 - IDS_LEFTBUTTON))
	{
		EnableDlgItem(hDlg, IDC_RADDOUBLE, FALSE);
		EnableDlgItem(hDlg, IDC_RADTRIPLE, FALSE);
		EnableDlgItem(hDlg, IDC_RADQUADRUPLE, FALSE);
	}
	else
	{
		EnableDlgItem(hDlg, IDC_RADDOUBLE, TRUE);
		EnableDlgItem(hDlg, IDC_RADTRIPLE, TRUE);
		EnableDlgItem(hDlg, IDC_RADQUADRUPLE, TRUE);
	}

	OnMouseClickTime(hDlg, IDC_RADSINGLE + j);
}

/*------------------------------------------------
  "Single" ... "Quadruple"
--------------------------------------------------*/
void OnMouseClickTime(HWND hDlg, int id)
{
	int n, button;
	int click, i, count, func;

	n = CBGetCurSel(hDlg, IDC_MOUSEBUTTON);
	button = n;
	if (!pData || button < 0 || button >= 28) return;

	click = id - IDC_RADSINGLE;
	if (click < 0 || click >= 4) return;
	func = pData[button].func[click];

	count = CBGetCount(hDlg, IDC_MOUSEFUNC);
	for(i = 0; i < count; i++)
	{
		if(func == CBGetItemData(hDlg, IDC_MOUSEFUNC, i))
		{
			CBSetCurSel(hDlg, IDC_MOUSEFUNC, i);
			break;
		}
	}

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
		SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_SETHOTKEY, pData[button].hotkey[click], 0);
	else
		SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_SETHOTKEY, 0, 0);

	OnMouseFunc(hDlg);
}

/*------------------------------------------------
  Mouse Functions combo box
--------------------------------------------------*/
void OnMouseFunc(HWND hDlg)
{
	int n, button, j;
	int click, index, func;

	n = CBGetCurSel(hDlg, IDC_MOUSEBUTTON);
	button = n;
	if (!pData || button < 0 || button >= 28) return;

	for(j = 0; j < 4; j++)
	{
		if(IsDlgButtonChecked(hDlg, IDC_RADSINGLE + j))
			break;
	}
	if(j == 4) return;
	click = j;

	index = CBGetCurSel(hDlg, IDC_MOUSEFUNC);
	if (index == CB_ERR) return;
	func = (int)(INT_PTR)CBGetItemData(hDlg, IDC_MOUSEFUNC, index);
	if (func == CB_ERR) return;
	pData[button].func[click] = func;

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
		pData[button].hotkey[click] = (WORD)SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);

	ShowDlgItem(hDlg, IDC_LABMOUSEFILE,
		(func == MOUSEFUNC_OPENFILE||func == MOUSEFUNC_FILELIST
			));
	ShowDlgItem(hDlg, IDC_MOUSEFILE,
		(func == MOUSEFUNC_OPENFILE||func == MOUSEFUNC_FILELIST));
	ShowDlgItem(hDlg, IDC_MOUSEFILESANSHO, (func == MOUSEFUNC_OPENFILE || func == MOUSEFUNC_FILELIST));

	if(func == MOUSEFUNC_OPENFILE || func == MOUSEFUNC_FILELIST)
	{
		NormalizeUtf8InPlaceNoWriteback(pData[button].fname[click], (int)sizeof(pData[button].fname[click]));
		SetDlgItemTextUTF8Strict(hDlg, IDC_LABMOUSEFILE, MyStringUTF8(IDS_FILE));
		SetDlgItemTextUTF8Strict(hDlg, IDC_MOUSEFILE, pData[button].fname[click]);
	}

}

/*------------------------------------------------
  Format/File - Mouse Function
--------------------------------------------------*/
void OnMouseFileChange(HWND hDlg)
{
	int n, button, j;
	int click, index, func;

	n = CBGetCurSel(hDlg, IDC_MOUSEBUTTON);
	button = n;
	if (!pData || button < 0 || button >= 28) return;

	for(j = 0; j < 4; j++)
	{
		if(IsDlgButtonChecked(hDlg, IDC_RADSINGLE + j))
			break;
	}
	if(j == 4) return;
	click = j;

	index = CBGetCurSel(hDlg, IDC_MOUSEFUNC);
	if (index == CB_ERR) return;
	func = (int)(INT_PTR)CBGetItemData(hDlg, IDC_MOUSEFUNC, index);
	if (func == CB_ERR) return;

	if(func == MOUSEFUNC_OPENFILE || func == MOUSEFUNC_FILELIST)
		GetDlgItemTextUTF8(hDlg, IDC_MOUSEFILE, pData[button].fname[click], (int)sizeof(pData[button].fname[click]));
}

/*------------------------------------------------
　「...」　ファイルの参照
--------------------------------------------------*/

void OnSansho(HWND hDlg, WORD id)
{
	int n;
	char filter[80], deffile[MAX_PATH], fname[MAX_PATH];
	HRESULT hrModern;

	if(id == IDC_DROPFILESAPPSANSHO)
	{
		n = CBGetCurSel(hDlg, IDC_DROPFILES);
		if(n >= 3)
		{
			char initdirUtf8[MAX_PATH];
			wchar_t initdirW[MAX_PATH];
			const wchar_t* initdirArg = NULL;
			GetDlgItemTextUTF8(hDlg, id - 1, initdirUtf8, MAX_PATH);
			if (initdirUtf8[0] && tc_utf8_to_utf16(initdirUtf8, initdirW, (int)(sizeof(initdirW) / sizeof(initdirW[0]))) > 0) {
				initdirArg = initdirW;
			}
			hrModern = SelectPathUTF8Modern(hDlg, TRUE, initdirArg, NULL, 0, fname, (int)sizeof(fname));
			if (FAILED(hrModern)) return;
			NormalizeUtf8InPlaceNoWriteback(fname, (int)sizeof(fname));
			SetDlgItemTextUTF8Strict(hDlg, id - 1, fname);
			PostMessage(hDlg, WM_NEXTDLGCTL, 1, FALSE);
			SendPSChanged(hDlg);
			return;
		}
	}

	filter[0] = 0;
	if(id == IDC_DROPFILESAPPSANSHO)
	{
		str0cat(filter, MyStringUTF8(IDS_PROGRAMFILE));
		str0cat(filter, "*.exe");
	}
	str0cat(filter, MyStringUTF8(IDS_ALLFILE));
	str0cat(filter, "*.*");

	GetDlgItemTextUTF8(hDlg, id - 1, deffile, MAX_PATH);

	if(!SelectMyFileUTF8(hDlg, filter, 0, deffile, fname, (int)sizeof(fname))) // propsheet.c
		return;

	NormalizeUtf8InPlaceNoWriteback(fname, (int)sizeof(fname));
	SetDlgItemTextUTF8Strict(hDlg, id - 1, fname);
	PostMessage(hDlg, WM_NEXTDLGCTL, 1, FALSE);
	SendPSChanged(hDlg);
}


/*------------------------------------------------
  set mouse functions to combo box
--------------------------------------------------*/
void InitMouseFuncList(HWND hDlg)
{
	int i, index, cnt;
	MOUSE_FUNC_INFO *pmfl;
	LONG tcapEnabled = GetTCaptureEnableForMousePage();
	LONG tcalEnabled = GetTCalendarEnableForMousePage();
	cnt = GetMouseFuncCount();
	pmfl = GetMouseFuncList();
	for (i = 0; i < cnt; i++)
	{
		if (pmfl[i].mousefunc == MOUSEFUNC_TCALENDAR_OPEN && !tcalEnabled) continue;
		if (pmfl[i].mousefunc == MOUSEFUNC_TCAPTURE_SETTINGS && !tcapEnabled) continue;
		//リストの各項目を追加
		index = CBAddStringUTF8Compat(hDlg, IDC_MOUSEFUNC, MyStringUTF8(pmfl[i].idstring));
		CBSetItemData(hDlg, IDC_MOUSEFUNC, index, pmfl[i].mousefunc);
	}
	//リスト項目の表示数を指定
	AdjustDlgConboBoxDropDown(hDlg, IDC_MOUSEFUNC, 29);

}
