/*-------------------------------------------------------------
  main.c
  WinMain, window procedure, and functions for initializing
---------------------------------------------------------------*/

#include "tclock.h"
#include <winver.h>
#include <shellapi.h>
#include "../common/text_codec.h"
#include "../common/ini_io_utf8.h"

#define AUTORESTART_WAIT_WIN11	5000	//Win11でのb_AutoRestart時のウェイト(ms)

// Globals
HINSTANCE g_hInst;           // instance handle
HINSTANCE g_hInstResource;   // instance handle of language module
HWND      g_hwndMain = 0;    // main window
HWND      g_hwndClock;       // clock window
HWND      g_hwndPropDlg;     // property sheet window

HICON     g_hIconTClock;
char      g_mydir[MAX_PATH]; // path to tclock.exe
char      g_langdllname[MAX_PATH];  // language dll name
//BOOL      g_bIniSetting = TRUE;

char      g_inifile[MAX_PATH];		//フルパスつき*.iniファイル名

BOOL	b_DisplayChanged = FALSE;






// スワップアウトさせる /WS:AGGRESSIVE			
// 20181125 Ver3.3.2.1でコードからはコメントアウトして様子を見る。スワップアウトしても0.3-2MB程度が、スワップしなければ最大3.5MB程度。
#define DO_WS_AGGRESSIVE() \
          SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);



#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL  0x020A
#endif
#ifndef WM_MENURBUTTONUP
#define WM_MENURBUTTONUP 0x0122
#endif


static DWORD exeVersionM = 0;
static DWORD exeVersionL = 0;
char exeVersionString[32];

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);	//tclock.exe本体のウィンドウプロシージャコールバック


wchar_t szClassName[] = L"TClockMainClass"; // window class name
wchar_t szWindowText[] = L"TClock";         // caption of the window	(TClock-Win10にする？)

static BOOL bMenuOpened = FALSE;
static BOOL bDestroy = FALSE;

static NOTIFYICONDATA notifyIconData;

void CheckCommandLine(HWND hwnd);
static void OnTimerMain(HWND hwnd);
static void InitError(int n);
static BOOL CheckTCDLL(void);
static BOOL CheckDLL(const char *fname);
static void CheckRegistry(void);
static BOOL IsWow64(void);
static BOOL CheckRegistry_Win10(void); //Added by TTTT
static void CreateDefaultIniFile_Win10(const wchar_t* fnameW); //Added by TTTT
void getExeVersion(const char *fname); //Added by TTTT
void SetIdlePriority(void);		//Added by TTTT

//void OnTimerZombieCheck(HWND hwnd); //Added by TTTT
void OnTimerZombieCheck2(HWND hwnd);

void OnDLLAliveMessage(WPARAM tempwParam); //Added by TTTT

void TerminateTClock(HWND hwnd);

void TerminateTClockFromDLL(HWND hwnd);

BOOL WaitQuitPrevTClock(int cycle);

static BOOL IsUserAdmin(void);


static BOOL AddMessageFilters(void);
static BOOL HasCommandLineOption(const wchar_t *option);
static BOOL PrefixEqualsNoCaseW(const wchar_t* text, const wchar_t* prefix, int prefixLen);
static int MessageBoxUtf8Strict(HWND hwnd, const char* text, const char* caption, UINT type);
static void NormalizeSettingUtf8InPlace(char* value, int valueBytes);
static int DecodeDialogUtf8StrictToWide(const char* utf8, wchar_t* wide, int wideCch);
static BOOL SetHideClockPolicyValue(DWORD value);
static BOOL IsHideClockPolicyEnabled(void);
static BOOL WaitExplorerReady(DWORD timeoutMs);
static void RestartExplorerForHideClock(void);
static void ApplyHideClockPolicyFlow(void);
static void RestoreHideClockPolicyFlow(void);
static LONG GetTCycleEnableConfig(void);
static void GetTCyclePathConfig(char* outPath, int outPathLen);
static void LaunchTCycleAgentIfEnabled(void);
static LONG GetTCaptureEnableConfig(void);
static void GetTCapturePathConfig(char* outPath, int outPathLen);
static void SyncTCaptureIntegrationIniPath(const char* tcapExePath);
static void LaunchTCaptureAgentIfEnabled(void);
static LONG GetTCalendarAlertEnableConfig(void);
static void GetTCalendarPathConfig(char* outPath, int outPathLen);
static void LaunchTCalendarAlertIfEnabled(void);
static void EnsureTCalendarConfigDefaults(void);
static void TerminateExtensionsOnExplicitExitIfNeeded(void);

static UINT s_uTaskbarRestart = 0;
static BOOL bcontractTimer = FALSE;
//static int nCountFindingClock = -1;

BOOL b_DebugLog = FALSE;		//added by TTTT
BOOL b_DebugLog_RegAccess = FALSE;
BOOL b_DebugLog_Specific = FALSE;

BOOL b_NormalLog = FALSE;		//added by TTTT
BOOL b_EnglishMenu =FALSE;		//Added by TTTT

BOOL b_ShowTrayIcon = FALSE;

// XButton Messages
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP   0x020C

// menu.c
extern HMENU g_hMenu;

BOOL b_AutoRestart = TRUE;

BOOL b_UnplugDriveAvailable = FALSE;
void CheckUnplugDrive(void);

int Language_Offset = LANGUAGE_OFFSET_JAPANESE;


//BOOL b_AcceptRisk;

//BOOL b_RestartDLL = FALSE;
BOOL b_RestartNOW = FALSE;

BOOL b_Exit = FALSE;

int zombieCheckInterval = 5;
int TaskbarThreadID = 0;

BOOL b_FlagDLLAlive = TRUE;

ULONGLONG lastFileTimeDLLAlive = 0;

HWND hwndTaskBar_Prev = NULL;


BOOL b_ModernStandbySupported = FALSE;

int countRestart = 0;
int g_Win11ZombieMissCount = 0;
BOOL b_UseHideClockPolicyFlow = FALSE;
BOOL b_HideClockPolicyApplied = FALSE;
BOOL b_HideClockPolicyWasEnabled = FALSE;
BOOL b_SkipHideClockRestore = FALSE;
BOOL b_IgnoreTaskbarRestartForHideClock = FALSE;
BOOL g_ExitRequestedFromMenu = FALSE;


/*-------------------------------------------------------
    mouse function list
---------------------------------------------------------*/
static MOUSE_FUNC_INFO mouse_func_list[] = {
	{	MOUSEFUNC_NONE,			IDS_NONE},
	{ MOUSEFUNC_TCLOCKMENU,		IDS_TCLOCKMENU},
	{ MOUSEFUNC_PROPERTY,		IDS_PROPERTY },
	{ MOUSEFUNC_VISTACALENDAR,	IDS_VISTACALENDAR },
	{ MOUSEFUNC_ALARM_CLOCK,	IDS_ALARM_CLOCK2 },
	{ MOUSEFUNC_PULLBACK,		IDS_PULLBACK },
	{ MOUSEFUNC_SHOWAVAILABLENETWORKS,		IDS_SHOWAVAILABLENETWORKS },
	{ MOUSEFUNC_TASKMGR,		IDS_TASKMGR },	//Added by TTTT
	{ MOUSEFUNC_CMD,			IDS_CMD },		//Added by TTTT
	{ MOUSEFUNC_CONTROLPNL,		IDS_CONTROLPNL },	//Added by TTTT
	{ MOUSEFUNC_POWERPNL,		IDS_POWERPNL },	//Added by TTTT
	{ MOUSEFUNC_NETWORKPNL,		IDS_NETWORKPNL },	//Added by TTTT
	{ MOUSEFUNC_SETTING,		IDS_SETTING },	//Added by TTTT
	{ MOUSEFUNC_NETWORKSTG,		IDS_NETWORKSTG},	//Added by TTTT
	{ MOUSEFUNC_DATAUSAGE,		IDS_DATAUSAGE },	//Added by TTTT
	{ MOUSEFUNC_TCALENDAR_OPEN,	IDS_TCAL_OPEN },
	{ MOUSEFUNC_TCAPTURE_SETTINGS,	IDS_TCAP_SETTING },
	{ MOUSEFUNC_DATETIME,		IDS_PROPDATE },
	{ MOUSEFUNC_OPENFILE,		IDS_OPENFILE},
	{ MOUSEFUNC_FILELIST,		IDS_FILELIST}
};

MOUSE_FUNC_INFO *GetMouseFuncList(void)
{
	return mouse_func_list;
}
int GetMouseFuncCount(void)
{
	return sizeof(mouse_func_list) / sizeof(MOUSE_FUNC_INFO);
}

static BOOL PrefixEqualsNoCaseW(const wchar_t* text, const wchar_t* prefix, int prefixLen)
{
	if (!text || !prefix || prefixLen <= 0) return FALSE;
	return CompareStringOrdinal(text, prefixLen, prefix, prefixLen, TRUE) == CSTR_EQUAL;
}

static BOOL HasCommandLineOption(const wchar_t *option)
{
    wchar_t *p = GetCommandLineW();
    size_t n = 0;
    while (option && option[n]) n++;

    while (*p) {
        while (*p && *p != '/' && *p != '-') p++;
        if (!*p) break;
        p++;
        if (PrefixEqualsNoCaseW(p, option, (int)n)) return TRUE;
        while (*p && *p != L' ') p++;
    }
	return FALSE;
}

static int DecodeDialogUtf8StrictToWide(const char* utf8, wchar_t* wide, int wideCch)
{
	if (!utf8) utf8 = "";
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, wideCch);
}

static int MessageBoxUtf8Strict(HWND hwnd, const char* text, const char* caption, UINT type)
{
	wchar_t wText[2048];
	wchar_t wCaption[256];
	int retText;
	int retCaption;

	if (!text) text = "";
	if (!caption) caption = "TClock-Win11";

	retText = DecodeDialogUtf8StrictToWide(text, wText, sizeof(wText) / sizeof(wText[0]));
	if (retText <= 0) {
		lstrcpynW(wText, L"[Message decode error]", sizeof(wText) / sizeof(wText[0]));
	}

	retCaption = DecodeDialogUtf8StrictToWide(caption, wCaption, sizeof(wCaption) / sizeof(wCaption[0]));
	if (retCaption <= 0) {
			lstrcpynW(wCaption, L"TClock-Win11", sizeof(wCaption) / sizeof(wCaption[0]));
	}

	{
		UINT uBeep = 0xFFFFFFFF;
		if (type & MB_ICONHAND) uBeep = MB_ICONHAND;
		else if (type & MB_ICONQUESTION) uBeep = MB_ICONQUESTION;
		else if (type & MB_ICONEXCLAMATION) uBeep = MB_ICONEXCLAMATION;
		else if (type & MB_ICONASTERISK) uBeep = MB_ICONASTERISK;
		return MyMessageBoxW(hwnd, wText, wCaption, type, uBeep);
	}
}

static BOOL SetHideClockPolicyValue(DWORD value)
{
    HKEY hkey;
    DWORD disp;
    LONG rc;
    char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";

    rc = RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disp);
    if (rc != ERROR_SUCCESS) return FALSE;

    rc = RegSetValueEx(hkey, "HideClock", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS);
}

static BOOL IsHideClockPolicyEnabled(void)
{
    HKEY hkey;
    DWORD reg_data = 0;
    DWORD regtype = 0;
    DWORD size = sizeof(DWORD);
    LONG rc;
    char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";

    rc = RegOpenKeyEx(HKEY_CURRENT_USER, regPath, 0, KEY_QUERY_VALUE, &hkey);
    if (rc != ERROR_SUCCESS) return FALSE;

    rc = RegQueryValueEx(hkey, "HideClock", NULL, &regtype, (LPBYTE)&reg_data, &size);
    RegCloseKey(hkey);
    if (rc != ERROR_SUCCESS) return FALSE;

    return (regtype == REG_DWORD && reg_data != 0);
}

static BOOL WaitExplorerReady(DWORD timeoutMs)
{
    DWORD deadline = GetTickCount() + timeoutMs;
    HWND hwndShell;
    HWND hwndTray;

    do {
        hwndShell = FindWindowW(L"Shell_TrayWnd", NULL);
        if (hwndShell) {
            hwndTray = FindWindowExW(hwndShell, NULL, L"TrayNotifyWnd", NULL);
            if (hwndTray) {
                Sleep(500);
                return TRUE;
            }
        }
        Sleep(250);
    } while (GetTickCount() < deadline);

    return FALSE;
}

static void RestartExplorerForHideClock(void)
{
    b_IgnoreTaskbarRestartForHideClock = TRUE;
    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/F /IM explorer.exe", NULL, SW_HIDE);
    Sleep(1200);
    ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOWDEFAULT);
    WaitExplorerReady(20000);
}

static void ApplyHideClockPolicyFlow(void)
{
    if (!b_UseHideClockPolicyFlow) return;
    if (b_HideClockPolicyWasEnabled) return;
    if (!SetHideClockPolicyValue(1)) return;
    b_HideClockPolicyApplied = TRUE;
    RestartExplorerForHideClock();
}

static void RestoreHideClockPolicyFlow(void)
{
    if (!b_HideClockPolicyApplied) return;
    if (!SetHideClockPolicyValue(0)) return;
    RestartExplorerForHideClock();
    b_HideClockPolicyApplied = FALSE;
}

static LONG GetTCycleEnableConfig(void)
{
    LONG v = GetMyRegLong("TCycle", "Enable", -1);
    if (v == -1) {
        SetMyRegLong("TCycle", "Enable", 0);
        return 0;
    }
    return (v != 0) ? 1 : 0;
}

static void GetTCyclePathConfig(char* outPath, int outPathLen)
{
    char before[MAX_PATH];
    BOOL wasMissing = FALSE;
    if (!outPath || outPathLen <= 0) return;
    outPath[0] = '\0';

    GetMyRegStr("TCycle", "Path", outPath, outPathLen, "");
    if (outPath[0] == '\0') {
        strcpy(outPath, "TCycle.exe");
        wasMissing = TRUE;
    }

    lstrcpyn(before, outPath, (int)sizeof(before));
    NormalizeSettingUtf8InPlace(outPath, outPathLen);
    if (wasMissing || lstrcmp(before, outPath) != 0) {
        SetMyRegStr("TCycle", "Path", outPath);
    }
}

static void LaunchTCycleAgentIfEnabled(void)
{
    char tcycPathCfg[MAX_PATH];
    char exePath[MAX_PATH];
    HINSTANCE launchResult;

    if (!GetTCycleEnableConfig()) return;

    GetTCyclePathConfig(tcycPathCfg, MAX_PATH);
    if (tcycPathCfg[0] == 0) strcpy(tcycPathCfg, "TCycle.exe");
    if ((tcycPathCfg[1] == ':') || (tcycPathCfg[0] == '\\') || (tcycPathCfg[0] == '/')) {
        strcpy(exePath, tcycPathCfg);
    }
    else {
        strcpy(exePath, g_mydir);
        add_title(exePath, tcycPathCfg);
    }

    if (!PathFileExistsUtf8Strict(exePath)) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] TCycleEnable=1 but TCycle target was not found");
        return;
    }

    launchResult = ShellExecuteUtf8Strict(NULL, "open", exePath, NULL, g_mydir, SW_HIDE);
    if ((INT_PTR)launchResult <= 32 && b_DebugLog) {
        WriteDebug_New2("[exemain.c] Failed to launch TCycle runtime");
    }
}

static LONG GetTCaptureEnableConfig(void)
{
    LONG v = GetMyRegLong("TCapture", "Enable", -1);
    if (v != -1) return (v != 0) ? 1 : 0;

    v = GetMyRegLong("ETC", "TCaptureEnable", 0);
    SetMyRegLong("TCapture", "Enable", (v != 0) ? 1 : 0);
    DelMyReg("ETC", "TCaptureEnable");
    return (v != 0) ? 1 : 0;
}

static void NormalizeSettingUtf8InPlace(char* value, int valueBytes)
{
    WCHAR wbuf[MAX_PATH];
    char utf8[MAX_PATH];
    if (!value || valueBytes <= 0 || value[0] == '\0') return;
    if (tc_utf8_to_utf16(value, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
    if (tc_utf16_to_utf8(wbuf, utf8, (int)sizeof(utf8)) <= 0) return;
    lstrcpyn(value, utf8, valueBytes);
}

static void GetTCapturePathConfig(char* outPath, int outPathLen)
{
    char legacyPath[MAX_PATH];
    char before[MAX_PATH];
    if (!outPath || outPathLen <= 0) return;
    outPath[0] = '\0';

    GetMyRegStr("TCapture", "Path", outPath, outPathLen, "");
    if (outPath[0] != '\0') {
        lstrcpyn(before, outPath, (int)sizeof(before));
        NormalizeSettingUtf8InPlace(outPath, outPathLen);
        if (lstrcmp(before, outPath) != 0) SetMyRegStr("TCapture", "Path", outPath);
        return;
    }

    GetMyRegStr("ETC", "TCapturePath", legacyPath, MAX_PATH, "TCapture.exe");
    if (legacyPath[0] == '\0') strcpy(legacyPath, "TCapture.exe");
    NormalizeSettingUtf8InPlace(legacyPath, (int)sizeof(legacyPath));
    lstrcpyn(outPath, legacyPath, outPathLen);
    SetMyRegStr("TCapture", "Path", outPath);
    DelMyReg("ETC", "TCapturePath");
}

static void SyncTCaptureIntegrationIniPath(const char* tcapExePath)
{
    char tcapIniPath[MAX_PATH];
    char current[MAX_PATH];

    if (!tcapExePath || !tcapExePath[0]) return;
    if (!g_inifile[0]) return;

    lstrcpyn(tcapIniPath, tcapExePath, MAX_PATH);
    del_title(tcapIniPath);
    add_title(tcapIniPath, "TCapture.ini");

    tc_ini_utf8_read_string(tcapIniPath, "Integration", "TClockIniPath", "", current, MAX_PATH);
    if (strcmp(current, g_inifile) == 0) return;

    if (!tc_ini_utf8_write_string(tcapIniPath, "Integration", "TClockIniPath", g_inifile)) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] Failed to sync Integration.TClockIniPath in TCapture.ini");
    }
}

static void EnsureTCalendarConfigDefaults(void)
{
    char tcalPath[MAX_PATH];
    LONG enable = GetMyRegLong("TCalendar", "Enable", -1);
    LONG alart = GetMyRegLong("TCalendar", "Alart", -1);
    if (enable == -1) {
        SetMyRegLong("TCalendar", "Enable", 0);
		SetMyRegLong("TCalendar", "Alart", 1);
    }
    if (alart == -1) {
        SetMyRegLong("TCalendar", "Alart", 1);
    }

    GetMyRegStr("TCalendar", "Path", tcalPath, MAX_PATH, "");
    if (tcalPath[0] == '\0') {
        SetMyRegStr("TCalendar", "Path", "TCalendar.exe");
    }
}

static void LaunchTCaptureAgentIfEnabled(void)
{
    char tcapPathCfg[MAX_PATH];
    char exePath[MAX_PATH];
    const char* launchParams;
    HINSTANCE launchResult;

    if (!GetTCaptureEnableConfig()) return;

    GetTCapturePathConfig(tcapPathCfg, MAX_PATH);
    if (tcapPathCfg[0] == 0) strcpy(tcapPathCfg, "TCapture.exe");
    if ((tcapPathCfg[1] == ':') || (tcapPathCfg[0] == '\\') || (tcapPathCfg[0] == '/')) {
        strcpy(exePath, tcapPathCfg);
    }
    else {
        strcpy(exePath, g_mydir);
        add_title(exePath, tcapPathCfg);
    }

    if (!PathFileExistsUtf8Strict(exePath)) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] TCaptureEnable=1 but TCapture target was not found");
        return;
    }

    SyncTCaptureIntegrationIniPath(exePath);

    launchParams = b_EnglishMenu ? "--agent --lang en" : "--agent --lang ja";
    launchResult = ShellExecuteUtf8Strict(NULL, "open", exePath, launchParams, g_mydir, SW_HIDE);
    if ((INT_PTR)launchResult <= 32 && b_DebugLog) {
        WriteDebug_New2("[exemain.c] Failed to launch TCapture agent");
    }
}

static LONG GetTCalendarAlertEnableConfig(void)
{
    LONG enable = GetMyRegLong("TCalendar", "Enable", -1);
    LONG alart = GetMyRegLong("TCalendar", "Alart", -1);

    if (enable == -1) {
        enable = 0;
        SetMyRegLong("TCalendar", "Enable", 0);
		SetMyRegLong("TCalendar", "Alart", 1);
    }
    if (alart == -1) {
        alart = 1;
        SetMyRegLong("TCalendar", "Alart", 1);
    }

    if (enable == 0) return 0;
    return (alart != 0) ? 1 : 0;
}

static void GetTCalendarPathConfig(char* outPath, int outPathLen)
{
    char before[MAX_PATH];
    if (!outPath || outPathLen <= 0) return;
    outPath[0] = '\0';

    GetMyRegStr("TCalendar", "Path", outPath, outPathLen, "");
    if (outPath[0] == '\0') {
        strcpy(outPath, "TCalendar.exe");
    }

    lstrcpyn(before, outPath, (int)sizeof(before));
    NormalizeSettingUtf8InPlace(outPath, outPathLen);
    if (lstrcmp(before, outPath) != 0) {
        SetMyRegStr("TCalendar", "Path", outPath);
    }
}

static void LaunchTCalendarAlertIfEnabled(void)
{
    char tcalPathCfg[MAX_PATH];
    char exePath[MAX_PATH];
    wchar_t wExePath[MAX_PATH];
    wchar_t wWorkDir[MAX_PATH];
    wchar_t cmdLine[MAX_PATH * 2];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (!GetTCalendarAlertEnableConfig()) return;

    GetTCalendarPathConfig(tcalPathCfg, MAX_PATH);
    if (tcalPathCfg[0] == 0) strcpy(tcalPathCfg, "TCalendar.exe");

    if ((tcalPathCfg[1] == ':') || (tcalPathCfg[0] == '\\\\') || (tcalPathCfg[0] == '/')) {
        strcpy(exePath, tcalPathCfg);
    }
    else {
        strcpy(exePath, g_mydir);
        add_title(exePath, tcalPathCfg);
    }

    if (!PathFileExistsUtf8Strict(exePath)) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] TCalendar alert enabled but target was not found");
        return;
    }

    if (DecodeDialogUtf8StrictToWide(exePath, wExePath, _countof(wExePath)) <= 0) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] Failed to decode TCalendar path for --alert");
        return;
    }
    if (DecodeDialogUtf8StrictToWide(g_mydir, wWorkDir, _countof(wWorkDir)) <= 0) {
        wWorkDir[0] = L'\0';
    }

    wsprintfW(cmdLine, L"\"%s\" --alert", wExePath);
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(wExePath, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        wWorkDir[0] ? wWorkDir : NULL, &si, &pi)) {
        if (b_DebugLog) WriteDebug_New2("[exemain.c] Failed to launch TCalendar --alert");
        return;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static void TerminateExtensionsOnExplicitExitIfNeeded(void)
{
    if (!g_ExitRequestedFromMenu) return;
    if (!GetMyRegLong("ETC", "ExitExtensionsOnTClockExit", 0)) return;

    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/F /IM TCycle.exe /T", NULL, SW_HIDE);
    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/F /IM TCalendar.exe /T", NULL, SW_HIDE);
    ShellExecuteW(NULL, L"open", L"taskkill.exe", L"/F /IM TCapture.exe /T", NULL, SW_HIDE);
    if (b_DebugLog) WriteDebug_New2("[exemain.c] ExitExtensionsOnTClockExit applied.");
}


/*-------------------------------------------------------
Wait Until Previous TClock Process Disappear
---------------------------------------------------------*/
BOOL WaitQuitPrevTClock(int cycle)
{
	HWND hwnd;

	for (int i = 0; i < cycle; i++)
	{
		hwnd = FindWindowW(szClassName, szWindowText);		//プロセスがすでに起動していたらhwnd != NULLになる
		if (hwnd == NULL) return FALSE;
		Sleep(100);
	}

	MessageBoxUtf8Strict(NULL, "TClock-Win11の再起動がうまくいかなかった可能性があります。現時点で正常に時計が改造されていない場合は、タスクマネージャーからTClock-Win11のプロセスを強制終了してください。\n\nRestarting TClock-Win11 may be unsuccessful. If you don't see the modified Clock on Taskbar, please kill the previous TClock-Win11 in the Taskmanager.",
		"TClock-Win11", MB_ICONEXCLAMATION | MB_SETFOREGROUND);

	return TRUE;
}


/*-------------------------------------------------------
Check UnplugDrive.exe availability
---------------------------------------------------------*/
void CheckUnplugDrive(void)
{

	char fname[MAX_PATH];
	strcpy(fname, g_mydir);
	add_title(fname, "UnplugDrive.exe");

	//b_UnplugDriveAvailable = TRUE;

	b_UnplugDriveAvailable = PathFileExistsUtf8Strict(fname);

}



/*-------------------------------------------------------
Status of DLL by TTTT
---------------------------------------------------------*/
void OnDLLAliveMessage(WPARAM tempwParam)
{
	(void)tempwParam;
	FILETIME ft;
	ULARGE_INTEGER ull;

	GetSystemTimeAsFileTime(&ft);
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;
	lastFileTimeDLLAlive = ull.QuadPart;
}


/*-------------------------------------------
   main routine
---------------------------------------------*/
static UINT WINAPI TclockExeMain(void)
{
	MSG msg;
	WNDCLASSW wndclass;
	HWND hwnd;
	HANDLE hSingleMutex;
	BOOL mutexAlreadyExists = FALSE;
	BOOL isRestartArg = FALSE;

	//CheckCommandLine(hwnd);



	//if (b_RestartDLL)
	//{
	//	hwnd = FindWindowW(szClassName, szWindowText);		//プロセスがすでに起動していたらhwnd != NULLになる
	//	for (int i = 0; i < 10; i++)
	//	{
	//		if (hwnd != NULL) Sleep(500);
	//		hwnd = FindWindowW(szClassName, szWindowText);		//プロセスがすでに起動していたらhwnd != NULLになる
	//	}
	//	if (hwnd != NULL)
	//	{
	//		MessageBoxUtf8Strict(NULL, "既存のTClock-Win10のプロセス終了に時間がかかっています。『OK』を押しても再起動しない場合にはタスクマネージャーからTClock-Win10のプロセスを強制終了してください。\n\nTerminating Previous TClock-Win10 is taking a long time. If you do not have the restarted TClock-Win10 even after clicking \"OK\", please kill the previous TClock-Win10 in the Taskmanager.",
	//			"TClock-Win10", MB_ICONEXCLAMATION | MB_SETFOREGROUND);
	//		hwnd = FindWindowW(szClassName, szWindowText);
	//		if (hwnd != NULL) return 1;
	//	}
	//}
	/* Use process mutex to prevent duplicate launch races; /restart is explicitly allowed. */
	isRestartArg = HasCommandLineOption(L"restart");
	hSingleMutex = CreateMutexW(NULL, FALSE, L"Local\\TClock-Win11-SingleInstance");
	if (hSingleMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
		mutexAlreadyExists = TRUE;
	}

	// check wow64
	if (IsWow64()) {
		MessageBoxUtf8Strict(NULL, "本実行ファイルは32bit (x86)バイナリです。\n64bit環境ではx64バイナリを使用する必要があります。\n\nThis is 32bit (x86) binary.\nx64 binary is required for 64bit Windows.",
			"TClock-Win11", MB_ICONERROR | MB_SETFOREGROUND);
		return 1;
	}



	// for Vista 
	if (IsUserAdmin()) {		//管理者権限があれば
		AddMessageFilters();	//メッセージフィルタ(ウィンドウプロシージャの)を設定する(AddMessageFilters)
	}	



	// Call WINAPI CheckWinVersion_Win10() in tcdll.dll
	if (CheckWinVersion_Win10() < 0x0400) // = WIN10, 1024
	{
		MessageBoxUtf8Strict(NULL, "本アプリケーションはWindows10以降用です。\n\nThis application works on Windows 10(Anniversary Update) or later.",
			"TClock-Win11", MB_ICONERROR | MB_SETFOREGROUND);
		return 1;
	}




	// get the path where .exe is positioned
	GetModuleFileNameUTF8(g_hInst, g_mydir, MAX_PATH);	//この時点ではフルパス付きのtclock実行ファイル名を取得
	char fname[MAX_PATH];
	strcpy(fname, g_mydir);		//exeのファイル名がついたままのg_mydirをfnameに入れて
	getExeVersion(fname);		//fileバージョンを取得してexeVersionM, exeVersionLを取得->DLLバージョンチェックに利用
	del_title(g_mydir);		//g_mydirはこれ以降、TClockのフォルダへのパスとして利用される



	//CheckRegistry();
	if (!CheckRegistry_Win10()) {		//名前にはRegistryとあるが、iniファイルを探し、なければ作成する関数
		MessageBoxUtf8Strict(NULL, "tclock-win11.iniが見当たらず、また作成に失敗しました。アプリケーションを終了します。\n\nCould not access / create tclock-win11.ini.",
			"TClock-Win11", MB_ICONERROR | MB_SETFOREGROUND);
		return 1;
	}




	// not to execute the program twice
	if (mutexAlreadyExists && !isRestartArg)
	{
		for (int i = 0; i < 50; i++) {
			hwnd = FindWindowW(szClassName, szWindowText);
			if (hwnd != NULL) break;
			Sleep(100);
		}
		if (hwnd == NULL) {
			MessageBoxUtf8Strict(NULL,
				"TClock-Win11 is already launching in another process. Please wait a moment and retry.",
				"TClock-Win11", MB_ICONEXCLAMATION | MB_SETFOREGROUND);
			return 1;
		}
		/* hwnd exists: continue to the legacy duplicate-process dialog flow below. */
	}

	hwnd = FindWindowW(szClassName, szWindowText);
	if(hwnd != NULL)				//すでにプロセスが起動していれば、	
	{
		CheckCommandLine(hwnd);		//コマンドラインオプションをチェック("/exit"の場合のため)して	

		if (b_Exit) return 1;
		else if (b_RestartNOW)
		{
			//SetMyRegLong("Status_DoNotEdit", "LastLaunchTimeStamp", 0);
			PostMessage(hwnd, WM_CLOSE, 0, 0);		//メインウィンドウにWM_CLOSE(102)を送出する。時々失敗するが、だいたいうまくいく。
			if (WaitQuitPrevTClock(50))	return 1;
		}
		else
		{
			int reply = MessageBoxUtf8Strict(NULL, "TClockのプロセスが稼働中です。再起動しますか？\n『OK』を選ぶと現在のプロセスを終了して新プロセスで再起動します。\n『キャンセル』を選ぶと現在のプロセスを維持します。\n\nPrevious TClock process is still running. Will you restart TClock?\nChoosing:\n\"OK\" initiates restarting from existing TClock Process.\n\"Cancel\" simply aborts this new process",
				"TClock-Win11", MB_ICONEXCLAMATION | MB_OKCANCEL | MB_DEFBUTTON1 | MB_SETFOREGROUND);
			if (reply == IDOK)
			{
				SetMyRegLong("Status_DoNotEdit", "LastLaunchTimeStamp", 0);
				PostMessage(hwnd, WM_CLOSE, 0, 0);		//メインウィンドウにWM_CLOSE(102)を送出する。時々失敗するが、だいたいうまくいく。			
				if (WaitQuitPrevTClock(50))	return 1;
				b_RestartNOW = TRUE;	//NormalLogへの連絡のために再利用。
			}
			else
			{
				return 1;					//終了する
			}
		}
	}



	b_DebugLog = GetMyRegLong(NULL, "DebugLog", FALSE);
	SetMyRegLong(NULL, "DebugLog", b_DebugLog);

	b_DebugLog_RegAccess = GetMyRegLong(NULL, "DebugLog_RegAccess", FALSE);

	b_DebugLog_Specific = GetMyRegLong(NULL, "DebugLog_Specific", FALSE);

	b_NormalLog = GetMyRegLong(NULL, "NormalLog", TRUE);
	SetMyRegLong(NULL, "NormalLog", b_NormalLog);

    b_UseHideClockPolicyFlow = GetMyRegLong("ETC", "UseHideClockPolicyFlow", FALSE);
    SetMyRegLong("ETC", "UseHideClockPolicyFlow", b_UseHideClockPolicyFlow);
    b_HideClockPolicyWasEnabled = IsHideClockPolicyEnabled();
    if (HasCommandLineOption(L"restart")) {
        b_HideClockPolicyApplied = FALSE;
    }
    else if (HasCommandLineOption(L"exit")) {
        b_UseHideClockPolicyFlow = FALSE;
    }
    else {
        ApplyHideClockPolicyFlow();
    }







	zombieCheckInterval = GetMyRegLong("ETC", "ZombieCheckInterval", 10);
	if (zombieCheckInterval < 5) zombieCheckInterval = 5;
	if (zombieCheckInterval > 300) zombieCheckInterval = 300;	
	SetMyRegLong("ETC", "ZombieCheckInterval", zombieCheckInterval);

	if (b_DebugLog) WriteDebug_New2("[exemain.c][TclockExeMain] TclockMain started");

	if (b_NormalLog)
	{
		CheckNormalLog();
		if (b_RestartNOW) WriteNormalLog("TClock-Win10 restarted");
		else WriteNormalLog("TClock-Win10 newly started");
	}

	b_AutoRestart = GetMyRegLong(NULL, "AutoRestart", TRUE);
	SetMyRegLong(NULL, "AutoRestart", b_AutoRestart);

	//起動時に前回終了時の連続リスタート回数を取得する
	countRestart = GetMyRegLong("Status_DoNotEdit", "CountAutoRestart", 0);
	if (countRestart >= MAX_AUTORESTART) {
		MessageBoxUtf8Strict(NULL, "クラッシュループを検出しました。アプリケーションを終了します。\n\nTClock is terminated because of repeting crash.",
			"TClock-Win11", MB_ICONERROR | MB_SETFOREGROUND);
		SetMyRegLong("Status_DoNotEdit", "CountAutoRestart", 0);
		return 1;
	}

	b_EnglishMenu = GetMyRegLong(NULL, "EnglishMenu", FALSE);
	SetMyRegLong(NULL, "EnglishMenu", b_EnglishMenu);
	if (b_EnglishMenu)
	{
		Language_Offset = LANGUAGE_OFFSET_ENGLISH;
	}
	else
	{
		Language_Offset = LANGUAGE_OFFSET_JAPANESE;
	}




	InitializeMenuItems();
	MenuCustomMigrateLegacyModeKeys();

	if(!CheckTCDLL()) { return 1; }	//tclock.dllのバージョンチェック

	g_hInstResource = LoadLanguageDLL(g_langdllname);		//langja.dllのロードを試みる
	if(g_hInstResource == NULL) { return 1; }				//langja.dllがロードできなければ停止

	CheckUnplugDrive();


	InitCommonControls();

	// Message of the taskbar recreating
	// Special thanks to Mr.Inuya
	//https://isobe.exblog.jp/113279/
	s_uTaskbarRestart = RegisterWindowMessageW(L"TaskbarCreated");

	g_hIconTClock = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_ICON1));

	//g_hwndPropDlg = g_hDlgTimer = NULL;
	g_hwndPropDlg = NULL;

	// register a window class
	wndclass.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wndclass.lpfnWndProc   = WndProc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = g_hInst;
	wndclass.hIcon         = g_hIconTClock;
	wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wndclass.lpszMenuName  = NULL;
	wndclass.lpszClassName = szClassName;
	RegisterClassW(&wndclass);

	if (b_DebugLog) WriteDebug_New2("[exemain.c][TclockExeMain] Window Class Registered");


	// create a hidden window
	//DO_WS_AGGRESSIVE();	// Comment out by TTTT 20181125
	hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, szClassName, szWindowText,		//ここでxzClassName, szWindowText等を登録して、hwndを取得
		0, CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
		NULL, NULL, g_hInst, NULL);
	//ShowWindow(hwnd, SW_SHOW);	//見えないウィンドウが存在している。この行のコメントアウトを外すと見える。これが"hwnd"
	//UpdateWindow(hwnd);


	if (b_DebugLog) WriteDebug_New2("[exemain.c][TclockExeMain] Hidden Main Window Created");


	if(OleInitialize(NULL) != S_OK){	//STA（シングルスレッドアパートメント）スレッドとして初期化し、OLE用の追加処理を行う…らしい。
		MessageBoxUtf8Strict(NULL, "OLEの初期化に失敗しました。\n\nFailed to initialize OLE.", "TClock-Win11", MB_ICONERROR);
	}

	g_hwndMain = hwnd;	//メイン隠しウィンドウのハンドルをグローバル変数のg_hwndMainにコピー

	CreateTClockTrayIcon(GetMyRegLong(NULL, "ShowTrayIcon", TRUE));
	SetMyRegLong(NULL, "ShowTrayIcon", b_ShowTrayIcon);

	SetIdlePriority();	//デフォルトではIDLE_PRIORITY_CLASSとする	added by TTTT

	CheckCommandLine(hwnd);		//コマンドラインチェック。この時点ではタスクトレイの改造は行っていない。この中で開始ウェイトも設定されている(?)
	//b_RestartDLL = FALSE;		//どうせこのあと使わないが、気持ち悪いのでクリア
	
	HPOWERNOTIFY handle_PowerNotify = NULL;
	b_ModernStandbySupported = CheckModernStandbyCapability_Win10();
	if (b_ModernStandbySupported)
	{
		handle_PowerNotify = RegisterPowerSettingNotification(hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
	}

	while(GetMessageW(&msg, NULL, 0, 0))		//キューからメッセージを受け取るGetMessageの戻り値が0になる(＝WM_QUITがポストされる) まで、
											//取得したメッセージをウィンドウプロシージャに送りつづける
	{
		if(g_hwndPropDlg && IsWindow(g_hwndPropDlg)
			&& IsDialogMessageW(g_hwndPropDlg, &msg))	//g_hwndPropDlgのメッセージは無視
			;
		//else if(g_hDlgTimer && IsWindow(g_hDlgTimer)
		//	&& IsDialogMessage(g_hDlgTimer, &msg))		//g_hDlgTimerのメッセージは無視
		//	;
		else		//それ以外は以下の2関数で処理
		{
			TranslateMessage(&msg);	//TranslateMessage: 仮想キーメッセージを文字メッセージへ変換(?)
			DispatchMessageW(&msg);	// DispatchMessageで受け取ったメッセージをウィンドウプロシージャ(?)に送出.
		}
	}

	if (b_DebugLog) WriteDebug_New2("[exemain.c][TclockExeMain] Got out from the main message loop");

	if(g_hMenu) DestroyMenu(g_hMenu);				//右クリックメニュ表示中なら、メニューを消す

	if(g_hInstResource) FreeLibrary(g_hInstResource);		//langja.dllをアンロード

	UnregisterClassW(szClassName, g_hInst);	// for TTBASE …と書いてあったが、たぶん必要 

	if (b_ModernStandbySupported && handle_PowerNotify) UnregisterPowerSettingNotification(handle_PowerNotify);

	return (UINT)msg.wParam;
}




/*-------------------------------------------
Set process priority idle as default
---------------------------------------------*/
void SetIdlePriority(void)
{
	HANDLE op = OpenProcess(PROCESS_ALL_ACCESS, TRUE, GetCurrentProcessId());
	SetPriorityClass(op, IDLE_PRIORITY_CLASS);
	Sleep(10);
}


void CreateTClockTrayIcon(BOOL bCreate)
{
	if (bCreate) {
		if (!b_ShowTrayIcon) {
			notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
			notifyIconData.hIcon = g_hIconTClock;
			notifyIconData.hWnd = g_hwndMain;
			notifyIconData.uCallbackMessage = CLOCKM_TRAYICONMSG;
			notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			notifyIconData.uID = ID_TRAYICON;
			strcpy(notifyIconData.szTip, "TClock-Win11");
			Shell_NotifyIcon(NIM_ADD, &notifyIconData);
			b_ShowTrayIcon = TRUE;
		}
	}
	else {
		if (b_ShowTrayIcon) {
			Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
			b_ShowTrayIcon = FALSE;
		}
	}
	return;
}


/*-------------------------------------------
   Command Line Option
   /prop : Show TClock Properties
   /exit : Exit TClock
   //http://tclock2ch.no.land.to/help2ch/about.html
---------------------------------------------*/
void CheckCommandLine(HWND hwnd)
{

	wchar_t *p;
	p = GetCommandLineW();
	while(*p)
	{
		if(*p == '/')
		{
			p++;
			if(PrefixEqualsNoCaseW(p, L"prop", 4))	//propオプション：プロパティを開く
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  prop option");
				PostMessage(hwnd, WM_COMMAND, IDC_SHOWPROP, 0);
				p += 4;
			}
			//else if (PrefixEqualsNoCaseW(p, L"restartdll", 10))	//exitオプション：終了処理を行う
			//{
			//	//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  restartdll option");
			//	b_RestartDLL = TRUE;
			//	p += 10;
			//}
			else if (PrefixEqualsNoCaseW(p, L"restart", 7))	//exitオプション：終了処理を行う
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  restart option");
				b_RestartNOW = TRUE;
				p += 10;
			}
			else if(PrefixEqualsNoCaseW(p, L"exit", 4))	//exitオプション：終了処理を行う
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  exit option");
				b_Exit = TRUE;
				PostMessage(hwnd, WM_CLOSE, 0, 0);		//メインウィンドウにWM_CLOSE(102)を送出する
				p += 4;
			}
			else if(PrefixEqualsNoCaseW(p, L"nowait", 6))	//nowaitオプション：遅延スタートを無視
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with nowait option");
				KillTimer(hwnd, IDTIMER_START);				//現在動いているIDTIMER_STARTを停止
				SetTimer(hwnd, IDTIMER_START, 100, NULL);	//100msウェイトでIDTIMER_STARTを開始, タイムアウト時にはメッセージ送出
				p += 6;
			}
			else if(PrefixEqualsNoCaseW(p, L"idle", 4))	//idleオプション：優先度をIDLEにして起動
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  idle option");
				HANDLE op = OpenProcess(PROCESS_ALL_ACCESS, TRUE, GetCurrentProcessId());
				SetPriorityClass(op, IDLE_PRIORITY_CLASS);
				Sleep(10);
				p += 4;
			}
			else if (PrefixEqualsNoCaseW(p, L"normal", 4))	//normalオプション：優先度をNORMALにして起動
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckCommandLine] Launched with  normal option");
				HANDLE op = OpenProcess(PROCESS_ALL_ACCESS, TRUE, GetCurrentProcessId());
				SetPriorityClass(op, NORMAL_PRIORITY_CLASS);
				Sleep(10);
				p += 6;
			}
		}
		p++;
	}

}


/*-------------------------------------------
   the window procedure	
---------------------------------------------*/
LRESULT CALLBACK WndProc(HWND hwnd,	UINT message, WPARAM wParam, LPARAM lParam)	//messageループの中のDispatchMessage()からのメッセージを受けてる？
{
	switch (message)
	{
		case WM_CREATE:
		{
			//if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_CREATE received");
			int nDelay;
			bDestroy = FALSE;
			InitFormat(); // initialize a Date/Time format
			nDelay = GetMyRegLong(NULL, "DelayStart", 0);		//設定ファイルから遅延スタート秒数を読み込み
			if(nDelay > 0)
			{
				SetTimer(hwnd, IDTIMER_START, nDelay * 1000, NULL);		//タイマーを起動(タイムアウト時にメインウィンドウあてWM_TIMER, IDTIMER_START)メッセージ
				bcontractTimer = TRUE;									//起動タイマー動作中フラグTRUE
			}
			else SendMessage(hwnd, WM_TIMER, IDTIMER_START, 0);			//タイマーの代わりにメインウィンドウに即刻IDTIMER_STARTメッセージ
			InitMouseFunction(hwnd);
			SetTimer(hwnd, IDTIMER_MAIN, 1000, NULL);		//メインループタイマー(IDTIMER_MAIN)周期を1秒に設定, 現時点では何の処理も行っていない。
			SetTimer(hwnd, IDTIMER_CREATE, 5000, NULL);		//クリエイトタイマー(IDTIMER_CREATE)周期を5秒に設定
			//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			return 0;
		}
		case WM_TIMER:		//WM_TIMERに対する処理
			if(wParam == IDTIMER_START)		//起動用タイマーのタイムアウトの処理
			{
				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_TIMER(IDTIMER_START) received");
				if(bcontractTimer) KillTimer(hwnd, wParam);		//タイマー停止
				bcontractTimer = FALSE;							//起動タイマー動作中フラグFALSE
				HookStart(hwnd);				// install a hook	dllmain.cの中にある。重要。タスクトレイのメッセージをフック。コア機能の起動
				EnsureTCalendarConfigDefaults();	// seed TCalendar config keys for existing INI
				LaunchTCycleAgentIfEnabled();	// launch TCycle runtime when enabled
				LaunchTCaptureAgentIfEnabled();	// launch TCapture agent when enabled
				LaunchTCalendarAlertIfEnabled();	// launch TCalendar alert runtime when enabled

				SetTimer(hwnd, IDTIMER_ZOMBIECHECK, zombieCheckInterval * 1000, NULL);	//

				//nCountFindingClock = 0;			// 時計カウンタ(エラー検出用？)
				//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			}
			else if (wParam == IDTIMER_MAIN)	//メインループタイマー(デフォルト1秒)のタイムアウトの処理
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_TIMER(IDTIMER_MAIN) received");
				OnTimerMain(hwnd);
				MenuOnTimerTick(hwnd);
			}
			else if (wParam == IDTIMER_MOUSE)
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_TIMER(IDTIMER_MOUSE) received");
				OnTimerMouse(hwnd);
			}
			else if(wParam == IDTIMER_CREATE)	//クリエイトタイマー(デフォルト5秒)のタイムアウトの処理
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_TIMER(IDTIMER_CREATE) received");
				SetTimer(hwnd, IDTIMER_CREATE, 60000, NULL);	//以降はタイムアウトを60秒に場合
				//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			}
			else if (wParam == IDTIMER_ZOMBIECHECK)	//ゾンビチェックのタイムアウトの処理 by TTTT 20181125
			{
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_TIMER(IDTIMER_ZOMBIECHECK) received");
//				OnTimerZombieCheck(hwnd);
				OnTimerZombieCheck2(hwnd);
			}
			return 0;
		case WM_CLOSE:		//手動での修了処理はこちらから行う。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_CLOSE received");
			TerminateTClock(hwnd);
			return 0;
		case WM_DESTROY:	//終了時処理。実際にはすべて終わってから届くようになっている。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_DESTROY received");
			return 0;
		case WM_ENDSESSION:	//セッション終了時のTClock終了はこちらから
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_ENDSESSION received. TClock is terminated from now.");
			//if(wParam) TerminateTClock(hwnd);
			PostMessage(g_hwndMain, WM_CLOSE, 0, 0);
			break;
		case WM_QUERYENDSESSION:	//セッション終了時の事前確認のようなもの。修了処理はWM_ENDSESSIONに実装する。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_QUERYENDSESSION received");
			if (b_NormalLog) WriteNormalLog("Exit TClock-Win10 by EndSession.");
			break;		//DefWindowProcが1を返してくれるので任せる。
		case WM_PAINT:
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_PAINT received");
			PAINTSTRUCT ps;
			HDC hdc;
			hdc = BeginPaint(hwnd, &ps);
			EndPaint(hwnd, &ps);
			return 0;
		}

		// Messages sent/posted from tclock.dll
		case WM_USER:
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_USER received");	//DLL Window生成時にハンドルを通知
			//nCountFindingClock = -1;
			g_hwndClock = (HWND)lParam;
			return 0;
		case (WM_USER + 1):   // error
			if (b_DebugLog) {
				char tempString[256];
				wsprintf(tempString, "[exemain.c][WndProc] WM_USER+1 received from dllmain.c, with error code = %d", (int)lParam);
				WriteDebug_New2(tempString);	//DLL Window生成時のエラー
			}
			//nCountFindingClock = -1;
			InitError((int)lParam);
			PostMessage(hwnd, WM_CLOSE, 0, 0);
			return 0;
		case (WM_USER + 2):   // exit (from tclock.c EndClock())	このコードはVer4.0.3以降では修了処理として呼ばれなくなっている(はず)。適当な時期に削除すること。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_USER+2 received");

			TerminateTClockFromDLL(hwnd);	//tcdlll(tclock.cから呼ばれるときはFromDLLを実行する)
			////タイマ機能のなごり
			////if(g_hDlgTimer && IsWindow(g_hDlgTimer))
			////	PostMessage(g_hDlgTimer, WM_CLOSE, 0, 0);
			////g_hDlgTimer = NULL;
			return 0;
		case CLOCKM_DLLALIVE:
			OnDLLAliveMessage(wParam);	//wParamに情報を入れる想定(未使用)
			return 0;
		case WM_WININICHANGE:		//画面テーマが変わった時の対応
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_WININICHANGE received");
			PostMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_BGCOLOR_UPDATE, 0);
			return 0;
		}
		case WM_SYSCOLORCHANGE:		//SYSCOLORが変わった場合は、アイコン名背景透明化のタイマー(IDTIMER_DESKTOPICON)を作動させるだけ。不要か。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_SYSCOLORCHANGE received");
			PostMessage(hwnd, WM_USER+10, 1,0);
			return 0;
		case (WM_USER+10):		//WM_USER+10はDESKCAL関連のようなので、削除してもよいか。そもそもフィルタされて届かないかも。
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_USER+10 received");
			return 0;
		}

		case CLOCKM_TRAYICONMSG:
			if (wParam == ID_TRAYICON) {
				switch (lParam) {
					case WM_RBUTTONDOWN:
					case WM_LBUTTONDOWN:
					//					OnContextMenu(hwnd, (HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
						{
						POINT pos;
						GetCursorPos(&pos);
						OnContextMenu(hwnd, (HWND)wParam, pos.x, pos.y);
						break;
						}
				}
			}
			return 0;



		// return from power saving
		case WM_POWERBROADCAST:		//これもカレンダー関係のみのコード。不要か。
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_POWERBROADCAST received");
			if (wParam == PBT_APMPOWERSTATUSCHANGE)
			{
				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] wParam: PBT_APMPOWERSTATUSCHANGE");
			}
			else if (wParam == PBT_APMRESUMEAUTOMATIC)
			{
				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] wParam: PBT_APMRESUMEAUTOMATIC");
			}
			else if (wParam == PBT_APMRESUMESUSPEND)
			{
				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] wParam: PBT_APMRESUMESUSPEND");
			}
			else if (wParam == PBT_APMSUSPEND)
			{
				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] wParam: PBT_APMSUSPEND");
			}
			else if (wParam == PBT_POWERSETTINGCHANGE)
			{
				POWERBROADCAST_SETTING* pbs = (POWERBROADCAST_SETTING*)lParam;

				if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] wParam: PBT_POWERSETTINGCHANGE");
				if (!pbs || pbs->DataLength < 1) {
					break;
				}
				if (memcmp(&pbs->PowerSetting, &GUID_CONSOLE_DISPLAY_STATE, sizeof(GUID)) != 0) {
					break;
				}

				if (pbs->Data[0] == 0x0)
				{
					if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] GUID_CONSOLE_DISPLAY_STATE=0 : Sleep in");
					PostMessage(g_hwndClock, CLOCKM_SLEEP_IN, 0, 0);
				}
				else
				{
					if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] GUID_CONSOLE_DISPLAY_STATE!=0 : Awake from Sleep");
					PostMessage(g_hwndClock, CLOCKM_SLEEP_AWAKE, 0, 0);
				}
			}
			break;
		}

		// context menu
		case WM_COMMAND:	//右クリックメニューからコマンド実行された場合はここを通ってmenu.cのコードに届けられる模様
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_COMMAND received");
			OnTClockCommand(hwnd, LOWORD(wParam), HIWORD(wParam)); // menu.c
			//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			return 0;
		// messages transfered from the dll
		case WM_CONTEXTMENU:	//右クリックされた場合->メニューを開く関数(OnContextMenu())をコール
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_CONTEXTMENU received");
			// menu.c
			OnContextMenu(hwnd, (HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			return 0;
		case WM_DROPFILES:	//ファイルがドロップされた場合の処理、要確認
			OnDropFiles(hwnd, (HDROP)wParam); // mouse.c
			//DO_WS_AGGRESSIVE(); // Comment out by TTTT 20181125
			return 0;
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
			//if (FindVistaCalenderWindow())
			//{
			//	DWORD_PTR dw = 0;
			//	SendMessageTimeout(g_hwndClock, CLOCKM_VISTACALENDAR, 1, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 5000, &dw);
			//}
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		case WM_MOUSEWHEEL:
			OnMouseMsg(hwnd, message, wParam, lParam); // mouse.c
			return 0;
		case WM_ENTERMENULOOP:	//右クリックメニューの入力待ちループに入った時に出る。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_ENTERMENULOOP received");
			bMenuOpened = TRUE;
			break;
		case WM_EXITMENULOOP:	//右クリックメニューの入力待ちループから出た時に出る。
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_EXITMENULOOP received");
			bMenuOpened = FALSE;
			break;
		case WM_HOTKEY:	//Hotkey機能。要否要検討
			OnHotkey(hwnd, (int)wParam);
			break;
		case WM_MEASUREITEM:	//ファイルリスト関係
			OnMeasureItem(hwnd, wParam, lParam); // filelist.c
			break;
		case WM_DRAWITEM:	//ファイルリスト関係
			OnDrawItem(hwnd, wParam, lParam); // filelist.c
			break;
		case WM_INITMENUPOPUP:	//ファイルリスト関係
			OnInitMenuPopup(hwnd, wParam, lParam); // filelist.c
			break;
		case WM_MENURBUTTONUP:	//ファイルリスト関係
			MenuOnMenuRButtonUp(hwnd, wParam, lParam); // menu.c
			OnMenuRButtonUp(hwnd, wParam, lParam); //filelist.c
			break;
		case WM_DISPLAYCHANGE:	//added by TTTT
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] WM_DISPLAYCHANGE received");
			PostMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_DISPLAYSTATUS_CHECK, 0);
			b_DisplayChanged = TRUE;
			break;
	}

	if(message == s_uTaskbarRestart) // When Explorer is hung up,
	{								 // and the taskbar is recreated.
		if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] message: s_uTaskbarRestart received");

        if (b_IgnoreTaskbarRestartForHideClock) {
            b_IgnoreTaskbarRestartForHideClock = FALSE;
            if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] Taskbar restart ignored (HideClock flow). ");
            return 0;
        }

		if (b_NormalLog)
		{
			WriteNormalLog("[Warning] Windows Taskbar restarted. (notified from OS)");
		}

		if (b_DisplayChanged)
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] No action is taken because b_DisplayChanged = TRUE.");
			b_DisplayChanged = FALSE;
		}
		else if (b_AutoRestart)
		{

			if (GetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 0) == 1) {
				Sleep(AUTORESTART_WAIT_WIN11);	//Win11の場合はExplorerの再起動に時間がかかるので待つ
			}

			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] Windows Taskbar restarted. b_AutoRestart = TRUE, Restart TClock-Win10.");
			if (b_NormalLog) WriteNormalLog("b_AutoRestart = TRUE, Restart TClock-Win10");

			char fname[MAX_PATH];
			strcpy(fname, g_mydir);
			add_title(fname, "TClock-Win11.exe");
            b_SkipHideClockRestore = TRUE;
			ShellExecuteUtf8Strict(NULL, "open", fname, "/restart", NULL, SW_HIDE);
			/* Avoid double-restart: this path already spawned a new process. */
			g_hwndClock = NULL;
			PostMessage(hwnd, WM_CLOSE, 0, 0);

		}
		else
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][WndProc] Windows Taskbar restarted. b_AutoRestart = FALSE. Quit TClock.");
			if (b_NormalLog) WriteNormalLog("b_AutoRestart = FALSE, Quit TClock-Win10");

			PostMessage(hwnd, WM_CLOSE, 0, 0);		//メインウィンドウにWM_CLOSE(102)を送出する。(この終了動作はdll先行ではなくてexemainからなので)

		}
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}

void TerminateTClock(HWND hwnd)
{
	//TClockが動作中から終了する場合には、こちらで終了する。

	if (b_DebugLog) WriteDebug_New2("[exemain.c] TerminateTClock called.");

	if (g_hwndClock)
	{
		SendMessage(g_hwndClock, WM_COMMAND, IDC_EXIT, 0);
		g_hwndClock = NULL;		//EndClockから呼ばれたこと(EndClock実行済みの確認のために消す)
	}
	if (g_hwndPropDlg && IsWindow(g_hwndPropDlg))
		PostMessage(g_hwndPropDlg, WM_CLOSE, 0, 0);
	g_hwndPropDlg = NULL;

	HookEnd();  // uninstall a hook, Ver 4.0.5.3以降ではすでにフック外れているはずだが念のため。

	if (bDestroy == TRUE) return;	//２重終了しないように

	bDestroy = TRUE;
	EndMouseFunction(hwnd);
	KillTimer(hwnd, IDTIMER_MAIN);
	KillTimer(hwnd, IDTIMER_CREATE);
	KillTimer(hwnd, IDTIMER_ZOMBIECHECK);
	TerminateExtensionsOnExplicitExitIfNeeded();
	g_ExitRequestedFromMenu = FALSE;

	if (bcontractTimer)
	{
		KillTimer(hwnd, IDTIMER_START);
		bcontractTimer = FALSE;
	}

//	if (b_ShowTrayIcon)Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
	CreateTClockTrayIcon(FALSE);
    if (!b_SkipHideClockRestore) RestoreHideClockPolicyFlow();

	SetMyRegLong("Status_DoNotEdit", "LastExitUser", 1);
	PostQuitMessage(0);
	g_hwndMain = NULL;

	PostMessage(hwnd, WM_DESTROY, 0, 0);
	if (b_DebugLog) WriteDebug_New2("[exemain.c] TerminateTClock completed.");

}


void TerminateTClockFromDLL(HWND hwnd)
{
	if (b_DebugLog) WriteDebug_New2("[exemain.c] TerminateTClockFromDLL called.");

	//FromDLLは、時計のほうで終了処理(EndClock)してから呼ばれる(WM_USER+2経由)状況で使う。

	if (g_hwndClock)
	{
//		SendMessage(g_hwndClock, WM_COMMAND, IDC_EXIT, 0);
		g_hwndClock = NULL;		//EndClockから呼ばれたこと(EndClock実行済みの確認のために消す)
	}
	if (g_hwndPropDlg && IsWindow(g_hwndPropDlg))
		PostMessage(g_hwndPropDlg, WM_CLOSE, 0, 0);
	g_hwndPropDlg = NULL;

	HookEnd();  // uninstall a hook, Ver 4.0.5.3以降ではすでにフック外れているはずだが念のため。

	if (bDestroy == TRUE) return;	//２重終了しないように

	bDestroy = TRUE;
	EndMouseFunction(hwnd);
	KillTimer(hwnd, IDTIMER_MAIN);
	KillTimer(hwnd, IDTIMER_CREATE);
	KillTimer(hwnd, IDTIMER_ZOMBIECHECK);
	TerminateExtensionsOnExplicitExitIfNeeded();
	g_ExitRequestedFromMenu = FALSE;

	if (bcontractTimer)
	{
		KillTimer(hwnd, IDTIMER_START);
		bcontractTimer = FALSE;
	}

    if (!b_SkipHideClockRestore) RestoreHideClockPolicyFlow();
	SetMyRegLong("Status_DoNotEdit", "LastExitUser", 1);
	PostQuitMessage(0);
	g_hwndMain = NULL;

	PostMessage(hwnd, WM_DESTROY, 0, 0);
	if (b_DebugLog) WriteDebug_New2("[exemain.c] TerminateTClockFromDLL completed.");

}



/*-------------------------------------------------------
  show a message when TClock failed to customize the clock
---------------------------------------------------------*/
void InitError(int n)
{
	wchar_t s[160];

	wsprintfW(s, L"%s: %d", MyStringW(IDS_NOTFOUNDCLOCK), n);
	MyMessageBoxW(NULL, s, NULL, MB_OK, MB_ICONEXCLAMATION);
}

/*-------------------------------------------------------
   Timer
   synchronize, alarm, timer, execute Desktop Calendar...
---------------------------------------------------------*/
void OnTimerMain(HWND hwnd)		//メインループタイマー(デフォルト1秒)のタイムアウトの処理
{
	(void)hwnd;
	//時計ウィンドウのタイマー動作が停止して2秒したらフラグが立つ。
	//負荷が小さいので残してあるが、現在のところ停止動作等は実装されていない。
	//Ver4.0.4時点で不測の自体に対する対処はOnTimerZombieCheck2で行っている。
	//スリープ等でフラグが経つのですぐに終了してはいけない。

	FILETIME currentFileTime;
	ULARGE_INTEGER currentUll;

	GetSystemTimeAsFileTime(&currentFileTime);
	currentUll.LowPart = currentFileTime.dwLowDateTime;
	currentUll.HighPart = currentFileTime.dwHighDateTime;

	if ((currentUll.QuadPart - lastFileTimeDLLAlive) > 20000000ULL)
	{
		b_FlagDLLAlive = FALSE;	//Aliveメッセージが2秒開くとDLL音信不通フラグを立てる(復活可能)
	}
	else 
	{
		b_FlagDLLAlive = TRUE;
	}
}





void OnTimerZombieCheck2(HWND hwnd)
{
	if (g_hwndClock)
	{
		int ret = 0;
		ret = (int)SendMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_ZOMBIECHECK_CALL, 0);
		if (ret == 255)
		{
			if (b_DebugLog) WriteDebug_New2("[exemain.c][OnTimerZombieCheck2] TClock is alive.");
		}
		else
		{

			if (GetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 0) == 1) {
				//Win11の場合は普通にExplorerが再起動したらここに来る。無視してExeを生かしておいたらOSからNotificationが来るのでその時に対処する。
				//if (b_DebugLog) WriteDebug_New2("[exemain.c][OnTimerZombieCheck2](Win11) No responce from DLL. Explorer.exe may be restarted. Continue operation to wait notification from OS.");
				//if (b_NormalLog) WriteNormalLog("(Win11) No responce from DLL. Explorer.exe may be restarted. Continue operation to wait notification from OS.");
				return;
			}
			else {
				//Win10の場合にはここに来た時点でかなり異常なのでそのまま終わることにする。ふつうは起きない。
				if (b_DebugLog) WriteDebug_New2("[exemain.c][OnTimerZombieCheck2](Win10) No responce from DLL. TClock may be unexpectedly dead. Quit TClock, regardless b_AutoRestart.");
				if (b_NormalLog) WriteNormalLog("(Win10) No responce from DLL. TClock may be unexpectedly dead. Quit TClock, regardless b_AutoRestart.");
				TerminateTClockFromDLL(hwnd);		//すでにTClockの改造部は終了/消失していると判断されるため、FromDLLでの終了動作を行う。
			}


			//if (b_AutoRestart)
			//{
			//	if (b_DebugLog) WriteDebug_New2("[exemain.c][OnTimerZombieCheck2] TClock is dead. b_AutoRestart = TRUE, Restart TClock.");
			//	if (b_NormalLog) WriteNormalLog("TClock is unexpectedly dead. b_AutoRestart = TRUE, Restart TClock-Win10");

			//	char fname[MAX_PATH];
			//	strcpy(fname, g_mydir);
			//	add_title(fname, "TClock-Win11.exe");
			//	ShellExecute(NULL, "open", fname, "/restart", NULL, SW_HIDE);
			//}
			//else
			//{
			//	if (b_DebugLog) WriteDebug_New2("[exemain.c][OnTimerZombieCheck2] TClock is dead. b_AutoRestart = FALSE. Quit TClock.");
			//	if (b_NormalLog) WriteNormalLog("TClock is unexpectedly dead. b_AutoRestart = FALSE, Quit TClock-Win10");
			//	TerminateTClockFromDLL(hwnd);		//すでにTClockの改造部は終了/消失していると判断されるため、FromDLLでの終了動作を行う。
			//}
		}
	}
}



//void OnTimerZombieCheck(HWND hwnd)	//ゾンビチェック by TTTT, Ver 4.0.4以降は使われていない。
//{
//	HWND hwndTaskBar;
//
//	b_DisplayChanged = FALSE;	 // Clear DisplayChange Flag
//
//	if (b_DebugLog) WriteDebug_New2("[exemain.c] OnTimerZombieCheck called.");
//
//	// find the taskbar
//	hwndTaskBar = FindWindow("Shell_TrayWnd", "");
//	if (hwndTaskBar)
//	{
//		if (hwndTaskBar_Prev == NULL)
//		{
//			hwndTaskBar_Prev = hwndTaskBar;
//		}
//		else if (hwndTaskBar != hwndTaskBar_Prev)
//		{
//			if (b_NormalLog)
//			{
//				WriteNormalLog("[Warning] Windows Taskbar restarted (detected by TClockMain). Cannot identify whether TClock-Win10 caused it or not.");
//			}
//
//
//			if (b_AutoRestart)
//			{
//				if (b_DebugLog) WriteDebug_New2("[OnTimerZombieCheck] Restart TClock with /restart option.");
//
//				if (b_NormalLog) WriteNormalLog("b_AutoRestart = TRUE, Restart TClock-Win10");
//
//				char fname[MAX_PATH];
//				strcpy(fname, g_mydir);
//				add_title(fname, "TClock-Win11.exe");
//				ShellExecute(NULL, "open", fname, "/restart", NULL, SW_HIDE);
//			}
//			else
//			{
//				if (b_DebugLog) WriteDebug_New2("[OnTimerZombieCheck] b_AutoRestart = FALSE. Quit TClock.");
//
//				if (b_NormalLog) WriteNormalLog("b_AutoRestart = FALSE, Quit TClock-Win10");
//
//
//				PostMessage(hwnd, WM_CLOSE, 0, 0);		//メインウィンドウにWM_CLOSE(102)を送出する。
//			}
//		}
//	}
//
//}


/*-------------------------------------------
  load a language dll
---------------------------------------------*/
HINSTANCE LoadLanguageDLL(char *langdllname)
{
	/* Compatibility boundary: output buffer contract is intentional.
	   langdllname is caller-owned writable storage, so this API stays char*. */
	if (b_DebugLog) WriteDebug_New2("[exemain.c][LoadLanguageDLL] LoadLanguageDLL() called");
	HINSTANCE hInst = NULL;
	char fname[MAX_PATH];
	wchar_t wPath[MAX_PATH];
	wchar_t wDir[MAX_PATH];
	WIN32_FIND_DATAW fd;
	HANDLE hfind = INVALID_HANDLE_VALUE;
	int i;

	if(hfind == INVALID_HANDLE_VALUE)
	{
		wPath[0] = L'\0';
		wDir[0] = L'\0';
		if (GetModuleFileNameW(g_hInst, wPath, (DWORD)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) {
			return NULL;
		}
		lstrcpynW(wDir, wPath, (int)(sizeof(wDir) / sizeof(wDir[0])));
		for (i = lstrlenW(wDir) - 1; i >= 0; --i) {
			if (wDir[i] == L'\\' || wDir[i] == L'/') {
				wDir[i + 1] = L'\0';
				break;
			}
		}
		lstrcpynW(wPath, wDir, (int)(sizeof(wPath) / sizeof(wPath[0])));
		lstrcatW(wPath, L"tclang-win11.dll");
		hfind = FindFirstFileW(wPath, &fd);
		if(hfind != INVALID_HANDLE_VALUE)
		{
			FindClose(hfind);
			lstrcpynW(wPath, wDir, (int)(sizeof(wPath) / sizeof(wPath[0])));
			lstrcatW(wPath, fd.cFileName);
			if (tc_utf16_to_utf8(wPath, fname, (int)sizeof(fname)) <= 0) {
				fname[0] = '\0';
			}
		}
	}

	if(hfind != INVALID_HANDLE_VALUE)
	{
		if(!CheckDLL(fname)) return NULL;
		hInst = LoadLibraryW(wPath);
	}

	if(hInst == NULL)
		MyMessageBoxW(NULL, L"Can't load a language module.",
			NULL, MB_OK, MB_ICONEXCLAMATION);
	else strcpy(langdllname, fname);	/* Keep writable output contract for compatibility. */
	return hInst;
}

HINSTANCE GetLangModule(void)
{
	return g_hInstResource;
}

/*-------------------------------------------
  Check version of dll
---------------------------------------------*/
BOOL CheckTCDLL(void)
{
	if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckTCDLL] CheckTCDLL() called");
	char fname[MAX_PATH];
	strcpy(fname, g_mydir);
	add_title(fname, "tcdll-win11.dll");
	return CheckDLL(fname);
}

/*-------------------------------------------
  Check version of dll
---------------------------------------------*/
BOOL CheckDLL(const char *fname)
{
	if (b_DebugLog) WriteDebug_New2("[exemain.c][CheckDLL] CheckDLL() called");
	DWORD size;
	BYTE *pBlock;
	VS_FIXEDFILEINFO *pffi;
	BOOL br = FALSE;
	wchar_t wFile[MAX_PATH];

	if (tc_utf8_to_utf16(fname, wFile, (int)(sizeof(wFile) / sizeof(wFile[0]))) <= 0) {
		wFile[0] = L'\0';
	}

	size = (wFile[0] != L'\0') ? GetFileVersionInfoSizeW(wFile, 0) : 0;
	if(size > 0)
	{
		pBlock = (BYTE*)malloc(size);
		if(pBlock && GetFileVersionInfoW(wFile, 0, size, pBlock))
		{
			UINT tmp;
			if(VerQueryValueW(pBlock, L"\\", (LPVOID*)&pffi, &tmp))
			{
				if(pffi->dwFileVersionMS == exeVersionM &&
					HIWORD(pffi->dwFileVersionLS) == HIWORD(exeVersionL))
				{
					br = TRUE;
				}
			}
		}
		if (pBlock) free(pBlock);
	}
	if(!br)
	{
		char titleA[MAX_PATH + 1];
		wchar_t wTitle[MAX_PATH + 1];
		wchar_t wmsg[MAX_PATH + 64];

		get_title(titleA, fname);
		if (tc_utf8_to_utf16(titleA, wTitle, (int)(sizeof(wTitle) / sizeof(wTitle[0]))) <= 0) {
			lstrcpynW(wTitle, L"[decode error]", (int)(sizeof(wTitle) / sizeof(wTitle[0])));
		}
		wsprintfW(wmsg, L"Invalid file version: %s", wTitle);
		MyMessageBoxW(NULL, wmsg,
			NULL, MB_OK, MB_ICONEXCLAMATION);
	}
	return br;
}


void My2chHelp(HWND hwnd)
{
	char helpurl[1024];
	char helpurlUtf8[1024];

	GetMyRegStr("ETC", "2chHelpURL", helpurl, 1024, "");
	if (helpurl[0] == 0)
	{
		strcpy(helpurl, MyStringUTF8(IDS_HELP2CH));
		SetMyRegStr("ETC", "2chHelpURL", helpurl);
	}

	lstrcpyn(helpurlUtf8, helpurl, (int)sizeof(helpurlUtf8));
	{
		char before[1024];
		lstrcpyn(before, helpurlUtf8, (int)sizeof(before));
		NormalizeSettingUtf8InPlace(helpurlUtf8, (int)sizeof(helpurlUtf8));
		if (lstrcmp(before, helpurlUtf8) != 0) {
			SetMyRegStr("ETC", "2chHelpURL", helpurlUtf8);
		}
	}

	ShellExecuteUtf8Strict(hwnd, NULL, helpurlUtf8, NULL, "", SW_SHOW);
}






static BOOL SetIniPathFromWide(const wchar_t* inifileW)
{
	if (!inifileW || !inifileW[0]) return FALSE;
	if (tc_utf16_to_utf8(inifileW, g_inifile, MAX_PATH) <= 0) {
		g_inifile[0] = '\0';
		return FALSE;
	}
	return TRUE;
}

static int tc_is_japanese_ui_locale(void)
{
	LANGID uiLang = GetUserDefaultUILanguage();
	if (PRIMARYLANGID(uiLang) == LANG_JAPANESE) return 1;
	uiLang = GetSystemDefaultUILanguage();
	if (PRIMARYLANGID(uiLang) == LANG_JAPANESE) return 1;
	return 0;
}

static BOOL BuildDefaultIniPathW(wchar_t* outPath, int outCch)
{
	DWORD n;
	int i;
	const wchar_t* iniName = L"tclock-win11.ini";
	if (!outPath || outCch <= 0) return FALSE;
	outPath[0] = L'\0';
	n = GetModuleFileNameW(g_hInst, outPath, (DWORD)outCch);
	if (n == 0 || n >= (DWORD)outCch) return FALSE;
	for (i = (int)n - 1; i >= 0; --i) {
		if (outPath[i] == L'\\' || outPath[i] == L'/') {
			outPath[i + 1] = L'\0';
			break;
		}
	}
	if (i < 0) return FALSE;
	if ((int)lstrlenW(outPath) + (int)lstrlenW(iniName) + 1 > outCch) return FALSE;
	lstrcatW(outPath, iniName);
	return TRUE;
}

/*------------------------------------------------
Create Default Setting File		//Added by TTTT
--------------------------------------------------*/
void CreateDefaultIniFile_Win10(const wchar_t* fnameW)
{
	HANDLE hCreate;
	DWORD written = 0;
	const BYTE utf8Bom[3] = { 0xEF, 0xBB, 0xBF };

	if (!fnameW || !fnameW[0]) return;

	hCreate = CreateFileW(fnameW, GENERIC_WRITE, 0, NULL,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hCreate != INVALID_HANDLE_VALUE) {
		/* Seed UTF-8 BOM so utf8 INI path is used from first SetMyReg*. */
		WriteFile(hCreate, utf8Bom, sizeof(utf8Bom), &written, NULL);
		CloseHandle(hCreate);

		//g_bIniSetting = TRUE;
		if (!SetIniPathFromWide(fnameW)) return;
		SetMyRegLong(NULL, "DebugLog", 1);
		SetMyRegLong(NULL, "NormalLog", 1);
		SetMyRegLong(NULL, "AutoClearLogFile", 1);
		SetMyRegLong(NULL, "AutoClearLogLines", 1000);
		SetMyRegLong(NULL, "AutoRestart", 1);
		SetMyRegLong(NULL, "CompactMode", 0);
		{
			int englishMenuDefault = tc_is_japanese_ui_locale() ? 0 : 1;
			SetMyRegLong(NULL, "EnglishMenu", englishMenuDefault);
		}
		SetMyRegLong(NULL, "AdjustThreshold", 200);
		SetMyRegLong(NULL, "EnableOnSubDisplay", 1);
		SetMyRegLong(NULL, "OffsetClockMS", 0);
		SetMyRegLong(NULL, "ShowTrayIcon", 1);
		SetMyRegLong("Status_DoNotEdit", "SafeMode", 0);
		SetMyRegLong("Status_DoNotEdit", "LastLaunchTimeStamp", 0);
		SetMyRegLong("Status_DoNotEdit", "ExcessNetProfiles", 0);
		SetMyRegLong("Status_DoNotEdit", "ExistLTEProfile", 0);
		SetMyRegLong("Status_DoNotEdit", "ExistMeteredProfile", 0);
		SetMyRegLong("Status_DoNotEdit", "PreviousLTEProfNumber", 0);
		SetMyRegStr("Status_DoNotEdit", "PreviousLTEProfName", "");
		SetMyRegLong("Status_DoNotEdit", "BatteryLifeAvailable", 1);
		SetMyRegLong("Status_DoNotEdit", "CurrentInternetProfileNumber", 2);
		SetMyRegLong("Status_DoNotEdit", "NumberOfProfiles", 5);
		SetMyRegLong("Status_DoNotEdit", "TimerCountForSec", 985);
		SetMyRegLong("Status_DoNotEdit", "ModernStandbySupported", 1);
		SetMyRegLong("Status_DoNotEdit", "CountAutoRestart", 0);
		SetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 1);
		SetMyRegLong("Status_DoNotEdit", "ClockWidth", 272);
		SetMyRegLong("Status_DoNotEdit", "ClockHeight", 48);
		SetMyRegLong("Status_DoNotEdit", "Win11IconSize", 32);
		SetMyRegLong("Status_DoNotEdit", "Win11LayoutDegraded", 0);
		SetMyRegLong("Status_DoNotEdit", "LastExitUser", 1);
		SetMyRegLong("Win11", "AdjustCutTray", 0);
		SetMyRegLong("Win11", "AdjustWin11ClockWidth", 0);
		SetMyRegLong("Win11", "AdjustDetectNotify", 0);
		SetMyRegLong("Win11", "AdjustWin11IconPosition", 1);
		SetMyRegLong("Win11", "EnableWin11NotifyIcon", 0);
		SetMyRegLong("Win11", "AlignTaskbarLeft", 1);
		SetMyRegLong("Color_Font", "UseBackColor", 0);
		SetMyRegLong("Color_Font", "BackColor", 2147483633);
		SetMyRegLong("Color_Font", "UseBackColor2", 0);
		SetMyRegLong("Color_Font", "BackColor2", 2147483633);
		SetMyRegLong("Color_Font", "GradDir", 0);
		SetMyRegLong("Color_Font", "ForeColor", 0);
		SetMyRegLong("Color_Font", "ForeColorShadow", 0);
		SetMyRegLong("Color_Font", "ForeColorBORDER", 0);
		SetMyRegLong("Color_Font", "ShadowColor", 0);
		SetMyRegLong("Color_Font", "ClockShadowRange", 1);
		SetMyRegStr("Color_Font", "Font", "MS Gothic");
		SetMyRegLong("Color_Font", "FontSize", 12);
		SetMyRegLong("Color_Font", "TextPos", 2);
		SetMyRegLong("Color_Font", "Bold", 0);
		SetMyRegLong("Color_Font", "Italic", 0);
		SetMyRegLong("Color_Font", "ClockWidth", 0);
		SetMyRegLong("Color_Font", "VertPos", 0);
		SetMyRegLong("Color_Font", "LineHeight", 0);
		SetMyRegLong("Color_Font", "UseAllColor", 0);
		SetMyRegLong("Color_Font", "UseDateColor", 0);
		SetMyRegLong("Color_Font", "UseDowColor", 0);
		SetMyRegLong("Color_Font", "UseTimeColor", 0);
		SetMyRegLong("Color_Font", "UseVPNColor", 0);
		SetMyRegLong("Color_Font", "Saturday_TextColor", 13172680);
		SetMyRegLong("Color_Font", "Sunday_TextColor", 13158655);
		SetMyRegLong("Color_Font", "Holiday_TextColor", 13158655);
		SetMyRegLong("Color_Font", "VPN_TextColor", 16776960);
		SetMyRegLong("Color_Font", "AutoBackMatchTaskbar", 1);
		SetMyRegLong("Color_Font", "AutoBackAlpha", 255);
		SetMyRegLong("Color_Font", "AutoBackBlendRatio", 30);
		SetMyRegLong("Color_Font", "AutoBackRefreshSec", 1);
		SetMyRegLong("Color_Font", "AutoBackSampleClockOffset", 2);
		SetMyRegLong("Color_Font", "AutoBackSampleShowDesktopOffset", 0);
		SetMyRegStr("Color_Font", "FontUtf8Hex", "4D5320476F74686963");
		SetMyRegLong("Color_Font", "AutoBackSnapshotColor", 15000804);
		SetMyRegLong("Color_Font", "AutoBackSnapshotColor2", 12895428);
		SetMyRegLong("Format", "Locale", 1041);
		SetMyRegLong("Format", "Year4", 0);
		SetMyRegLong("Format", "Year", 1);
		SetMyRegLong("Format", "Month", 1);
		SetMyRegLong("Format", "MonthS", 0);
		SetMyRegLong("Format", "Day", 1);
		SetMyRegLong("Format", "Weekday", 1);
		SetMyRegLong("Format", "Hour", 1);
		SetMyRegLong("Format", "Minute", 1);
		SetMyRegLong("Format", "Second", 1);
		SetMyRegLong("Format", "Kaigyo", 0);
		SetMyRegLong("Format", "InternetTime", 0);
		SetMyRegLong("Format", "AMPM", 0);
		SetMyRegLong("Format", "Hour12", 0);
		SetMyRegLong("Format", "HourZero", 0);
		SetMyRegStr("Format", "AMsymbol", "AM");
		SetMyRegStr("Format", "PMsymbol", "PM");
		SetMyRegLong("Format", "Custom", 1);
		SetMyRegStr("Format", "Format", "yyyy/mm/dd ddd tt hh:nn:ss");
		SetMyRegStr("Format", "CustomFormat", "yyyy/mm/dd ddd tt hh:nn:ss");
		SetMyRegStr("Format", "FormatUtf8Hex", "797979792F6D6D2F6464206464642074742068683A6E6E3A7373");
		SetMyRegStr("Format", "CustomFormatUtf8Hex", "797979792F6D6D2F6464206464642074742068683A6E6E3A7373");
		SetMyRegLong("Mouse", "DropFiles", 0);
		SetMyRegStr("Mouse", "DropFilesApp", "");
		SetMyRegLong("Mouse", "RightClickMenu", 1);
		SetMyRegLong("Mouse", "01", 208);
		SetMyRegLong("Graph", "BackNetColSend", 255);
		SetMyRegLong("Graph", "BackNetColSR", 0);
		SetMyRegLong("Graph", "BackNetColRecv", 65280);
		SetMyRegLong("Graph", "BackNet", 0);
		SetMyRegLong("Graph", "LogGraph", 1);
		SetMyRegLong("Graph", "GraphTate", 0);
		SetMyRegLong("Graph", "ReverseGraph", 0);
		SetMyRegLong("Graph", "NetGraphScaleRecv", 1000);
		SetMyRegLong("Graph", "NetGraphScaleSend", 1000);
		SetMyRegLong("Graph", "GraphMode", 1);
		SetMyRegLong("Graph", "GraphType", 2);
		SetMyRegLong("Graph", "GraphLeft", 0);
		SetMyRegLong("Graph", "GraphTop", 0);
		SetMyRegLong("Graph", "GraphRight", 230);
		SetMyRegLong("Graph", "GraphBottom", 1);
		SetMyRegLong("Graph", "EnableGPUGraph", 1);
		SetMyRegLong("Graph", "UseBarMeterColForGraph", 0);
		SetMyRegLong("Graph", "ColorCPUGraph", 65280);
		SetMyRegLong("Graph", "ColorCPUGraph2", 255);
		SetMyRegLong("Graph", "ColorGPUGraph", 16711935);
		SetMyRegLong("AnalogClock", "UseAnalogClock", 0);
		SetMyRegStr("AnalogClock", "AnalogClockBmp", "tclock.bmp");
		SetMyRegLong("AnalogClock", "AClockHourHandColor", 255);
		SetMyRegLong("AnalogClock", "AClockMinHandColor", 16711680);
		SetMyRegLong("AnalogClock", "AnalogClockHourHandBold", 0);
		SetMyRegLong("AnalogClock", "AnalogClockMinHandBold", 0);
		SetMyRegLong("AnalogClock", "AnalogClockPos", 0);
		SetMyRegLong("AnalogClock", "AnalogClockAtStartBtn", 0);
		SetMyRegLong("AnalogClock", "AnalogClockHPos", 10);
		SetMyRegLong("AnalogClock", "AnalogClockVPos", 2);
		SetMyRegLong("AnalogClock", "AnalogClockSize", 25);
		SetMyRegLong("Tooltip", "EnableTooltip", 1);
		SetMyRegStr("Tooltip", "Tooltip", "file:tclock_tooltip.txt");
		SetMyRegStr("Tooltip", "Tooltip2", "\"\"TClock <%LDATE%>\"\"");
		SetMyRegStr("Tooltip", "Tooltip3", "\"\"TClock <%LDATE%>\"\"");
		SetMyRegLong("Tooltip", "Tip2Use", 0);
		SetMyRegLong("Tooltip", "Tip3Use", 0);
		SetMyRegLong("Tooltip", "TipTateFlg", 0);
		SetMyRegLong("Tooltip", "Tip1Update", 0);
		SetMyRegLong("Tooltip", "Tip2Update", 0);
		SetMyRegLong("Tooltip", "Tip3Update", 0);
		SetMyRegStr("Tooltip", "TipFont", "MS Gothic");
		SetMyRegStr("Tooltip", "TipTitle", "");
		SetMyRegLong("Tooltip", "TipFontSize", 10);
		SetMyRegLong("Tooltip", "TipBold", 0);
		SetMyRegLong("Tooltip", "TipItalic", 0);
		SetMyRegLong("Tooltip", "BalloonFlg", 1);
		SetMyRegLong("Tooltip", "TipFontColor", 0);
		SetMyRegLong("Tooltip", "TipTitleColor", 16711680);
		SetMyRegLong("Tooltip", "TipBakColor", 16777215);
		SetMyRegStr("Tooltip", "TipFontUtf8Hex", "4D5320476F74686963");
		SetMyRegStr("Tooltip", "TooltipUtf8Hex", "66696C653A74636C6F636B5F746F6F6C7469702E747874");
		SetMyRegStr("Tooltip", "Tooltip2Utf8Hex", "222254436C6F636B203C254C44415445253E2222");
		SetMyRegStr("Tooltip", "Tooltip3Utf8Hex", "222254436C6F636B203C254C44415445253E2222");
		SetMyRegLong("BarMeter", "UseBarMeterVL", 1);
		SetMyRegLong("BarMeter", "BarMeterVL_Horizontal", 0);
		SetMyRegLong("BarMeter", "ColorBarMeterVL", 65280);
		SetMyRegLong("BarMeter", "ColorBarMeterVL_Mute", 255);
		SetMyRegLong("BarMeter", "BarMeterVL_Right", 150);
		SetMyRegLong("BarMeter", "BarMeterVL_Width", 5);
		SetMyRegLong("BarMeter", "BarMeterVL_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterVL_Top", 0);
		SetMyRegLong("BarMeter", "UseBarMeterBL", 0);
		SetMyRegLong("BarMeter", "BarMeterBL_Horizontal", 0);
		SetMyRegLong("BarMeter", "ColorBarMeterBL_Charge", 42495);
		SetMyRegLong("BarMeter", "ColorBarMeterBL_High", 65280);
		SetMyRegLong("BarMeter", "ColorBarMeterBL_Mid", 65535);
		SetMyRegLong("BarMeter", "ColorBarMeterBL_Low", 255);
		SetMyRegLong("BarMeter", "BarMeterBL_Right", 130);
		SetMyRegLong("BarMeter", "BarMeterBL_Width", 5);
		SetMyRegLong("BarMeter", "BarMeterBL_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterBL_Top", 0);
		SetMyRegLong("BarMeter", "UseBarMeterCU", 0);
		SetMyRegLong("BarMeter", "BarMeterCU_Horizontal", 0);
		SetMyRegLong("BarMeter", "ColorBarMeterCU_High", 255);
		SetMyRegLong("BarMeter", "ColorBarMeterCU_Mid", 65535);
		SetMyRegLong("BarMeter", "ColorBarMeterCU_Low", 65280);
		SetMyRegLong("BarMeter", "BarMeterCU_Right", 110);
		SetMyRegLong("BarMeter", "BarMeterCU_Width", 5);
		SetMyRegLong("BarMeter", "BarMeterCU_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterCU_Top", 0);
		SetMyRegLong("BarMeter", "UseBarMeterCore", 0);
		SetMyRegLong("BarMeter", "NumberBarMeterCore", 8);
		SetMyRegLong("BarMeter", "ColorBarMeterCore_High", 255);
		SetMyRegLong("BarMeter", "ColorBarMeterCore_Mid", 65535);
		SetMyRegLong("BarMeter", "ColorBarMeterCore_Low", 65280);
		SetMyRegLong("BarMeter", "BarMeterCore_Left", 0);
		SetMyRegLong("BarMeter", "BarMeterCore_Width", 5);
		SetMyRegLong("BarMeter", "BarMeterCore_Spacing", 2);
		SetMyRegLong("BarMeter", "UseBarMeterNet", 0);
		SetMyRegLong("BarMeter", "BarMeterNet_LogGraph", 0);
		SetMyRegLong("BarMeter", "ColorBarMeterNet_Recv", 65280);
		SetMyRegLong("BarMeter", "ColorBarMeterNet_Send", 255);
		SetMyRegLong("BarMeter", "BarMeterNet_Width", 5);
		SetMyRegLong("BarMeter", "BarMeterNetRecv_Top", 0);
		SetMyRegLong("BarMeter", "BarMeterNetRecv_Right", 160);
		SetMyRegLong("BarMeter", "BarMeterNetRecv_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterNetSend_Top", 0);
		SetMyRegLong("BarMeter", "BarMeterNetSend_Right", 170);
		SetMyRegLong("BarMeter", "BarMeterNetSend_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterNet_Horizontal", 0);
		SetMyRegLong("BarMeter", "BarMeterVL_HorizontalToLeft", 0);
		SetMyRegLong("BarMeter", "BarMeterBL_HorizontalToLeft", 0);
		SetMyRegLong("BarMeter", "BarMeterCU_HorizontalToLeft", 0);
		SetMyRegLong("BarMeter", "BarMeterNet_HorizontalToLeft", 0);
		SetMyRegLong("BarMeter", "BarMeterBL_Threshold_High", 50);
		SetMyRegLong("BarMeter", "BarMeterBL_Threshold_Mid", 20);
		SetMyRegLong("BarMeter", "BarMeterCU_Threshold_High", 70);
		SetMyRegLong("BarMeter", "BarMeterCU_Threshold_Mid", 50);
		SetMyRegLong("BarMeter", "UseBarMeterGU", 0);
		SetMyRegLong("BarMeter", "BarMeterGU_Right", 175);
		SetMyRegLong("BarMeter", "BarMeterGU_Bottom", 0);
		SetMyRegLong("BarMeter", "BarMeterGU_Top", 0);
		SetMyRegLong("BarMeter", "ColorBarMeterGPU", 16711935);
		SetMyRegStr("VPN", "SoftEtherKeyword", "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"");
		SetMyRegStr("VPN", "VPN_Keyword1", "");
		SetMyRegStr("VPN", "VPN_Keyword2", "");
		SetMyRegStr("VPN", "VPN_Keyword3", "");
		SetMyRegStr("VPN", "VPN_Keyword4", "");
		SetMyRegStr("VPN", "VPN_Keyword5", "");
		SetMyRegStr("VPN", "VPN_Exclude1", "");
		SetMyRegStr("VPN", "VPN_Exclude2", "");
		SetMyRegStr("VPN", "VPN_Exclude3", "");
		SetMyRegStr("VPN", "VPN_Exclude4", "");
		SetMyRegStr("VPN", "VPN_Exclude5", "");
		SetMyRegLong("ETC", "ZombieCheckInterval", 10);
		SetMyRegStr("ETC", "LTEString", "LTE");
		SetMyRegStr("ETC", "LTEChar", "L");
		SetMyRegStr("ETC", "MuteString", "*");
		SetMyRegStr("ETC", "2chHelpURL", "http://tclock2ch.no.land.to/");
		SetMyRegLong("ETC", "NetMIX_Length", 10);
		SetMyRegLong("ETC", "SSID_AP_Length", 10);
		SetMyRegStr("ETC", "Ethernet_Keyword1", "");
		SetMyRegStr("ETC", "Ethernet_Keyword2", "");
		SetMyRegStr("ETC", "Ethernet_Keyword3", "");
		SetMyRegStr("ETC", "Ethernet_Keyword4", "");
		SetMyRegStr("ETC", "Ethernet_Keyword5", "");
		SetMyRegStr("ETC", "AdditionalMountPath0", "");
		SetMyRegStr("ETC", "AdditionalMountPath1", "");
		SetMyRegStr("ETC", "AdditionalMountPath2", "");
		SetMyRegStr("ETC", "AdditionalMountPath3", "");
		SetMyRegStr("ETC", "AdditionalMountPath4", "");
		SetMyRegStr("ETC", "AdditionalMountPath5", "");
		SetMyRegStr("ETC", "AdditionalMountPath6", "");
		SetMyRegStr("ETC", "AdditionalMountPath7", "");
		SetMyRegStr("ETC", "AdditionalMountPath8", "");
		SetMyRegStr("ETC", "AdditionalMountPath9", "");
		SetMyRegLong("ETC", "MegabytesInGigaByte", 1000);
		SetMyRegStr("ETC", "ExtTXT_String", "");
		SetMyRegLong("ETC", "SelectedThermalZone", 0);
		SetMyRegLong("ETC", "UseHideClockPolicyFlow", 1);
		SetMyRegLong("TCapture", "Enable", 0);
		SetMyRegStr("TCapture", "Path", "TCapture.exe");
		SetMyRegLong("TCalendar", "Enable", 0);
		SetMyRegLong("TCalendar", "Alart", 1);
		SetMyRegStr("TCalendar", "Path", "TCalendar.exe");
		SetMyRegLong("Chime", "EnableChime", 0);
		SetMyRegLong("Chime", "OffsetChimeSec", 0);
		SetMyRegLong("Chime", "ChimeHourStart", 0);
		SetMyRegLong("Chime", "ChimeHourEnd", 24);
		SetMyRegStr("Chime", "ChimeWav", "C:\\Windows\\Media\\notify.wav");
		SetMyRegLong("Chime", "EnableBlinkOnChime", 0);
		SetMyRegLong("Chime", "BlinksOnChime", 3);
		SetMyRegLong("Chime", "EnableSecondaryChime", 0);
		SetMyRegLong("Chime", "CuckooClock", 0);
		SetMyRegLong("Chime", "OffsetSecondaryChimeSec", 1800);
		SetMyRegStr("Chime", "SecondaryChimeWav", "C:\\Windows\\Media\\chimes.wav");
		SetMyRegLong(NULL, "DebugLog", 0);
		SetMyRegLong(NULL, "AutoClearLogFile", 1);
		SetMyRegLong(NULL, "AutoClearLogLines", 1000);
		SetMyRegLong(NULL, "EnableOnSubDisplay", 1);
		SetMyRegLong(NULL, "OffsetClockMS", 0);
		SetMyRegLong(NULL, "CompactMode", 1);
		SetMyRegLong(NULL, "AdjustThreshold", 200);
		SetMyRegLong(NULL, "NormalLog", 1);
		SetMyRegLong(NULL, "AutoRestart", 1);
		{
			int englishMenuDefault = tc_is_japanese_ui_locale() ? 0 : 1;
			SetMyRegLong(NULL, "EnglishMenu", englishMenuDefault);
		}
		SetMyRegLong(NULL, "ShowTrayIcon", 1);

	}
	else
	{
		MessageBoxUtf8Strict(NULL, "tclock-win11.iniの作成に失敗しました。書き込み可能なフォルダで実行してください",
			"TClock-Win11", MB_ICONERROR | MB_OK);
	}
}


/*------------------------------------------------
initialize the registy	//Added by TTTT
--------------------------------------------------*/
BOOL CheckRegistry_Win10(void)
{
	WIN32_FIND_DATAW fd;
	HANDLE hfind;
	wchar_t fnameW[MAX_PATH];
	char s[80];
	BOOL br = FALSE;

	if (!BuildDefaultIniPathW(fnameW, (int)(sizeof(fnameW) / sizeof(fnameW[0])))) {
		return FALSE;
	}
	hfind = FindFirstFileW(fnameW, &fd);

	if (hfind == INVALID_HANDLE_VALUE)
	{
		CreateDefaultIniFile_Win10(fnameW);
		hfind = FindFirstFileW(fnameW, &fd);
	}

	if (hfind != INVALID_HANDLE_VALUE)
	{
		FindClose(hfind);
		//g_bIniSetting = TRUE;
		if (!SetIniPathFromWide(fnameW)) return FALSE;

		br = TRUE;

		GetMyRegStr("Color_Font", "Font", s, 80, "");
		if (s[0] == 0)
		{
			HFONT hfont;
			LOGFONT lf;
			hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			if (hfont)
			{
				GetObject(hfont, sizeof(lf), (LPVOID)&lf);
				SetMyRegStr("Color_Font", "Font", lf.lfFaceName);
			}
		}


		//if ((strcmp(s, "3.3.4.1") == 0)
		//	| (strcmp(s, "3.3.3.1") == 0)
		//	| (strcmp(s, "3.3.2.1") == 0)
		//	| (strcmp(s, "3.3.1.1") == 0))
		//{
		//	SetMyRegStr("VPN", "VPN_Keyword1", "");
		//	SetMyRegStr("VPN", "VPN_Keyword2", "");
		//	SetMyRegStr("VPN", "VPN_Keyword3", "");
		//	SetMyRegStr("VPN", "VPN_Keyword4", "");
		//	SetMyRegStr("VPN", "VPN_Keyword5", "");
		//}

		//if ((strcmp(s, "") != 0) && (strcmp(s, exeVersionString) != 0)) {
		//	//Version Upの時のダイアログ表示
		//	char tempString[1024];
		//	wsprintf(tempString, "%s%s%s%s%s%s%s%s",
		//		"アップデート情報(",
		//		exeVersionString,
		//		")\n\nサブモニタへの時計表示機能追加 (\"書式\"設定パネル)\n※起動時点でメインと同じ方向のタスクバーのみ。サブモニタクロックではクリック等の機能は働きません。",
		//		"\n\nツールチップの無効化設定追加 (\"ツールチップ\"設定パネル)",
		//		"\n\n\nUpdate Information (",
		//		exeVersionString,
		//		")\n\nTClocks on sub monitors (On \"Format\" settings)\n※Only same-direction taskbars. No TClock functions available on sub monitors",
		//		"\n\nTooltip can be disabled (On \"Tooltip\" settings)"
		//		);

		//	MessageBoxUtf8Strict(NULL, tempString, "TClock-Win10", MB_OK | MB_SETFOREGROUND | MB_ICONINFORMATION);
		//}


	}


	return br;

}



// IsUserAnAdmin shell32.dll@680
// http://msdn2.microsoft.com/en-us/library/aa376389.aspx
static BOOL IsUserAdmin(void)
{
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	PSID AdministratorsGroup;
	BOOL b = AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
																		DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
																		&AdministratorsGroup);
	if (b)
	{
		if (!CheckTokenMembership(NULL, AdministratorsGroup, &b))
		{
			b = FALSE;
		}
		FreeSid(AdministratorsGroup);
	}
	return b;
}





#define MSGFLT_ADD 1
#define MSGFLT_REMOVE 2
static BOOL AddMessageFilters(void)
{
	typedef BOOL (WINAPI *pfnChangeWindowMessageFilter)(UINT, DWORD);
	int i;
	UINT messages[] = {
		WM_CLOSE,
		WM_DESTROY,
		WM_COMMAND,
		WM_CONTEXTMENU,
		WM_EXITMENULOOP,
		WM_DROPFILES,
		WM_MOUSEWHEEL,
		WM_LBUTTONDOWN,
		WM_RBUTTONDOWN,
		WM_MBUTTONDOWN,
		WM_XBUTTONDOWN,
		WM_LBUTTONUP,
		WM_RBUTTONUP,
		WM_MBUTTONUP,
		WM_XBUTTONUP,
		WM_USER,
		WM_USER+1,
		WM_USER+2,
	};

	HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
	pfnChangeWindowMessageFilter ChangeWindowMessageFilter = (pfnChangeWindowMessageFilter)
		GetProcAddress(hUser32, "ChangeWindowMessageFilter");
	if (!ChangeWindowMessageFilter)
		return FALSE;

	for (i = 0; i < RTL_NUMBER_OF(messages); i++) {
		ChangeWindowMessageFilter(messages[i], MSGFLT_ADD);
	}
	return TRUE;
}






/*-------------------------------------------
getExeVersion added by TTTT
---------------------------------------------*/
void getExeVersion(const char *fname)
{
	DWORD size;
	BYTE *pBlock;
	VS_FIXEDFILEINFO *pffi;
	wchar_t wFile[MAX_PATH];

	if (tc_utf8_to_utf16(fname, wFile, (int)(sizeof(wFile) / sizeof(wFile[0]))) <= 0) {
		wFile[0] = L'\0';
	}

	size = (wFile[0] != L'\0') ? GetFileVersionInfoSizeW(wFile, 0) : 0;
	if (size > 0)
	{
		pBlock = (BYTE*)malloc(size);
		if (pBlock && GetFileVersionInfoW(wFile, 0, size, pBlock))
		{
			UINT tmp;
			if (VerQueryValueW(pBlock, L"\\", (LPVOID*)&pffi, &tmp))
			{
				exeVersionM = pffi->dwFileVersionMS;
				exeVersionL = pffi->dwFileVersionLS;

				wsprintf(exeVersionString, "%d.%d.%d.%d", (int)HIWORD(exeVersionM), (int)LOWORD(exeVersionM)
					, (int)HIWORD(exeVersionL), (int)LOWORD(exeVersionL));
			}
		}
		if (pBlock) free(pBlock);
	}

}




/*-------------------------------------------
  entry point of program
  not use "WinMain" for compacting the file size
---------------------------------------------*/
#ifndef _DEBUG
#pragma comment(linker, "/subsystem:windows")
#pragma message("entry WinMainCRTStartup")
void __cdecl WinMainCRTStartup(void)
{
#else
#pragma message("entry WinMain")
int WINAPI WinMain(HINSTANCE hinst,HINSTANCE hinstPrev,LPSTR lpszCmdLine, int nShow)
{
	UNREFERENCED_PARAMETER(hinst);
	UNREFERENCED_PARAMETER(hinstPrev);
	UNREFERENCED_PARAMETER(lpszCmdLine);
	UNREFERENCED_PARAMETER(nShow);
#endif

	g_hInst = GetModuleHandleW(NULL);

	SetProcessShutdownParameters(0x1FF, 0); // 最後の方でシャットダウンするアプリケーション

	ExitProcess(TclockExeMain());
}


/*------------------------------------------------
Open a file copied from alarm.c
--------------------------------------------------*/
BOOL ExecFile(HWND hwnd, const char* command)
{
	char fname[MAX_PATH], fpath[MAX_PATH], *opt;
	SHELLEXECUTEINFO sei;
	size_t commandLen;

	UNREFERENCED_PARAMETER(hwnd);
	if (!command || *command == 0) return FALSE;

	commandLen = strlen(command);
	opt = malloc(commandLen + 1);
	if (opt == NULL) return FALSE;
	GetFileAndOption(command, fname, (int)sizeof(fname), opt, (int)(commandLen + 1));
	strcpy(fpath, fname);
	del_title(fpath);
	memset(&sei, 0, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.lpFile = fname;
	sei.lpDirectory = fpath;
	sei.lpParameters = opt[0] ? opt : NULL;
	sei.nShow = SW_SHOW;
	ShellExecuteEx(&sei);
	free(opt);

	return (sei.hInstApp > (HINSTANCE)32);
}


/*--------------------------------------------------------
Retrieve a file name and option from a command string
copied from alarm.c
----------------------------------------------------------*/
void GetFileAndOption(const char* command, char* fname, int fnameBytes, char* opt, int optBytes)
{
	const char* p;
	const char* pe;
	const char* pscan;
	char probe[MAX_PATH];
	WIN32_FIND_DATAW fd;
	HANDLE hfind;
	int i;
	int n;

	if (!fname || fnameBytes <= 0 || !opt || optBytes <= 0) return;
	fname[0] = '\0';
	opt[0] = '\0';
	if (!command || !*command) return;

	pe = NULL;
	pscan = command;
	for (;;)
	{
		if (*pscan == ' ' || *pscan == 0)
		{
			n = (int)(pscan - command);
			if (n > 0)
			{
				if (n >= (int)sizeof(probe)) n = (int)sizeof(probe) - 1;
				for (i = 0; i < n; ++i) probe[i] = command[i];
				probe[n] = '\0';
				hfind = tc_find_first_file_utf8_compat(probe, &fd);
				if (hfind != INVALID_HANDLE_VALUE)
				{
					FindClose(hfind);
					pe = pscan;
					break;
				}
			}
			if (*pscan == 0) break;
		}
		++pscan;
	}
	if (pe == NULL) pe = pscan;

	p = command;
	n = 0;
	while (p != pe && n < fnameBytes - 1)
	{
		fname[n++] = *p++;
	}
	fname[n] = '\0';

	p = pe;
	if (*p == ' ') ++p;
	n = 0;
	while (*p && n < optBytes - 1)
	{
		opt[n++] = *p++;
	}
	opt[n] = '\0';
}


static BOOL IsWow64(void)
{
	BOOL bIsWow64 = FALSE;

	typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS)(HANDLE hProcess, PBOOL Wow64Process);
	LPFN_ISWOW64PROCESS IsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(
		GetModuleHandleW(L"kernel32"), "IsWow64Process");
	if (IsWow64Process)
	{
		if (!IsWow64Process(GetCurrentProcess(), &bIsWow64))
		{
			bIsWow64 = FALSE;
		}
	}
	return bIsWow64;
}
