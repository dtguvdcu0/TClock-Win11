/*-------------------------------------------
  pagedataplan.c
     「データ利用状況設定」
     by TTTT
---------------------------------------------*/

#include "tclock.h"
#include "..\common\text_codec.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static void LoadRegUtf8AndSetStrict(HWND hDlg, int id, const char* section, const char* entry, const char* defval);

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern char g_mydir[];

extern BOOL b_EnglishMenu;
extern int Language_Offset;


/*------------------------------------------------
　「バージョン情報」ページ用ダイアログプロシージャ
--------------------------------------------------*/

INT_PTR CALLBACK PageKeywordProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
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
			switch (id)
			{
			}
			SendPSChanged(hDlg);
			return TRUE;
		}
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
	LoadRegUtf8AndSetStrict(hDlg, IDC_ETHERNET_KEYWORD1, "ETC", "Ethernet_Keyword1", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_ETHERNET_KEYWORD2, "ETC", "Ethernet_Keyword2", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_ETHERNET_KEYWORD3, "ETC", "Ethernet_Keyword3", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_ETHERNET_KEYWORD4, "ETC", "Ethernet_Keyword4", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_ETHERNET_KEYWORD5, "ETC", "Ethernet_Keyword5", "");

	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_KEYWORD1, "VPN", "VPN_Keyword1", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_KEYWORD2, "VPN", "VPN_Keyword2", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_KEYWORD3, "VPN", "VPN_Keyword3", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_KEYWORD4, "VPN", "VPN_Keyword4", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_KEYWORD5, "VPN", "VPN_Keyword5", "");

	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_EXCLUDE1, "VPN", "VPN_Exclude1", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_EXCLUDE2, "VPN", "VPN_Exclude2", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_EXCLUDE3, "VPN", "VPN_Exclude3", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_EXCLUDE4, "VPN", "VPN_Exclude4", "");
	LoadRegUtf8AndSetStrict(hDlg, IDC_VPN_EXCLUDE5, "VPN", "VPN_Exclude5", "");
}

static void LoadRegUtf8AndSetStrict(HWND hDlg, int id, const char* section, const char* entry, const char* defval)
{
	char str[32];
	char before[32];
	WCHAR wbuf[64];

	GetMyRegStr((char*)section, (char*)entry, str, (int)sizeof(str), (char*)defval);
	lstrcpyn(before, str, (int)sizeof(before));
	if (tc_utf8_to_utf16(str, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) {
		return;
	}
	SetDlgItemTextUTF8Strict(hDlg, id, str);
}

/*------------------------------------------------
  "Apply" button
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	char str[32];

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD1, str, 32);
	SetMyRegStr("ETC", "Ethernet_Keyword1", str);

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD2, str, 32);
	SetMyRegStr("ETC", "Ethernet_Keyword2", str);

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD3, str, 32);
	SetMyRegStr("ETC", "Ethernet_Keyword3", str);

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD4, str, 32);
	SetMyRegStr("ETC", "Ethernet_Keyword4", str);

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD5, str, 32);
	SetMyRegStr("ETC", "Ethernet_Keyword5", str);


	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD1, str, 32);
	SetMyRegStr("VPN", "VPN_Keyword1", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD2, str, 32);
	SetMyRegStr("VPN", "VPN_Keyword2", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD3, str, 32);
	SetMyRegStr("VPN", "VPN_Keyword3", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD4, str, 32);
	SetMyRegStr("VPN", "VPN_Keyword4", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD5, str, 32);
	SetMyRegStr("VPN", "VPN_Keyword5", str);


	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE1, str, 32);
	SetMyRegStr("VPN", "VPN_Exclude1", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE2, str, 32);
	SetMyRegStr("VPN", "VPN_Exclude2", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE3, str, 32);
	SetMyRegStr("VPN", "VPN_Exclude3", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE4, str, 32);
	SetMyRegStr("VPN", "VPN_Exclude4", str);

	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE5, str, 32);
	SetMyRegStr("VPN", "VPN_Exclude5", str);
}

