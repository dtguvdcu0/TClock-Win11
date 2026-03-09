/*-------------------------------------------
  pagedataplan.c
     「データ利用状況設定」
     by TTTT
---------------------------------------------*/

#include "tclock.h"
#include "..\common\text_codec.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static void LoadKeywordList(HWND hDlg, int id, const char* section, const char* combined_entry, const char* legacy_prefix);
static void BuildLegacyKeywordList(const char* section, const char* legacy_prefix, char* out, int cch_out);
static void AppendKeyword(char* out, int cch_out, const char* value);
static void ClearLegacyKeywordList(const char* section, const char* legacy_prefix);
static void InitGipProviderCombo(HWND hDlg);
static void UpdateGipControls(HWND hDlg);
static void UpdateGipResult(HWND hDlg);

typedef struct {
	const char* key;
	const char* label;
} GIP_PROVIDER_ITEM;

static const GIP_PROVIDER_ITEM g_gipProviders[] = {
	{ "ipify", "ipify" },
	{ "seeip", "SeeIP" },
	{ "ipinfo", "IPinfo" }
};

static char g_gipProviderIni[64];
static int g_gipCustomIndex = -1;

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
			case IDC_GIP_ENABLE:
				if (code == BN_CLICKED) UpdateGipControls(hDlg);
				break;
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
	LoadKeywordList(hDlg, IDC_ETHERNET_KEYWORD1, "ETC", "EthernetKeywords", "Ethernet_Keyword");
	LoadKeywordList(hDlg, IDC_VPN_KEYWORD1, "VPN", "VPNKeywords", "VPN_Keyword");
	LoadKeywordList(hDlg, IDC_VPN_EXCLUDE1, "VPN", "VPNExcludeKeywords", "VPN_Exclude");
	CheckDlgButton(hDlg, IDC_GIP_ENABLE, GetMyRegLong("ETC", "GipEnabled", 0) ? BST_CHECKED : BST_UNCHECKED);
	{
		UINT hours = (UINT)GetMyRegLong("ETC", "GipRefreshHours", 6);
		if (hours < 1) hours = 1;
		if (hours > 168) hours = 168;
		SetDlgItemInt(hDlg, IDC_GIP_INTERVAL, hours, FALSE);
	}
	SendDlgItemMessage(hDlg, IDC_SPIN_GIP_INTERVAL, UDM_SETRANGE32, 1, 168);
	InitGipProviderCombo(hDlg);
	UpdateGipControls(hDlg);
	UpdateGipResult(hDlg);
}

static void LoadKeywordList(HWND hDlg, int id, const char* section, const char* combined_entry, const char* legacy_prefix)
{
	char str[256];
	WCHAR wbuf[512];

	GetMyRegStr(section, combined_entry, str, (int)sizeof(str), "");
	if (str[0] == '\0') {
		BuildLegacyKeywordList(section, legacy_prefix, str, (int)sizeof(str));
	}
	if (tc_utf8_to_utf16(str, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
	SetDlgItemTextUTF8Strict(hDlg, id, str);
}

static void BuildLegacyKeywordList(const char* section, const char* legacy_prefix, char* out, int cch_out)
{
	int index;
	char entry[64];
	char value[64];

	out[0] = '\0';
	for (index = 1; index <= 5; index++) {
		wsprintf(entry, "%s%d", legacy_prefix, index);
		GetMyRegStr(section, entry, value, (int)sizeof(value), "");
		AppendKeyword(out, cch_out, value);
	}
}

static void AppendKeyword(char* out, int cch_out, const char* value)
{
	int out_len;
	int value_len;

	if (value == NULL || value[0] == '\0') return;

	out_len = (int)strlen(out);
	value_len = (int)strlen(value);
	if (out_len >= cch_out - 1) return;

	if (out_len > 0) {
		if (out_len + 1 >= cch_out - 1) return;
		out[out_len++] = ',';
		out[out_len] = '\0';
	}

	if (out_len + value_len >= cch_out) {
		value_len = cch_out - out_len - 1;
	}
	if (value_len > 0) {
		lstrcpyn(out + out_len, value, value_len + 1);
	}
}

static void ClearLegacyKeywordList(const char* section, const char* legacy_prefix)
{
	int index;
	char entry[64];

	for (index = 1; index <= 5; index++) {
		wsprintf(entry, "%s%d", legacy_prefix, index);
		DelMyReg(section, entry);
	}
}

static void InitGipProviderCombo(HWND hDlg)
{
	int i;
	int sel = -1;

	g_gipCustomIndex = -1;
	g_gipProviderIni[0] = '\0';
	GetMyRegStr("ETC", "GipProvider", g_gipProviderIni, (int)sizeof(g_gipProviderIni), "ipify");
	if (!g_gipProviderIni[0]) lstrcpyn(g_gipProviderIni, "ipify", (int)sizeof(g_gipProviderIni));

	CBResetContent(hDlg, IDC_GIP_PROVIDER);
	for (i = 0; i < (int)(sizeof(g_gipProviders) / sizeof(g_gipProviders[0])); ++i) {
		CBAddString(hDlg, IDC_GIP_PROVIDER, g_gipProviders[i].label);
		if (_stricmp(g_gipProviderIni, g_gipProviders[i].key) == 0) sel = i;
	}
	g_gipCustomIndex = CBAddString(hDlg, IDC_GIP_PROVIDER, "Custom (INI)");
	if (sel < 0) {
		sel = g_gipCustomIndex;
	}
	CBSetCurSel(hDlg, IDC_GIP_PROVIDER, sel);
}

static void UpdateGipControls(HWND hDlg)
{
	BOOL enabled = (IsDlgButtonChecked(hDlg, IDC_GIP_ENABLE) == BST_CHECKED);
	EnableDlgItem(hDlg, IDC_GIP_INTERVAL, enabled);
	EnableDlgItem(hDlg, IDC_SPIN_GIP_INTERVAL, enabled);
	EnableDlgItem(hDlg, IDC_GIP_PROVIDER, enabled);
}

static void UpdateGipResult(HWND hDlg)
{
	char value[128];
	char line[160];

	GetMyRegStr("ETC", "GipLastValue", value, (int)sizeof(value), "N/A");
	if (!value[0]) lstrcpyn(value, "N/A", (int)sizeof(value));
	if (b_EnglishMenu) wsprintf(line, "[GIP] Result: %s", value);
	else wsprintf(line, "[GIP] 実行結果: %s", value);
	SetDlgItemTextUTF8Strict(hDlg, IDC_GIP_RESULT, line);
}

/*------------------------------------------------
  "Apply" button
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	char str[256];
	BOOL translated = FALSE;
	UINT hours;
	int sel;

	GetDlgItemTextUTF8(hDlg, IDC_ETHERNET_KEYWORD1, str, (int)sizeof(str));
	if (str[0]) SetMyRegStr("ETC", "EthernetKeywords", str);
	else DelMyReg("ETC", "EthernetKeywords");
	ClearLegacyKeywordList("ETC", "Ethernet_Keyword");

	GetDlgItemTextUTF8(hDlg, IDC_VPN_KEYWORD1, str, (int)sizeof(str));
	if (str[0]) SetMyRegStr("VPN", "VPNKeywords", str);
	else DelMyReg("VPN", "VPNKeywords");
	ClearLegacyKeywordList("VPN", "VPN_Keyword");

	GetDlgItemTextUTF8(hDlg, IDC_VPN_EXCLUDE1, str, (int)sizeof(str));
	if (str[0]) SetMyRegStr("VPN", "VPNExcludeKeywords", str);
	else DelMyReg("VPN", "VPNExcludeKeywords");
	ClearLegacyKeywordList("VPN", "VPN_Exclude");

	SetMyRegLong("ETC", "GipEnabled", (IsDlgButtonChecked(hDlg, IDC_GIP_ENABLE) == BST_CHECKED) ? 1 : 0);
	hours = GetDlgItemInt(hDlg, IDC_GIP_INTERVAL, &translated, FALSE);
	if (!translated) hours = 6;
	if (hours < 1) hours = 1;
	if (hours > 168) hours = 168;
	SetMyRegLong("ETC", "GipRefreshHours", hours);

	sel = CBGetCurSel(hDlg, IDC_GIP_PROVIDER);
	if (sel >= 0 && sel < (int)(sizeof(g_gipProviders) / sizeof(g_gipProviders[0]))) {
		SetMyRegStr("ETC", "GipProvider", (char*)g_gipProviders[sel].key);
	}
	else if (g_gipCustomIndex >= 0 && sel == g_gipCustomIndex) {
		SetMyRegStr("ETC", "GipProvider", g_gipProviderIni);
	}
	UpdateGipResult(hDlg);
}

