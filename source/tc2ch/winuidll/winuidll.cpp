#include <windows.h>
#include <gdiplus.h>
#include "wui_api.h"

#pragma comment(lib, "gdiplus.lib")

#define WUI_TIMER_ID 1
#define WUI_TIMER_MS 100

static HINSTANCE g_wuiInst = NULL;
static HWND g_wuiTarget = NULL;
static HWND g_wuiHost = NULL;
static TC_DISPLAY_BACKEND_RENDER_STATE g_wuiState;
static ULONG_PTR g_wuiGdip = 0;
static RECT g_wuiLastTarget = { 0, 0, 0, 0 };
static RECT g_wuiLastPlace = { 0, 0, 0, 0 };
static BOOL g_wuiHasTarget = FALSE;
static BOOL g_wuiHasPlace = FALSE;
static BOOL g_wuiOverlayOn = FALSE;

static BOOL wui_has_class(const RECT* prcTarget, LPCWSTR className)
{
	HWND hwnd = NULL;
	RECT rcWindow;
	RECT rcIntersect;

	if (!prcTarget || !className || !className[0]) return FALSE;
	while ((hwnd = FindWindowExW(NULL, hwnd, className, NULL)) != NULL) {
		if (!IsWindowVisible(hwnd)) continue;
		if (!GetWindowRect(hwnd, &rcWindow)) continue;
		if (IntersectRect(&rcIntersect, prcTarget, &rcWindow)) return TRUE;
	}
	return FALSE;
}

static BOOL wui_has_overlay(const RECT* prcTarget)
{
	if (wui_has_class(prcTarget, L"#32768")) return TRUE;
	if (wui_has_class(prcTarget, L"tooltips_class32")) return TRUE;
	return FALSE;
}

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

	pGraphics = new Gdiplus::Graphics(hdcMem);
	pGraphics->Clear(Gdiplus::Color(0, 0, 0, 0));
	wui_draw_text(*pGraphics, RECT{ 0, 0, sizeWindow.cx, sizeWindow.cy });

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

static void wui_place(HWND hwnd)
{
	RECT rcTarget;
	RECT rcPlace;
	BOOL bOverlay;
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
	bOverlay = wui_has_overlay(&rcTarget);
	if (g_wuiHasPlace
	 && EqualRect(&g_wuiLastPlace, &rcPlace)
	 && g_wuiOverlayOn == bOverlay) {
		return;
	}
	g_wuiLastPlace = rcPlace;
	g_wuiHasPlace = TRUE;
	g_wuiOverlayOn = bOverlay;
	SetWindowPos(hwnd,
		bOverlay ? HWND_NOTOPMOST : HWND_TOP,
		rcPlace.left,
		rcPlace.top,
		width,
		height,
		SWP_NOACTIVATE);
}

static LRESULT CALLBACK wui_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_TIMER:
		if (wParam == WUI_TIMER_ID) {
			wui_place(hwnd);
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
	g_wuiHost = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
		wcx.lpszClassName, L"TClockWinUIDllHost", WS_POPUP, 0, 0, 1, 1, g_wuiTarget, NULL, g_wuiInst, NULL);
	if (!g_wuiHost) return FALSE;
	ShowWindow(g_wuiHost, SW_HIDE);
	wui_place(g_wuiHost);
	wui_present(g_wuiHost);
	SetTimer(g_wuiHost, WUI_TIMER_ID, WUI_TIMER_MS, NULL);
	return TRUE;
}

extern "C" void WINAPI WuiDestroyHost(void)
{
	if (g_wuiHost && IsWindow(g_wuiHost)) {
		KillTimer(g_wuiHost, WUI_TIMER_ID);
		DestroyWindow(g_wuiHost);
	}
	g_wuiHost = NULL;
	g_wuiTarget = NULL;
	g_wuiHasTarget = FALSE;
	g_wuiHasPlace = FALSE;
	g_wuiOverlayOn = FALSE;
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
	if (!IsWindowVisible(g_wuiHost)) {
		ShowWindow(g_wuiHost, SW_SHOWNOACTIVATE);
	}
	wui_place(g_wuiHost);
	wui_present(g_wuiHost);
	return TRUE;
}
