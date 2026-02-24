/*-------------------------------------------
  propdlg.c
  show "properties for TClock"
---------------------------------------------*/

//#define NONAMELESSUNION
#include "tclock.h"
#include "..\common\text_codec.h"
#include <shobjidl.h>

#define MAX_PAGE  20
#define WM_TCLOCK_APPLY_REFRESH (WM_APP + 101)

INT_PTR CALLBACK PropertyDialog(HWND, UINT, WPARAM, LPARAM);

void SetMyDialgPos(HWND hwnd);

// dialog procedures of each page
INT_PTR CALLBACK PageColorProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageFormatProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageMouseProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageTooltipProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageGraphProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageMiscProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageAnalogClockProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageAboutProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageChimeProc(HWND, UINT, WPARAM, LPARAM);

INT_PTR CALLBACK PageBarmeterProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageEtcProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageKeywordProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageAppControlProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageEtc1Proc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageWin11Proc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageColorAdditionalProc(HWND, UINT, WPARAM, LPARAM);




// TV_INSERTSTRUCTではなぜかエラーがでた
typedef struct{
	HTREEITEM hParent;
	HTREEITEM hInsertAfter;
	TVITEMW item;
} _TV_INSERTSTRUCT;


static HHOOK hookMsgfilter = 0;

static WNDPROC oldWndProc; // to save default window procedure
static int startpage = 0;  // page to open first

BOOL g_bApplyClock = FALSE;
BOOL g_bApplyTaskbar = FALSE;
BOOL g_bApplyLangDLL = FALSE;
static LONG g_inApplyDispatch = 0;
static LONG g_refreshDispatchQueued = 0;
static LONG g_propdlgCommandDepth = 0;
static LONG g_propdlgNotifyDepth = 0;

// menu.c
extern HMENU g_hMenu;

extern BOOL b_EnglishMenu;
extern BOOL b_NormalLog;
extern int Language_Offset;

static int GetSafeLanguageOffset(void)
{
	if (Language_Offset == LANGUAGE_OFFSET_JAPANESE || Language_Offset == LANGUAGE_OFFSET_ENGLISH) {
		return Language_Offset;
	}

	if (b_NormalLog) {
		char msg[128];
		wsprintf(msg, "[Warning] Invalid Language_Offset=%d. reset.", Language_Offset);
		WriteNormalLog(msg);
	}

	Language_Offset = b_EnglishMenu ? LANGUAGE_OFFSET_ENGLISH : LANGUAGE_OFFSET_JAPANESE;
	return Language_Offset;
}




/*-------------------------------------------
  show property sheet
---------------------------------------------*/
void MyPropertyDialog(void)
{
	DWORD err;
	char msg[192];

	g_bApplyClock = FALSE;
	g_bApplyTaskbar = FALSE;
	g_bApplyLangDLL = FALSE;

	if(!(g_hwndPropDlg && IsWindow(g_hwndPropDlg))) {
		g_hwndPropDlg = CreateDialogW(GetLangModule(), MAKEINTRESOURCEW(GetSafeLanguageOffset() + IDD_PROPERTY), g_hwndMain, PropertyDialog);
		if(!(g_hwndPropDlg && IsWindow(g_hwndPropDlg))) {
			err = GetLastError();
			if (b_NormalLog) {
				wsprintf(msg, "[Warning] CreateDialog failed (id=%u, off=%d, err=%lu)", (unsigned)(GetSafeLanguageOffset() + IDD_PROPERTY), Language_Offset, (unsigned long)err);
				WriteNormalLog(msg);
			}

			if (Language_Offset != 0)
				g_hwndPropDlg = CreateDialogW(GetLangModule(), MAKEINTRESOURCEW(IDD_PROPERTY), g_hwndMain, PropertyDialog);
			else
				g_hwndPropDlg = CreateDialogW(GetLangModule(), MAKEINTRESOURCEW(1000 + IDD_PROPERTY), g_hwndMain, PropertyDialog);
		}

		if(!(g_hwndPropDlg && IsWindow(g_hwndPropDlg))) {
			char fname[MAX_PATH];
			HINSTANCE hInst = LoadLanguageDLL(fname);
			if(hInst != NULL) {
				if(g_hInstResource) FreeLibrary(g_hInstResource);
				g_hInstResource = hInst;
				strcpy(g_langdllname, fname);
				g_hwndPropDlg = CreateDialogW(GetLangModule(), MAKEINTRESOURCEW(GetSafeLanguageOffset() + IDD_PROPERTY), g_hwndMain, PropertyDialog);
			}
		}
	}

	if(g_hwndPropDlg && IsWindow(g_hwndPropDlg)) {
		ShowWindow(g_hwndPropDlg, SW_SHOW);
		UpdateWindow(g_hwndPropDlg);
		SetForegroundWindow98(g_hwndPropDlg);
	}
	else {
		if (b_NormalLog) WriteNormalLog("[Error] Property dialog could not be opened.");
		MyMessageBoxW(g_hwndMain, L"Failed to open property dialog.", L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
	}
}

static VOID SetPageDlgPos(HWND hParent, HWND hDlg)
{
	LONG DlgBase;
	WORD DlgBaseH;
	HWND hTree;
	RECT rect;
	POINT pos;

	hTree = GetDlgItem(hParent, IDC_TREE);
	GetWindowRect(hTree, &rect);
	pos.x = rect.right;
	pos.y = rect.top;
	ScreenToClient(hParent, &pos);

	DlgBase = GetDialogBaseUnits();
	DlgBaseH = LOWORD(DlgBase);

	pos.x = pos.x + (DlgBaseH / 4);
	pos.y = pos.y;
	SetWindowPos(hDlg, NULL, pos.x, pos.y, 0, 0, SWP_NOSIZE);
}

static VOID CreatePageDialog(HWND hParent, HWND hDlg[], BOOL bDlgFlg[], int index, int wID, DLGPROC dlgprc)
{
	HINSTANCE hInst;

	if (bDlgFlg[index]) {
		return;
	}
	hInst   = GetLangModule();
	hDlg[index] = CreateDialog(hInst, MAKEINTRESOURCE((WORD)wID), hParent, dlgprc);
	SetPageDlgPos(hParent, hDlg[index]);

	bDlgFlg[index] = TRUE;
}

static VOID CreatePageDialogW(HWND hParent, HWND hDlg[], BOOL bDlgFlg[], int index, int wID, DLGPROC dlgprc)
{
	HINSTANCE hInst;

	if (bDlgFlg[index]) {
		return;
	}
	hInst = GetLangModule();
	hDlg[index] = CreateDialogW(hInst, MAKEINTRESOURCEW((WORD)wID), hParent, dlgprc);
	SetPageDlgPos(hParent, hDlg[index]);

	bDlgFlg[index] = TRUE;
}

/*-------------------------------------------
  Property dialog
---------------------------------------------*/
INT_PTR CALLBACK PropertyDialog(HWND hDwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HWND hTree;
	static HWND hDlg[MAX_PAGE];
	static BOOL bDlgFlg[MAX_PAGE];
	static HWND *hNowDlg;
	_TV_INSERTSTRUCT tv;
	HTREEITEM hParent[6], hChild[MAX_PAGE];
	NM_TREEVIEW *pNMTV;
//	HINSTANCE hInst;
	static int nowDlg;
	int i;

	switch(message){
		case WM_INITDIALOG:
			InitCommonControls();

			hTree = GetDlgItem(hDwnd, IDC_TREE);
			memset(&tv, 0, sizeof(_TV_INSERTSTRUCT));

			//設定ダイアログ左メニューの順序は、数字ではなく、以下の行の順番で決まっている。

			tv.hInsertAfter = TVI_LAST;
			tv.hParent = TVI_ROOT;
			tv.item.mask = TVIF_TEXT | TVIF_STATE | TVIF_PARAM;
			tv.item.state = TVIS_EXPANDED;
			tv.item.stateMask = TVIS_EXPANDED;

			tv.item.pszText = (LPWSTR)MyStringW(IDS_CLOCK);
			tv.item.lParam = 0;
			hParent[0] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 1;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_TOOLTIP);
			hParent[1] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 6;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_MOUSE);
			hParent[6] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 7;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_ETC);
			hParent[7] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 2;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_KEYWORDS);
			hParent[2] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 5;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_WIN11);
			hParent[5] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 3;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_ABOUT);
			hParent[3] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 4;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_MISC);
			hParent[4] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);



			tv.hParent = hParent[0];
			tv.item.mask = TVIF_TEXT | TVIF_PARAM;

			tv.item.lParam = 100;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_COLOR);
			hChild[0] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 106;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_COLOR_ADDITIONAL);
			hChild[6] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 101;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_FORMAT);
			hChild[1] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 107;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_CHIME);
			hChild[7] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			tv.item.lParam = 103;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_GRAPH);
			hChild[3] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);

			//	BarMeter設定	20181103
			tv.item.lParam = 105;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_BARMETER);
			hChild[5] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);


			//tv.item.lParam = 102;
			//tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_MOUSE);
			//hChild[2] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);


			tv.item.lParam = 104;
			tv.item.pszText = (LPWSTR)MyStringW(IDS_PROP_ANALOG);
			hChild[4] = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tv);



			CreatePageDialog(hDwnd, hDlg, bDlgFlg, 0, GetSafeLanguageOffset() + IDD_PAGECOLOR, PageColorProc);
			nowDlg = startpage;
			//nowDlg = 0;
			hNowDlg = &hDlg[nowDlg];
			ShowWindow(*hNowDlg, SW_SHOW);
			UpdateWindow(*hNowDlg);

			if (nowDlg < 9)
				TreeView_SelectItem(hTree, hChild[nowDlg]);
			else
				TreeView_SelectItem(hTree, hParent[nowDlg - 10]);


			break;

		case WM_SHOWWINDOW: // adjust the window position
			SetMyDialgPos(hDwnd);
			break;

		case WM_NOTIFY:
			{
			LONG notifyDepth = InterlockedIncrement(&g_propdlgNotifyDepth);
			if (notifyDepth > 64) {
								InterlockedDecrement(&g_propdlgNotifyDepth);
				break;
			}
			pNMTV = (NM_TREEVIEW *)lParam;
			switch (pNMTV->hdr.code){
				case TVN_SELCHANGEDA:
				case TVN_SELCHANGEDW:
					ShowWindow(*hNowDlg, SW_HIDE);
					UpdateWindow(*hNowDlg);
					nowDlg = (int)pNMTV->itemNew.lParam;
					switch (nowDlg)		//nowDlgはここのところだけはTree選択のバッファとして利用されているが、このswitchを出るときにもともとのnowDlgとしての意味に戻っている。
					{
						case 0:
							nowDlg = 0;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGECOLOR, PageColorProc);
							break;

						case 1:
							nowDlg = 11;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGETOOLTIP, PageTooltipProc);
							break;

						case 2:
							nowDlg = 12;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_KEYWORDS, PageKeywordProc);
							break;

						case 3:
							nowDlg = 13;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEABOUT, PageAboutProc);
							break;

						case 4:
							nowDlg = 14;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEMISC, PageMiscProc);
							break;

						case 5:
							nowDlg = 15;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_WIN11, PageWin11Proc);
							break;

						case 6:
							nowDlg = 16;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEMOUSE, PageMouseProc);
							break;

						case 7:
							nowDlg = 17;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_ETC, PageEtcProc);
							break;

						case 100:
							//nowDlg -= 10;
							nowDlg = 0;		//PAGECOLORの親と同じ
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGECOLOR, PageColorProc);
							break;
						case 101:
							//nowDlg -= 10;
							nowDlg = 1;
							/* Format page: force Unicode dialog creation to keep non-ACP input lossless. */
							CreatePageDialogW(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEFORMAT, PageFormatProc);
							break;

						//case 102:
						//	//nowDlg -= 10;
						//	nowDlg = 2;
						//	CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEMOUSE, PageMouseProc);
						//	break;
						case 103:
							//nowDlg -= 10;
							nowDlg = 3;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEGRAPH, PageGraphProc);
							break;
						case 104:
							//nowDlg -= 10;
							nowDlg = 4;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEANALOG, PageAnalogClockProc);
							break;

						case 105:
							nowDlg = 5;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEBARMETER, PageBarmeterProc);
							break;

						case 106:
							nowDlg = 6;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGECOLOR_ADDITIONAL, PageColorAdditionalProc);
							break;

						case 107:
							nowDlg = 7;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGECHIME, PageChimeProc);
							break;

						default:
							//nowDlg -= 10;
							nowDlg = 0;
							break;
					}
					hNowDlg = &hDlg[nowDlg];
					ShowWindow(*hNowDlg, SW_SHOW);
					UpdateWindow(*hNowDlg);
					break;
			}
			InterlockedDecrement(&g_propdlgNotifyDepth);
			}
			break;

		case WM_COMMAND:
			{
				LONG cmdDepth = InterlockedIncrement(&g_propdlgCommandDepth);
				if (cmdDepth > 64) {
										InterlockedDecrement(&g_propdlgCommandDepth);
					break;
				}
			// apply settings
			if(LOWORD(wParam) == IDOK || LOWORD(wParam) == ID_APPLY)
			{
				if (InterlockedCompareExchange(&g_inApplyDispatch, 1, 0) != 0) {
										InterlockedDecrement(&g_propdlgCommandDepth);
					break;
				}
				{
					NMHDR lp;
					lp.code = PSN_APPLY;
					/* Apply only the active page to avoid unnecessary full-save latency. */
					if (hNowDlg && *hNowDlg && IsWindow(*hNowDlg)) {
						SendMessage(*hNowDlg, WM_NOTIFY, 0, (LPARAM)&lp);
					}
				}
				if(g_bApplyClock)
				{
					g_bApplyClock = FALSE;
					InterlockedExchange(&g_refreshDispatchQueued, 1);
				}
				if(g_bApplyTaskbar)
				{
					g_bApplyTaskbar = FALSE;
					InterlockedExchange(&g_refreshDispatchQueued, 1);
				}
				PostMessage(hDwnd, WM_TCLOCK_APPLY_REFRESH, 0, 0);
				InterlockedExchange(&g_inApplyDispatch, 0);
			}
			if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			{
				startpage = nowDlg;

				// reload language dll
				if(g_bApplyLangDLL)
				{
					char fname[MAX_PATH];
					HINSTANCE hInst;
					hInst = LoadLanguageDLL(fname);
					if(hInst != NULL)
					{
						if(g_hMenu) DestroyMenu(g_hMenu);
						g_hMenu = NULL;
						if(g_hInstResource) FreeLibrary(g_hInstResource);
						g_hInstResource = hInst;
						strcpy(g_langdllname, fname);
					}
				}
				for (i = 0; i < MAX_PAGE; i++) {
					bDlgFlg[i] = FALSE;
				}
				DestroyWindow(hDwnd);
				g_hwndPropDlg = NULL;
			}
			// HELP
			if(LOWORD(wParam) == ID_HELP){
				char fname[1024];
				strcpy(fname, g_mydir);
				if (b_EnglishMenu) {
					add_title(fname, "readme_en.txt");
				}
				else {
					add_title(fname, "readme_jp.txt");
				}
				ShellExecuteUtf8Strict(NULL, "open", "notepad.exe", fname, NULL, SW_SHOWNORMAL);
			}
			InterlockedDecrement(&g_propdlgCommandDepth);
			}
			break;

		case WM_TCLOCK_APPLY_REFRESH:
			if (InterlockedExchange(&g_refreshDispatchQueued, 0) != 0) {
				PostMessage(g_hwndClock, CLOCKM_REFRESHCLOCK, 0, 0);
				PostMessage(g_hwndClock, CLOCKM_REFRESHTASKBAR, 0, 0);
			}
			break;

		// close by "x" button
		case WM_SYSCOMMAND:
			if((wParam & 0xfff0) == SC_CLOSE)
				PostMessage(hDwnd, WM_COMMAND, IDCANCEL, 0);
			break;
	}

	return 0;
}



/*------------------------------------------------
   adjust the window position
--------------------------------------------------*/
void SetMyDialgPos(HWND hwnd)
{
	HWND hwndTray;
	RECT rc, rcTray;
	int wscreen, hscreen, wProp, hProp;
	int x, y;

	GetWindowRect(hwnd, &rc);
	wProp = rc.right - rc.left;
	hProp = rc.bottom - rc.top;

	wscreen = GetSystemMetrics(SM_CXSCREEN);
	hscreen = GetSystemMetrics(SM_CYSCREEN);

	hwndTray = FindWindowW(L"Shell_TrayWnd", NULL);
	if(hwndTray == NULL) return;
	GetWindowRect(hwndTray, &rcTray);
	if(rcTray.right - rcTray.left >
		rcTray.bottom - rcTray.top)
	{
		x = wscreen - wProp - 32;
		if(rcTray.top < hscreen / 2)
			y = rcTray.bottom + 2;
		else
			y = rcTray.top - hProp - 32;
		if(y < 0) y = 0;
	}
	else
	{
		y = hscreen - hProp - 2;
		if(rcTray.left < wscreen / 2)
			x = rcTray.right + 2;
		else
			x = rcTray.left - wProp - 2;
		if(x < 0) x = 0;
	}

	MoveWindow(hwnd, x, y, wProp, hProp, FALSE);
}

/*------------------------------------------------
   select file (UTF-8 contract)
--------------------------------------------------*/
/* Dialog input is handled as UTF-8 and converted to UTF-16. */
static int DecodeUtf8ForDialog(const char* src, wchar_t* dst, int dstCch)
{
	int r;
	if (!src || !dst || dstCch <= 0) return 0;
	r = tc_utf8_to_utf16(src, dst, dstCch);
	if (r <= 0) dst[0] = L'\0';
	return r;
}

static BOOL Utf8MultiSzToWide(const char* src, wchar_t* dst, int dstCch)
{
	const char* p;
	int pos = 0;
	wchar_t wseg[512];
	if (!src || !dst || dstCch <= 0) return FALSE;
	p = src;
	for (;;)
	{
		if (*p == '\0') {
			if (pos >= dstCch) return FALSE;
			dst[pos++] = L'\0';
			break;
		}
		if (tc_utf8_to_utf16(p, wseg, (int)(sizeof(wseg) / sizeof(wseg[0]))) <= 0) return FALSE;
		{
			int wlen = lstrlenW(wseg);
			if (pos + wlen + 1 > dstCch) return FALSE;
			memcpy(dst + pos, wseg, (size_t)(wlen + 1) * sizeof(wchar_t));
			pos += wlen + 1;
		}
		p += lstrlen(p) + 1;
	}
	return TRUE;
}


static UINT BuildFileDialogFilterSpecs(const wchar_t* wmulti, COMDLG_FILTERSPEC* specs, UINT maxSpecs)
{
	const wchar_t* p;
	UINT n = 0;
	if (!wmulti || !specs || maxSpecs == 0) return 0;
	p = wmulti;
	while (*p && n < maxSpecs) {
		const wchar_t* name = p;
		p += lstrlenW(p) + 1;
		if (!*p) break;
		specs[n].pszName = name;
		specs[n].pszSpec = p;
		n++;
		p += lstrlenW(p) + 1;
	}
	return n;
}

static void TrySetDialogFolderFromPath(IFileDialog* pfd, const wchar_t* initpath, BOOL pickFolder)
{
	typedef HRESULT(WINAPI* PFN_SHCreateItemFromParsingName)(PCWSTR, IBindCtx*, REFIID, void**);
	PFN_SHCreateItemFromParsingName pfnCreateItem = NULL;
	wchar_t folder[MAX_PATH];
	int i, lastSep = -1;
	IShellItem* psiFolder = NULL;
	HRESULT hr;

	if (!pfd || !initpath || !initpath[0]) return;

	lstrcpynW(folder, initpath, (int)(sizeof(folder) / sizeof(folder[0])));
	if (!pickFolder) {
		for (i = 0; folder[i] != L'\0'; ++i) {
			if (folder[i] == L'\\' || folder[i] == L'/') lastSep = i;
		}
		if (lastSep < 0) return;
		folder[lastSep] = L'\0';
		if (!folder[0]) return;
	}

	pfnCreateItem = (PFN_SHCreateItemFromParsingName)GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHCreateItemFromParsingName");
	if (!pfnCreateItem) return;

	hr = pfnCreateItem(folder, NULL, &IID_IShellItem, (void**)&psiFolder);
	if (FAILED(hr) || !psiFolder) return;

	pfd->lpVtbl->SetDefaultFolder(pfd, psiFolder);
	pfd->lpVtbl->SetFolder(pfd, psiFolder);
	psiFolder->lpVtbl->Release(psiFolder);
}

BOOL IsDialogCanceledHr(HRESULT hr)
{
	return (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED));
}

HRESULT SelectPathUTF8Modern(HWND hDlg, BOOL pickFolder, const wchar_t* initdir, const wchar_t* wfilter, DWORD nFilterIndex, char* outUtf8, int outUtf8Bytes)
{
	IFileDialog* pfd = NULL;
	IShellItem* psiResult = NULL;
	PWSTR pwszPath = NULL;
	COMDLG_FILTERSPEC specs[16];
	UINT specCount = 0;
	DWORD opts = 0;
	HRESULT hr;

	hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void**)&pfd);
	if (FAILED(hr) || !pfd) return hr;

	if (SUCCEEDED(pfd->lpVtbl->GetOptions(pfd, &opts))) {
		opts |= (FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
		if (pickFolder) opts |= FOS_PICKFOLDERS;
		else opts |= FOS_FILEMUSTEXIST;
		pfd->lpVtbl->SetOptions(pfd, opts);
	}
	TrySetDialogFolderFromPath(pfd, initdir, pickFolder);
	if (!pickFolder && initdir && initdir[0]) {
		pfd->lpVtbl->SetFileName(pfd, initdir);
	}
	if (!pickFolder && wfilter && wfilter[0]) {
		specCount = BuildFileDialogFilterSpecs(wfilter, specs, (UINT)(sizeof(specs) / sizeof(specs[0])));
		if (specCount > 0) {
			pfd->lpVtbl->SetFileTypes(pfd, specCount, specs);
			pfd->lpVtbl->SetFileTypeIndex(pfd, nFilterIndex ? nFilterIndex : 1);
		}
	}
	hr = pfd->lpVtbl->Show(pfd, hDlg);
	if (FAILED(hr)) goto done;
	hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
	if (FAILED(hr) || !psiResult) goto done;
	hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &pwszPath);
	if (FAILED(hr) || !pwszPath) goto done;
	if (tc_utf16_to_utf8(pwszPath, outUtf8, outUtf8Bytes) <= 0) hr = E_FAIL;
	else hr = S_OK;

done:
	if (pwszPath) CoTaskMemFree(pwszPath);
	if (psiResult) psiResult->lpVtbl->Release(psiResult);
	if (pfd) pfd->lpVtbl->Release(pfd);
	return hr;
}

BOOL SelectMyFileUTF8(HWND hDlg, const char *filterUtf8, DWORD nFilterIndex,
	const char *deffileUtf8, char *retfileUtf8, int retfileUtf8Bytes)
{
	wchar_t initdir[MAX_PATH], wfilter[1024], wdeffile[MAX_PATH];
	const wchar_t* initForDialog = NULL;
	HRESULT hrModern;

	if (!retfileUtf8 || retfileUtf8Bytes <= 0) return FALSE;
	retfileUtf8[0] = '\0';

	if (DecodeUtf8ForDialog(g_mydir, initdir, (int)(sizeof(initdir) / sizeof(initdir[0]))) <= 0) {
		initdir[0] = L'\0';
	}
	if (deffileUtf8 && deffileUtf8[0] &&
		DecodeUtf8ForDialog(deffileUtf8, wdeffile, (int)(sizeof(wdeffile) / sizeof(wdeffile[0]))) > 0)
	{
		WIN32_FIND_DATAW fd;
		HANDLE hfind = FindFirstFileW(wdeffile, &fd);
		if (hfind != INVALID_HANDLE_VALUE)
		{
			int i;
			int lastSep = -1;
			FindClose(hfind);
			lstrcpynW(initdir, wdeffile, (int)(sizeof(initdir) / sizeof(initdir[0])));
			for (i = 0; initdir[i] != L'\0'; ++i) {
				if (initdir[i] == L'\\' || initdir[i] == L'/') lastSep = i;
			}
			if (lastSep >= 0) initdir[lastSep] = L'\0';
		}
		initForDialog = wdeffile;
	}
	if (!initForDialog && initdir[0]) initForDialog = initdir;

	if (!Utf8MultiSzToWide(filterUtf8 ? filterUtf8 : "", wfilter, (int)(sizeof(wfilter) / sizeof(wfilter[0])))) {
		return FALSE;
	}

	hrModern = SelectPathUTF8Modern(hDlg, FALSE, initForDialog, wfilter, nFilterIndex, retfileUtf8, retfileUtf8Bytes);
	if (SUCCEEDED(hrModern)) return TRUE;
	return FALSE;
}


