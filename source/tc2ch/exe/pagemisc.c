/*-------------------------------------------
  pagemisc.c
　　「その他」プロパティページ
　　KAZUBON 1997-1998
---------------------------------------------*/
#include "tclock.h"
#include "../common/text_codec.h"
#include "../version.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);


static void OnStartup(HWND hDlg);
BOOL CreateLink(LPCSTR fname, LPCSTR dstpath, LPCSTR name);
static BOOL Utf8ToWideStrict(const char* src, wchar_t* dst, int dstCch);

#define SendPSChanged(hDlg) SendMessage(GetParent(hDlg),PSM_CHANGED,(WPARAM)(hDlg),0)

extern BOOL b_EnglishMenu;
extern int Language_Offset;

/*------------------------------------------------
　「その他」ページ用ダイアログプロシージャ
--------------------------------------------------*/
BOOL CALLBACK PageMiscProc(HWND hDlg, UINT message,
	WPARAM wParam, LPARAM lParam)
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
			// 「スタートアップ」にショートカットをつくる
			case IDC_STARTUP:
				OnStartup(hDlg);
				break;
			case IDC_DELREG:
				{
					int r;
					r = DelRegAll();
					if (r == 0)
						MyMessageBoxW(hDlg, MyStringW(IDS_DELREGNGINI), L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
					else if (r == 1)
						MyMessageBoxW(hDlg, MyStringW(IDS_DELREGOK), L"TClock-Win11", MB_OK, MB_ICONINFORMATION);
					else
						MyMessageBoxW(hDlg, MyStringW(IDS_DELREGNG), L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
				}
				break;
				//  readme.txtを開く
			case IDC_README1:
				My2chHelp(GetParent(hDlg));
				break;
			}
			return TRUE;
		}
		case WM_NOTIFY:
			switch(((NMHDR *)lParam)->code)
			{
				case PSN_APPLY: OnApply(hDlg); break;
				case PSN_HELP: My2chHelp(GetParent(hDlg)); break;
			}
			return TRUE;
	}
	return FALSE;
}

/*------------------------------------------------
　ページの初期化
--------------------------------------------------*/
void OnInit(HWND hDlg)
{
	char s[256];

	SendDlgItemMessage(hDlg, IDC_ABOUTICON, STM_SETIMAGE,
		IMAGE_ICON, (LPARAM)g_hIconTClock);

	wsprintf(s, "TClock-Win11 Ver %s", TCLOCK_VER_PRODUCT_STR);

	SendDlgItemMessage(hDlg, IDC_STATIC_VersionText, WM_SETTEXT, 0, (LPARAM)s);

}

/*------------------------------------------------
　更新
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	UNREFERENCED_PARAMETER(hDlg);
}

/*------------------------------------------------
　「スタートアップ」にショートカットをつくる
--------------------------------------------------*/
void OnStartup(HWND hDlg)
{
	LPITEMIDLIST pidl;
	WCHAR wdstpath[MAX_PATH];
	char dstpath[MAX_PATH], myexe[MAX_PATH];

	if(SHGetSpecialFolderLocation(hDlg, CSIDL_STARTUP, &pidl) != NOERROR)
		return;
	if(SHGetPathFromIDListW(pidl, wdstpath) != TRUE)
		return;
	if(tc_utf16_to_utf8(wdstpath, dstpath, (int)sizeof(dstpath)) <= 0)
		return;

	if(MyMessageBoxW(hDlg, MyStringW(IDS_STARTUPLINK),
		L"TClock-Win11", MB_YESNO, MB_ICONQUESTION) != IDYES) return;

	GetModuleFileNameUTF8(GetModuleHandle(NULL), myexe, MAX_PATH);
	CreateLink(myexe, dstpath, "TClock-Win11");
}

/*------------------------------------------------
　ショートカットの作成
--------------------------------------------------*/
BOOL CreateLink(LPCSTR fname, LPCSTR dstpath, LPCSTR name)
{
	HRESULT hres;
	IShellLinkW* psl;

	CoInitialize(NULL);

	hres = CoCreateInstance(&CLSID_ShellLink, NULL,
		CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (LPVOID*)&psl);
	if(SUCCEEDED(hres))
	{
		IPersistFile* ppf;
		char path[MAX_PATH];
		char lnkfile[MAX_PATH];
		WCHAR wfname[MAX_PATH], wpath[MAX_PATH], wname[MAX_PATH], wlnkfile[MAX_PATH];

		if (!Utf8ToWideStrict(fname, wfname, (int)(sizeof(wfname) / sizeof(wfname[0]))) ||
			!Utf8ToWideStrict(name, wname, (int)(sizeof(wname) / sizeof(wname[0])))) {
			psl->lpVtbl->Release(psl);
			CoUninitialize();
			return FALSE;
		}

		strcpy(path, fname);
		del_title(path);
		if (!Utf8ToWideStrict(path, wpath, (int)(sizeof(wpath) / sizeof(wpath[0])))) {
			psl->lpVtbl->Release(psl);
			CoUninitialize();
			return FALSE;
		}

		psl->lpVtbl->SetPath(psl, wfname);
		psl->lpVtbl->SetDescription(psl, wname);
		psl->lpVtbl->SetWorkingDirectory(psl, wpath);

		hres = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile,
			(LPVOID*)&ppf);

		if(SUCCEEDED(hres))
		{
			strcpy(lnkfile, dstpath);
			add_title(lnkfile, (char*)name);
			strcat(lnkfile, ".lnk");

			if(!Utf8ToWideStrict(lnkfile, wlnkfile, (int)(sizeof(wlnkfile) / sizeof(wlnkfile[0]))))
			{
				ppf->lpVtbl->Release(ppf);
				psl->lpVtbl->Release(psl);
				CoUninitialize();
				return FALSE;
			}

			hres = ppf->lpVtbl->Save(ppf, wlnkfile, TRUE);
			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
	CoUninitialize();

	if(SUCCEEDED(hres)) return TRUE;
	else return FALSE;
}

static BOOL Utf8ToWideStrict(const char* src, wchar_t* dst, int dstCch)
{
	if (!src || !dst || dstCch <= 0) return FALSE;
	return tc_utf8_to_utf16(src, dst, dstCch) > 0;
}
