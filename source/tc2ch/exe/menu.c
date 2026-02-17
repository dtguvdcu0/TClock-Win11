/*-------------------------------------------------------------
  menu.c
  pop-up menu on right button click
---------------------------------------------------------------*/

#include "tclock.h"

#define	REMOVE_DRIVE_MENUPOSITION 15

// main.c
HMENU g_hMenu = NULL;
static HMENU hPopupMenu = NULL;




//Added by TTTT
BOOL b_CompactMode_menu;
BOOL b_SafeMode_menu;
//BOOL b_UseDataPlanFunc;
extern BOOL b_DebugLog;
//BOOL b_AutomaticFindWLTEProfile;
BOOL b_UseBarMeterVL;
BOOL b_UseBarMeterBL;
BOOL b_UseBarMeterCU;
BOOL b_BatteryLifeAvailable;




char	driveLetter_Win10[10][20];

extern BOOL b_UnplugDriveAvailable;

void InitializeMenuItems(void);
char stringMenuItem_RemoveDriveHeader[32];
char stringMenuItem_RemoveDriveNoDrive[32];

BOOL b_MenuItems_Initialized = FALSE;


//extern BOOL b_AcceptRisk;

extern BOOL b_NormalLog;
extern BOOL b_SkipHideClockRestore;

static char* SafeMyString(UINT id);

#define TC_MENU_CUSTOM_MAX_ITEMS 64
#define TC_MENU_SECTION "MenuCustom"
#define TC_MENU_DYNAMIC_BASE 46000
#define TC_MENU_DYNAMIC_MAX 128
#define TC_MENU_LABEL_CACHE_MAX (TC_MENU_CUSTOM_MAX_ITEMS + 1)
#define TC_MENU_LIVE_MAX TC_MENU_CUSTOM_MAX_ITEMS

typedef struct {
	UINT id;
	int mode; /* 1: shell, 2: commandline */
	char target[MAX_PATH];
	char args[512];
	char workdir[MAX_PATH];
	int show;
} TC_MENU_DYNAMIC_ENTRY;

static TC_MENU_DYNAMIC_ENTRY g_menuDynamicEntries[TC_MENU_DYNAMIC_MAX];
static int g_menuDynamicCount = 0;
typedef struct {
	UINT cmdId;
	int itemIndex;
	int intervalSec;
	char plainLabel[256];
	char format[256];
} TC_MENU_LIVE_ENTRY;
typedef struct {
	DWORD tick;
	int intervalSec;
	char format[1024];
	char text[256];
	BOOL valid;
} TC_MENU_LABEL_CACHE;
static TC_MENU_LABEL_CACHE g_menuLabelCache[TC_MENU_LABEL_CACHE_MAX];
static TC_MENU_LIVE_ENTRY g_menuLiveEntries[TC_MENU_LIVE_MAX];
static int g_menuLiveCount = 0;
static BOOL g_menuPopupActive = FALSE;
typedef BOOL(WINAPI* PFN_FormatMenuLabel_Win11)(const char* fmt, char* out, int outBytes);
static PFN_FormatMenuLabel_Win11 g_pfnFormatMenuLabel_Win11 = NULL;
static BOOL g_menuFormatApiChecked = FALSE;

static void tc_menu_dynamic_reset(void)
{
	g_menuDynamicCount = 0;
	g_menuLiveCount = 0;
}

static void tc_menu_live_register(UINT cmdId, int itemIndex, int intervalSec, const char* plainLabel, const char* format)
{
	TC_MENU_LIVE_ENTRY* e;
	if (g_menuLiveCount >= TC_MENU_LIVE_MAX) return;
	if (!format || !format[0]) return;
	e = &g_menuLiveEntries[g_menuLiveCount++];
	e->cmdId = cmdId;
	e->itemIndex = itemIndex;
	e->intervalSec = intervalSec;
	lstrcpyn(e->plainLabel, plainLabel ? plainLabel : "", (int)sizeof(e->plainLabel));
	lstrcpyn(e->format, format, (int)sizeof(e->format));
}

static UINT tc_menu_dynamic_register(int mode, const char* target, const char* args, const char* workdir, int show)
{
	TC_MENU_DYNAMIC_ENTRY* e;
	if (g_menuDynamicCount >= TC_MENU_DYNAMIC_MAX) {
		return 0;
	}
	e = &g_menuDynamicEntries[g_menuDynamicCount];
	e->id = (UINT)(TC_MENU_DYNAMIC_BASE + g_menuDynamicCount);
	e->mode = mode;
	lstrcpyn(e->target, target ? target : "", (int)sizeof(e->target));
	lstrcpyn(e->args, args ? args : "", (int)sizeof(e->args));
	lstrcpyn(e->workdir, workdir ? workdir : "", (int)sizeof(e->workdir));
	e->show = show;
	++g_menuDynamicCount;
	return e->id;
}

static BOOL tc_menu_dynamic_execute(UINT id)
{
	int i;
	for (i = 0; i < g_menuDynamicCount; ++i) {
		TC_MENU_DYNAMIC_ENTRY* e = &g_menuDynamicEntries[i];
		if (e->id != id) {
			continue;
		}
		if (e->mode == 1) {
			ShellExecuteUtf8Compat(g_hwndMain, "open", e->target[0] ? e->target : NULL,
				e->args[0] ? e->args : NULL,
				e->workdir[0] ? e->workdir : NULL,
				e->show ? e->show : SW_SHOWNORMAL);
			return TRUE;
		}
		if (e->mode == 2) {
			ShellExecuteUtf8Compat(g_hwndMain, "open", "cmd.exe", e->target[0] ? e->target : NULL,
				e->workdir[0] ? e->workdir : NULL,
				e->show ? e->show : SW_SHOWNORMAL);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

static BOOL tc_menu_is_fixed_id(UINT id)
{
	return id == IDC_SHOWPROP || id == IDC_SHOWDIR || id == IDC_RESTART || id == IDC_EXIT;
}

static int tc_menu_find_position_by_id(HMENU hMenu, UINT id)
{
	int i;
	int count = GetMenuItemCount(hMenu);
	for (i = 0; i < count; ++i) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_ID;
		if (GetMenuItemInfo(hMenu, i, TRUE, &mii) && mii.wID == id) {
			return i;
		}
	}
	return -1;
}

static int tc_menu_append_text(char* dst, int dstLen, int pos, const char* src)
{
	int i = 0;
	if (!dst || dstLen <= 0 || !src) return pos;
	while (src[i] && pos < dstLen - 1) {
		dst[pos++] = src[i++];
	}
	dst[pos] = '\0';
	return pos;
}

static PFN_FormatMenuLabel_Win11 tc_menu_get_format_api(void)
{
	if (!g_menuFormatApiChecked) {
		HMODULE hDll = GetModuleHandleW(L"tcdll-win11.dll");
		if (!hDll) {
			hDll = GetModuleHandleW(L"tcdll.dll");
		}
		if (hDll) {
			g_pfnFormatMenuLabel_Win11 = (PFN_FormatMenuLabel_Win11)GetProcAddress(hDll, "FormatMenuLabel_Win11");
		}
		g_menuFormatApiChecked = TRUE;
	}
	return g_pfnFormatMenuLabel_Win11;
}

static int tc_menu_append_num(char* dst, int dstLen, int pos, int value, int width)
{
	char tmp[32];
	char fmt[8];
	fmt[0] = '%';
	if (width > 0) {
		fmt[1] = '0';
		fmt[2] = (char)('0' + (width % 10));
		fmt[3] = 'd';
		fmt[4] = '\0';
	} else {
		fmt[1] = 'd';
		fmt[2] = '\0';
	}
	wsprintf(tmp, fmt, value);
	return tc_menu_append_text(dst, dstLen, pos, tmp);
}

static void tc_menu_resolve_format_alias(const char* inFormat, char* outFormat, int outLen)
{
	int i;
	int j;
	char tmp[1024];
	if (!outFormat || outLen <= 0) return;
	outFormat[0] = '\0';
	if (!inFormat || !inFormat[0]) return;
	if (_stricmp(inFormat, "@CLOCK") == 0 || _stricmp(inFormat, "@MAIN_FORMAT") == 0 || _stricmp(inFormat, "@TCLOCK_FORMAT") == 0) {
		int useCustom = GetMyRegLong("Format", "Custom", 1);
		GetMyRegStr("Format", useCustom ? "CustomFormat" : "Format", tmp, sizeof(tmp), "");
		/* Single-line normalize for popup menu label rendering. */
		j = 0;
		for (i = 0; tmp[i] && j < outLen - 1; ++i) {
			if (tmp[i] == '\\' && tmp[i + 1] == 'n') {
				outFormat[j++] = ' ';
				++i;
				continue;
			}
			if (tmp[i] == '\r' || tmp[i] == '\n') {
				outFormat[j++] = ' ';
				continue;
			}
			outFormat[j++] = tmp[i];
		}
		outFormat[j] = '\0';
		return;
	}
	lstrcpyn(outFormat, inFormat, outLen);
}

static BOOL tc_menu_match_token(const char* p, const char* token)
{
	int i = 0;
	if (!p || !token) return FALSE;
	while (token[i]) {
		if (p[i] != token[i]) return FALSE;
		++i;
	}
	return TRUE;
}

static void tc_menu_format_label_datetime(const char* format, char* out, int outLen)
{
	SYSTEMTIME st;
	char am[32];
	char pm[32];
	char wk[32];
	int pos = 0;
	const char* p = format;
	static const char* kWeekdayShort[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

	if (!out || outLen <= 0) return;
	out[0] = '\0';
	if (!format || !format[0]) return;
	{
		PFN_FormatMenuLabel_Win11 pfn = tc_menu_get_format_api();
		if (pfn && pfn(format, out, outLen) && out[0] && strcmp(out, format) != 0) {
			return;
		}
	}

	GetLocalTime(&st);
	GetMyRegStr("Format", "AMsymbol", am, sizeof(am), "AM");
	GetMyRegStr("Format", "PMsymbol", pm, sizeof(pm), "PM");
	wk[0] = '\0';
	GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, "ddd", wk, (int)sizeof(wk));
	if (!wk[0]) {
		lstrcpyn(wk, kWeekdayShort[(st.wDayOfWeek <= 6) ? st.wDayOfWeek : 0], (int)sizeof(wk));
	}

	while (*p && pos < outLen - 1) {
		if (tc_menu_match_token(p, "yyyy")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wYear, 4);
			p += 4;
		} else if (tc_menu_match_token(p, "yy")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wYear % 100, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "ddd")) {
			pos = tc_menu_append_text(out, outLen, pos, wk);
			p += 3;
		} else if (tc_menu_match_token(p, "dd")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wDay, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "d")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wDay, 0);
			p += 1;
		} else if (tc_menu_match_token(p, "hh")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wHour, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "h")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wHour, 0);
			p += 1;
		} else if (tc_menu_match_token(p, "nn")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wMinute, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "n")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wMinute, 0);
			p += 1;
		} else if (tc_menu_match_token(p, "ss")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wSecond, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "s")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wSecond, 0);
			p += 1;
		} else if (tc_menu_match_token(p, "mm")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wMonth, 2);
			p += 2;
		} else if (tc_menu_match_token(p, "m")) {
			pos = tc_menu_append_num(out, outLen, pos, st.wMonth, 0);
			p += 1;
		} else if (tc_menu_match_token(p, "tt")) {
			pos = tc_menu_append_text(out, outLen, pos, (st.wHour < 12) ? am : pm);
			p += 2;
		} else {
			out[pos++] = *p++;
			out[pos] = '\0';
		}
	}
}

static void tc_menu_resolve_label_text(int itemIndex, const char* plainLabel, const char* labelFormat, int intervalSec, char* out, int outLen)
{
	TC_MENU_LABEL_CACHE* cache;
	DWORD now;
	DWORD waitMs;
	char effectiveFormat[1024];

	if (!out || outLen <= 0) return;
	out[0] = '\0';
	effectiveFormat[0] = '\0';
	tc_menu_resolve_format_alias(labelFormat, effectiveFormat, (int)sizeof(effectiveFormat));
	if (!effectiveFormat[0] || itemIndex < 1 || itemIndex >= TC_MENU_LABEL_CACHE_MAX) {
		lstrcpyn(out, plainLabel ? plainLabel : "", outLen);
		return;
	}

	cache = &g_menuLabelCache[itemIndex];
	if (intervalSec < 0) intervalSec = 0;
	if (intervalSec > 86400) intervalSec = 86400;
	now = GetTickCount();
	waitMs = (DWORD)(intervalSec * 1000);

	if (cache->valid &&
		cache->intervalSec == intervalSec &&
		strcmp(cache->format, effectiveFormat) == 0 &&
		(intervalSec > 0) &&
		(now - cache->tick) < waitMs) {
		lstrcpyn(out, cache->text, outLen);
		return;
	}

	tc_menu_format_label_datetime(effectiveFormat, cache->text, (int)sizeof(cache->text));
	if (!cache->text[0]) {
		lstrcpyn(cache->text, plainLabel ? plainLabel : "", (int)sizeof(cache->text));
	}
	lstrcpyn(cache->format, effectiveFormat, (int)sizeof(cache->format));
	cache->intervalSec = intervalSec;
	cache->tick = now;
	cache->valid = TRUE;
	lstrcpyn(out, cache->text, outLen);
}

void MenuOnTimerTick(HWND hwnd)
{
	int i;
	UNREFERENCED_PARAMETER(hwnd);
	if (!g_menuPopupActive || !hPopupMenu || g_menuLiveCount <= 0) return;
	for (i = 0; i < g_menuLiveCount; ++i) {
		char text[256];
		TC_MENU_LIVE_ENTRY* e = &g_menuLiveEntries[i];
		text[0] = '\0';
		tc_menu_resolve_label_text(e->itemIndex, e->plainLabel, e->format, e->intervalSec, text, (int)sizeof(text));
		if (text[0]) {
			ModifyMenu(hPopupMenu, e->cmdId, MF_BYCOMMAND, e->cmdId, text);
		}
	}
}

static void tc_menu_default_param_for_action(const char* action, char* out, int outLen)
{
	if (!action || !out || outLen <= 0) return;
	out[0] = '\0';
	if (_stricmp(action, "taskmgr") == 0) { lstrcpyn(out, "taskmgr", outLen); return; }
	if (_stricmp(action, "cmd") == 0) { lstrcpyn(out, "/k cd \\ ", outLen); return; }
	if (_stricmp(action, "alarm_clock") == 0) { lstrcpyn(out, "ms-clock:", outLen); return; }
	if (_stricmp(action, "control_panel") == 0) { lstrcpyn(out, "control", outLen); return; }
	if (_stricmp(action, "power_options") == 0) { lstrcpyn(out, "control /name Microsoft.PowerOptions /page pageGlobalSettings", outLen); return; }
	if (_stricmp(action, "network_connections") == 0) { lstrcpyn(out, "control ncpa.cpl", outLen); return; }
	if (_stricmp(action, "settings_home") == 0) { lstrcpyn(out, "ms-settings:", outLen); return; }
	if (_stricmp(action, "settings_network") == 0) { lstrcpyn(out, "ms-settings:network-status", outLen); return; }
	if (_stricmp(action, "settings_datetime") == 0) { lstrcpyn(out, "ms-settings:dateandtime", outLen); return; }
}

static const char* tc_menu_default_exec_type_for_action(const char* action)
{
	if (!action || !action[0]) return "builtin";
	if (_stricmp(action, "cmd") == 0) return "commandline";
	if (_stricmp(action, "pullback") == 0) return "builtin";
	if (_stricmp(action, "remove_drive_dynamic") == 0) return "builtin";
	/* Safe external-launch candidates first. */
	if (_stricmp(action, "taskmgr") == 0) return "shell";
	if (_stricmp(action, "alarm_clock") == 0) return "shell";
	if (_stricmp(action, "control_panel") == 0) return "shell";
	if (_stricmp(action, "power_options") == 0) return "shell";
	if (_stricmp(action, "network_connections") == 0) return "shell";
	if (_stricmp(action, "settings_home") == 0) return "shell";
	if (_stricmp(action, "settings_network") == 0) return "shell";
	if (_stricmp(action, "settings_datetime") == 0) return "shell";
	return "builtin";
}

static BOOL tc_menu_get_default_item(int index, char* type, int typeLen, char* action, int actionLen, int* enabled)
{
	/* Baseline that mirrors current non-fixed menu block (with separators). */
	static const char* kType[] = {
		"separator",
		"command", "command", "command", "command",
		"separator",
		"command", "command", "command",
		"separator",
		"command", "command", "command",
		"separator",
		"command",
		"separator"
	};
	static const char* kAction[] = {
		"",
		"taskmgr", "cmd", "alarm_clock", "pullback",
		"",
		"control_panel", "power_options", "network_connections",
		"",
		"settings_home", "settings_network", "settings_datetime",
		"",
		"remove_drive_dynamic",
		""
	};
	int n = (int)(sizeof(kType) / sizeof(kType[0]));
	if (index < 1 || index > n) {
		return FALSE;
	}
	lstrcpyn(type, kType[index - 1], typeLen);
	lstrcpyn(action, kAction[index - 1], actionLen);
	*enabled = 1;
	return TRUE;
}

static const char* tc_menu_default_label_for_action(const char* action)
{
	if (!action || !action[0]) return "";
	if (_stricmp(action, "taskmgr") == 0) return SafeMyString(IDS_TASKMGR);
	if (_stricmp(action, "cmd") == 0) return SafeMyString(IDS_CMD);
	if (_stricmp(action, "alarm_clock") == 0) return SafeMyString(IDS_ALARM_CLOCK);
	if (_stricmp(action, "pullback") == 0) return SafeMyString(IDS_PULLBACK);
	if (_stricmp(action, "control_panel") == 0) return SafeMyString(IDS_CONTROLPNL);
	if (_stricmp(action, "power_options") == 0) return SafeMyString(IDS_POWERPNL);
	if (_stricmp(action, "network_connections") == 0) return SafeMyString(IDS_NETWORKPNL);
	if (_stricmp(action, "settings_home") == 0) return SafeMyString(IDS_SETTING);
	if (_stricmp(action, "settings_network") == 0) return SafeMyString(IDS_NETWORKSTG);
	if (_stricmp(action, "settings_datetime") == 0) return SafeMyString(IDS_PROPDATE);
	if (_stricmp(action, "remove_drive_dynamic") == 0) return SafeMyString(IDS_ABOUTRMVDRV);
	return action;
}

static BOOL tc_menu_action_to_command(const char* action, UINT* outId)
{
	if (!action || !outId) return FALSE;
	if (_stricmp(action, "taskmgr") == 0) { *outId = IDC_TASKMAN; return TRUE; }
	if (_stricmp(action, "cmd") == 0) { *outId = IDC_CMD; return TRUE; }
	if (_stricmp(action, "alarm_clock") == 0) { *outId = IDC_ALARM_CLOCK; return TRUE; }
	if (_stricmp(action, "pullback") == 0) { *outId = IDC_PULLBACK; return TRUE; }
	if (_stricmp(action, "control_panel") == 0) { *outId = IDC_CONTROLPNL; return TRUE; }
	if (_stricmp(action, "power_options") == 0) { *outId = IDC_POWERPNL; return TRUE; }
	if (_stricmp(action, "network_connections") == 0) { *outId = IDC_NETWORKPNL; return TRUE; }
	if (_stricmp(action, "settings_home") == 0) { *outId = IDC_SETTING; return TRUE; }
	if (_stricmp(action, "settings_network") == 0) { *outId = IDC_NETWORKSTG; return TRUE; }
	if (_stricmp(action, "settings_datetime") == 0) { *outId = IDC_DATETIME_Win10; return TRUE; }
	if (_stricmp(action, "remove_drive_dynamic") == 0) { *outId = IDC_REMOVE_DRIVE0; return TRUE; }
	return FALSE;
}

static void tc_menu_prune_to_fixed(HMENU hMenu)
{
	int i;
	for (i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_ID | MIIM_SUBMENU;
		if (!GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
			continue;
		}
		if (mii.hSubMenu != NULL) {
			continue; /* keep Language submenu */
		}
		if (tc_menu_is_fixed_id(mii.wID)) {
			continue;
		}
		RemoveMenu(hMenu, i, MF_BYPOSITION);
	}
}

static void tc_menu_normalize_separators(HMENU hMenu)
{
	int i;
	BOOL prevSep = TRUE;
	for (i = 0; i < GetMenuItemCount(hMenu); ) {
		MENUITEMINFO mii;
		BOOL isSep;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE;
		if (!GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
			++i;
			continue;
		}
		isSep = (mii.fType & MFT_SEPARATOR) ? TRUE : FALSE;
		if (isSep && prevSep) {
			RemoveMenu(hMenu, i, MF_BYPOSITION);
			continue;
		}
		prevSep = isSep;
		++i;
	}
	for (i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
		MENUITEMINFO mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_FTYPE;
		if (!GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
			continue;
		}
		if ((mii.fType & MFT_SEPARATOR) == 0) {
			break;
		}
		RemoveMenu(hMenu, i, MF_BYPOSITION);
	}
}

static void tc_menu_ensure_ini_defaults(void)
{
	int i;
	if (GetMyRegLong(TC_MENU_SECTION, "Version", 0) > 0) {
		return;
	}
	SetMyRegLong(TC_MENU_SECTION, "Version", 1);
	SetMyRegLong(TC_MENU_SECTION, "ItemCount", 16);
	for (i = 1; i <= 16; ++i) {
		char key[64];
		char type[32];
		char action[64];
		int enabled = 1;
		const char* defaultLabel;
		type[0] = '\0';
		action[0] = '\0';
		tc_menu_get_default_item(i, type, (int)sizeof(type), action, (int)sizeof(action), &enabled);
		wsprintf(key, "Item%dType", i);
		SetMyRegStr(TC_MENU_SECTION, key, type);
		wsprintf(key, "Item%dEnabled", i);
		SetMyRegLong(TC_MENU_SECTION, key, enabled);
		if (_stricmp(type, "command") == 0) {
			char param[512];
			const char* execType;
			wsprintf(key, "Item%dAction", i);
			SetMyRegStr(TC_MENU_SECTION, key, action);
			defaultLabel = tc_menu_default_label_for_action(action);
			wsprintf(key, "Item%dLabel", i);
			SetMyRegStr(TC_MENU_SECTION, key, (char*)defaultLabel);
			execType = tc_menu_default_exec_type_for_action(action);
			wsprintf(key, "Item%dExecType", i);
			SetMyRegStr(TC_MENU_SECTION, key, (char*)execType);
			param[0] = '\0';
			tc_menu_default_param_for_action(action, param, (int)sizeof(param));
			wsprintf(key, "Item%dParam", i);
			SetMyRegStr(TC_MENU_SECTION, key, param);
		}
	}
}

static void tc_menu_apply_custom_from_ini(HMENU hMenu)
{
	int i;
	int insertPos;
	int globalLabelUpdateSec = GetMyRegLong(TC_MENU_SECTION, "LabelFormatUpdateSec", 1);
	int count = GetMyRegLong(TC_MENU_SECTION, "ItemCount", 16);
	if (globalLabelUpdateSec < 0) globalLabelUpdateSec = 0;
	if (count < 0) count = 0;
	if (count > TC_MENU_CUSTOM_MAX_ITEMS) count = TC_MENU_CUSTOM_MAX_ITEMS;

	tc_menu_prune_to_fixed(hMenu);

	insertPos = tc_menu_find_position_by_id(hMenu, IDC_SHOWPROP);
	if (insertPos < 0) {
		insertPos = GetMenuItemCount(hMenu);
	}

	for (i = 1; i <= count; ++i) {
		char key[64];
		char type[32];
		char action[64];
		char execType[32];
		char param[512];
		char args[512];
		char workdir[MAX_PATH];
		char label[256];
		char labelFormat[256];
		char resolvedLabel[256];
		int enabled;
		int show;
		int labelUpdateSec;
		const char* defaultLabel;
		UINT cmdId;

		type[0] = '\0';
		action[0] = '\0';
		execType[0] = '\0';
		param[0] = '\0';
		args[0] = '\0';
		workdir[0] = '\0';
		label[0] = '\0';
		labelFormat[0] = '\0';
		resolvedLabel[0] = '\0';
		enabled = 1;
		show = SW_SHOWNORMAL;
		labelUpdateSec = globalLabelUpdateSec;
		tc_menu_get_default_item(i, type, (int)sizeof(type), action, (int)sizeof(action), &enabled);

		wsprintf(key, "Item%dType", i);
		GetMyRegStr(TC_MENU_SECTION, key, type, sizeof(type), type);
		wsprintf(key, "Item%dEnabled", i);
		enabled = GetMyRegLong(TC_MENU_SECTION, key, enabled);
		if (!enabled) {
			continue;
		}

		if (_stricmp(type, "separator") == 0) {
			InsertMenu(hMenu, insertPos, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
			++insertPos;
			continue;
		}

		wsprintf(key, "Item%dAction", i);
		GetMyRegStr(TC_MENU_SECTION, key, action, sizeof(action), action);
		defaultLabel = tc_menu_default_label_for_action(action);
		wsprintf(key, "Item%dLabel", i);
		GetMyRegStr(TC_MENU_SECTION, key, label, sizeof(label), (char*)defaultLabel);
		wsprintf(key, "Item%dLabelFormat", i);
		GetMyRegStr(TC_MENU_SECTION, key, labelFormat, sizeof(labelFormat), "");
		wsprintf(key, "Item%dLabelUpdateSec", i);
		labelUpdateSec = GetMyRegLong(TC_MENU_SECTION, key, labelUpdateSec);
		tc_menu_resolve_label_text(i, label, labelFormat, labelUpdateSec, resolvedLabel, (int)sizeof(resolvedLabel));

		wsprintf(key, "Item%dExecType", i);
		GetMyRegStr(TC_MENU_SECTION, key, execType, sizeof(execType), (char*)tc_menu_default_exec_type_for_action(action));
		wsprintf(key, "Item%dParam", i);
		tc_menu_default_param_for_action(action, param, (int)sizeof(param));
		GetMyRegStr(TC_MENU_SECTION, key, param, sizeof(param), param);
		wsprintf(key, "Item%dArgs", i);
		GetMyRegStr(TC_MENU_SECTION, key, args, sizeof(args), "");
		wsprintf(key, "Item%dWorkDir", i);
		GetMyRegStr(TC_MENU_SECTION, key, workdir, sizeof(workdir), "");
		wsprintf(key, "Item%dShow", i);
		show = GetMyRegLong(TC_MENU_SECTION, key, SW_SHOWNORMAL);

		if (_stricmp(execType, "builtin") == 0 || execType[0] == '\0') {
			if (!tc_menu_action_to_command(action, &cmdId)) {
				char warn[256];
				wsprintf(warn, "[menu.c][menucustom] Unknown action skipped: %s", action);
				if (b_NormalLog) WriteNormalLog(warn);
				continue;
			}
		} else if (_stricmp(execType, "shell") == 0) {
			char target[MAX_PATH];
			target[0] = '\0';
			if (param[0]) {
				/* Support quoted executable paths with spaces, then optional args. */
				if (param[0] == '"') {
					int j = 1;
					while (param[j] && param[j] != '"') {
						++j;
					}
					if (j > 1) {
						int n = j - 1;
						if (n >= (int)sizeof(target)) n = (int)sizeof(target) - 1;
						memcpy(target, param + 1, n);
						target[n] = '\0';
						if (param[j] == '"') {
							int k = j + 1;
							while (param[k] == ' ') ++k;
							if (param[k]) lstrcpyn(args, param + k, (int)sizeof(args));
						}
					}
				} else {
					int k = 0;
					while (param[k] && param[k] != ' ') {
						++k;
					}
					if (k > 0) {
						int n = k;
						if (n >= (int)sizeof(target)) n = (int)sizeof(target) - 1;
						memcpy(target, param, n);
						target[n] = '\0';
						if (param[k] == ' ') {
							lstrcpyn(args, param + k + 1, (int)sizeof(args));
						}
					} else {
						lstrcpyn(target, param, (int)sizeof(target));
					}
				}
			}
			if (!target[0]) {
				lstrcpyn(target, action, (int)sizeof(target));
			}
			cmdId = tc_menu_dynamic_register(1, target, args, workdir, show);
			if (!cmdId) continue;
		} else if (_stricmp(execType, "commandline") == 0) {
			cmdId = tc_menu_dynamic_register(2, param, "", workdir, show);
			if (!cmdId) continue;
		} else {
			char warn2[256];
			wsprintf(warn2, "[menu.c][menucustom] Unknown exec type skipped: %s", execType);
			if (b_NormalLog) WriteNormalLog(warn2);
			continue;
		}

		InsertMenu(hMenu, insertPos, MF_BYPOSITION | MF_STRING, cmdId, resolvedLabel[0] ? resolvedLabel : label);
		if (labelFormat[0]) {
			tc_menu_live_register(cmdId, i, labelUpdateSec, label, labelFormat);
		}
		++insertPos;
	}

	tc_menu_normalize_separators(hMenu);
}

static char* SafeMyString(UINT id)
{
	char* s = MyString(id);
	if (s[0] && strcmp(s, "NG_String") != 0) {
		return s;
	}

	/* Recover once if language module was lost/corrupted at runtime. */
	{
		char fname[MAX_PATH];
		HINSTANCE hInst = LoadLanguageDLL(fname);
		if (hInst != NULL) {
			if (g_hInstResource) FreeLibrary(g_hInstResource);
			g_hInstResource = hInst;
			strcpy(g_langdllname, fname);
			s = MyString(id);
			if (s[0] && strcmp(s, "NG_String") != 0) {
				return s;
			}
		}
	}

	return "String_Error";
}

/*------------------------------------------------
   when the clock is right-clicked
   show pop-up menu
--------------------------------------------------*/
void OnContextMenu(HWND hwnd, HWND hwndClicked, int xPos, int yPos)
{
	int i;

	UNREFERENCED_PARAMETER(hwndClicked);
	if (g_hMenu) {
		DestroyMenu(g_hMenu);
		g_hMenu = NULL;
		hPopupMenu = NULL;
	}
	g_hMenu = LoadMenu(GetLangModule(), MAKEINTRESOURCE(IDR_MENU));
	if (!g_hMenu) {
		char fname[MAX_PATH];
		HINSTANCE hInst = LoadLanguageDLL(fname);
		if (hInst != NULL) {
			if (g_hInstResource) FreeLibrary(g_hInstResource);
			g_hInstResource = hInst;
			strcpy(g_langdllname, fname);
			g_hMenu = LoadMenu(GetLangModule(), MAKEINTRESOURCE(IDR_MENU));
		}
	}
	if (!g_hMenu) {
		return;
	}
	hPopupMenu = GetSubMenu(g_hMenu, 0);
	if (!hPopupMenu) {
		return;
	}
	SetMenuDefaultItem(hPopupMenu, 408, FALSE);

	if (!b_MenuItems_Initialized)
	{
		InitializeMenuItems();
		b_MenuItems_Initialized = TRUE;
	}
	tc_menu_dynamic_reset();
	tc_menu_ensure_ini_defaults();
	tc_menu_apply_custom_from_ini(hPopupMenu);

	b_CompactMode_menu = GetMyRegLong(NULL, "CompactMode", FALSE);
	b_SafeMode_menu = GetMyRegLong("Status_DoNotEdit", "SafeMode", FALSE);
	//b_UseDataPlanFunc = GetMyRegLong("DataPlan", "UseDataPlanFunction", FALSE);
	MENUITEMINFO menuiteminfo_temp;
	menuiteminfo_temp.cbSize = sizeof(MENUITEMINFO);
	menuiteminfo_temp.fMask = MIIM_STATE;

	BOOL b_ExistLTEProfile;
	BOOL b_ExistMeteredProfile;
	b_ExistLTEProfile = GetMyRegLong("Status_DoNotEdit", "ExistLTEProfile", FALSE);
	b_ExistMeteredProfile = GetMyRegLong("Status_DoNotEdit", "ExistMeteredProfile", FALSE);
	//b_AutomaticFindWLTEProfile = GetMyRegLong("DataPlan", "AutomaticFindLTEProfile", FALSE);


	b_UseBarMeterVL = GetMyRegLong("BarMeter", "UseBarMeterVL", 0);
	b_UseBarMeterBL = GetMyRegLong("BarMeter", "UseBarMeterBL", 0);
	b_UseBarMeterCU = GetMyRegLong("BarMeter", "UseBarMeterCU", 0);
	b_BatteryLifeAvailable = GetMyRegLong("Status_DoNotEdit", "BatteryLifeAvailable", 0);

	extern BOOL b_EnglishMenu;



	if (b_EnglishMenu)
	{
		menuiteminfo_temp.fState = MFS_CHECKED;
		SetMenuItemInfo(hPopupMenu, IDC_LANGUAGE_ENGLISH, FALSE, &menuiteminfo_temp);
		menuiteminfo_temp.fState = MFS_UNCHECKED;
		SetMenuItemInfo(hPopupMenu, IDC_LANGUAGE_JAPANESE, FALSE, &menuiteminfo_temp);
	}
	else
	{
		menuiteminfo_temp.fState = MFS_UNCHECKED;
		SetMenuItemInfo(hPopupMenu, IDC_LANGUAGE_ENGLISH, FALSE, &menuiteminfo_temp);
		menuiteminfo_temp.fState = MFS_CHECKED;
		SetMenuItemInfo(hPopupMenu, IDC_LANGUAGE_JAPANESE, FALSE, &menuiteminfo_temp);
	}



	if (b_UnplugDriveAvailable && GetMenuState(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND) != 0xFFFFFFFF)
	{
	char   volume_name[256], volume_system[256];
	DWORD	serial, length, flags;
	DWORD   dwDrive;
	INT     nDrive;
	char	driveLetter[20];
	char	strTemp_Win10[256];

	char	driveList_Win10[10][265];
	//char	driveLetter_Win10[10][20];
	int		driveIndex_Win10;

	if (b_DebugLog) {
		WriteDebug_New2("[menu.c][OnContextMenu] Drive list will be made based on GetLogicalDrives()");
		char buf[1000];
		DWORD buf_size, cnt;
		buf_size = GetLogicalDriveStrings(1000, buf);
		cnt = 0;
		i = 0;
		while (cnt < buf_size) {
			wsprintf(strTemp_Win10, "[menu.c][OnContextMenu] DriveName(%d): %s", i, &buf[cnt]);
			WriteDebug_New2(strTemp_Win10);
			i++;
			cnt += lstrlen(&buf[cnt]) + 1;
		}
	}


	//HDD(DRIVE_FIXED) 情報取得用構造体等
	DWORD dwResult = 0;
	STORAGE_PROPERTY_QUERY tStragePropertyQuery;
	tStragePropertyQuery.PropertyId = StorageDeviceProperty;
	tStragePropertyQuery.QueryType = PropertyStandardQuery;
	// メモリの確保
	DWORD dwInfoSize = 4096;
	STORAGE_DEVICE_DESCRIPTOR* tpStorageDeviceDescripter = (STORAGE_DEVICE_DESCRIPTOR*)malloc(dwInfoSize);


		driveIndex_Win10 = 0;
		dwDrive = GetLogicalDrives();
		for (nDrive = 0; nDrive < 26; nDrive++) 
		{

			if (dwDrive & (1 << nDrive)) {

				wsprintf(driveLetter, "%c:\\", 'A' + nDrive);

				UINT tempDriveType = GetDriveType(driveLetter);
				
				if (b_DebugLog && tempDriveType != DRIVE_UNKNOWN)
				{
					wsprintf(strTemp_Win10, "[menu.c][OnContextMenu] DriveType for Drive %c =%d", ('A' + nDrive), (int)tempDriveType);
					WriteDebug_New2(strTemp_Win10);
				}

				if (tempDriveType == DRIVE_REMOVABLE)
				{
					if (GetVolumeInformation(driveLetter, volume_name, sizeof(volume_name), &serial, &length, &flags, volume_system, sizeof(volume_system)) != 0)
					{
						//wsprintf(driveList_Win10[driveIndex_Win10], "%s%s (%c:)", stringMenuItem_RemoveDriveHeader, volume_name, ('A' + nDrive));
						wsprintf(driveList_Win10[driveIndex_Win10], "%s(%c) %s", stringMenuItem_RemoveDriveHeader, ('A' + nDrive), volume_name);
						wsprintf(driveLetter_Win10[driveIndex_Win10], "%c", 'A' + nDrive);
						driveIndex_Win10++;

						if (b_DebugLog)
						{
							wsprintf(strTemp_Win10, "[menu.c][OnContextMenu] %s (%c:)", volume_name, ('A' + nDrive));
							WriteDebug_New2(strTemp_Win10);
						}

					}

				}

				if (tempDriveType == DRIVE_FIXED)
				{
					char fname[16];
					wsprintf(fname, "\\\\.\\%c:", 'A' + nDrive);
					HANDLE hDevice = CreateFile(fname, 0,
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL, OPEN_EXISTING, 0, NULL);

					if (hDevice != INVALID_HANDLE_VALUE) {



						////HDD(DRIVE_FIXED) 情報取得用構造体等
						//DWORD dwResult = 0;
						//STORAGE_PROPERTY_QUERY tStragePropertyQuery;
						//tStragePropertyQuery.PropertyId = StorageDeviceProperty;
						//tStragePropertyQuery.QueryType = PropertyStandardQuery;
						//// メモリの確保
						//DWORD dwInfoSize = 4096;
						//STORAGE_DEVICE_DESCRIPTOR* tpStorageDeviceDescripter = (STORAGE_DEVICE_DESCRIPTOR*)malloc(dwInfoSize);


						/*
						IOCTL_STORAGE_QUERY_PROPERTY
						*/
						BOOL bResult = DeviceIoControl(
							(HANDLE)hDevice                            // handle to device
							, IOCTL_STORAGE_QUERY_PROPERTY                // dwIoControlCode
							, &tStragePropertyQuery                       // lpInBuffer
							, (DWORD)sizeof(tStragePropertyQuery)       // nInBufferSize
							, (LPVOID)tpStorageDeviceDescripter           // output buffer
							, (DWORD)dwInfoSize                           // size of output buffer
							, (LPDWORD)&dwResult                          // number of bytes returned
							, (LPOVERLAPPED)NULL                          // OVERLAPPED structure
						);

						if (0 == bResult) {
							if (b_DebugLog) WriteDebug_New2("[menu.c][OnContextMenu] Failed in IOCTL_STORAGE_QUERY_PROPERTY for HDD (Fixed_Drive)");
						}
						else
						{
							if (tpStorageDeviceDescripter->BusType == BusTypeUsb)	// BusTypeUsb = 0x7
							{
								if (GetVolumeInformation(driveLetter, volume_name, sizeof(volume_name), &serial, &length, &flags, volume_system, sizeof(volume_system)) != 0)
								{
									wsprintf(driveList_Win10[driveIndex_Win10], "%s(%c) %s (HDD)", stringMenuItem_RemoveDriveHeader, ('A' + nDrive), volume_name);
									wsprintf(driveLetter_Win10[driveIndex_Win10], "%c", 'A' + nDrive);
									driveIndex_Win10++;

									if (b_DebugLog)
									{
										wsprintf(strTemp_Win10, "[menu.c][OnContextMenu] %s (%c:)", volume_name, ('A' + nDrive));
										WriteDebug_New2(strTemp_Win10);
									}
								}
							}

							if (b_DebugLog)
							{
								wsprintf(strTemp_Win10, "[menu.c][OnContextMenu] %s (%c:) is FixedDrive with STORAGE_BUS_TYPE =%02x", volume_name, ('A' + nDrive), (int)tpStorageDeviceDescripter->BusType);
								WriteDebug_New2(strTemp_Win10);
							}
						}
						CloseHandle(hDevice);
					}
					else
					{
						if (b_DebugLog) WriteDebug_New2("[menu.c][OnContextMenu] Failed to get HANDLE(CreateFile Func) for HDD (Fixed_Drive)");
					}
				}

			}
		}

		for (i = 1; i < 10; i++) DeleteMenu(hPopupMenu, IDC_REMOVE_DRIVE0 + i, MF_BYCOMMAND);


		if (driveIndex_Win10 > 10) driveIndex_Win10 = 10;
		if (driveIndex_Win10 > 1)
		{
			driveIndex_Win10--;
			ModifyMenu(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND, IDC_REMOVE_DRIVE0 + driveIndex_Win10, driveList_Win10[driveIndex_Win10]);
			driveIndex_Win10--;
			for (i = driveIndex_Win10; i >= 0; i--)
			{
				InsertMenu(hPopupMenu, IDC_REMOVE_DRIVE0 + i + 1, MF_BYCOMMAND, IDC_REMOVE_DRIVE0 + i, driveList_Win10[i]);
			}
		}
		else if (driveIndex_Win10 == 1)
		{
			ModifyMenu(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND, IDC_REMOVE_DRIVE0, driveList_Win10[0]);
			EnableMenuItem(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND | MF_ENABLED);
		}
		else
		{
			ModifyMenu(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND, IDC_REMOVE_DRIVE0, stringMenuItem_RemoveDriveNoDrive);
			EnableMenuItem(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND | MF_GRAYED);
		}

	}


	SetForegroundWindow98(hwnd);
	g_menuPopupActive = TRUE;
	TrackPopupMenu(hPopupMenu, TPM_LEFTALIGN|TPM_RIGHTBUTTON,
		xPos, yPos, 0, hwnd, NULL);
	g_menuPopupActive = FALSE;

}

/*------------------------------------------------
    command message
--------------------------------------------------*/
void OnTClockCommand(HWND hwnd, WORD wID, WORD wCode)
{
	extern BOOL b_EnglishMenu;
	extern int Language_Offset;

	switch(wID)
	{
		case IDC_SHOWDIR: // Show Directory
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_SHOWDIR received");
			ShellExecuteUtf8Compat(g_hwndMain, NULL, g_mydir, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_SHOWPROP: // Show property
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_SHOWPROP received");
			MyPropertyDialog();
			return;
		case IDC_EXIT: // Exit
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_EXIT received");
			if (b_NormalLog) WriteNormalLog("Exit TClock-Win10 from right-click menu.");
			/* Avoid Explorer restart side-effects on explicit user exit. */
			b_SkipHideClockRestore = TRUE;
			PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
//			PostMessage(g_hwndClock, WM_COMMAND, IDC_EXIT, 0);
			return;

		case IDC_RESTART: // Restart
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_RESTART received");
			if (b_NormalLog) WriteNormalLog("Restart TClock-Win10 from right-click menu.");
			/* Avoid Explorer restart side-effects on explicit user restart. */
			b_SkipHideClockRestore = TRUE;
			PostMessage(g_hwndClock, WM_COMMAND, IDC_RESTART, 0);
			return;
		case IDC_POWERPNL:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_POWERPNL received");
			ShellExecuteW(NULL, L"open", L"control", L"/name Microsoft.PowerOptions /page pageGlobalSettings", NULL, SW_SHOWNORMAL);
			return;

		case IDC_NETWORKSTG:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_NETWORKSTG received");
			ShellExecuteW(NULL, L"open", L"ms-settings:network-status", NULL, NULL, SW_SHOWNORMAL);
			return;

		case IDC_NETWORKPNL:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_NETWORKPNL received");
			ShellExecuteW(NULL, L"open", L"control", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
			return;

		case IDC_DATAUSAGE:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_DATAUSAGE received");
			ShellExecuteW(NULL, L"open", L"ms-settings:datausage", NULL, NULL, SW_SHOWNORMAL);
			return;

		case IDC_CONTROLPNL:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_CONTROLPNL received");
			ShellExecuteW(NULL, L"open", L"control", NULL, NULL, SW_SHOWNORMAL);
			return;

		case IDC_SETTING:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][CLOCKM_TOGGLE_DATAPLANFUNCOnTClockCommand] IDC_SETTING received");
			ShellExecuteW(NULL, L"open", L"ms-settings:", NULL, NULL, SW_SHOWNORMAL);
			return;

		case IDC_CMD:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_CMD received");
			ShellExecuteW(NULL, L"open", L"cmd.exe", L"/k cd \\", NULL, SW_SHOWNORMAL);
			return;

		case IDC_LANGUAGE_JAPANESE:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_LANGUAGE_JAPANESE received");
			if (b_EnglishMenu)
			{
				b_EnglishMenu = !b_EnglishMenu;
				SetMyRegLong(NULL, "EnglishMenu", b_EnglishMenu);
				b_MenuItems_Initialized = FALSE;
				Language_Offset = LANGUAGE_OFFSET_JAPANESE;
			}
			return;

		case IDC_LANGUAGE_ENGLISH:	//Added by TTTT
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_LANGUAGEC_ENGLISH received");
			if (!b_EnglishMenu)
			{
				b_EnglishMenu = !b_EnglishMenu;
				SetMyRegLong(NULL, "EnglishMenu", b_EnglishMenu);
				b_MenuItems_Initialized = FALSE;
				Language_Offset = LANGUAGE_OFFSET_ENGLISH;
			}
			return;

		//case IDC_TOGGLE_CLOUD_APP: //Added by TTTT
		//{
		//	if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_TOGGLE_CLOUD_APP received");
		//	PostMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_TOGGLE_AUTOPAUSE_CLOUDAPP, 0);
		//	return;
		//}

		//case IDC_TOGGLE_DATAPLANFUNC: //Added by TTTT
		//{
		//	if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_TOGGLE_DATAPLANFUNC received");
		//	PostMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_TOGGLE_DATAPLANFUNC, 0);
		//	return;
		//}

		case IDC_VISTACALENDAR:
		{
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_VISTACALENDAR received");
			PostMessage(g_hwndClock, CLOCKM_VISTACALENDAR, 0, 0);
			return;
		}

		case IDC_SHOWAVAILABLENETWORKS:
		{
			if (b_DebugLog) WriteDebug_New2("[menu.c][OnTClockCommand] IDC_SHOWAVAILABLENETWORKS received");
			PostMessage(g_hwndClock, CLOCKM_SHOWAVAILABLENETWORKS, 0, 0);
			return;
		}


		case IDC_REMOVE_DRIVE0:
		case IDC_REMOVE_DRIVE1:
		case IDC_REMOVE_DRIVE2:
		case IDC_REMOVE_DRIVE3:
		case IDC_REMOVE_DRIVE4:
		case IDC_REMOVE_DRIVE5:
		case IDC_REMOVE_DRIVE6:
		case IDC_REMOVE_DRIVE7:
		case IDC_REMOVE_DRIVE8:
		case IDC_REMOVE_DRIVE9:
		{
			if (wID == IDC_REMOVE_DRIVE0 && !b_UnplugDriveAvailable)
			{
				MyMessageBox(NULL, "TClockフォルダにフリーソフトのUnplugDrive Portable (UnplugDrive.exe)を置くと、リムーバブルドライブ取り外し機能を利用することができます。\n\nHaving UnplugDrive.exe (Japanese freeware) in TClock folder enables \"Remove Drive\" function.",
					"TClock-Win11", MB_OK | MB_SETFOREGROUND | MB_ICONINFORMATION, 0xFFFFFFFF);
				return;
			}

			char strAppTemp_Win10[MAX_PATH];
			strcpy(strAppTemp_Win10, g_mydir);
			add_title(strAppTemp_Win10, "UnplugDrive.exe");
			if (b_DebugLog) WriteDebug_New2("Remove Drive Application will be executed:");
			if (b_DebugLog) WriteDebug_New2(strAppTemp_Win10);
			if (b_DebugLog) WriteDebug_New2(driveLetter_Win10[wID - IDC_REMOVE_DRIVE0]);
			ShellExecuteUtf8Compat(NULL, "open", strAppTemp_Win10, driveLetter_Win10[wID - IDC_REMOVE_DRIVE0], NULL, SW_SHOWNORMAL);
			return;
		}

		case IDC_TASKMAN:
		{
			ShellExecuteW(NULL, L"open", L"taskmgr", NULL, NULL, SW_SHOWNORMAL);
			SetWindowPos(GetActiveWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			return;
		}

		case IDC_ALARM_CLOCK:
		{
			ShellExecuteW(NULL, L"open", L"ms-clock:", NULL, NULL, SW_SHOWNORMAL);
			SetWindowPos(GetActiveWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			return;
		}

		case IDC_PULLBACK:
		{
			extern int PullBackIndex;
			PullBackIndex = 0;
			EnumWindows(PullBackOBWindow, (LPARAM)0);
			return;
		}

		case IDC_DATETIME_Win10:
		{
			HWND hwndTray;
			hwndTray = FindWindowW(L"Shell_TrayWnd", NULL);
			if(hwndTray)
			{
				if (wID == IDC_DATETIME_Win10)
					PostMessage(hwndTray, WM_COMMAND, (WPARAM)IDC_DATETIME, 0);
				else
					PostMessage(hwndTray, WM_COMMAND, (WPARAM)wID, 0);
			}
			return;
		}
		case IDC_HOTKEY0:
			ExecuteMouseFunction(hwnd, wCode, IDS_HOTKEY - IDS_LEFTBUTTON, 0);
            return;
        case IDC_HOTKEY1:
        case IDC_HOTKEY2:
        case IDC_HOTKEY3:
        case IDC_HOTKEY4:
            ExecuteMouseFunction(hwnd, -1, IDS_HOTKEY - IDS_LEFTBUTTON, wID - IDC_HOTKEY1 + 1);
            return;
    }

    if (tc_menu_dynamic_execute(wID)) {
        return;
    }
    if (b_DebugLog) {
        char tmp[96];
        wsprintf(tmp, "[menu.c][OnTClockCommand] Unknown wID=%u", wID);
        WriteDebug_New2(tmp);
    }
    if (b_NormalLog) {
        char tmp2[96];
        wsprintf(tmp2, "[Warning] Unknown menu command wID=%u", wID);
        WriteNormalLog(tmp2);
    }
    return;
}



/*-------------------------------------------------------
Initialize Menu Items Japanese or English, Added by TTTT
---------------------------------------------------------*/
void InitializeMenuItems(void)
{

	extern BOOL b_EnglishMenu;
	char s[64];
	char stringLTE[32];


	GetMyRegStr("ETC", "LTEstring", stringLTE, 32, "LTE");





	strcpy(stringMenuItem_RemoveDriveHeader, SafeMyString(IDS_RMVDRVHEAD));
	strcpy(stringMenuItem_RemoveDriveNoDrive, SafeMyString(IDS_NORMVDRV));


	//if (b_AcceptRisk)
	//{
	//	if (b_EnglishMenu)	
	//		wsprintf(s, "Kill Apps on %s / Metered WiFi", stringLTE);
	//	else
	//		wsprintf(s, "%s・従量課金WiFiでアプリ強制終了", stringLTE);
	//	ModifyMenu(hPopupMenu, IDC_TOGGLE_CLOUD_APP, MF_BYCOMMAND, IDC_TOGGLE_CLOUD_APP, s);
	//}
	//else
	//{
	//	DeleteMenu(hPopupMenu, IDC_TOGGLE_CLOUD_APP, MF_BYCOMMAND);
	//}

	wsprintf(s, SafeMyString(IDS_MENURETRIEVE));
//	ModifyMenu(hPopupMenu, IDC_TOGGLE_DATAPLANFUNC, MF_BYCOMMAND, IDC_TOGGLE_DATAPLANFUNC, s);

	ModifyMenu(hPopupMenu, IDC_TASKMAN, MF_BYCOMMAND, IDC_TASKMAN, SafeMyString(IDS_TASKMGR));
	ModifyMenu(hPopupMenu, IDC_CMD, MF_BYCOMMAND, IDC_CMD,SafeMyString(IDS_CMD));
	ModifyMenu(hPopupMenu, IDC_ALARM_CLOCK, MF_BYCOMMAND, IDC_ALARM_CLOCK, SafeMyString(IDS_ALARM_CLOCK));
	ModifyMenu(hPopupMenu, IDC_PULLBACK, MF_BYCOMMAND, IDC_PULLBACK, SafeMyString(IDS_PULLBACK));
	ModifyMenu(hPopupMenu, IDC_VISTACALENDAR, MF_BYCOMMAND, IDC_VISTACALENDAR, SafeMyString(IDS_VISTACALENDAR));
	ModifyMenu(hPopupMenu, IDC_SHOWAVAILABLENETWORKS, MF_BYCOMMAND, IDC_SHOWAVAILABLENETWORKS, SafeMyString(IDS_SHOWAVAILABLENETWORKS));
	ModifyMenu(hPopupMenu, IDC_CONTROLPNL, MF_BYCOMMAND, IDC_CONTROLPNL, SafeMyString(IDS_CONTROLPNL));
	ModifyMenu(hPopupMenu, IDC_POWERPNL, MF_BYCOMMAND, IDC_POWERPNL, SafeMyString(IDS_POWERPNL));
	ModifyMenu(hPopupMenu, IDC_NETWORKPNL, MF_BYCOMMAND, IDC_NETWORKPNL, SafeMyString(IDS_NETWORKPNL));
	ModifyMenu(hPopupMenu, IDC_SETTING, MF_BYCOMMAND, IDC_SETTING, SafeMyString(IDS_SETTING));
	ModifyMenu(hPopupMenu, IDC_NETWORKSTG, MF_BYCOMMAND, IDC_NETWORKSTG, SafeMyString(IDS_NETWORKSTG));
	ModifyMenu(hPopupMenu, IDC_DATETIME_Win10, MF_BYCOMMAND, IDC_DATETIME_Win10, SafeMyString(IDS_PROPDATE));
	ModifyMenu(hPopupMenu, IDC_REMOVE_DRIVE0, MF_BYCOMMAND, IDC_REMOVE_DRIVE0, SafeMyString(IDS_ABOUTRMVDRV));
	ModifyMenu(hPopupMenu, IDC_SHOWDIR, MF_BYCOMMAND, IDC_SHOWDIR, SafeMyString(IDS_OPENTCFOLDER));
	ModifyMenu(hPopupMenu, IDC_SHOWPROP, MF_BYCOMMAND, IDC_SHOWPROP, SafeMyString(IDS_PROPERTY));
	ModifyMenu(hPopupMenu, IDC_EXIT, MF_BYCOMMAND, IDC_EXIT, SafeMyString(IDS_EXITTCLOCK));
	ModifyMenu(hPopupMenu, IDC_RESTART, MF_BYCOMMAND, IDC_RESTART, SafeMyString(IDS_RESTART));






}
