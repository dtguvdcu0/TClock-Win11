/*-------------------------------------------
  propdlg.c
  show "properties for TClock"
---------------------------------------------*/

//#define NONAMELESSUNION
#include "tclock.h"
#include "..\common\text_codec.h"
#include <shobjidl.h>
#include <stdio.h>

#include "../common/analog_skin.h"
#include "../common/chime_schedule.h"
#include "../common/ini_io_utf8.h"
#include "plugin_runtime.h"
#include "../common/page_api.h"
#define PL_PLUGIN_FIRST 22
#define PL_PLUGIN_TREE PRT_PAGE_TREE_BASE
#define PL_RETRY_ROW (-2)
#define WM_TCLOCK_PLUGIN_SYNC (WM_APP + 73)
/* Page UI slots stay stable while callbacks and tree notifications are active. */
#define MAX_PAGE (PL_PLUGIN_FIRST + pl_slot_count)
typedef struct PL_SLOT { HWND window; BOOL created; HTREEITEM child; ULONGLONG token; } PL_SLOT;
#define PL_SLOT_BLOCK 32
static PL_SLOT** pl_blocks;
static int pl_slot_count, pl_block_count;
static HTREEITEM pl_error_row;
static BOOL pl_syncing, pl_title_warning, pl_deferred_failed;
static ULONGLONG dialogGeneration;
static WCHAR pl_title[512];
extern BOOL b_EnglishMenu;
static void pl_warn_title(HWND dialog) {
    if (SetWindowTextW(dialog, b_EnglishMenu ? L"Some plugin pages unavailable - reopen settings to retry" :
        L"\u4e00\u90e8\u306e\u30d7\u30e9\u30b0\u30a4\u30f3\u30da\u30fc\u30b8\u3092\u8868\u793a\u3067\u304d\u307e\u305b\u3093 - \u8a2d\u5b9a\u3092\u958b\u304d\u76f4\u3057\u3066\u518d\u8a66\u884c")) pl_title_warning = TRUE;
}
static PL_SLOT* pl_slot(int index) { return &pl_blocks[index / PL_SLOT_BLOCK][index % PL_SLOT_BLOCK]; }
static BOOL pl_reserve(int count) {
    while (pl_slot_count < count) {
        int block = pl_slot_count / PL_SLOT_BLOCK;
        if (block >= pl_block_count) {
            PL_SLOT** grown; SIZE_T bytes = ((SIZE_T)block + 1) * sizeof(*grown);
            if (pl_blocks) grown = (PL_SLOT**)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pl_blocks, bytes);
            else grown = (PL_SLOT**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
            if (!grown) return FALSE; pl_blocks = grown; pl_block_count = block + 1;
        }
        if (!pl_blocks[block]) {
            pl_blocks[block] = (PL_SLOT*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PL_SLOT_BLOCK * sizeof(PL_SLOT));
            if (!pl_blocks[block]) return FALSE;
        }
        ++pl_slot_count;
    }
    return TRUE;
}
static HWND* pl_window(HWND* builtins, int index) {return index < PL_PLUGIN_FIRST ? &builtins[index] : &pl_slot(index - PL_PLUGIN_FIRST)->window;}
static BOOL* pl_created(BOOL* builtins, int index) {return index < PL_PLUGIN_FIRST ? &builtins[index] : &pl_slot(index - PL_PLUGIN_FIRST)->created;}
static HTREEITEM pl_child(HTREEITEM* builtins, int index) {return index < PL_PLUGIN_FIRST ? builtins[index] : pl_slot(index - PL_PLUGIN_FIRST)->child;}
#include "plugin_registry.h"
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
INT_PTR CALLBACK PageExtendedProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageChimeProc(HWND, UINT, WPARAM, LPARAM);

INT_PTR CALLBACK PageBarmeterProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageEtcProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageKeywordProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageAppControlProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageEtc1Proc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageWin11Proc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageCustomVarsProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK PageRClickMenuProc(HWND, UINT, WPARAM, LPARAM);




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
static LONG g_propdlgCancelPending = 0;
static LONG g_refreshDispatchQueued = 0;
static LONG g_propdlgCommandDepth = 0;
static LONG g_propdlgNotifyDepth = 0;

// menu.c
extern HMENU g_hMenu;

extern BOOL b_NormalLog;
extern int Language_Offset;

#include "property_layout.h"

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

static BOOL pl_begin_read(void)
{
    WCHAR path[MAX_PATH];
    return tc_utf8_to_utf16(g_inifile, path, MAX_PATH) > 0 && tc_ini_beginW(path);
}

static VOID CreatePageDialog(HWND hParent, HWND hDlg[], BOOL bDlgFlg[], int index, int wID, DLGPROC dlgprc)
{
	HINSTANCE hInst;
    BOOL readScope;

	if (bDlgFlg[index]) {
		return;
	}
    readScope = pl_begin_read();
	hInst   = GetLangModule();
	hDlg[index] = CreateDialog(hInst, MAKEINTRESOURCE((WORD)wID), hParent, dlgprc);
    if (readScope) tc_ini_end();
	SetPageDlgPos(hParent, hDlg[index]);

	bDlgFlg[index] = hDlg[index] != NULL;
}

/* Settings entry substitution (tclock.page/1). The Chime entry's owner is resolved when it
   is first opened in a dialog session and kept until the dialog closes, so a selection
   change never replaces a page that may hold edits. An unavailable selection keeps the
   built-in page and says so on the tree item; the stored selection itself is kept. */
static int pl_chime_page = -1;
static HWND pl_open_chime(HWND parent, HWND hDlg[], BOOL bDlgFlg[], HWND tree, HTREEITEM item)
{
    char raw[TC_PLUGIN_ID_CHARS * 3];
    WCHAR owner[TC_PLUGIN_ID_CHARS] = L"";
    if (bDlgFlg[7]) return NULL;
    GetMyRegStr("Plugins", "Page.chime", raw, sizeof(raw), "");
    if (!MultiByteToWideChar(CP_UTF8, 0, raw, -1, owner, TC_PLUGIN_ID_CHARS)) owner[0] = 0;
    pl_chime_page = owner[0] ? prt_find_substitute(owner, TC_PAGE_TARGET_CHIME) : -1;
    if (pl_chime_page >= 0) {
        HWND page = prt_create_page(pl_chime_page, parent, b_EnglishMenu);
        if (page) return page;
        pl_chime_page = -1;
    }
    if (owner[0] && item) {
        WCHAR label[128];
        TVITEMW entry = { 0 };
        swprintf_s(label, 128, b_EnglishMenu ? L"%ls (plugin page unavailable)" : L"%ls (\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u30da\u30fc\u30b8\u306f\u5229\u7528\u3067\u304d\u307e\u305b\u3093)", MyStringW(IDS_PROP_CHIME));
        entry.mask = TVIF_TEXT;entry.hItem = item;entry.pszText = label;
        SendMessageW(tree, TVM_SETITEMW, 0, (LPARAM)&entry);
    }
    CreatePageDialog(parent, hDlg, bDlgFlg, 7, GetSafeLanguageOffset() + IDD_PAGECHIME, PageChimeProc);
    return NULL;
}

static VOID CreatePageDialogW(HWND hParent, HWND hDlg[], BOOL bDlgFlg[], int index, int wID, DLGPROC dlgprc)
{
	HINSTANCE hInst;
    BOOL readScope;

	if (bDlgFlg[index]) {
		return;
	}
    readScope = pl_begin_read();
	hInst = GetLangModule();
	hDlg[index] = CreateDialogW(hInst, MAKEINTRESOURCEW((WORD)wID), hParent, dlgprc);
    if (readScope) tc_ini_end();
	SetPageDlgPos(hParent, hDlg[index]);

	bDlgFlg[index] = hDlg[index] != NULL;
}

static void pl_sync_extensions(HWND tree, HTREEITEM children[])
{
    int i, count = prt_page_count(); BOOL complete; HTREEITEM after = children[0];
    ULONGLONG generation = dialogGeneration;
#define PL_SYNC_CHECK() do {if (generation != dialogGeneration || !IsWindow(tree)) {if (generation == dialogGeneration) pl_syncing = FALSE;return;}} while (0)
    if (pl_syncing) {if (!PostMessageW(GetParent(tree), WM_TCLOCK_PLUGIN_SYNC, 0, 0)) {pl_deferred_failed = TRUE;pl_warn_title(GetParent(tree));}return;}
    pl_syncing = TRUE;pl_deferred_failed = FALSE;complete = pl_reserve(count);
    for (i = 0; i < pl_slot_count; ++i) {
        /* Substitution pages appear only in place of their built-in entry. */
        PL_SLOT* slot = pl_slot(i); LPCWSTR label = prt_is_substitute(i) ? NULL : prt_page_label(i, b_EnglishMenu);
        ULONGLONG token = label ? prt_page_token(i) : 0;
        if (label && label[0]) {
            if (!slot->child) {
                TVINSERTSTRUCTW item = { 0 };
                item.hParent = TreeView_GetParent(tree, children[0]);PL_SYNC_CHECK();item.hInsertAfter = after;
                item.item.mask = TVIF_TEXT | TVIF_PARAM;item.item.pszText = (LPWSTR)label;item.item.lParam = PL_PLUGIN_TREE+i;
                {HTREEITEM inserted = (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&item);
                PL_SYNC_CHECK();slot->child = inserted;}
                if (slot->child) slot->token = token;else complete = FALSE;
            } else {
                TVITEMW item = { 0 };
                item.mask = TVIF_TEXT;item.hItem = slot->child;item.pszText = (LPWSTR)label;
                {BOOL updated = (BOOL)SendMessageW(tree, TVM_SETITEMW, 0, (LPARAM)&item);PL_SYNC_CHECK();if (updated) slot->token = token;else complete = FALSE;}
            }
            if (slot->child) after = slot->child;
        } else if (slot->child) {
            if (prt_owns_page(i, slot->window)) after = slot->child;
            else {BOOL removed = TreeView_DeleteItem(tree, slot->child);PL_SYNC_CHECK();
                if (removed) {slot->child = NULL;slot->token = 0;}else {after = slot->child;complete = FALSE;}}
        }
    }
    if (pl_deferred_failed) complete = FALSE;
    if (!complete && !pl_error_row) {
        TVINSERTSTRUCTW item = { 0 };
        item.hParent = TreeView_GetParent(tree, children[0]);PL_SYNC_CHECK();item.hInsertAfter = after;
        item.item.mask = TVIF_TEXT | TVIF_PARAM;item.item.lParam = PL_RETRY_ROW;
        item.item.pszText = b_EnglishMenu ? L"Some plugin pages are unavailable. Select to retry." : L"\u4e00\u90e8\u306e\u30d7\u30e9\u30b0\u30a4\u30f3\u30da\u30fc\u30b8\u3092\u8868\u793a\u3067\u304d\u307e\u305b\u3093\u3002\u9078\u629e\u3057\u3066\u518d\u8a66\u884c\u3002";
        {HTREEITEM inserted = (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&item);PL_SYNC_CHECK();pl_error_row = inserted;}
        if (!pl_error_row) pl_warn_title(GetParent(tree));
    } else if (complete && pl_error_row) {BOOL removed = TreeView_DeleteItem(tree, pl_error_row);PL_SYNC_CHECK();if (removed) pl_error_row = NULL;}
    if (complete && pl_title_warning && SetWindowTextW(GetParent(tree), pl_title)) pl_title_warning = FALSE;
    pl_syncing = FALSE;
#undef PL_SYNC_CHECK
}

static VOID AdjustPropertyTreeHeight(HWND hTree)
{
	LRESULT itemHeight;
	int targetHeight;

	if (!hTree) {
		return;
	}

	itemHeight = SendMessageW(hTree, TVM_GETITEMHEIGHT, 0, 0);
	if (itemHeight <= 0) {
		return;
	}

	targetHeight = (int)itemHeight - 2;
	if (targetHeight < 16) {
		targetHeight = 16;
	}
	if (targetHeight != (int)itemHeight) {
		SendMessageW(hTree, TVM_SETITEMHEIGHT, (WPARAM)targetHeight, 0);
	}
}

/*-------------------------------------------
  Property dialog
---------------------------------------------*/
INT_PTR CALLBACK PropertyDialog(HWND hDwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HWND hTree;
	static HWND hDlg[PL_PLUGIN_FIRST];
	static BOOL bDlgFlg[PL_PLUGIN_FIRST];
	static HWND *hNowDlg;
	static HTREEITEM hParent[PL_PLUGIN_FIRST], hChild[PL_PLUGIN_FIRST];
	NM_TREEVIEW *pNMTV;
//	HINSTANCE hInst;
	static int nowDlg;
	int i;

	switch(message){
		case WM_INITDIALOG:
            ++dialogGeneration;
            SetWindowLongPtrW(hDwnd, GWL_STYLE,
                GetWindowLongPtrW(hDwnd, GWL_STYLE) | WS_CLIPCHILDREN);
			InitCommonControls();

			hTree = GetDlgItem(hDwnd, IDC_TREE);
			AdjustPropertyTreeHeight(hTree);
            ZeroMemory(hDlg, sizeof(hDlg));ZeroMemory(bDlgFlg, sizeof(bDlgFlg));
            ZeroMemory(hParent, sizeof(hParent));ZeroMemory(hChild, sizeof(hChild));
            for (i = 0; i < pl_slot_count; ++i) ZeroMemory(pl_slot(i), sizeof(PL_SLOT));
            pl_error_row = NULL;pl_syncing = FALSE;pl_deferred_failed = FALSE;pl_title_warning = FALSE;pl_chime_page = -1;
            GetWindowTextW(hDwnd, pl_title, (int)(sizeof(pl_title)/sizeof(pl_title[0])));
            prt_refresh();
            pl_tree(hTree, hParent, hChild);
            pl_sync_extensions(hTree, hChild);

			CreatePageDialog(hDwnd, hDlg, bDlgFlg, 0, GetSafeLanguageOffset() + IDD_PAGECOLOR, PageColorProc);
			nowDlg = startpage;
            if (nowDlg == 4) nowDlg = 20; // Legacy analog navigation is now inside Extended Display.
			if (nowDlg < 0 || nowDlg >= 22 || nowDlg == 9 || nowDlg == 10) nowDlg = 0;
			if (nowDlg == 6) nowDlg = 0; /* Hidden additional color page. */
			if (nowDlg == 13) {
				nowDlg = 14;
			}
			//nowDlg = 0;
			if (nowDlg == 21) CreatePageDialogW(hDwnd, hDlg, bDlgFlg, 21, GetSafeLanguageOffset() + IDD_PAGE_PLUGINS, PagePluginsProc);
			hNowDlg = pl_window(hDlg, nowDlg);
			ShowWindow(*hNowDlg, SW_SHOW);
			UpdateWindow(*hNowDlg);

			if (nowDlg == 21)
				TreeView_SelectItem(hTree, hParent[10]);
			else if (nowDlg == 20)
				TreeView_SelectItem(hTree, hChild[20]);
			else if (nowDlg < 9)
				TreeView_SelectItem(hTree, hChild[nowDlg]);
			else
				TreeView_SelectItem(hTree, hParent[nowDlg - 10]);


			break;

		case WM_SHOWWINDOW: // adjust the window position
			SetMyDialgPos(hDwnd);
			break;

		case WM_NOTIFY:
			{
            ULONGLONG generation = dialogGeneration;
			LONG notifyDepth = InterlockedIncrement(&g_propdlgNotifyDepth);
			if (notifyDepth > 64) {
								InterlockedDecrement(&g_propdlgNotifyDepth);
				break;
			}
			pNMTV = (NM_TREEVIEW *)lParam;
			switch (pNMTV->hdr.code){
                case TVN_SELCHANGINGA:
                case TVN_SELCHANGINGW:
                    if (pNMTV->itemNew.lParam == PL_RETRY_ROW) {
                        if (!PostMessageW(hDwnd, WM_TCLOCK_PLUGIN_SYNC, 0, 0)) pl_warn_title(hDwnd);
                        SetWindowLongPtrW(hDwnd, DWLP_MSGRESULT, TRUE);
                        InterlockedDecrement(&g_propdlgNotifyDepth);return TRUE;
                    }
                    /* A displayed row must still name the registration it was built from. */
                    if (pNMTV->itemNew.lParam >= PL_PLUGIN_TREE && pNMTV->itemNew.lParam < PL_PLUGIN_TREE + pl_slot_count) {
                        int page = (int)pNMTV->itemNew.lParam - PL_PLUGIN_TREE;
                        /* Clear only a cached window no longer owned by this page. */
                        if ((*pl_created(bDlgFlg, PL_PLUGIN_FIRST+page)) && !prt_owns_page(page, (*pl_window(hDlg, PL_PLUGIN_FIRST+page)))) {
                            (*pl_created(bDlgFlg, PL_PLUGIN_FIRST+page)) = FALSE;(*pl_window(hDlg, PL_PLUGIN_FIRST+page)) = NULL;
                        }
                        if (!(*pl_created(bDlgFlg, PL_PLUGIN_FIRST+page)) && (!pl_slot(page)->token || pl_slot(page)->token != prt_page_token(page))) {
                            SetWindowLongPtrW(hDwnd, DWLP_MSGRESULT, TRUE);
                            InterlockedDecrement(&g_propdlgNotifyDepth);
                            return TRUE;
                        }
                    }
                    if (pNMTV->itemNew.lParam < 0) {
                        SetWindowLongPtrW(hDwnd, DWLP_MSGRESULT, TRUE);
                        InterlockedDecrement(&g_propdlgNotifyDepth);
                        return TRUE;
                    }
                    break;
                case TVN_ITEMEXPANDINGA:
                case TVN_ITEMEXPANDINGW:
                    if (pNMTV->action == TVE_COLLAPSE) {
                        SetWindowLongPtrW(hDwnd, DWLP_MSGRESULT, TRUE);
                        InterlockedDecrement(&g_propdlgNotifyDepth);
                        return TRUE;
                    }
                    break;
				case TVN_SELCHANGEDA:
				case TVN_SELCHANGEDW:
                {
                    HWND previous = hNowDlg ? *hNowDlg : NULL;
                    int previousPage = nowDlg;
                    BOOL redraw = IsWindowVisible(hDwnd);
                    if (redraw) SendMessageW(hDwnd, WM_SETREDRAW, FALSE, 0);
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
							CreatePageDialogW(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEMOUSE, PageMouseProc);
							break;

						case 7:
							nowDlg = 17;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_ETC, PageEtcProc);
							break;

						case 8:
							nowDlg = 18;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_CUSTOMVARS, PageCustomVarsProc);
							break;

						case 9:
							nowDlg = 19;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_RCLICK_MENU, PageRClickMenuProc);
							break;

						case 10:
							nowDlg = 21;
							CreatePageDialogW(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGE_PLUGINS, PagePluginsProc);
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
						case 104: // Compatibility route for old analog navigation.
						case 108:
							nowDlg = 20;
							CreatePageDialogW(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEEXTENDED, PageExtendedProc);
							break;

						case 105:
							nowDlg = 5;
							CreatePageDialog(hDwnd, hDlg, bDlgFlg, nowDlg, GetSafeLanguageOffset() + IDD_PAGEBARMETER, PageBarmeterProc);
							break;


						case 107:
							nowDlg = 7;
							{
								HWND created = pl_open_chime(hDwnd, hDlg, bDlgFlg, hTree, hChild[7]);
								/* The plugin callback may have closed or replaced this dialog. */
								if (generation != dialogGeneration || !IsWindow(hDwnd)) {
									InterlockedDecrement(&g_propdlgNotifyDepth);return TRUE;
								}
								if (created) {SetPageDlgPos(hDwnd, created);hDlg[7] = created;bDlgFlg[7] = TRUE;}
							}
							break;

                        default:
                            if (nowDlg >= PL_PLUGIN_TREE && nowDlg < PL_PLUGIN_TREE + pl_slot_count) {
                                int page = nowDlg - PL_PLUGIN_TREE;nowDlg = PL_PLUGIN_FIRST + page;
                                if ((*pl_created(bDlgFlg, nowDlg)) && !prt_owns_page(page, (*pl_window(hDlg, nowDlg)))) {(*pl_created(bDlgFlg, nowDlg)) = FALSE;(*pl_window(hDlg, nowDlg)) = NULL;}
                                if (!(*pl_created(bDlgFlg, nowDlg)) && pl_slot(page)->token && pl_slot(page)->token == prt_page_token(page)) {
                                    int slot = nowDlg;HWND created = prt_create_page(page, hDwnd, b_EnglishMenu);
                                    if (generation != dialogGeneration || !IsWindow(hDwnd)) {
                                        HWND current = g_hwndPropDlg;
                                        if (current && IsWindow(current) && !PostMessageW(current, WM_TCLOCK_PLUGIN_SYNC, 0, 0))
                                            SendMessageW(current, WM_TCLOCK_PLUGIN_SYNC, 0, 0);
                                        InterlockedDecrement(&g_propdlgNotifyDepth);return TRUE;
                                    }
                                    (*pl_window(hDlg, slot)) = created;
                                    if (created) { SetPageDlgPos(hDwnd, created);(*pl_created(bDlgFlg, slot)) = TRUE; }
                                }
                                if (!(*pl_window(hDlg, nowDlg))) nowDlg = 0;
                                break;
                            }
							//nowDlg -= 10;
							nowDlg = 0;
							break;
					}
                    if (!(*pl_window(hDlg, nowDlg))) nowDlg = previous ? previousPage : 0;
					hNowDlg = pl_window(hDlg, nowDlg);
                    if (*hNowDlg) {
                        SetWindowPos(*hNowDlg, HWND_TOP, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        if (previous && previous != *hNowDlg) ShowWindow(previous, SW_HIDE);
                    }
                    if (redraw) {
                        SendMessageW(hDwnd, WM_SETREDRAW, TRUE, 0);
                        RedrawWindow(hDwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
                    }
					break;
                }
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
            /* Keep the dialog alive until an in-flight apply callback returns. */
            if (LOWORD(wParam) == IDCANCEL && g_inApplyDispatch) {
                InterlockedExchange(&g_propdlgCancelPending, 1);
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
					int applyActiveOnly;
					int pageIdx;
					lp.code = PSN_APPLY;
					/* INI switch: [ETC] ApplyActivePageOnly=1 keeps active-page-only apply. */
					applyActiveOnly = GetMyRegLong("ETC", "ApplyActivePageOnly", 0) ? 1 : 0;
					if ((*pl_window(hDlg, 20)) && IsWindow((*pl_window(hDlg, 20))) && (!applyActiveOnly || nowDlg == 20) &&
                        !SendMessageW((*pl_window(hDlg, 20)), ACS_VALIDATE, 0, 0)) {
                        TreeView_SelectItem(hTree, hChild[20]);
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    if (pl_chime_page < 0 && (*pl_window(hDlg, 7)) && IsWindow((*pl_window(hDlg, 7))) && (!applyActiveOnly || nowDlg == 7) &&
                        !SendMessageW((*pl_window(hDlg, 7)), CHM_VALIDATE, 0, 0)) {
                        TreeView_SelectItem(hTree, hChild[7]);
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    /* Commit mouse edits before callbacks or other page writes. */
                    if ((*pl_window(hDlg, 16)) && IsWindow((*pl_window(hDlg, 16))) && (!applyActiveOnly || nowDlg == 16) &&
                        !SendMessageW((*pl_window(hDlg, 16)), MOUSE_COMMIT, 0, 0)) {
                        TreeView_SelectItem(hTree, hParent[6]);
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    /* A substituted Chime page applies through its plugin; a denied save keeps the
                       dialog open with the edits. If the plugin went away, the built-in page
                       is created the next time the entry is opened. */
                    if (pl_chime_page >= 0 && hDlg[7] && (!applyActiveOnly || nowDlg == 7)) {
                        if (!prt_owns_page(pl_chime_page, hDlg[7])) {hDlg[7] = NULL;bDlgFlg[7] = FALSE;pl_chime_page = -1;}
                        else if (!prt_apply_page(pl_chime_page, hDlg[7])) {
                            TreeView_SelectItem(hTree, hChild[7]);
                            InterlockedExchange(&g_inApplyDispatch, 0);
                            InterlockedDecrement(&g_propdlgCommandDepth);
                            break;
                        }
                    }
                    for (pageIdx = PL_PLUGIN_FIRST; pageIdx < MAX_PAGE; ++pageIdx) {
                        if ((*pl_window(hDlg, pageIdx)) && !prt_owns_page(pageIdx - PL_PLUGIN_FIRST, (*pl_window(hDlg, pageIdx)))) {(*pl_window(hDlg, pageIdx)) = NULL;(*pl_created(bDlgFlg, pageIdx)) = FALSE;}
                        if ((*pl_window(hDlg, pageIdx)) && (!applyActiveOnly || nowDlg == pageIdx) &&
                            !prt_apply_page(pageIdx - PL_PLUGIN_FIRST, (*pl_window(hDlg, pageIdx)))) break;
                    }
                    if (pageIdx < MAX_PAGE) {
                        TreeView_SelectItem(hTree, pl_child(hChild, pageIdx));
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    /* Commit the menu first so a failed atomic save cannot close OK or write other pages. */
                    if ((*pl_window(hDlg, 19)) && IsWindow((*pl_window(hDlg, 19))) && (!applyActiveOnly || nowDlg == 19) &&
                        !SendMessageW((*pl_window(hDlg, 19)), RM_COMMIT, 0, 0)) {
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    if ((*pl_window(hDlg, 21)) && IsWindow((*pl_window(hDlg, 21))) && (!applyActiveOnly || nowDlg == 21) &&
                        !SendMessageW((*pl_window(hDlg, 21)), PPG_COMMIT, 0, 0)) {
                        InterlockedExchange(&g_inApplyDispatch, 0);
                        InterlockedDecrement(&g_propdlgCommandDepth);
                        break;
                    }
                    if (nowDlg == 21 || (!applyActiveOnly && (*pl_window(hDlg, 21)) && IsWindow((*pl_window(hDlg, 21))))) pl_sync_extensions(hTree, hChild);
					if (applyActiveOnly) {
						if (nowDlg != 16 && !(nowDlg == 7 && pl_chime_page >= 0) && hNowDlg && *hNowDlg && IsWindow(*hNowDlg)) {
							SendMessage(*hNowDlg, WM_NOTIFY, 0, (LPARAM)&lp);
						}
					}
					else {
						for (pageIdx = 0; pageIdx < MAX_PAGE; ++pageIdx) {
							if (pageIdx != 16 && !(pageIdx == 7 && pl_chime_page >= 0) && (*pl_window(hDlg, pageIdx)) && IsWindow((*pl_window(hDlg, pageIdx)))) {
								SendMessage((*pl_window(hDlg, pageIdx)), WM_NOTIFY, 0, (LPARAM)&lp);
							}
						}
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
				InterlockedExchange(&g_inApplyDispatch, 0);
                if (LOWORD(wParam) == IDOK) {
                    // This dialog is destroyed below: queued messages to it would be lost.
                    if (InterlockedExchange(&g_refreshDispatchQueued, 0) != 0) {
                        PostMessageW(g_hwndClock, CLOCKM_REFRESHCLOCK, 0, 0);
                        PostMessageW(g_hwndClock, CLOCKM_REFRESHTASKBAR, 0, 0);
                    }
                } else {
                    PostMessageW(hDwnd, WM_TCLOCK_APPLY_REFRESH, 0, 0);
                }
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
					(*pl_created(bDlgFlg, i)) = FALSE;
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

        case WM_TCLOCK_PLUGIN_SYNC:
            pl_sync_extensions(hTree, hChild);
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

    if (!g_inApplyDispatch && !g_propdlgCommandDepth &&
        InterlockedExchange(&g_propdlgCancelPending, 0)) {
        if (g_hwndPropDlg == hDwnd && IsWindow(hDwnd))
            SendMessageW(hDwnd, WM_COMMAND, IDCANCEL, 0);
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


