/*-------------------------------------------
  mouse.c
  mouse operation
  KAZUBON 1997-2001
---------------------------------------------*/

#include "tclock.h"
#include "..\common\text_codec.h"


// XButton Messages
#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP   0x020C
#define XBUTTON1       0x0001
#define XBUTTON2       0x0002
#endif
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL  0x020A
#endif

static char reg_section[] = "Mouse";
static UINT last_mousedown  = 0;
static WORD last_xmousedown = 0;
static DWORD last_tickcount;
static int num_click = 0;
static int exec_button = -1;
static BOOL timer = FALSE;
static BOOL exec_zone_active = FALSE;
static int exec_zone_button = -1;
static int exec_zone_click = 0;
static int exec_zone_entry = 0;
static LONG exec_zone_func = -1;

static int GetMouseFuncNum(int button, int nclick);
void GetMouseFileEntry(int btn, int clk, char* entry, int cch);
static void GetMouseWorkDirEntry(int btn, int clk, char* entry, int cch);
static void mouse_zone_reset(void);
static int mouse_zone_count(int button, int nclick);
static BOOL mouse_has_text(const char* entry);
static void mouse_pick_text(int button, int nclick, int zone_number, const char* suffix, char* entry, int cch);
static int mouse_zone_hit_test(POINT pt_client, const RECT* rc_client, int zone_count);
static LONG mouse_zone_read_func(int button, int nclick, int zone_number, BOOL* found);
static BOOL mouse_zone_has_action(int button, int nclick);
static BOOL mouse_zone_resolve_pending(int button, int nclick, LPARAM lParam, LONG* fnc_out);

static ATOM atomHotkey[4] = { 0,0,0,0 };
static UINT idTCaptureHotkey[32] = { 0 };
static char tcapProfileByHotkey[32][128] = { { 0 } };
extern BOOL b_EnglishMenu;

static const char *atomName[4] = {
	"hotkey1_atom_tcklock2ch",
	"hotkey2_atom_tcklock2ch",
	"hotkey3_atom_tcklock2ch",
	"hotkey4_atom_tcklock2ch"
};

static void tc_mouse_normalize_setting_utf8_in_place(char* value, int valueBytes)
{
	WCHAR wbuf[MAX_PATH];
	char utf8[MAX_PATH];
	if (!value || valueBytes <= 0 || value[0] == '\0') return;
	if (tc_utf8_to_utf16(value, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
	if (tc_utf16_to_utf8(wbuf, utf8, (int)sizeof(utf8)) <= 0) return;
	lstrcpyn(value, utf8, valueBytes);
}

static LONG GetTCaptureEnableConfigMouse(void)
{
	LONG v = GetMyRegLong("TCapture", "Enable", -1);
	if (v != -1) return (v != 0) ? 1 : 0;
	v = GetMyRegLong("ETC", "TCaptureEnable", 0);
	SetMyRegLong("TCapture", "Enable", (v != 0) ? 1 : 0);
	DelMyReg("ETC", "TCaptureEnable");
	return (v != 0) ? 1 : 0;
}

static void mouse_zone_reset(void)
{
	exec_zone_active = FALSE;
	exec_zone_button = -1;
	exec_zone_click = 0;
	exec_zone_entry = 0;
	exec_zone_func = -1;
}

static int mouse_zone_count(int button, int nclick)
{
	char entry[32];
	LONG count;
	const LONG missing = -32768;

	wsprintf(entry, "%d%dZoneCount", button, nclick);
	count = GetMyRegLong(reg_section, entry, missing);
	if (count == missing && button == 0 && nclick == 1)
		count = GetMyRegLong(reg_section, "LeftClickZoneCount", 1);
	if (count == missing) count = 1;
	if (count < 1) count = 1;
	if (count > 3) count = 3;
	return (int)count;
}

static BOOL mouse_has_text(const char* entry)
{
	char missing[2] = { 1, 0 };
	char value[2] = { 1, 0 };

	if (!entry || !entry[0]) return FALSE;
	GetMyRegStr(reg_section, entry, value, (int)sizeof(value), missing);
	return !(value[0] == missing[0] && value[1] == 0);
}

static void mouse_pick_text(int button, int nclick, int zone_number, const char* suffix, char* entry, int cch)
{
	char candidate[32];

	if (!entry || cch <= 0) return;
	entry[0] = '\0';
	if (zone_number < 1 || zone_number > 3) return;

	if (zone_number == 1)
		wsprintf(candidate, "%d%d%s", button, nclick, suffix);
	else
		wsprintf(candidate, "%d%d%s%d", button, nclick, suffix, zone_number);
	if (mouse_has_text(candidate)) {
		lstrcpyn(entry, candidate, cch);
		return;
	}
	wsprintf(candidate, "%d%dZone%d%s", button, nclick, zone_number, suffix);
	if (mouse_has_text(candidate)) {
		lstrcpyn(entry, candidate, cch);
		return;
	}
	if (button == 0 && nclick == 1) {
		wsprintf(candidate, "LeftClickZone%d%s", zone_number, suffix);
		if (mouse_has_text(candidate)) {
			lstrcpyn(entry, candidate, cch);
			return;
		}
	}
	if (zone_number == 1)
		wsprintf(candidate, "%d%d%s", button, nclick, suffix);
	else
		wsprintf(candidate, "%d%d%s%d", button, nclick, suffix, zone_number);
	lstrcpyn(entry, candidate, cch);
}

static int mouse_zone_hit_test(POINT pt_client, const RECT* rc_client, int zone_count)
{
	int axis;
	int length;

	if (!rc_client || zone_count < 2) return -1;
	if (pt_client.x < rc_client->left || pt_client.x >= rc_client->right ||
		pt_client.y < rc_client->top || pt_client.y >= rc_client->bottom) {
		return -1;
	}

	axis = pt_client.x - rc_client->left;
	length = rc_client->right - rc_client->left;
	if (length <= 0) return -1;

	if (zone_count >= 3) {
		if (axis >= (2 * length / 3)) return 2;
		if (axis >= (length / 3)) return 1;
		return 0;
	}
	if (axis >= (length / 2)) return 1;
	return 0;
}

static LONG mouse_zone_read_func(int button, int nclick, int zone_number, BOOL* found)
{
	char entry[32];
	LONG value;
	const LONG missing = -32768;

	if (found) *found = FALSE;
	if (zone_number < 1 || zone_number > 3) return missing;

	if (zone_number == 1) {
		wsprintf(entry, "%d%d", button, nclick);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	else {
		wsprintf(entry, "%d%dFunc%d", button, nclick, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing) {
		wsprintf(entry, "%d%dZone%dFunc", button, nclick, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing) {
		wsprintf(entry, "%d%dZone%d", button, nclick, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing && button == 0 && nclick == 1) {
		wsprintf(entry, "LeftClickZone%dFunc", zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value == missing) {
			wsprintf(entry, "LeftClickZone%d", zone_number);
			value = GetMyRegLong(reg_section, entry, missing);
		}
	}
	if (value != missing && found) *found = TRUE;
	return value;
}

static BOOL mouse_zone_has_action(int button, int nclick)
{
	BOOL found;
	int zone_number;

	if (GetMouseFuncNum(button, nclick) >= 0) return TRUE;
	for (zone_number = 1; zone_number <= 3; zone_number++)
	{
		mouse_zone_read_func(button, nclick, zone_number, &found);
		if (found) return TRUE;
	}
	return FALSE;
}

static BOOL mouse_zone_resolve_pending(int button, int nclick, LPARAM lParam, LONG* fnc_out)
{
	RECT rc_client;
	POINT pt_client;
	int zone_count;
	int zone_index;
	int source_zone = 0;
	BOOL found = FALSE;
	LONG fnc;

	if (!fnc_out) return FALSE;
	*fnc_out = GetMouseFuncNum(button, nclick);
	mouse_zone_reset();

	zone_count = mouse_zone_count(button, nclick);
	if (zone_count <= 1) {
		fnc = mouse_zone_read_func(button, nclick, 1, &found);
		if (!found) return FALSE;

		*fnc_out = fnc;
		exec_zone_active = TRUE;
		exec_zone_button = button;
		exec_zone_click = nclick;
		exec_zone_entry = 1;
		exec_zone_func = fnc;
		return TRUE;
	}
	if (!g_hwndClock || !IsWindow(g_hwndClock)) return FALSE;
	if (!GetClientRect(g_hwndClock, &rc_client)) return FALSE;

	pt_client.x = GET_X_LPARAM(lParam);
	pt_client.y = GET_Y_LPARAM(lParam);
	zone_index = mouse_zone_hit_test(pt_client, &rc_client, zone_count);
	if (zone_index < 0) return FALSE;

	fnc = mouse_zone_read_func(button, nclick, zone_index + 1, &found);
	if (found) {
		source_zone = zone_index + 1;
	}
	else if (zone_index != 0) {
		fnc = mouse_zone_read_func(button, nclick, 1, &found);
		if (found) source_zone = 1;
	}

	if (!found) {
		fnc = GetMouseFuncNum(button, nclick);
	}

	*fnc_out = fnc;
	if (source_zone <= 0) return FALSE;

	exec_zone_active = TRUE;
	exec_zone_button = button;
	exec_zone_click = nclick;
	exec_zone_entry = source_zone;
	exec_zone_func = fnc;
	return TRUE;
}

static LONG GetTCalendarEnableConfigMouse(void)
{
	LONG v = GetMyRegLong("TCalendar", "Enable", -1);
	if (v == -1) {
		SetMyRegLong("TCalendar", "Enable", 0);
		return 0;
	}
	return (v != 0) ? 1 : 0;
}

static void GetTCapturePathConfigMouse(char* outPath, int outPathLen)
{
	char legacyPath[MAX_PATH];
	char before[MAX_PATH];
	if (!outPath || outPathLen <= 0) return;
	outPath[0] = '\0';
	GetMyRegStr("TCapture", "Path", outPath, outPathLen, "");
	if (outPath[0]) {
		lstrcpyn(before, outPath, (int)sizeof(before));
		tc_mouse_normalize_setting_utf8_in_place(outPath, outPathLen);
		if (lstrcmp(before, outPath) != 0) SetMyRegStr("TCapture", "Path", outPath);
		return;
	}
	GetMyRegStr("ETC", "TCapturePath", legacyPath, MAX_PATH, "plugins\\TCapture.exe");
	if (legacyPath[0] == '\0') strcpy(legacyPath, "plugins\\TCapture.exe");
	tc_mouse_normalize_setting_utf8_in_place(legacyPath, (int)sizeof(legacyPath));
	lstrcpyn(outPath, legacyPath, outPathLen);
	SetMyRegStr("TCapture", "Path", outPath);
	DelMyReg("ETC", "TCapturePath");
}

static BOOL ResolveTCaptureExePathMouse(char* outPath, int outPathLen)
{
	char cfgPath[MAX_PATH];
	if (!outPath || outPathLen <= 0) return FALSE;
	outPath[0] = '\0';
	GetTCapturePathConfigMouse(cfgPath, MAX_PATH);
	if (cfgPath[0] == 0) strcpy(cfgPath, "plugins\\TCapture.exe");
	if (PathFileExistsUtf8Strict(cfgPath)) {
		lstrcpyn(outPath, cfgPath, outPathLen);
		return TRUE;
	}
	lstrcpyn(outPath, g_mydir, outPathLen);
	add_title(outPath, cfgPath);
	if (PathFileExistsUtf8Strict(outPath)) return TRUE;
	lstrcpyn(outPath, cfgPath, outPathLen);
	return PathFileExistsUtf8Strict(outPath);
}

static void GetTCalendarPathConfigMouse(char* outPath, int outPathLen)
{
	char before[MAX_PATH];
	BOOL wasMissing = FALSE;
	if (!outPath || outPathLen <= 0) return;
	outPath[0] = '\0';
	GetMyRegStr("TCalendar", "Path", outPath, outPathLen, "");
	if (outPath[0] == '\0') {
		strcpy(outPath, "plugins\\TCalendar.exe");
		wasMissing = TRUE;
	}
	lstrcpyn(before, outPath, (int)sizeof(before));
	tc_mouse_normalize_setting_utf8_in_place(outPath, outPathLen);
	if (wasMissing || lstrcmp(before, outPath) != 0) SetMyRegStr("TCalendar", "Path", outPath);
}

static BOOL ResolveTCalendarExePathMouse(char* outPath, int outPathLen)
{
	char cfgPath[MAX_PATH];
	if (!outPath || outPathLen <= 0) return FALSE;
	outPath[0] = '\0';
	GetTCalendarPathConfigMouse(cfgPath, MAX_PATH);
	if (cfgPath[0] == 0) strcpy(cfgPath, "plugins\\TCalendar.exe");
	if (PathFileExistsUtf8Strict(cfgPath)) {
		lstrcpyn(outPath, cfgPath, outPathLen);
		return TRUE;
	}
	lstrcpyn(outPath, g_mydir, outPathLen);
	add_title(outPath, cfgPath);
	if (PathFileExistsUtf8Strict(outPath)) return TRUE;
	lstrcpyn(outPath, cfgPath, outPathLen);
	return PathFileExistsUtf8Strict(outPath);
}

static BOOL ParseTCaptureHotkey(const char* text, UINT* modifiers, UINT* vk)
{
	char token[64];
	int tok = 0;
	const unsigned char* p;
	UINT mods = 0;
	UINT key = 0;
	if (!text || !modifiers || !vk) return FALSE;
	for (p = (const unsigned char*)text;; ++p)
	{
		unsigned char c = *p;
		if (c == '+' || c == 0)
		{
			int s = 0;
			int e = tok;
			token[tok] = 0;
			while (token[s] == ' ' || token[s] == '\t') s++;
			while (e > s && (token[e - 1] == ' ' || token[e - 1] == '\t')) e--;
			token[e] = 0;
			if (token[s])
			{
				char* t = &token[s];
				int i;
				for (i = 0; t[i]; ++i) { if (t[i] >= 'a' && t[i] <= 'z') t[i] = (char)(t[i] - ('a' - 'A')); }
				if (strcmp(t, "CTRL") == 0 || strcmp(t, "CONTROL") == 0) mods |= MOD_CONTROL;
				else if (strcmp(t, "SHIFT") == 0) mods |= MOD_SHIFT;
				else if (strcmp(t, "ALT") == 0) mods |= MOD_ALT;
				else if (strcmp(t, "WIN") == 0 || strcmp(t, "WINDOWS") == 0) mods |= MOD_WIN;
				else if (!key)
				{
					if (t[0] && !t[1])
					{
						if ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= '0' && t[0] <= '9')) key = (UINT)t[0];
					}
					else if (t[0] == 'F' && t[1])
					{
						int fn = atoi(t + 1);
						if (fn >= 1 && fn <= 24) key = VK_F1 + (UINT)(fn - 1);
					}
					else if (strcmp(t, "PRINTSCREEN") == 0 || strcmp(t, "PRTSC") == 0 || strcmp(t, "PRTSCR") == 0) key = VK_SNAPSHOT;
					else if (strcmp(t, "INSERT") == 0) key = VK_INSERT;
					else if (strcmp(t, "DELETE") == 0) key = VK_DELETE;
					else if (strcmp(t, "HOME") == 0) key = VK_HOME;
					else if (strcmp(t, "END") == 0) key = VK_END;
					else if (strcmp(t, "PGUP") == 0 || strcmp(t, "PAGEUP") == 0) key = VK_PRIOR;
					else if (strcmp(t, "PGDN") == 0 || strcmp(t, "PAGEDOWN") == 0) key = VK_NEXT;
					else if (strcmp(t, "SPACE") == 0 || strcmp(t, "SPACEBAR") == 0) key = VK_SPACE;
					else if (strcmp(t, "ENTER") == 0 || strcmp(t, "RETURN") == 0) key = VK_RETURN;
				}
			}
			tok = 0;
			if (c == 0) break;
		}
		else if (tok < (int)sizeof(token) - 1)
		{
			token[tok++] = (char)c;
		}
	}
	if (!key) return FALSE;
#ifdef MOD_NOREPEAT
	mods |= MOD_NOREPEAT;
#endif
	*modifiers = mods;
	*vk = key;
	return TRUE;
}

static void TriggerTCaptureProfile(HWND hwnd, const char* profileName)
{
	char exePath[MAX_PATH];
	char safeProfile[128];
	char params[384];
	int i;
	if (!profileName || !profileName[0]) return;
	if (!ResolveTCaptureExePathMouse(exePath, MAX_PATH)) return;
	tc_mouse_normalize_setting_utf8_in_place(exePath, (int)sizeof(exePath));
	lstrcpyn(safeProfile, profileName, (int)sizeof(safeProfile));
	for (i = 0; safeProfile[i]; ++i) {
		if (safeProfile[i] == '"') safeProfile[i] = '\'';
	}
	wsprintf(params, "--capture --profile \"%s\"", safeProfile);
	ShellExecuteUtf8Strict(hwnd, "open", exePath, params, g_mydir, SW_SHOWNORMAL);
}

typedef struct {
	char file[MAX_PATH];
	char args[256];
	char workdir[MAX_PATH];
	int showCmd;
	int activateTCalendar;
} TC_MOUSE_DELAYED_LAUNCH;

static void tc_mouse_activate_tcalendar_window(void)
{
	HWND hwndTcal = FindWindowW(L"TCalendarStandaloneWindowClass", NULL);
	if (!hwndTcal) hwndTcal = FindWindowW(L"TCalendarWindowClass", NULL);
	if (!hwndTcal) return;
	if (IsIconic(hwndTcal)) ShowWindow(hwndTcal, SW_RESTORE);
	{
		HWND hwndFg = GetForegroundWindow();
		DWORD fgThread = hwndFg ? GetWindowThreadProcessId(hwndFg, NULL) : 0;
		DWORD targetThread = GetWindowThreadProcessId(hwndTcal, NULL);
		DWORD currentThread = GetCurrentThreadId();
		BOOL attachedCurrent = FALSE;
		BOOL attachedFg = FALSE;
		if (targetThread && currentThread != targetThread) {
			attachedCurrent = AttachThreadInput(currentThread, targetThread, TRUE);
		}
		if (targetThread && fgThread && fgThread != targetThread) {
			attachedFg = AttachThreadInput(fgThread, targetThread, TRUE);
		}
		BringWindowToTop(hwndTcal);
		SetWindowPos(hwndTcal, HWND_TOP, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow(hwndTcal);
		SetActiveWindow(hwndTcal);
		if (attachedFg) AttachThreadInput(fgThread, targetThread, FALSE);
		if (attachedCurrent) AttachThreadInput(currentThread, targetThread, FALSE);
	}
}

static DWORD WINAPI tc_mouse_delayed_launch_thread(LPVOID lp)
{
	TC_MOUSE_DELAYED_LAUNCH* launch = (TC_MOUSE_DELAYED_LAUNCH*)lp;
	int retry;
	if (!launch) return 0;
	Sleep(250);
	ShellExecuteUtf8Strict(g_hwndMain, "open", launch->file[0] ? launch->file : NULL,
		launch->args[0] ? launch->args : NULL,
		launch->workdir[0] ? launch->workdir : NULL,
		launch->showCmd ? launch->showCmd : SW_SHOWNORMAL);
	if (launch->activateTCalendar) {
		for (retry = 0; retry < 20; ++retry) {
			HWND hwndTcal = FindWindowW(L"TCalendarStandaloneWindowClass", NULL);
			if (!hwndTcal) hwndTcal = FindWindowW(L"TCalendarWindowClass", NULL);
			if (hwndTcal) {
				tc_mouse_activate_tcalendar_window();
				break;
			}
			Sleep(100);
		}
	}
	free(launch);
	return 0;
}

static void tc_mouse_launch_with_delay(const char* file, const char* args, const char* workdir, int showCmd, int activateTCalendar)
{
	TC_MOUSE_DELAYED_LAUNCH* launch = (TC_MOUSE_DELAYED_LAUNCH*)malloc(sizeof(TC_MOUSE_DELAYED_LAUNCH));
	HANDLE hThread;
	if (!launch) {
		ShellExecuteUtf8Strict(g_hwndMain, "open", file, args, workdir, showCmd ? showCmd : SW_SHOWNORMAL);
		if (activateTCalendar) tc_mouse_activate_tcalendar_window();
		return;
	}
	ZeroMemory(launch, sizeof(TC_MOUSE_DELAYED_LAUNCH));
	if (file) lstrcpyn(launch->file, file, MAX_PATH);
	if (args) lstrcpyn(launch->args, args, 256);
	if (workdir) lstrcpyn(launch->workdir, workdir, MAX_PATH);
	launch->showCmd = showCmd;
	launch->activateTCalendar = activateTCalendar;
	hThread = CreateThread(NULL, 0, tc_mouse_delayed_launch_thread, launch, 0, NULL);
	if (hThread) {
		CloseHandle(hThread);
	}
	else {
		free(launch);
		ShellExecuteUtf8Strict(g_hwndMain, "open", file, args, workdir, showCmd ? showCmd : SW_SHOWNORMAL);
		if (activateTCalendar) tc_mouse_activate_tcalendar_window();
	}
}

/*------------------------------------------------
    convert hotkey flag
--------------------------------------------------*/
BYTE hkf2modf(BYTE hkf)
{
	BYTE modf = 0;
	if (hkf & HOTKEYF_ALT) modf |= MOD_ALT;
	if (hkf & HOTKEYF_SHIFT) modf |= MOD_SHIFT;
	if (hkf & HOTKEYF_CONTROL) modf |= MOD_CONTROL;
	if (hkf & HOTKEYF_EXT) modf |= MOD_WIN;
	return modf;
}

/*------------------------------------------------
    register hotkey
--------------------------------------------------*/
static void InitHotkey(HWND hwnd)
{
	int i;
	char entry[32];
	WORD hotkey;
	for (i = 0; i < 4; i++)
	{
		if (atomHotkey[i]) continue;
		wsprintf(entry, "15%dHotkey", i + 1);
		hotkey = (WORD)GetMyRegLong(reg_section, entry, 0);
		if (!hotkey) continue;
		atomHotkey[i] = GlobalAddAtom(atomName[i]);
		if (!atomHotkey[i]) continue;
		if (!RegisterHotKey(hwnd, atomHotkey[i], hkf2modf(HIBYTE(hotkey)), LOBYTE(hotkey)))
		{
			GlobalDeleteAtom(atomHotkey[i]);
			atomHotkey[i] = 0;
		}
	}

	for (i = 0; i < 32; ++i) {
		idTCaptureHotkey[i] = 0;
		tcapProfileByHotkey[i][0] = 0;
	}

	{
		int count = (int)GetMyRegLong("TCapture", "HotkeyCount", 0);
		if (count > 32) count = 32;
		for (i = 1; i <= count; ++i)
		{
			char keyProfile[32];
			char keyValue[32];
			char profile[128];
			char value[128];
			UINT mods = 0;
			UINT vk = 0;
			UINT hotkeyId;
			wsprintf(keyProfile, "Hotkey%dProfile", i);
			wsprintf(keyValue, "Hotkey%dValue", i);
			GetMyRegStr("TCapture", keyProfile, profile, (int)sizeof(profile), "");
			GetMyRegStr("TCapture", keyValue, value, (int)sizeof(value), "");
			if (!profile[0] || !value[0]) continue;
			if (!ParseTCaptureHotkey(value, &mods, &vk)) continue;
			hotkeyId = 0x7A00u + (UINT)(i - 1);
			if (!RegisterHotKey(hwnd, hotkeyId, mods, vk)) {
				continue;
			}
			idTCaptureHotkey[i - 1] = hotkeyId;
			lstrcpyn(tcapProfileByHotkey[i - 1], profile, (int)sizeof(tcapProfileByHotkey[i - 1]));
		}
	}
}

/*------------------------------------------------
    unregister hotkey
--------------------------------------------------*/
static void EndHotkey(HWND hwnd)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (!atomHotkey[i]) continue;
		UnregisterHotKey(hwnd, atomHotkey[i]);
		GlobalDeleteAtom(atomHotkey[i]);
		atomHotkey[i] = 0;
	}
	for (i = 0; i < 32; ++i)
	{
		if (!idTCaptureHotkey[i]) continue;
		UnregisterHotKey(hwnd, idTCaptureHotkey[i]);
		idTCaptureHotkey[i] = 0;
		tcapProfileByHotkey[i][0] = 0;
	}
}

/*------------------------------------------------
    reset hotkey
--------------------------------------------------*/
void ResetHotkey(HWND hwnd)
{
	EndHotkey(hwnd);
	InitHotkey(hwnd);
}

/*------------------------------------------------
    invoke hotkey
--------------------------------------------------*/
void OnHotkey(HWND hwnd, int id)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (atomHotkey[i] == id)
		{
			PostMessage(hwnd, WM_COMMAND, IDC_HOTKEY1 + i, 0);
			return;
		}
	}
	for (i = 0; i < 32; ++i)
	{
		if ((int)idTCaptureHotkey[i] == id)
		{
			TriggerTCaptureProfile(hwnd, tcapProfileByHotkey[i]);
			return;
		}
	}
}

/*------------------------------------------------
    initialize registry data
--------------------------------------------------*/
void InitMouseFunction(HWND hwnd)
{
	int i;
	LONG n;
	char *old_entry[] = { "LClick", "LDblClick" };
	char entry[20];
	char s[256];

	last_tickcount = GetTickCount();

	// save old data with new format
	for(i = 0; i < 2; i++)
	{
		n = GetMyRegLong(reg_section, old_entry[i], -1);
		if(n < 0) continue;

		DelMyReg(reg_section, old_entry[i]);
		wsprintf(entry, "0%d", i + 1);
		SetMyRegLong(reg_section, entry, n);
		if(n == 6)
		{
			GetMyRegStr(reg_section, "ClipFormat", s, 256, "");
			if(s[0])
			{
				DelMyReg(reg_section, "ClipFormat");
				wsprintf(entry, "0%dClip", i + 1);
				SetMyRegStr(reg_section, entry, s);
			}
		}
		else if(n == 100)
		{
			strcpy(entry, old_entry[i]); strcat(entry, "File");
			GetMyRegStr(reg_section, entry, s, 256, "");
			if(s[0])
			{
				DelMyReg(reg_section, entry);
				wsprintf(entry, "0%dFile", i + 1);
				SetMyRegStr(reg_section, entry, s);
			}
		}
	}
	InitHotkey(hwnd);
}

void EndMouseFunction(HWND hwnd)
{
	EndHotkey(hwnd);
}

/*------------------------------------------------
   when files dropped to the clock
--------------------------------------------------*/
void OnDropFiles(HWND hwnd, HDROP hdrop)
{
	char fname[MAX_PATH], sname[MAX_PATH];
	char app[1024];
	SHFILEOPSTRUCT shfos;
	char *buf, *p;
	int i, num;
	int nType;

	nType = GetMyRegLong(reg_section, "DropFiles", 0);

	num = DragQueryFile(hdrop, (UINT)-1, NULL, 0);
	if(num <= 0) return;
	buf = malloc(num*MAX_PATH);
	if(buf == NULL) return;
	p = buf;
	for(i = 0; i < num; i++)
	{
		DragQueryFile(hdrop, i, fname, MAX_PATH);
		if(nType == 1 || nType == 3 || nType == 4)  // ごみ箱、コピー、移動
		{                             // '\0'で区切られたファイル名
			strcpy(p, fname); p += strlen(p) + 1;
		}
		else if(nType == 2) //プログラムで開く：
		{                   //スペースで区切られた短いファイル名
			if(num > 1) GetShortPathName(fname, sname, MAX_PATH);
			else wsprintf(sname, "\"%s\"", fname);
			strcpy(p, sname);
			p += strlen(p);
			if(num > 1 && i < num - 1) { *p = ' '; p++; }
		}
	}
	*p = 0;
	DragFinish(hdrop);

	GetMyRegStr(reg_section, "DropFilesApp", app, 1024, "");

	if(nType == 1 || nType == 3 || nType == 4)  // ごみ箱、コピー、移動
	{
		memset(&shfos, 0, sizeof(SHFILEOPSTRUCT));
		shfos.hwnd = NULL;
		if(nType == 1) shfos.wFunc = FO_DELETE;
		else if(nType == 3) shfos.wFunc = FO_COPY;
		else if(nType == 4) shfos.wFunc = FO_MOVE;
		shfos.pFrom = buf;
		if(nType == 3 || nType == 4) shfos.pTo = app;
		shfos.fFlags = FOF_ALLOWUNDO|FOF_NOCONFIRMATION;
		SHFileOperation(&shfos);
	}
	else if(nType == 2) //ファイルで開く
	{
		char *command;
		DWORD len = lstrlen(app) + 1 + lstrlen(buf) + 1;
		command = malloc(len);
		if (command)
		{
			strcpy(command, app);
			strcat(command, " ");
			strcat(command, buf);
			ExecFile(hwnd, command);
			free(command);
		}
	}
	free(buf);
}

/*------------------------------------------------------------
   when the clock clicked

   registry format
   name    value
   03      3           left button triple click -> Minimize All
   32      100         x-1 button  double click -> Run Notepad
   32File  C:\Windows\notepad.exe
--------------------------------------------------------------*/
void OnMouseMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LONG n_func;
	int button;
	UINT doubleclick_time;
	int i;
	BOOL bDown = FALSE;

	if(timer) KillTimer(hwnd, IDTIMER_MOUSE);
	timer = FALSE;
	mouse_zone_reset();

	switch(message)
	{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
			if (wParam & MK_CONTROL)
				button = 5;
			else if (wParam & MK_SHIFT)
				button = 10;
			else
				button = 0;
			break;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			if (wParam & MK_CONTROL)
				button = 6;
			else if (wParam & MK_SHIFT)
				button = 11;
			else
				button = 1;
			break;
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
			if (wParam & MK_CONTROL)
				button = 7;
			else if (wParam & MK_SHIFT)
				button = 12;
			else
				button = 2;
			break;
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
			if(HIWORD(wParam) == XBUTTON1)
			{
				if (LOWORD(wParam) & MK_CONTROL)
					button = 8;
				else if (LOWORD(wParam) & MK_SHIFT)
					button = 13;
				else
					button = 3;
			}
			else if(HIWORD(wParam) == XBUTTON2)
			{
				if (LOWORD(wParam) & MK_CONTROL)
					button = 9;
				else if (LOWORD(wParam) & MK_SHIFT)
					button = 14;
				else
					button = 4;
			}
			else return;
			break;
		case WM_MOUSEWHEEL:
			{
				int zDelta, xPos, yPos;
				RECT rcClock;

				GetWindowRect(g_hwndClock, &rcClock);
				xPos = GET_X_LPARAM(lParam);
				yPos = GET_Y_LPARAM(lParam);
				if (!( xPos >= rcClock.left && xPos <= rcClock.right && yPos >= rcClock.top && yPos <= rcClock.bottom ))
					return;
				zDelta = (short) HIWORD(wParam);
				if (zDelta > 0)
				{
				if (LOWORD(wParam) & MK_CONTROL)
					ExecuteMouseFunction(hwnd, -1, 18, 1);
				else if (LOWORD(wParam) & MK_SHIFT)
					ExecuteMouseFunction(hwnd, -1, 20, 1);
				else
					ExecuteMouseFunction(hwnd, -1, 16, 1);
				}
				else
				{
				if (LOWORD(wParam) & MK_CONTROL)
					ExecuteMouseFunction(hwnd, -1, 19, 1);
				else if (LOWORD(wParam) & MK_SHIFT)
					ExecuteMouseFunction(hwnd, -1, 21, 1);
				else
					ExecuteMouseFunction(hwnd, -1, 17, 1);
				}
			return;
			break;
			}
		default: return;
	}

	switch(message)
	{
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
			if(last_mousedown != message) num_click = 0;
			last_mousedown = message;
			if(last_mousedown == WM_XBUTTONDOWN)
				last_xmousedown = HIWORD(wParam);
			bDown = TRUE;
			break;
		case WM_LBUTTONUP:
			if(last_mousedown != WM_LBUTTONDOWN) last_mousedown = 0;
			break;
		case WM_RBUTTONUP:
			if(last_mousedown != WM_RBUTTONDOWN) last_mousedown = 0;
			break;
		case WM_MBUTTONUP:
			if(last_mousedown != WM_MBUTTONDOWN) last_mousedown = 0;
			break;
		case WM_XBUTTONUP:
			if(last_mousedown != WM_XBUTTONDOWN ||
				last_xmousedown != HIWORD(wParam))
			{
				last_mousedown = 0; last_xmousedown = 0;
			}
			break;
	}

	if(last_mousedown == 0) { num_click = 0; return; }

	doubleclick_time = GetDoubleClickTime();


	if(GetTickCount() - last_tickcount > doubleclick_time)
		num_click = 0;
	last_tickcount = GetTickCount();

	if(bDown)
	{
		if (!mouse_zone_resolve_pending(button, num_click + 1, lParam, &n_func))
			n_func = GetMouseFuncNum(button, num_click + 1);
		if(n_func >= 0 )
		{
			for(i = num_click + 2; i <= 4; i++)
			{
				if(mouse_zone_has_action(button, i)) {
					mouse_zone_reset();
					return;
				}
			}
			num_click++;
			exec_button = button;
			OnTimerMouse(hwnd);
		}
		else
		{
			mouse_zone_reset();
		}
		return;
	}

	num_click++;

	if (!mouse_zone_resolve_pending(button, num_click, lParam, &n_func))
		n_func = GetMouseFuncNum(button, num_click);
	if(n_func < 0) {
		mouse_zone_reset();
		return;
	}

	for(i = num_click + 1; i <= 4; i++)
	{
		if(mouse_zone_has_action(button, i))
		{
			exec_button = button;
			timer = TRUE;
			SetTimer(hwnd, IDTIMER_MOUSE, doubleclick_time, 0);
			return;
		}
	}

	exec_button = button;
	OnTimerMouse(hwnd);
}

/*------------------------------------------------
   execute mouse function
--------------------------------------------------*/
void ExecuteMouseFunction(HWND hwnd, LONG fnc, int btn, int clk)
{
	if(fnc < 0)
	{
		fnc = GetMouseFuncNum(btn, clk);
		if(fnc < 0) return;
	}
	switch (fnc)
	{
		case MOUSEFUNC_DATETIME:
		{
			WPARAM wParam = 0;
			HWND hwndTray;
			if(fnc == MOUSEFUNC_DATETIME)
			{ 
				wParam = IDC_DATETIME;
			}
			hwndTray = FindWindowW(L"Shell_TrayWnd", NULL);
			if(hwndTray) PostMessage(hwndTray, WM_COMMAND, wParam, 0);
			break;
		}
		case MOUSEFUNC_PROPERTY:
		{
			MyPropertyDialog();
			break;
		}
		case MOUSEFUNC_TCLOCKMENU:
		{
			POINT pos;
			GetCursorPos(&pos);
			OnContextMenu(hwnd, NULL, pos.x, pos.y);
			break;
		}
		case MOUSEFUNC_VISTACALENDAR:
		{
			PostMessage(g_hwndClock, CLOCKM_VISTACALENDAR, 0, 0);
			break;
		}
		case MOUSEFUNC_SHOWAVAILABLENETWORKS:
		{
			PostMessage(g_hwndClock, CLOCKM_SHOWAVAILABLENETWORKS, 0, 0);
			return;
		}
		case MOUSEFUNC_OPENFILE:
		{
			char fname[1024];
			char entry[32];
			GetMouseFileEntry(btn, clk, entry, (int)sizeof(entry));
			GetMyRegStr(reg_section, entry, fname, 1024, "");
			if(fname[0]) ExecFile(hwnd, fname);
			break;
		}
		case MOUSEFUNC_CUSTOMPROGRAM:
		{
			char file[1024];
			char workdir[1024];
			char entry[32];
			GetMouseFileEntry(btn, clk, entry, (int)sizeof(entry));
			GetMyRegStr(reg_section, entry, file, (int)sizeof(file), "");
			GetMouseWorkDirEntry(btn, clk, entry, (int)sizeof(entry));
			GetMyRegStr(reg_section, entry, workdir, (int)sizeof(workdir), "");
			if (file[0])
				ShellExecuteUtf8Strict(hwnd, "open", file, NULL, workdir[0] ? workdir : NULL, SW_SHOWNORMAL);
			break;
		}
		case MOUSEFUNC_FILELIST:
		{
			POINT tPoint;

			//現在のマウスカーソル位置を取得
			GetCursorPos(&tPoint);

			showUserMenu(hwnd, 0, tPoint.x, tPoint.y, btn, clk);
			break;
		}
		//  Added by TTTT to execute Task Manager
		case MOUSEFUNC_TASKMGR:
		{
			ShellExecuteW(NULL, L"open", L"taskmgr", NULL, NULL, SW_SHOWNORMAL);
			SetWindowPos(GetActiveWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			break;
		}

		//  Added by TTTT to execute Control Panel
		case MOUSEFUNC_CONTROLPNL:
		{
			ShellExecuteW(NULL, L"open", L"control", NULL, NULL, SW_SHOWNORMAL);
//			SetWindowPos(GetActiveWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			break;
		}

		//  Added by TTTT to execute Setting Application
		case MOUSEFUNC_SETTING:
		{
			ShellExecuteW(NULL, L"open", L"ms-settings:", NULL, NULL, SW_SHOWNORMAL);
			//			SetWindowPos(GetActiveWindow(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			break;
		}

		//  Added by TTTT to open Command prompt
		case MOUSEFUNC_CMD:
		{
			ShellExecuteW(NULL, L"open", L"cmd.exe", L"/k cd \\", NULL, SW_SHOWNORMAL);
			break;
		}

		//  Added by TTTT to open Power Management Panel
		case MOUSEFUNC_POWERPNL:
		{
			ShellExecuteW(NULL, L"open", L"control", L"/name Microsoft.PowerOptions /page pageGlobalSettings", NULL, SW_SHOWNORMAL);
			break;
		}

		//  Added by TTTT to open Network Setting Panel
		case MOUSEFUNC_NETWORKSTG:
		{
			ShellExecuteW(NULL, L"open", L"ms-settings:network-status", NULL, NULL, SW_SHOWNORMAL);
			break;
		}

		case MOUSEFUNC_NETWORKPNL:
		{
			ShellExecuteW(NULL, L"open", L"control", L"ncpa.cpl", NULL, SW_SHOWNORMAL);
			break;
		}



		//  Added by TTTT to open DataPlan Usage Panel
		case MOUSEFUNC_DATAUSAGE:
		{
			ShellExecuteW(NULL, L"open", L"ms-settings:datausage", NULL, NULL, SW_SHOWNORMAL);
			break;
		}

		case MOUSEFUNC_ALARM_CLOCK:
		{
			ShellExecuteW(NULL, L"open", L"ms-clock:", NULL, NULL, SW_SHOWNORMAL);
			break;
		}

		case MOUSEFUNC_TCALENDAR_OPEN:
		{
			char tcalExePath[MAX_PATH];
			if (!GetTCalendarEnableConfigMouse()) break;
			if (!ResolveTCalendarExePathMouse(tcalExePath, MAX_PATH)) break;
			tc_mouse_launch_with_delay(tcalExePath, NULL, g_mydir, SW_SHOWNORMAL, 1);
			break;
		}

		case MOUSEFUNC_TCAPTURE_SETTINGS:
		{
			char tcapExePath[MAX_PATH];
			const char* tcapSettingsParams;
			if (!GetTCaptureEnableConfigMouse()) break;
			if (!ResolveTCaptureExePathMouse(tcapExePath, MAX_PATH)) break;
			tcapSettingsParams = b_EnglishMenu ? "--settings --lang en" : "--settings --lang ja";
			ShellExecuteUtf8Strict(hwnd, "open", tcapExePath, tcapSettingsParams, g_mydir, SW_SHOWNORMAL);
			break;
		}

		case MOUSEFUNC_PULLBACK:
		{
			extern int PullBackIndex;
			PullBackIndex = 0;
			EnumWindows(PullBackOBWindow, (LPARAM)0);
			break;
		}
	}
}

/*------------------------------------------------
   mouse timer event
--------------------------------------------------*/
void OnTimerMouse(HWND hwnd)
{
	int button;
	LONG fnc;

	button = exec_button;
	if(timer) KillTimer(hwnd, IDTIMER_MOUSE); timer = FALSE;
	fnc = exec_zone_active ? exec_zone_func : -1;

	ExecuteMouseFunction(hwnd, fnc, button, num_click);
	mouse_zone_reset();
}

int GetMouseFuncNum(int button, int nclick)
{
	char entry[20];
	wsprintf(entry, "%d%d", button, nclick);
	return GetMyRegLong(reg_section, entry, -1);
}

void GetMouseFileEntry(int btn, int clk, char* entry, int cch)
{
	if (!entry || cch <= 0) return;
	entry[0] = '\0';
	if (exec_zone_active &&
		exec_zone_button == btn &&
		exec_zone_click == clk &&
		exec_zone_entry >= 1 &&
		exec_zone_entry <= 3) {
		mouse_pick_text(btn, clk, exec_zone_entry, "File", entry, cch);
	}
	else {
		wsprintf(entry, "%d%dFile", btn, clk);
	}
}

static void GetMouseWorkDirEntry(int btn, int clk, char* entry, int cch)
{
	if (!entry || cch <= 0) return;
	entry[0] = '\0';
	if (exec_zone_active &&
		exec_zone_button == btn &&
		exec_zone_click == clk &&
		exec_zone_entry >= 1 &&
		exec_zone_entry <= 3) {
		mouse_pick_text(btn, clk, exec_zone_entry, "WorkDir", entry, cch);
	}
	else {
		wsprintf(entry, "%d%dWorkDir", btn, clk);
	}
}

void PushKeybd(LPKEYEVENT lpkey)
{
	while(lpkey->key != 0xff){
		keybd_event(lpkey->key,(BYTE)MapVirtualKey(lpkey->key,0),lpkey->flag, 0);
		lpkey++;
	}
}
