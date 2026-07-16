#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include "wui_api.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

#define WUI_TIMER_ID 1
#define WUI_TIMER_MS 100
#define WUI_TIP_TIMER_ID 2
#define WUI_TIP_FOLLOWUP_TIMER_ID 3
#define WUI_TIP_LEAVE_TIMER_ID 4
#define WUI_TIP_LEAVE_MS 320

static HINSTANCE g_wuiInst = NULL;
static HWND g_wuiTarget = NULL;
static HWND g_wuiHost = NULL;
static HWND g_wuiTooltip = NULL;
static WCHAR g_wuiTooltipText[4096];
static TC_DISPLAY_BACKEND_RENDER_STATE g_wuiState;
static ULONG_PTR g_wuiGdip = 0;
static RECT g_wuiLastTarget = { 0, 0, 0, 0 };
static RECT g_wuiLastPlace = { 0, 0, 0, 0 };
static int g_wuiContentLeft = 0;
static int g_wuiContentWidth = 0;
static BOOL g_wuiHasTarget = FALSE;
static BOOL g_wuiHasPlace = FALSE;
static BOOL g_wuiHoverInside = FALSE;
static BOOL g_wuiTipPending = FALSE;
static BOOL g_wuiTipVisible = FALSE;
static BOOL g_wuiTipShownOnce = FALSE;
static UINT g_wuiTipAutoPopDelay = 0;
static UINT g_wuiTipStage = 0;
// TEMP_VERIFY_START: remove after comparing vertical tooltip activation backends.
static BOOL g_wuiLegacyTip = FALSE;
static POINT g_wuiLastHover = { 0, 0 };
// TEMP_VERIFY_END

// TEMP_VERIFY_START: remove after mode0 vertical tooltip runtime investigation is complete.
static void wui_tip_trace(const WCHAR* tag)
{
	WCHAR path[MAX_PATH];
	WCHAR line[512];
	DWORD written;
	HANDLE file;
	POINT cursor = { 0, 0 };
	RECT rcHost = { 0, 0, 0, 0 };
	RECT rcTip = { 0, 0, 0, 0 };
	int hostOk = 0;
	int cursorOk = 0;
	int tipOk = 0;
	int tipVisible = 0;

	if (g_wuiLegacyTip) return;
	if (!g_wuiInst) return;
	if (!GetModuleFileNameW(g_wuiInst, path, _countof(path))) return;
	for (int i = lstrlenW(path) - 1; i >= 0; --i) {
		if (path[i] == L'\\' || path[i] == L'/') {
			path[i + 1] = L'\0';
			break;
		}
	}
	lstrcatW(path, L"wui_tip_trace.log");
	if (g_wuiHost && IsWindow(g_wuiHost)) hostOk = GetWindowRect(g_wuiHost, &rcHost) ? 1 : 0;
	if (g_wuiTooltip && IsWindow(g_wuiTooltip)) {
		tipOk = GetWindowRect(g_wuiTooltip, &rcTip) ? 1 : 0;
		tipVisible = IsWindowVisible(g_wuiTooltip) ? 1 : 0;
	}
	cursorOk = GetCursorPos(&cursor) ? 1 : 0;
	wsprintfW(line,
		L"%lu %ls stage=%u pending=%d visible=%d hover=%d shown=%d cursor=%d,%d cursor_ok=%d host=%d,%d,%d,%d host_ok=%d tip=%d,%d,%d,%d tip_ok=%d tip_visible=%d\r\n",
		GetTickCount(),
		tag ? tag : L"(null)",
		g_wuiTipStage,
		g_wuiTipPending,
		g_wuiTipVisible,
		g_wuiHoverInside,
		g_wuiTipShownOnce,
		cursor.x,
		cursor.y,
		cursorOk,
		rcHost.left,
		rcHost.top,
		rcHost.right,
		rcHost.bottom,
		hostOk,
		rcTip.left,
		rcTip.top,
		rcTip.right,
		rcTip.bottom,
		tipOk,
		tipVisible);
	file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return;
	WriteFile(file, line, lstrlenW(line) * sizeof(WCHAR), &written, NULL);
	CloseHandle(file);
}

static void wui_tip_trace_reset(void)
{
	WCHAR path[MAX_PATH];

	if (!g_wuiInst) return;
	if (!GetModuleFileNameW(g_wuiInst, path, _countof(path))) return;
	for (int i = lstrlenW(path) - 1; i >= 0; --i) {
		if (path[i] == L'\\' || path[i] == L'/') {
			path[i + 1] = L'\0';
			break;
		}
	}
	lstrcatW(path, L"wui_tip_trace.log");
	DeleteFileW(path);
}
// TEMP_VERIFY_END

static Gdiplus::Color wui_argb(COLORREF color)
{
	return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void wui_draw_text(Gdiplus::Graphics& graphics, const RECT& rcClient)
{
	WCHAR textBuffer[4096];
	WCHAR* context = NULL;
	WCHAR* line = NULL;
	Gdiplus::FontFamily fontFamilyDefault(L"Segoe UI");
	Gdiplus::FontFamily* pFontFamily = &fontFamilyDefault;
	Gdiplus::FontFamily* pFontFamilyCustom = NULL;
	Gdiplus::Font* pFont = NULL;
	Gdiplus::SolidBrush brushText(Gdiplus::Color(255, 255, 255, 255));
	Gdiplus::SolidBrush brushShadow(Gdiplus::Color(255, 0, 0, 0));
	Gdiplus::StringFormat format;
	int lineCount = 0;
	int lineStep = 0;
	int totalHeight = 0;
	int y = 0;
	int fontPixelHeight = 0;
	INT fontStyle = Gdiplus::FontStyleRegular;

	if (!g_wuiState.text[0]) return;

	lstrcpynW(textBuffer, g_wuiState.text, _countof(textBuffer));
	fontPixelHeight = abs(g_wuiState.fontHeight);
	if (fontPixelHeight <= 0) {
		fontPixelHeight = (rcClient.bottom - rcClient.top) - 2;
	}
	if (g_wuiState.fontFace[0]) {
		pFontFamilyCustom = new Gdiplus::FontFamily(g_wuiState.fontFace);
		if (pFontFamilyCustom->IsAvailable()) {
			pFontFamily = pFontFamilyCustom;
		}
		else {
			delete pFontFamilyCustom;
			pFontFamilyCustom = NULL;
		}
	}
	if (g_wuiState.fontWeight >= FW_BOLD) fontStyle |= Gdiplus::FontStyleBold;
	if (g_wuiState.fontItalic) fontStyle |= Gdiplus::FontStyleItalic;
	pFont = new Gdiplus::Font(pFontFamily, (Gdiplus::REAL)fontPixelHeight, fontStyle, Gdiplus::UnitPixel);
	brushText.SetColor(wui_argb(g_wuiState.textColor));
	brushShadow.SetColor(wui_argb(g_wuiState.shadowColor));
	format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);
	graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
	graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
	graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

	line = wcstok_s(textBuffer, L"\r\n", &context);
	while (line) {
		++lineCount;
		line = wcstok_s(NULL, L"\r\n", &context);
	}
	if (lineCount <= 0) lineCount = 1;
	lineStep = fontPixelHeight + g_wuiState.lineHeight;
	if (lineStep <= 0) lineStep = fontPixelHeight;
	totalHeight = fontPixelHeight + ((lineCount - 1) * lineStep);
	y = ((rcClient.bottom - rcClient.top) - totalHeight) / 2;
	y += g_wuiState.vertPos;

	lstrcpynW(textBuffer, g_wuiState.text, _countof(textBuffer));
	line = wcstok_s(textBuffer, L"\r\n", &context);
	if (!line) line = textBuffer;
	while (line) {
		Gdiplus::RectF rcMeasure;
		Gdiplus::RectF rcLine;
		Gdiplus::REAL x = 0.0f;

		graphics.MeasureString(line, -1, pFont, Gdiplus::PointF(0.0f, 0.0f), &format, &rcMeasure);
		if (g_wuiState.textPos == 1) x = 0.0f;
		else if (g_wuiState.textPos == 2) x = (Gdiplus::REAL)((rcClient.right - rcClient.left) - rcMeasure.Width);
		else x = ((Gdiplus::REAL)(rcClient.right - rcClient.left) - rcMeasure.Width) / 2.0f;
		rcLine = Gdiplus::RectF(x, (Gdiplus::REAL)y, rcMeasure.Width + 4.0f, (Gdiplus::REAL)fontPixelHeight + 4.0f);
		if (g_wuiState.clockShadow && g_wuiState.shadowRange > 0) {
			Gdiplus::RectF rcShadow = rcLine;
			rcShadow.X += (Gdiplus::REAL)g_wuiState.shadowRange;
			rcShadow.Y += (Gdiplus::REAL)g_wuiState.shadowRange;
			graphics.DrawString(line, -1, pFont, rcShadow, &format, &brushShadow);
		}
		if (g_wuiState.clockBorder) {
			Gdiplus::RectF rcBorder = rcLine;
			rcBorder.X -= 1.0f;
			rcBorder.Y += 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
			rcBorder = rcLine;
			rcBorder.X += 1.0f;
			rcBorder.Y -= 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
			rcBorder = rcLine;
			rcBorder.X += 1.0f;
			rcBorder.Y += 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
			rcBorder = rcLine;
			rcBorder.Y -= 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
			rcBorder = rcLine;
			rcBorder.X += 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
			rcBorder = rcLine;
			rcBorder.X -= 1.0f;
			rcBorder.Y -= 1.0f;
			graphics.DrawString(line, -1, pFont, rcBorder, &format, &brushShadow);
		}
		graphics.DrawString(line, -1, pFont, rcLine, &format, &brushText);
		y += lineStep;
		line = wcstok_s(NULL, L"\r\n", &context);
	}

	delete pFont;
	delete pFontFamilyCustom;
}

static void wui_present(HWND hwnd)
{
	RECT rcWindow;
	POINT ptDst;
	POINT ptSrc;
	SIZE sizeWindow;
	BLENDFUNCTION blend;
	BITMAPINFO bmi;
	void* pBits = NULL;
	HDC hdcScreen = NULL;
	HDC hdcMem = NULL;
	HBITMAP hBitmap = NULL;
	HGDIOBJ hOldBitmap = NULL;
	Gdiplus::Graphics* pGraphics = NULL;
	BYTE* pixels = NULL;
	SIZE_T pixelCount;

	if (!hwnd || !IsWindow(hwnd)) return;
	if (!GetWindowRect(hwnd, &rcWindow)) return;
	sizeWindow.cx = rcWindow.right - rcWindow.left;
	sizeWindow.cy = rcWindow.bottom - rcWindow.top;
	if (sizeWindow.cx <= 0 || sizeWindow.cy <= 0) return;
	ptDst.x = rcWindow.left;
	ptDst.y = rcWindow.top;
	ptSrc.x = 0;
	ptSrc.y = 0;

	hdcScreen = GetDC(NULL);
	if (!hdcScreen) return;
	hdcMem = CreateCompatibleDC(hdcScreen);
	if (!hdcMem) goto cleanup;

	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = sizeWindow.cx;
	bmi.bmiHeader.biHeight = -sizeWindow.cy;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
	if (!hBitmap || !pBits) goto cleanup;
	hOldBitmap = SelectObject(hdcMem, hBitmap);
	ZeroMemory(pBits, (size_t)sizeWindow.cx * (size_t)sizeWindow.cy * 4u);
	pixels = (BYTE*)pBits;
	pixelCount = (SIZE_T)sizeWindow.cx * (SIZE_T)sizeWindow.cy;
	for (SIZE_T pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
		pixels[(pixelIndex * 4u) + 3u] = 1;
	}

	pGraphics = new Gdiplus::Graphics(hdcMem);
	wui_draw_text(*pGraphics, RECT{ g_wuiContentLeft, 0, g_wuiContentLeft + g_wuiContentWidth, sizeWindow.cy });

	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;
	UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWindow, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

cleanup:
	delete pGraphics;
	if (hOldBitmap) SelectObject(hdcMem, hOldBitmap);
	if (hBitmap) DeleteObject(hBitmap);
	if (hdcMem) DeleteDC(hdcMem);
	if (hdcScreen) ReleaseDC(NULL, hdcScreen);
}

static BOOL wui_is_fullscreen(void)
{
	HWND foreground;
	RECT rect;
	LONG_PTR style;
	WCHAR className[64];
	MONITORINFO monitor = { sizeof(monitor) };

	foreground = GetForegroundWindow();
	if (!foreground || foreground == g_wuiHost || foreground == g_wuiTarget) return FALSE;
	className[0] = L'\0';
	GetClassNameW(foreground, className, _countof(className));
	if (lstrcmpW(className, L"WorkerW") == 0
	 || lstrcmpW(className, L"Progman") == 0
	 || lstrcmpW(className, L"Shell_TrayWnd") == 0) return FALSE;
	style = GetWindowLongPtrW(foreground, GWL_STYLE);
	if (style & (WS_CAPTION | WS_THICKFRAME)) return FALSE;
	if (!GetWindowRect(foreground, &rect)) return FALSE;
	if (!GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &monitor)) return FALSE;
	return rect.left <= monitor.rcMonitor.left && rect.top <= monitor.rcMonitor.top
		&& rect.right >= monitor.rcMonitor.right && rect.bottom >= monitor.rcMonitor.bottom;
}


static void wui_place(HWND hwnd)
{
	RECT rcTarget;
	RECT rcPlace;
	int width;
	int height;

	if (!hwnd || !IsWindow(hwnd)) return;
	if (!g_wuiState.text[0]) return;
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return;
	if (!IsWindowVisible(g_wuiTarget)) {
		if (!g_wuiHasTarget) return;
		rcTarget = g_wuiLastTarget;
		width = rcTarget.right - rcTarget.left;
		height = rcTarget.bottom - rcTarget.top;
		if (width <= 0 || height <= 0) return;
		goto apply_place;
	}
	if (!GetWindowRect(g_wuiTarget, &rcTarget)) {
		if (!g_wuiHasTarget) return;
		rcTarget = g_wuiLastTarget;
		width = rcTarget.right - rcTarget.left;
		height = rcTarget.bottom - rcTarget.top;
		if (width <= 0 || height <= 0) return;
		goto apply_place;
	}
	width = rcTarget.right - rcTarget.left;
	height = rcTarget.bottom - rcTarget.top;
	if (width <= 0 || height <= 0) {
		if (!g_wuiHasTarget) return;
		rcTarget = g_wuiLastTarget;
		width = rcTarget.right - rcTarget.left;
		height = rcTarget.bottom - rcTarget.top;
		if (width <= 0 || height <= 0) return;
		goto apply_place;
	}
	g_wuiLastTarget = rcTarget;
	g_wuiHasTarget = TRUE;

apply_place:
	rcPlace = rcTarget;
	g_wuiContentLeft = 0;
	g_wuiContentWidth = rcTarget.right - rcTarget.left;
	{
		MONITORINFO monitor = { sizeof(monitor) };
		if (GetMonitorInfoW(MonitorFromRect(&rcTarget, MONITOR_DEFAULTTONEAREST), &monitor)) {
			if (rcTarget.left <= monitor.rcMonitor.left) {
				rcPlace.right += 2;
			}
			else if (rcTarget.right >= monitor.rcMonitor.right) {
				rcPlace.left -= 2;
				g_wuiContentLeft = 2;
			}
		}
	}
	width = rcPlace.right - rcPlace.left;
	height = rcPlace.bottom - rcPlace.top;
	if (g_wuiHasPlace
	 && EqualRect(&g_wuiLastPlace, &rcPlace)) {
		return;
	}
	g_wuiLastPlace = rcPlace;
	g_wuiHasPlace = TRUE;
	SetWindowPos(hwnd,
		HWND_TOPMOST,
		rcPlace.left,
		rcPlace.top,
		width,
		height,
		SWP_NOACTIVATE);
}

static void wui_show_front(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd)) return;
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

static void wui_forward_mouse(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	POINT point;

	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return;
	point.x = (int)(short)LOWORD(lParam);
	point.y = (int)(short)HIWORD(lParam);
	if (msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL) {
		ClientToScreen(hwnd, &point);
	}
	ScreenToClient(g_wuiTarget, &point);
	SendMessageW(g_wuiTarget, msg, wParam, MAKELPARAM(point.x, point.y));
}

static void wui_hide_tip(void)
{
	TOOLINFOW ti;

	if (g_wuiHost) KillTimer(g_wuiHost, WUI_TIP_TIMER_ID);
	if (g_wuiHost) KillTimer(g_wuiHost, WUI_TIP_FOLLOWUP_TIMER_ID);
	if (g_wuiHost) KillTimer(g_wuiHost, WUI_TIP_LEAVE_TIMER_ID);
	g_wuiTipPending = FALSE;
	g_wuiTipVisible = FALSE;
	g_wuiTipStage = 0;
	if (!g_wuiLegacyTip) g_wuiTipShownOnce = FALSE;
	wui_tip_trace(L"hide_tip");
	if (!g_wuiTooltip || !g_wuiTarget) return;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
	ShowWindow(g_wuiTooltip, SW_HIDE);
}

static void wui_activate_tip(void)
{
	TOOLINFOW ti;
	RECT rcTarget;
	RECT rcWork;
	MONITORINFO monitor = { sizeof(monitor) };
	int x;
	int y;

	if (!g_wuiTooltip || !g_wuiTarget || !g_wuiTooltipText[0]) return;
	if (!GetWindowRect(g_wuiTarget, &rcTarget)) return;
	if (!GetMonitorInfoW(MonitorFromWindow(g_wuiTarget, MONITOR_DEFAULTTONEAREST), &monitor)) return;
	rcWork = monitor.rcWork;
	x = rcTarget.right + 8;
	if (x > rcWork.right - 8) x = rcTarget.left - 8;
	if (x < rcWork.left) x = rcWork.left;
	y = rcTarget.bottom - 8;
	if (y > rcWork.bottom - 8) y = rcWork.bottom - 8;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	ti.lpszText = g_wuiTooltipText;
	SendMessageW(g_wuiTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
	wui_tip_trace(L"activate_tip");
}

static void wui_show_tip(void)
{
	if (!g_wuiTipPending) return;
	wui_activate_tip();
	g_wuiTipPending = FALSE;
	g_wuiTipVisible = TRUE;
	g_wuiTipShownOnce = TRUE;
	if (g_wuiTipAutoPopDelay) SetTimer(g_wuiHost, WUI_TIP_TIMER_ID, g_wuiTipAutoPopDelay, NULL);
}

// TEMP_VERIFY_START: committed 9b211ce hover activation sequence for comparison only.
static void wui_sync_hover(void)
{
	POINT point;
	RECT rect;
	BOOL inside;

	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return;
	if (!GetCursorPos(&point) || !GetWindowRect(g_wuiTarget, &rect)) return;
	inside = PtInRect(&rect, point);
	if (!inside && !g_wuiHoverInside) return;
	if (inside && g_wuiHoverInside
		&& point.x == g_wuiLastHover.x && point.y == g_wuiLastHover.y) return;
	if (inside) g_wuiLastHover = point;
	ScreenToClient(g_wuiTarget, &point);
	SendMessageW(g_wuiTarget, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
	g_wuiHoverInside = inside;
}
// TEMP_VERIFY_END

static void wui_track_leave(HWND hwnd)
{
	TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };

	if (g_wuiHoverInside) return;
	TrackMouseEvent(&tme);
	g_wuiHoverInside = TRUE;
}

static BOOL wui_post_tip_move(void)
{
	POINT point;
	RECT rcHost;

	if (!g_wuiHost || !IsWindow(g_wuiHost)) { wui_tip_trace(L"post_move_no_host"); return FALSE; }
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) { wui_tip_trace(L"post_move_no_target"); return FALSE; }
	if (!GetCursorPos(&point)) { wui_tip_trace(L"post_move_no_cursor"); return FALSE; }
	if (!GetWindowRect(g_wuiHost, &rcHost)) { wui_tip_trace(L"post_move_no_rect"); return FALSE; }
	if (!PtInRect(&rcHost, point)) { wui_tip_trace(L"post_move_outside"); return FALSE; }
	ScreenToClient(g_wuiTarget, &point);
	SendMessageW(g_wuiTarget, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
	wui_tip_trace(L"post_move_sent");
	return TRUE;
}

static LRESULT CALLBACK wui_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_MOUSEMOVE:
		if (!g_wuiLegacyTip) {
			KillTimer(hwnd, WUI_TIP_LEAVE_TIMER_ID);
			wui_track_leave(hwnd);
			wui_tip_trace(L"host_mousemove");
			if (g_wuiTipVisible) return 0;
		}
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		wui_forward_mouse(hwnd, msg, wParam, lParam);
		return 0;
	case WM_MOUSELEAVE:
		if (g_wuiLegacyTip) return 0;
		g_wuiHoverInside = FALSE;
		wui_tip_trace(L"host_mouseleave");
		if (g_wuiTipVisible) SetTimer(hwnd, WUI_TIP_LEAVE_TIMER_ID, WUI_TIP_LEAVE_MS, NULL);
		else wui_hide_tip();
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
		SetCapture(hwnd);
		wui_forward_mouse(hwnd, msg, wParam, lParam);
		return 0;
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
		wui_forward_mouse(hwnd, msg, wParam, lParam);
		if (GetCapture() == hwnd) ReleaseCapture();
		return 0;
	case WM_TIMER:
		if (wParam == WUI_TIP_LEAVE_TIMER_ID) {
			KillTimer(hwnd, WUI_TIP_LEAVE_TIMER_ID);
			wui_tip_trace(L"leave_timer");
			if (!g_wuiHoverInside) wui_hide_tip();
			return 0;
		}
		if (wParam == WUI_TIP_FOLLOWUP_TIMER_ID) {
			KillTimer(hwnd, WUI_TIP_FOLLOWUP_TIMER_ID);
			wui_tip_trace(L"followup_timer");
			if (g_wuiTipStage == 2 && g_wuiTipVisible && g_wuiHoverInside) {
				if (!wui_post_tip_move()) wui_hide_tip();
			}
			else wui_hide_tip();
			return 0;
		}
		if (wParam == WUI_TIP_TIMER_ID) {
			KillTimer(hwnd, WUI_TIP_TIMER_ID);
			wui_tip_trace(L"tip_timer");
			if (g_wuiTipPending && g_wuiHoverInside) {
				g_wuiTipStage = 1;
				if (!wui_post_tip_move()) wui_hide_tip();
			}
			else if (g_wuiLegacyTip) wui_hide_tip();
			return 0;
		}
		if (wParam == WUI_TIMER_ID) {
			if (wui_is_fullscreen()) {
				ShowWindow(hwnd, SW_HIDE);
				wui_hide_tip();
			}
			else {
				wui_place(hwnd);
				if (!IsWindowVisible(hwnd)) wui_show_front(hwnd);
				wui_present(hwnd);
				if (g_wuiLegacyTip) wui_sync_hover();
			}
			return 0;
		}
		break;
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			if (!BeginPaint(hwnd, &ps)) return 0;
			EndPaint(hwnd, &ps);
			wui_present(hwnd);
			return 0;
		}
	case WM_DESTROY:
		KillTimer(hwnd, WUI_TIMER_ID);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);

	if (fdwReason == DLL_PROCESS_ATTACH) {
		g_wuiInst = hinstDLL;
	}
	return TRUE;
}

extern "C" BOOL WINAPI WuiCreateHost(HWND hwndTargetClock)
{
	Gdiplus::GdiplusStartupInput gdip;
	WNDCLASSEXW wcx;

	g_wuiTarget = hwndTargetClock;
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return FALSE;
	if (g_wuiHost && IsWindow(g_wuiHost)) return TRUE;
	ZeroMemory(&g_wuiState, sizeof(g_wuiState));
	g_wuiState.cb = sizeof(g_wuiState);
	ZeroMemory(&gdip, sizeof(gdip));
	gdip.GdiplusVersion = 1;
	if (!g_wuiGdip && Gdiplus::GdiplusStartup(&g_wuiGdip, &gdip, NULL) != Gdiplus::Ok) {
		return FALSE;
	}
	ZeroMemory(&wcx, sizeof(wcx));
	wcx.cbSize = sizeof(wcx);
	wcx.lpfnWndProc = wui_proc;
	wcx.hInstance = g_wuiInst;
	wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wcx.lpszClassName = L"TClockWinUIDllWindow";
	RegisterClassExW(&wcx);
	g_wuiHost = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
		wcx.lpszClassName, L"TClockWinUIDllHost", WS_POPUP, 0, 0, 1, 1, g_wuiTarget, NULL, g_wuiInst, NULL);
	wui_tip_trace_reset();
	if (!g_wuiHost) return FALSE;
	ShowWindow(g_wuiHost, SW_HIDE);
	wui_place(g_wuiHost);
	wui_present(g_wuiHost);
	SetTimer(g_wuiHost, WUI_TIMER_ID, WUI_TIMER_MS, NULL);
	return TRUE;
}

extern "C" void WINAPI WuiDestroyHost(void)
{
	wui_hide_tip();
	if (g_wuiTooltip && IsWindow(g_wuiTooltip)) {
		DestroyWindow(g_wuiTooltip);
	}
	g_wuiTooltip = NULL;
	if (g_wuiHost && IsWindow(g_wuiHost)) {
		KillTimer(g_wuiHost, WUI_TIMER_ID);
		DestroyWindow(g_wuiHost);
	}
	g_wuiHost = NULL;
	g_wuiTarget = NULL;
	g_wuiHasTarget = FALSE;
	g_wuiHasPlace = FALSE;
	ZeroMemory(&g_wuiLastTarget, sizeof(g_wuiLastTarget));
	ZeroMemory(&g_wuiLastPlace, sizeof(g_wuiLastPlace));
	ZeroMemory(&g_wuiState, sizeof(g_wuiState));
	if (g_wuiInst) {
		UnregisterClassW(L"TClockWinUIDllWindow", g_wuiInst);
	}
	if (g_wuiGdip) {
		Gdiplus::GdiplusShutdown(g_wuiGdip);
		g_wuiGdip = 0;
	}
}

extern "C" BOOL WINAPI WuiUpdateState(const TC_DISPLAY_BACKEND_RENDER_STATE* state)
{
	SIZE_T cb;

	if (!state) return FALSE;
	cb = state->cb;
	if (cb > sizeof(g_wuiState)) cb = sizeof(g_wuiState);
	ZeroMemory(&g_wuiState, sizeof(g_wuiState));
	CopyMemory(&g_wuiState, state, cb);
	g_wuiState.cb = sizeof(g_wuiState);
	if (g_wuiHost && IsWindow(g_wuiHost)) {
		wui_present(g_wuiHost);
	}
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefresh(void)
{
	if (!g_wuiHost || !IsWindow(g_wuiHost)) return FALSE;
	if (wui_is_fullscreen()) {
		ShowWindow(g_wuiHost, SW_HIDE);
		wui_hide_tip();
		return TRUE;
	}
	if (!IsWindowVisible(g_wuiHost)) wui_show_front(g_wuiHost);
	wui_place(g_wuiHost);
	wui_present(g_wuiHost);
	return TRUE;
}

extern "C" BOOL WINAPI WuiSetTooltip(const WCHAR* text, BOOL visible, HFONT font, COLORREF backColor,
	UINT initialDelay, UINT reshowDelay, UINT autoPopDelay, BOOL legacyMode)
{
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
	TOOLINFOW ti;

	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return FALSE;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	if (!visible) {
		wui_hide_tip();
		return TRUE;
	}
	if (!text || !text[0]) return FALSE;
	if (g_wuiLegacyTip != legacyMode) {
		wui_hide_tip();
		g_wuiHoverInside = FALSE;
		g_wuiLegacyTip = legacyMode;
	}
	BOOL textChanged = lstrcmpW(g_wuiTooltipText, text) != 0;
	if (textChanged) lstrcpynW(g_wuiTooltipText, text, _countof(g_wuiTooltipText));
	if (!g_wuiTooltip) {
		InitCommonControlsEx(&icc);
		g_wuiTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
			0, 0, 0, 0, g_wuiTarget, NULL, g_wuiInst, NULL);
		if (!g_wuiTooltip) return FALSE;
		ti.lpszText = g_wuiTooltipText;
		SendMessageW(g_wuiTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	}
	if (font) SendMessageW(g_wuiTooltip, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(g_wuiTooltip, TTM_SETTIPBKCOLOR, backColor, 0);
	SendMessageW(g_wuiTooltip, TTM_SETTIPTEXTCOLOR, backColor, 0);
	SendMessageW(g_wuiTooltip, TTM_SETMAXTIPWIDTH, 0, 420);
	ti.lpszText = g_wuiTooltipText;
	if (textChanged) SendMessageW(g_wuiTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	g_wuiTipAutoPopDelay = autoPopDelay;
	if (g_wuiLegacyTip) {
		wui_activate_tip();
		g_wuiTipVisible = TRUE;
		g_wuiTipShownOnce = TRUE;
		return TRUE;
	}
	wui_tip_trace(L"set_tooltip_visible");
	if (g_wuiTipStage == 1 && g_wuiTipPending) {
		wui_tip_trace(L"stage1_show");
		wui_show_tip();
		g_wuiTipStage = 2;
		SetTimer(g_wuiHost, WUI_TIP_FOLLOWUP_TIMER_ID, WUI_TIMER_MS, NULL);
		return TRUE;
	}
	if (g_wuiTipStage == 2 && g_wuiTipVisible) {
		wui_tip_trace(L"stage2_refresh");
		KillTimer(g_wuiHost, WUI_TIP_FOLLOWUP_TIMER_ID);
		g_wuiTipStage = 0;
		wui_activate_tip();
		return TRUE;
	}
	if (!g_wuiTipPending && !g_wuiTipVisible) {
		UINT delay = g_wuiTipShownOnce ? reshowDelay : initialDelay;
		g_wuiTipPending = TRUE;
		wui_tip_trace(delay ? L"arm_delay" : L"show_now");
		if (delay) SetTimer(g_wuiHost, WUI_TIP_TIMER_ID, delay, NULL);
		else wui_show_tip();
	}
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefreshTooltipText(const WCHAR* text)
{
	if (!g_wuiTooltip || !g_wuiTipVisible) return FALSE;
	if (!text || !text[0]) return FALSE;
	if (lstrcmpW(g_wuiTooltipText, text) == 0) return TRUE;
	lstrcpynW(g_wuiTooltipText, text, _countof(g_wuiTooltipText));
	SendMessageW(g_wuiTooltip, TTM_UPDATE, 0, 0);
	return TRUE;
}

extern "C" BOOL WINAPI WuiIsTooltip(HWND hwnd)
{
	return hwnd && hwnd == g_wuiTooltip;
}
