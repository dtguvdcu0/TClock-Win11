/*-------------------------------------------
  page_win11.c
  Win11 settings page
  by TTTT
---------------------------------------------*/

#include "tclock.h"
#include "../autoback_points.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

static void OnInit(HWND hDlg);
static BOOL OnApply(HWND hDlg);
static DWORD ReadPolicyDword(const char* subkey, const char* valueName, DWORD defval);
static void WritePolicyDword(const char* subkey, const char* valueName, DWORD value);
static BOOL ApplyHideClockActionElevated(HWND hDlg, DWORD hideClock);
static void EnsureHideClockActionButtons(HWND hDlg);
static int MapDluY(HWND hDlg, int dluY);

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern char g_mydir[];

BOOL b_exe_Win11Main = FALSE;
int exe_AdjustTrayCutPosition = 0;
int exe_AdjustWin11ClockWidth = 0;
int exe_AdjutDetectNotify = 0;
BOOL b_exe_AdjustTrayWin11SmallTaskbar = TRUE;

extern BOOL b_EnglishMenu;
extern int Language_Offset;

#ifndef IDC_WIN11_ENABLE_TRANSPARENCY
#define IDC_WIN11_ENABLE_TRANSPARENCY 1900
#endif
#ifndef IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT
#define IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT 1904
#endif
#ifndef IDC_WIN11_HIDE_NATIVE_CLOCK
#define IDC_WIN11_HIDE_NATIVE_CLOCK 1930
#define IDC_WIN11_SHOW_NATIVE_CLOCK 1931
#endif
#ifndef IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI
#define IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI 1932
#endif

static BOOL ApplyHideClockActionElevated(HWND hDlg, DWORD hideClock)
{
	wchar_t params[640];
	HINSTANCE hRet;
	wsprintfW(params,
		L"/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\" /v HideClock /t REG_DWORD /d %lu /f && taskkill /F /IM explorer.exe && start \"\" explorer.exe",
		(unsigned long)hideClock);
	hRet = ShellExecuteW(hDlg, L"runas", L"cmd.exe", params, NULL, SW_HIDE);
	return ((INT_PTR)hRet > 32) ? TRUE : FALSE;
}

static int MapDluY(HWND hDlg, int dluY)
{
	RECT rc;
	rc.left = 0;
	rc.top = 0;
	rc.right = 0;
	rc.bottom = dluY;
	MapDialogRect(hDlg, &rc);
	return rc.bottom;
}

static void EnsureHideClockActionButtons(HWND hDlg)
{
	HWND hCheck;
	HWND hAlign;
	HWND hSave;
	HWND hExperimental;
	RECT rcCheck;
	RECT rcAlign;
	RECT rcSave;
	HWND hBtnHide;
	HWND hBtnShow;
	HFONT hFont;
	int gap;
	int btnWidth;
	int rowLeft;
	int rowWidth;
	int btnHeight;
	int btnTop;
	int alignHeight;
	int experimentalTop;
	const wchar_t* hideLabel;
	const wchar_t* showLabel;
	const wchar_t* experimentalLabel;

	hCheck = GetDlgItem(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR);
	hAlign = GetDlgItem(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT);
	hSave = GetDlgItem(hDlg, IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT);
	hExperimental = GetDlgItem(hDlg, IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI);
	if (!hCheck || !hAlign || !hSave) return;
	if (GetDlgItem(hDlg, IDC_WIN11_HIDE_NATIVE_CLOCK) && GetDlgItem(hDlg, IDC_WIN11_SHOW_NATIVE_CLOCK) && hExperimental) return;
	if (!GetWindowRect(hCheck, &rcCheck)) return;
	if (!GetWindowRect(hAlign, &rcAlign)) return;
	if (!GetWindowRect(hSave, &rcSave)) return;
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcCheck, 2);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcAlign, 2);
	MapWindowPoints(NULL, hDlg, (LPPOINT)&rcSave, 2);
	ShowWindow(hCheck, SW_HIDE);
	rowLeft = rcCheck.left;
	rowWidth = rcCheck.right - rcCheck.left;
	gap = 6;
	btnWidth = (rowWidth - gap) / 2;
	if (btnWidth < 80) btnWidth = 80;
	btnHeight = rcSave.bottom - rcSave.top;
	if (btnHeight <= 0) btnHeight = 14;
	btnTop = rcCheck.top;
	alignHeight = rcAlign.bottom - rcAlign.top;
	if (alignHeight <= 0) alignHeight = 11;
	experimentalTop = btnTop + MapDluY(hDlg, 42);
	hideLabel = b_EnglishMenu ? L"Hide" : L"非表示";
	showLabel = b_EnglishMenu ? L"Show" : L"表示";
	experimentalLabel = b_EnglishMenu ? L"Display with WinUI (experimental)" : L"WinUIで表示(実験的)";

	hBtnHide = CreateWindowExW(0, L"BUTTON",
		hideLabel,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		rowLeft, btnTop, btnWidth, btnHeight,
		hDlg, (HMENU)(INT_PTR)IDC_WIN11_HIDE_NATIVE_CLOCK, g_hInst, NULL);
	hBtnShow = CreateWindowExW(0, L"BUTTON",
		showLabel,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		rowLeft + btnWidth + gap, btnTop, rowWidth - btnWidth - gap, btnHeight,
		hDlg, (HMENU)(INT_PTR)IDC_WIN11_SHOW_NATIVE_CLOCK, g_hInst, NULL);
	if (!hExperimental) {
		hExperimental = CreateWindowExW(0, L"BUTTON",
			experimentalLabel,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
			rcAlign.left, experimentalTop, rcAlign.right - rcAlign.left, alignHeight,
			hDlg, (HMENU)(INT_PTR)IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI, g_hInst, NULL);
	}

	hFont = (HFONT)SendMessage(hCheck, WM_GETFONT, 0, 0);
	if (hFont) {
		if (hBtnHide) SendMessage(hBtnHide, WM_SETFONT, (WPARAM)hFont, TRUE);
		if (hBtnShow) SendMessage(hBtnShow, WM_SETFONT, (WPARAM)hFont, TRUE);
		if (hExperimental) SendMessage(hExperimental, WM_SETFONT, (WPARAM)hFont, TRUE);
	}
}
static DWORD ReadPolicyDword(const char* subkey, const char* valueName, DWORD defval)
{
	HKEY hkey;
	DWORD value = defval;
	DWORD regtype = REG_DWORD;
	DWORD size = sizeof(DWORD);

	if (RegOpenKeyEx(HKEY_CURRENT_USER, subkey, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS) {
		return defval;
	}
	if (RegQueryValueEx(hkey, valueName, 0, &regtype, (LPBYTE)&value, &size) != ERROR_SUCCESS || regtype != REG_DWORD) {
		value = defval;
	}
	RegCloseKey(hkey);
	return value;
}

static void WritePolicyDword(const char* subkey, const char* valueName, DWORD value)
{
	HKEY hkey;
	DWORD disposition = 0;

	if (RegCreateKeyEx(HKEY_CURRENT_USER, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, valueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}
static void WriteTaskbarAlignLeftQuiet(void)
{
	HKEY hkey;
	DWORD disposition = 0;
	DWORD value = 0;
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

	if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, "TaskbarAl", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}


static void WriteTaskbarAlignCenterQuiet(void)
{
	HKEY hkey;
	DWORD disposition = 0;
	DWORD value = 1;
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

	if (RegCreateKeyEx(HKEY_CURRENT_USER, regPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, &disposition) != ERROR_SUCCESS) {
		return;
	}
	RegSetValueEx(hkey, "TaskbarAl", 0, REG_DWORD, (const BYTE*)&value, sizeof(DWORD));
	RegCloseKey(hkey);
}


static void NotifyExplorerAdvancedChanged(void)
{
	DWORD_PTR dw = 0;
	HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
	const char regPath[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)regPath, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"TraySettings", SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	if (hwndTaskbar) {
		SendMessageTimeout(hwndTaskbar, WM_SETTINGCHANGE, 0, (LPARAM)regPath, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
		SendMessageTimeout(hwndTaskbar, WM_SETTINGCHANGE, 0, (LPARAM)"TraySettings", SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &dw);
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}


/*------------------------------------------------
  Win11 property page dialog procedure
--------------------------------------------------*/

// Side-taskbar coordinates are pending until the parent property page is applied.
#define IDD_AB_POINTS 24000
#define IDC_AB_OPEN 1933
#define IDC_AB_HORIZONTAL 1934
#define IDC_AB_X1 24001
#define IDC_AB_Y1 24002
#define IDC_AB_X2 24003
#define IDC_AB_Y2 24004
#define IDC_AB_PICK1 24005
#define IDC_AB_PICK2 24006
#define IDC_AB_BALANCE 24007
#define IDC_AB_SPIN 24008
#define IDC_AB_COLOR1 24009
#define IDC_AB_COLOR2 24010
#define IDC_AB_STATUS 24011
#define IDC_AB_COUNT 24018
#define AB_MARKER_SIZE 17
static AB_POINTS g_abPending[AB_PROFILE_COUNT];
static BOOL g_abDirty[AB_PROFILE_COUNT];
static int g_abModes[AB_PROFILE_COUNT];

typedef struct AB_DIALOG {
 AB_POINTS points;
 int profile;
 int mode;
 BOOL proposed;
 HWND markers[2];
 COLORREF colors[2];
 int picking;
 int dragging;
 BOOL writing;
 BOOL closing;
 AB_POINTS beforeDrag;
} AB_DIALOG;

static void ab_show_markers(AB_DIALOG* state, BOOL show);
static void ab_write_edits(HWND dlg, const AB_POINTS* points);
static BOOL ab_read_edits(HWND dlg, AB_POINTS* points);
static BOOL ab_check_dialog(HWND dlg, AB_DIALOG* state);
static void ab_preview(HWND dlg, AB_DIALOG* state);

static BOOL ab_move_point(AB_DIALOG* state, int index, POINT point)
{
 RECT rect;
 HWND taskbar = ab_find_taskbar(g_hwndClock);
 if (ab_get_profile(taskbar) != state->profile || !GetWindowRect(taskbar, &rect) || !PtInRect(&rect, point) ||
     ab_contains(g_hwndClock, point) || ab_contains(FindWindowW(L"TClockWinUIDllWindow", NULL), point)) return FALSE;
 state->points.position[index * 2] = MulDiv(point.x - rect.left, 10000, max(1, rect.right - rect.left - 1));
 state->points.position[index * 2 + 1] = MulDiv(point.y - rect.top, 10000, max(1, rect.bottom - rect.top - 1));
 return TRUE;
}

static void ab_finish_drag(HWND dlg, AB_DIALOG* state, BOOL cancel)
{
 if (!state->dragging) return;
 state->dragging = 0;
 if (cancel) state->points = state->beforeDrag;
 if (GetCapture() == state->markers[0] || GetCapture() == state->markers[1]) ReleaseCapture();
 if (!state->closing) { ab_write_edits(dlg, &state->points); ab_preview(dlg, state); }
}

static LRESULT CALLBACK ab_handle_marker(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
 HWND dlg = GetWindow(hwnd, GW_OWNER);
 AB_DIALOG* state = (AB_DIALOG*)GetWindowLongPtrW(dlg, DWLP_USER);
 int index = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
 switch (message) {
 case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
 case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32646))); return TRUE;
 case WM_LBUTTONDOWN:
  if (state && index < state->points.count && !state->picking && ab_check_dialog(dlg, state) && ab_read_edits(dlg, &state->points)) {
   state->beforeDrag = state->points; state->dragging = index + 1; SetCapture(hwnd);
  }
  return 0;
 case WM_MOUSEMOVE:
  if (state && state->dragging == index + 1 && GetCapture() == hwnd) {
   POINT point = { (short)LOWORD(lp), (short)HIWORD(lp) };
   if (ClientToScreen(hwnd, &point) && ab_move_point(state, index, point)) { ab_write_edits(dlg, &state->points); ab_show_markers(state, TRUE); }
  }
  return 0;
 case WM_LBUTTONUP:
  if (state && state->dragging == index + 1) ab_finish_drag(dlg, state, FALSE);
  return 0;
 case WM_CAPTURECHANGED:
  if (state && state->dragging == index + 1) ab_finish_drag(dlg, state, TRUE);
  return 0;
 case WM_PAINT: {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(hwnd, &paint);
  RECT rect;
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 255));
  HBRUSH brush = CreateSolidBrush(RGB(24, 24, 24));
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  GetClientRect(hwnd, &rect); Rectangle(dc, 0, 0, rect.right, rect.bottom);
  SetTextColor(dc, RGB(255, 255, 0)); SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, index ? L"2" : L"1", 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen); DeleteObject(brush);
  EndPaint(hwnd, &paint);
  return 0;
 }
 }
 return DefWindowProcW(hwnd, message, wp, lp);
}

static void ab_show_markers(AB_DIALOG* state, BOOL show)
{
 RECT rect;
 HWND taskbar = ab_find_taskbar(g_hwndClock);
 int i;
 if (ab_get_profile(taskbar) != state->profile || !GetWindowRect(taskbar, &rect)) show = FALSE;
 for (i = 0; i < 2; ++i) {
  if (show && i < state->points.count) {
   POINT point = ab_get_point(&rect, state->points.position[i * 2], state->points.position[i * 2 + 1]);
   SetWindowPos(state->markers[i], HWND_TOPMOST, point.x - AB_MARKER_SIZE / 2, point.y - AB_MARKER_SIZE / 2, AB_MARKER_SIZE, AB_MARKER_SIZE, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else ShowWindow(state->markers[i], SW_HIDE);
 }
}

static void ab_write_edits(HWND dlg, const AB_POINTS* points)
{
 AB_DIALOG* state = (AB_DIALOG*)GetWindowLongPtrW(dlg, DWLP_USER);
 int i;
 state->writing = TRUE;
 for (i = 0; i < 4; ++i) {
  WCHAR text[24];
  wsprintfW(text, L"%d.%02d", points->position[i] / 100, points->position[i] % 100);
  SetDlgItemTextW(dlg, IDC_AB_X1 + i, text);
 }
 SetDlgItemInt(dlg, IDC_AB_BALANCE, points->balance, FALSE);
 SendDlgItemMessageW(dlg, IDC_AB_COUNT, CB_SETCURSEL, points->count - 1, 0);
 state->writing = FALSE;
}

static BOOL ab_read_edits(HWND dlg, AB_POINTS* points)
{
 AB_POINTS result = *points;
 int i;
 BOOL valid;
 result.count = (int)SendDlgItemMessageW(dlg, IDC_AB_COUNT, CB_GETCURSEL, 0, 0) + 1;
 if (result.count != 1 && result.count != 2) return FALSE;
 for (i = 0; i < result.count * 2; ++i) {
  WCHAR text[32];
  int whole = 0, fraction = 0, decimals = 0, j;
  BOOL dot = FALSE, digit = FALSE;
  GetDlgItemTextW(dlg, IDC_AB_X1 + i, text, _countof(text));
  for (j = 0; text[j]; ++j) {
   if (text[j] == L'.' && !dot) { dot = TRUE; continue; }
   if (text[j] < L'0' || text[j] > L'9') return FALSE;
   digit = TRUE;
   if (dot) {
    if (++decimals > 2) return FALSE;
    fraction = fraction * 10 + text[j] - L'0';
   } else {
    whole = whole * 10 + text[j] - L'0';
    if (whole > 100) return FALSE;
   }
  }
  result.position[i] = whole * 100 + fraction * (decimals == 1 ? 10 : 1);
  if (!digit || result.position[i] > 10000) return FALSE;
 }
 if (result.count == 2) {
  result.balance = GetDlgItemInt(dlg, IDC_AB_BALANCE, &valid, FALSE);
  if (!valid || result.balance < 0 || result.balance > 100) return FALSE;
 }
 *points = result;
 return TRUE;
}

static BOOL ab_check_dialog(HWND dlg, AB_DIALOG* state)
{
 BOOL active = ab_get_profile(ab_find_taskbar(g_hwndClock)) == state->profile;
 BOOL editable = state->mode != AB_FUTURE;
 EnableWindow(GetDlgItem(dlg, IDC_AB_PICK1), active && editable);
 EnableWindow(GetDlgItem(dlg, IDC_AB_PICK2), active && editable && state->points.count == 2);
 EnableWindow(GetDlgItem(dlg, IDC_AB_COUNT), editable);
 EnableWindow(GetDlgItem(dlg, IDC_AB_X2), editable && state->points.count == 2);
 EnableWindow(GetDlgItem(dlg, IDC_AB_Y2), editable && state->points.count == 2);
 EnableWindow(GetDlgItem(dlg, IDC_AB_BALANCE), editable && state->points.count == 2);
 EnableWindow(GetDlgItem(dlg, IDC_AB_SPIN), editable && state->points.count == 2);
 ShowWindow(GetDlgItem(dlg, IDC_AB_COLOR2), state->points.count == 2 ? SW_SHOW : SW_HIDE);
 EnableWindow(GetDlgItem(dlg, IDOK), active && editable);
 if (!active || !editable) {
  if (state->dragging) {
   state->points = state->beforeDrag; state->dragging = 0;
   if (GetCapture() == state->markers[0] || GetCapture() == state->markers[1]) ReleaseCapture();
   ab_write_edits(dlg, &state->points);
  }
  if (state->picking) { state->picking = 0; if (GetCapture() == dlg) ReleaseCapture(); }
  ab_show_markers(state, FALSE);
  state->colors[0] = state->colors[1] = CLR_INVALID;
  SetDlgItemTextW(dlg, IDC_AB_COLOR1, L"---"); SetDlgItemTextW(dlg, IDC_AB_COLOR2, L"---");
  SetDlgItemTextW(dlg, IDC_AB_STATUS, !editable ?
   (b_EnglishMenu ? L"Settings belong to a newer version; changes are disabled." : L"\u65b0\u3057\u3044\u30d0\u30fc\u30b8\u30e7\u30f3\u306e\u8a2d\u5b9a\u306e\u305f\u3081\u5909\u66f4\u3067\u304d\u307e\u305b\u3093\u3002") :
   (b_EnglishMenu ? L"This profile is inactive at the current taskbar position." : L"\u73fe\u5728\u306e\u30bf\u30b9\u30af\u30d0\u30fc\u914d\u7f6e\u3067\u306f\u3053\u306e\u8a2d\u5b9a\u306f\u9078\u629e\u3067\u304d\u307e\u305b\u3093\u3002"));
  return FALSE;
 }
 return TRUE;
}

static void ab_preview(HWND dlg, AB_DIALOG* state)
{
 HWND taskbar = ab_find_taskbar(g_hwndClock);
 WCHAR text[120];
 int i;
 if (state->writing || state->closing || !ab_check_dialog(dlg, state) || state->picking || state->dragging) return;
 if (!ab_read_edits(dlg, &state->points)) {
  ab_show_markers(state, FALSE);
  SetDlgItemTextW(dlg, IDC_AB_STATUS, b_EnglishMenu ? L"Enter percentages from 0 to 100." : L"\u4f4d\u7f6e\u306f0\uff5e100%\u3067\u5165\u529b\u3057\u3066\u304f\u3060\u3055\u3044\u3002");
  return;
 }
 ab_check_dialog(dlg, state);
 ab_show_markers(state, FALSE);
 DwmFlush();
 if (ab_sample(taskbar, g_hwndClock, &state->points, state->colors)) {
  for (i = 0; i < 2; ++i) {
   wsprintfW(text, L"#%02X%02X%02X", GetRValue(state->colors[i]), GetGValue(state->colors[i]), GetBValue(state->colors[i]));
   SetDlgItemTextW(dlg, IDC_AB_COLOR1 + i, text);
  }
  if (state->points.count == 1) SetDlgItemTextW(dlg, IDC_AB_STATUS, b_EnglishMenu ? L"Use Point 1 color. Apply in Properties." : L"\u30dd\u30a4\u30f3\u30c81\u306e\u8272\u3092\u4f7f\u3044\u307e\u3059\u3002\u30d7\u30ed\u30d1\u30c6\u30a3\u3067\u9069\u7528\u3057\u307e\u3059\u3002");
  else SetDlgItemTextW(dlg, IDC_AB_STATUS, b_EnglishMenu ? L"0 = Point 1, 100 = Point 2. Apply in Properties." : L"0: \u30dd\u30a4\u30f3\u30c81 / 100: \u30dd\u30a4\u30f3\u30c82\u3002\u30d7\u30ed\u30d1\u30c6\u30a3\u3067\u9069\u7528\u3057\u307e\u3059\u3002");
 } else {
  SetDlgItemTextW(dlg, IDC_AB_COLOR1, L"---"); SetDlgItemTextW(dlg, IDC_AB_COLOR2, L"---");
  SetDlgItemTextW(dlg, IDC_AB_STATUS, b_EnglishMenu ? L"Choose visible background outside the clock." : L"\u6642\u8a08\u3068\u91cd\u306a\u3089\u306a\u3044\u80cc\u666f\u3092\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002");
 }
 ab_show_markers(state, TRUE);
}

static INT_PTR CALLBACK ab_handle_dialog(HWND dlg, UINT message, WPARAM wp, LPARAM lp)
{
 AB_DIALOG* state = (AB_DIALOG*)GetWindowLongPtrW(dlg, DWLP_USER);
 if (message == WM_INITDIALOG) {
  WNDCLASSW wc = { 0 };
  int i;
  state = (AB_DIALOG*)lp;
  SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)state);
  wc.lpfnWndProc = ab_handle_marker; wc.hInstance = g_hInst; wc.lpszClassName = L"TClockSamplePoint";
  RegisterClassW(&wc);
  for (i = 0; i < 2; ++i) {
   state->markers[i] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    wc.lpszClassName, L"", WS_POPUP, 0, 0, AB_MARKER_SIZE, AB_MARKER_SIZE, dlg, NULL, g_hInst, NULL);
   SetWindowLongPtrW(state->markers[i], GWLP_USERDATA, i);
   SetLayeredWindowAttributes(state->markers[i], RGB(0, 0, 0), 255, LWA_COLORKEY);
  }
  SetDlgItemTextW(dlg, 24019, b_EnglishMenu ? L"Sampling points" : L"\u53d6\u5f97\u30dd\u30a4\u30f3\u30c8\u6570");
  SendDlgItemMessageW(dlg, IDC_AB_COUNT, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"1 point" : L"1\u70b9"));
  SendDlgItemMessageW(dlg, IDC_AB_COUNT, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"2 points" : L"2\u70b9"));
  if (!b_EnglishMenu) {
   SetWindowTextW(dlg, L"\u5de6\u53f3\u914d\u7f6e\u306e\u80cc\u666f\u30dd\u30a4\u30f3\u30c8");
   SetDlgItemTextW(dlg, 24012, L"\u4f4d\u7f6e\u306f\u30bf\u30b9\u30af\u30d0\u30fc\u306e\u5de6\u4e0a\u304b\u3089\u306e\u5272\u5408(%)\u3067\u3059\u3002");
   SetDlgItemTextW(dlg, 24013, L"\u30dd\u30a4\u30f3\u30c81"); SetDlgItemTextW(dlg, 24014, L"\u30dd\u30a4\u30f3\u30c82");
   SetDlgItemTextW(dlg, IDC_AB_PICK1, L"\u753b\u9762\u3067\u9078\u629e"); SetDlgItemTextW(dlg, IDC_AB_PICK2, L"\u753b\u9762\u3067\u9078\u629e");
   SetDlgItemTextW(dlg, 24015, L"2\u70b9\u306e\u8272\u306e\u30d0\u30e9\u30f3\u30b9");
   SetDlgItemTextW(dlg, 24016, L"\u6b63\u65b9\u5f62\u3092\u30c9\u30e9\u30c3\u30b0\u3057\u3066\u4f4d\u7f6e\u3092\u8abf\u6574\u3067\u304d\u307e\u3059\u3002");
   SetDlgItemTextW(dlg, IDCANCEL, L"\u30ad\u30e3\u30f3\u30bb\u30eb");
  }
  SetWindowTextW(dlg, state->profile == AB_SIDE ?
   (b_EnglishMenu ? L"Left/right background points" : L"\u5de6\u53f3\u914d\u7f6e\u306e\u80cc\u666f\u30dd\u30a4\u30f3\u30c8") :
   (b_EnglishMenu ? L"Top/bottom background points" : L"\u4e0a\u4e0b\u914d\u7f6e\u306e\u80cc\u666f\u30dd\u30a4\u30f3\u30c8"));
  if (state->profile == AB_HORIZONTAL && state->mode != AB_POINTS_SAVED && state->mode != AB_FUTURE) {
   SetDlgItemTextW(dlg, 24017, state->proposed ?
    (b_EnglishMenu ? L"Proposed from legacy positions. Apply switches to point sampling; the old gradient may change." : L"\u5f93\u6765\u306e\u4f4d\u7f6e\u304b\u3089\u306e\u5019\u88dc\u3067\u3059\u3002\u9069\u7528\u3059\u308b\u3068\u30dd\u30a4\u30f3\u30c8\u65b9\u5f0f\u306b\u5207\u308a\u66ff\u308f\u308a\u3001\u5f93\u6765\u306e\u30b0\u30e9\u30c7\u30fc\u30b7\u30e7\u30f3\u304c\u5909\u308f\u308b\u5834\u5408\u304c\u3042\u308a\u307e\u3059\u3002") :
    (b_EnglishMenu ? L"Default positions: choose background points. Only Properties Apply switches from legacy settings." : L"\u521d\u671f\u4f4d\u7f6e\u3067\u3059\u3002\u80cc\u666f\u3092\u9078\u629e\u3057\u3066\u304f\u3060\u3055\u3044\u3002\u30d7\u30ed\u30d1\u30c6\u30a3\u3067\u9069\u7528\u3059\u308b\u307e\u3067\u65e7\u8a2d\u5b9a\u306f\u5909\u308f\u308a\u307e\u305b\u3093\u3002"));
  } else SetDlgItemTextW(dlg, 24017, b_EnglishMenu ? L"Top/bottom and left/right positions are saved separately." : L"\u4e0a\u4e0b\u914d\u7f6e\u3068\u5de6\u53f3\u914d\u7f6e\u306e\u4f4d\u7f6e\u306f\u5225\u3005\u306b\u4fdd\u5b58\u3055\u308c\u307e\u3059\u3002");
  if (state->mode == AB_FUTURE) {
   for (i = IDC_AB_X1; i <= IDC_AB_Y2; ++i) EnableWindow(GetDlgItem(dlg, i), FALSE);
   EnableWindow(GetDlgItem(dlg, IDC_AB_BALANCE), FALSE); EnableWindow(GetDlgItem(dlg, IDC_AB_SPIN), FALSE);
  }
  SendDlgItemMessageW(dlg, IDC_AB_SPIN, UDM_SETRANGE32, 0, 100);
  ab_write_edits(dlg, &state->points);
  SetTimer(dlg, 1, 500, NULL); ab_preview(dlg, state);
  return TRUE;
 }
 if (!state) return FALSE;
 switch (message) {
 case WM_CTLCOLORSTATIC: {
  int id = GetDlgCtrlID((HWND)lp);
  if ((id == IDC_AB_COLOR1 || id == IDC_AB_COLOR2) && state->colors[id - IDC_AB_COLOR1] != CLR_INVALID) {
   COLORREF color = state->colors[id - IDC_AB_COLOR1];
   HDC dc = (HDC)wp;
   SetBkColor(dc, color); SetDCBrushColor(dc, color);
   SetTextColor(dc, (GetRValue(color) + GetGValue(color) + GetBValue(color) > 382) ? RGB(0,0,0) : RGB(255,255,255));
   return (INT_PTR)GetStockObject(DC_BRUSH);
  }
  break;
 }
 case WM_TIMER: case WM_DISPLAYCHANGE: ab_preview(dlg, state); return TRUE;
 case WM_SETCURSOR:
  if (state->picking) { SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32515))); return TRUE; }
  break;
 case WM_LBUTTONDOWN:
  if (state->picking) {
   RECT rect; POINT point; int index = (state->picking - 1) * 2;
   HWND taskbar = ab_find_taskbar(g_hwndClock);
   GetCursorPos(&point);
   if (ab_check_dialog(dlg, state) && GetWindowRect(taskbar, &rect) && PtInRect(&rect, point) &&
    !ab_contains(g_hwndClock, point) && !ab_contains(FindWindowW(L"TClockWinUIDllWindow", NULL), point)) {
    state->points.position[index] = MulDiv(point.x - rect.left, 10000, max(1, rect.right - rect.left - 1));
    state->points.position[index + 1] = MulDiv(point.y - rect.top, 10000, max(1, rect.bottom - rect.top - 1));
    state->picking = 0; ReleaseCapture(); ab_write_edits(dlg, &state->points); ab_preview(dlg, state);
   }
   return TRUE;
  }
  break;
 case WM_CAPTURECHANGED: state->picking = 0; return TRUE;
 case WM_COMMAND:
  if (!state->writing && ((HIWORD(wp) == EN_CHANGE && LOWORD(wp) >= IDC_AB_X1 && LOWORD(wp) <= IDC_AB_Y2) ||
      (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == IDC_AB_BALANCE) ||
      (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_AB_COUNT))) {
   ab_preview(dlg, state); return TRUE;
  }
  switch (LOWORD(wp)) {
  case IDC_AB_PICK1: case IDC_AB_PICK2:
   if (!ab_check_dialog(dlg, state) || !ab_read_edits(dlg, &state->points)) { ab_preview(dlg, state); return TRUE; }
   state->picking = LOWORD(wp) == IDC_AB_PICK1 ? 1 : 2;
   SetCapture(dlg); SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32515)));
   SetDlgItemTextW(dlg, IDC_AB_STATUS, b_EnglishMenu ? L"Click taskbar background. Esc cancels selection." : L"\u30bf\u30b9\u30af\u30d0\u30fc\u306e\u80cc\u666f\u3092\u30af\u30ea\u30c3\u30af\u3002Esc\u3067\u4e2d\u6b62\u3002");
   return TRUE;
  case IDOK:
   if (!ab_check_dialog(dlg, state) || !ab_read_edits(dlg, &state->points)) { ab_preview(dlg, state); return TRUE; }
   ab_show_markers(state, FALSE); DwmFlush();
   if (!ab_sample(ab_find_taskbar(g_hwndClock), g_hwndClock, &state->points, state->colors)) { ab_preview(dlg, state); return TRUE; }
   g_abPending[state->profile] = state->points; g_abDirty[state->profile] = TRUE; EndDialog(dlg, IDOK); return TRUE;
  case IDCANCEL:
   if (state->dragging) { ab_finish_drag(dlg, state, TRUE); return TRUE; }
   if (state->picking) { state->picking = 0; ReleaseCapture(); ab_preview(dlg, state); return TRUE; }
   EndDialog(dlg, IDCANCEL); return TRUE;
  }
  break;
 case WM_DESTROY:
  state->closing = TRUE; ab_finish_drag(dlg, state, TRUE);
  KillTimer(dlg, 1);
  if (GetCapture() == dlg) ReleaseCapture();
  DestroyWindow(state->markers[0]); DestroyWindow(state->markers[1]); return TRUE;
 }
 return FALSE;
}

static void ab_update_buttons(HWND dlg)
{
 int profile = ab_get_profile(ab_find_taskbar(g_hwndClock));
 EnableWindow(GetDlgItem(dlg, IDC_AB_HORIZONTAL), b_exe_Win11Main && profile == AB_HORIZONTAL);
 EnableWindow(GetDlgItem(dlg, IDC_AB_OPEN), b_exe_Win11Main && profile == AB_SIDE);
}

static void ab_create_buttons(HWND dlg)
{
 RECT rect, saveRect;
 int i, width, gap = 6;
 HWND save = GetDlgItem(dlg, IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT);
 HWND row = GetDlgItem(dlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR);
 GetWindowRect(row, &rect); MapWindowPoints(NULL, dlg, (POINT*)&rect, 2);
 GetWindowRect(save, &saveRect); MapWindowPoints(NULL, dlg, (POINT*)&saveRect, 2);
 width = (rect.right - rect.left - gap) / 2;
 for (i = 0; i < AB_PROFILE_COUNT; ++i) {
  const WCHAR* label = i == AB_HORIZONTAL ?
   (b_EnglishMenu ? L"Top/bottom..." : L"\u4e0a\u4e0b\u306e\u8a2d\u5b9a...") :
   (b_EnglishMenu ? L"Left/right..." : L"\u5de6\u53f3\u306e\u8a2d\u5b9a...");
  HWND button = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
   rect.left + i * (width + gap), saveRect.top - MapDluY(dlg, 32), width, saveRect.bottom - saveRect.top,
   dlg, (HMENU)(INT_PTR)(i == AB_HORIZONTAL ? IDC_AB_HORIZONTAL : IDC_AB_OPEN), g_hInst, NULL);
  SendMessageW(button, WM_SETFONT, SendMessageW(save, WM_GETFONT, 0, 0), TRUE);
 }
 ab_update_buttons(dlg);
 SetTimer(dlg, 2, 500, NULL);
}

static BOOL ab_propose_legacy(AB_POINTS* points)
{
 AB_POINTS proposed = *points;
 RECT before, after;
 HWND taskbar = ab_find_taskbar(g_hwndClock);
 UINT query = RegisterWindowMessageW(AB_QUERY_MESSAGE);
 int i;
 COLORREF colors[2];
 if (ab_get_profile(taskbar) != AB_HORIZONTAL || !GetWindowRect(taskbar, &before)) return FALSE;
 for (i = 0; i < 4; ++i) {
  DWORD_PTR value = 0;
  if (!SendMessageTimeoutW(g_hwndClock, query, i, 0, SMTO_ABORTIFHUNG, 500, &value) || value < 1 || value > 10001) return FALSE;
  proposed.position[i] = (int)value - 1;
 }
 if (!GetWindowRect(taskbar, &after) || !EqualRect(&before, &after) || !ab_sample(taskbar, g_hwndClock, &proposed, colors)) return FALSE;
 proposed.balance = ab_clamp((int)GetMyRegLong("Color_Font", "AutoBackBlendRatio", 50), 100);
 *points = proposed;
 return TRUE;
}

INT_PTR CALLBACK PageWin11Proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	switch(message)
	{
		case WM_INITDIALOG:
			OnInit(hDlg);
			return TRUE;
		case WM_TIMER: case WM_DISPLAYCHANGE:
			ab_update_buttons(hDlg); return TRUE;
		case WM_DESTROY:
			KillTimer(hDlg, 2); return TRUE;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_AB_OPEN || LOWORD(wParam) == IDC_AB_HORIZONTAL) {
				AB_DIALOG state = { 0 };
				state.profile = LOWORD(wParam) == IDC_AB_OPEN ? AB_SIDE : AB_HORIZONTAL;
				if (ab_get_profile(ab_find_taskbar(g_hwndClock)) != state.profile) return TRUE;
				state.points = g_abPending[state.profile];
				state.mode = g_abModes[state.profile];
				if (state.profile == AB_HORIZONTAL && !g_abDirty[AB_HORIZONTAL] && (state.mode == AB_LEGACY || state.mode == AB_INVALID))
					state.proposed = ab_propose_legacy(&state.points);
				if (DialogBoxParamW(GetLangModule(), MAKEINTRESOURCEW(IDD_AB_POINTS), hDlg, ab_handle_dialog, (LPARAM)&state) == IDOK) SendPSChanged(hDlg);
				return TRUE;
			}
			if (LOWORD(wParam) == IDC_WIN11_HIDE_NATIVE_CLOCK || LOWORD(wParam) == IDC_WIN11_SHOW_NATIVE_CLOCK) {
				DWORD hideClock = (LOWORD(wParam) == IDC_WIN11_HIDE_NATIVE_CLOCK) ? 1 : 0;
				WritePolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", hideClock);
				CheckDlgButton(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, hideClock ? BST_CHECKED : BST_UNCHECKED);
				if (!ApplyHideClockActionElevated(hDlg, hideClock)) {
					MyMessageBoxW(hDlg,
						L"Failed to run elevated action.\nPlease approve UAC prompt and try again.",
						L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
				}
				return TRUE;
			}
			if (LOWORD(wParam) == IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT) {
				LRESULT saved = 0;
				if (IsWindow(g_hwndClock)) {
					saved = SendMessage(g_hwndClock, WM_COMMAND, (WPARAM)CLOCKM_SNAPSHOT_AUTOBACK_SAVE, 0);
				}
				if (saved) {
					MyMessageBoxW(hDlg,
						L"現在の自動取得背景色を保存しました。\n固定値として使うには「背景色をタスクバーに自動一致」をOFFにしてください。\n\nSaved current auto-matched background colors.\nTurn off \"Auto match taskbar background\" to use fixed values.",
						L"TClock-Win11", MB_OK, MB_ICONINFORMATION);
				}
				else {
					MyMessageBoxW(hDlg,
						L"背景色の保存に失敗しました。\nタスクバー色の取得状態を確認して再実行してください。\n\nFailed to save background colors.\nPlease retry after taskbar color sampling is available.",
						L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
				}
				return TRUE;
			}
			SendPSChanged(hDlg);
			return TRUE;
		case WM_NOTIFY:
			switch (((NMHDR *)lParam)->code)
			{
			case PSN_APPLY:
				SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, OnApply(hDlg) ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE);
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
	int autoBackRefreshSec;
	DWORD hideClock;
	DWORD transparency;
	BOOL alignLeft;
	BOOL displayWinUIExperimental;

	b_exe_Win11Main = GetMyRegLong("Status_DoNotEdit", "Win11TClockMain", 9);

	autoBackRefreshSec = (int)GetMyRegLong("Color_Font", "AutoBackRefreshSec", 1);
	if (autoBackRefreshSec < 1) autoBackRefreshSec = 1;
	if (autoBackRefreshSec > 120) autoBackRefreshSec = 120;

	hideClock = ReadPolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "HideClock", 0);
	transparency = ReadPolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", 1);
	alignLeft = (BOOL)GetMyRegLong("Win11", "AlignTaskbarLeft", 1);
	displayWinUIExperimental = (BOOL)GetMyRegLong("Win11", "ExperimentalDisplayBackend", 0);

	CheckDlgButton(hDlg, IDC_ETC_USE_WIN11NOTIFY, (BOOL)GetMyRegLong("Color_Font", "AutoBackMatchTaskbar", 1));
	CheckDlgButton(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, hideClock ? BST_CHECKED : BST_UNCHECKED);
	EnsureHideClockActionButtons(hDlg);
	{
		int profile;
		for (profile = 0; profile < AB_PROFILE_COUNT; ++profile) {
			g_abModes[profile] = ab_load(profile, &g_abPending[profile]);
			g_abDirty[profile] = FALSE;
		}
	}
	ab_create_buttons(hDlg);
	CheckDlgButton(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, transparency ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, alignLeft ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI, displayWinUIExperimental ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETRANGE, 0, MAKELONG(120, 1));
	SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_SETPOS, 0, autoBackRefreshSec);

	if (!b_exe_Win11Main) {
		EnableDlgItem(hDlg, IDC_ETC_USE_WIN11NOTIFY, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_ADJUST_WIN11_SMALLTASKBAR, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_HIDE_NATIVE_CLOCK, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_SHOW_NATIVE_CLOCK, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI, FALSE);
		EnableDlgItem(hDlg, IDC_SPG_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_ETC_CUT_LIMIT, FALSE);
		EnableDlgItem(hDlg, IDC_WIN11_SAVE_AUTOBACK_SNAPSHOT, FALSE);
	}
}

/*------------------------------------------------
  "Apply" button
--------------------------------------------------*/
static BOOL OnApply(HWND hDlg)
{
	int autoBackRefreshSec;
	DWORD transparency;
	BOOL alignLeft;
	BOOL displayWinUIExperimental;
	BOOL notifyExplorer = FALSE;

 {
  int profile;
  for (profile = 0; profile < AB_PROFILE_COUNT; ++profile) {
   if (g_abDirty[profile]) {
    if (!ab_save(profile, &g_abPending[profile])) {
     MyMessageBoxW(hDlg, b_EnglishMenu ? L"Point settings could not be saved. Check the INI or its version." : L"\u30dd\u30a4\u30f3\u30c8\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002INI\u3068\u30d0\u30fc\u30b8\u30e7\u30f3\u3092\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044\u3002", L"TClock-Win11", MB_OK, MB_ICONEXCLAMATION);
     return FALSE;
    }
    g_abModes[profile] = AB_POINTS_SAVED;
    g_abDirty[profile] = FALSE;
   }
  }
 }
	SetMyRegLong("Color_Font", "AutoBackMatchTaskbar", IsDlgButtonChecked(hDlg, IDC_ETC_USE_WIN11NOTIFY));
	if (IsDlgButtonChecked(hDlg, IDC_ETC_USE_WIN11NOTIFY)) {
		SetMyRegLong("Color_Font", "UseBackColor", 0);
	}

	autoBackRefreshSec = (int)(short)SendDlgItemMessage(hDlg, IDC_SPG_ETC_CUT_LIMIT, UDM_GETPOS, 0, 0);
	if (autoBackRefreshSec < 1) autoBackRefreshSec = 1;
	if (autoBackRefreshSec > 120) autoBackRefreshSec = 120;
	SetMyRegLong("Color_Font", "AutoBackRefreshSec", autoBackRefreshSec);

	transparency = IsDlgButtonChecked(hDlg, IDC_WIN11_ENABLE_TRANSPARENCY) ? 1 : 0;
	if (ReadPolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", 1) != transparency) {
		WritePolicyDword("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "EnableTransparency", transparency);
		notifyExplorer = TRUE;
	}

	alignLeft = IsDlgButtonChecked(hDlg, IDC_WIN11_TASKBAR_ALIGN_LEFT) ? TRUE : FALSE;
	SetMyRegLongDef("Win11", "AlignTaskbarLeft", alignLeft);
	displayWinUIExperimental = IsDlgButtonChecked(hDlg, IDC_WIN11_EXPERIMENTAL_DISPLAY_WINUI) ? TRUE : FALSE;
	SetMyRegLong("Win11", "ExperimentalDisplayBackend", displayWinUIExperimental ? 1 : 0);
	if (ReadPolicyDword("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "TaskbarAl", 1) != (DWORD)(alignLeft ? 0 : 1)) {
		if (alignLeft) WriteTaskbarAlignLeftQuiet();
		else WriteTaskbarAlignCenterQuiet();
		notifyExplorer = TRUE;
	}
	if (notifyExplorer) NotifyExplorerAdvancedChanged();
	return TRUE;
}
