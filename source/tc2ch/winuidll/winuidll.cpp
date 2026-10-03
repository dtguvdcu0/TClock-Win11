#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <commctrl.h>
#include <gdiplus.h>
#include "wui_api.h"
#include "wui_text.h"
#include "../common/taskbar_edge.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

#define WUI_TIMER_ID 1
#define WUI_TIMER_MS 100
#define WUI_TIP_TIMER_ID 2
#define WUI_TIP_LEAVE_TIMER_ID 4
#define WUI_TIP_LEAVE_MS 320

static HINSTANCE g_wuiInst = NULL;
static HWND g_wuiTarget = NULL;
static HWND g_wuiHost = NULL;
static TBE_REGION g_wuiEdge = {0};
static HWND g_wuiTooltip = NULL;
static WCHAR g_wuiTooltipText[4096];
static TC_DISPLAY_BACKEND_RENDER_STATE g_wuiState;
static BYTE* g_wuiLayerPixels = NULL;
static SIZE_T g_wuiLayerCapacity = 0;
static LONG g_wuiLayerWidth = 0;
static LONG g_wuiLayerHeight = 0;
static ULONG_PTR g_wuiGdip = 0;
static RECT g_wuiLastTarget = { 0, 0, 0, 0 };
static RECT g_wuiLastPlace = { 0, 0, 0, 0 };
static int g_wuiContentLeft = 0;
static int g_wuiContentWidth = 0;
static BOOL g_wuiHasTarget = FALSE;
static BOOL g_wuiHasPlace = FALSE;
static BOOL g_wuiHoverInside = FALSE;
static BOOL g_wuiTipSuppressed = FALSE;
static BOOL g_wuiTipPending = FALSE;
static BOOL g_wuiTipVisible = FALSE;
static BOOL g_wuiTipShownOnce = FALSE;
static UINT g_wuiTipAutoPopDelay = 0;
static HFONT g_wuiTipFont = NULL;
static COLORREF g_wuiTipBackColor = RGB(255, 255, 225);
static WCHAR g_wuiTipTitle[300];
static LOGFONTW g_wuiTipFontInfo = {};
static LOGFONTW g_wuiTipTitleInfo = {};
static HFONT g_wuiTipTitleFont = NULL;
static COLORREF g_wuiTipTextColor = 0;
static COLORREF g_wuiTipTitleColor = 0;
static UINT g_wuiTipDpi = 0;
static BOOL g_wuiTipHasStyle = FALSE;

typedef struct {
	WCHAR face[LF_FACESIZE];
	int pixelHeight;
	INT fontStyle;
	Gdiplus::FontFamily* family;
	Gdiplus::Font* font;
	DWORD lastUse;
	DWORD frameUse;
} WUI_FONT_ENTRY;

static WUI_FONT_ENTRY g_wuiFonts[TC_WUI_MAX_STYLES];
static DWORD g_wuiFontClock = 0;
static DWORD g_wuiFontFrame = 0;

static Gdiplus::Color wui_argb(COLORREF color)
{
	return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void wui_clear_fonts(void)
{
	int i;
	for (i = 0; i < (int)_countof(g_wuiFonts); i++) {
		delete g_wuiFonts[i].font;
		delete g_wuiFonts[i].family;
		ZeroMemory(&g_wuiFonts[i], sizeof(g_wuiFonts[i]));
	}
	g_wuiFontClock = 0;
	g_wuiFontFrame = 0;
}

static void wui_begin_font_frame(void)
{
	if (++g_wuiFontFrame == 0) {
		int i;
		g_wuiFontFrame = 1;
		for (i = 0; i < (int)_countof(g_wuiFonts); i++) g_wuiFonts[i].frameUse = 0;
	}
}

static Gdiplus::Font* wui_get_font(const WCHAR* face, int pixelHeight, INT fontStyle)
{
	const WCHAR* resolvedFace = (face && face[0]) ? face : L"Segoe UI";
	int i;
	int slot = -1;
	int emptySlot = -1;
	DWORD oldest = MAXDWORD;
	for (i = 0; i < (int)_countof(g_wuiFonts); i++) {
		WUI_FONT_ENTRY* entry = &g_wuiFonts[i];
		if (entry->font && entry->pixelHeight == pixelHeight && entry->fontStyle == fontStyle &&
			_wcsicmp(entry->face, resolvedFace) == 0) {
			entry->lastUse = ++g_wuiFontClock;
			entry->frameUse = g_wuiFontFrame;
			return entry->font;
		}
		if (!entry->font) {
			if (emptySlot < 0) emptySlot = i;
		}
	else if (entry->frameUse != g_wuiFontFrame && entry->lastUse < oldest) {
			oldest = entry->lastUse;
			slot = i;
		}
	}
	if (emptySlot >= 0) slot = emptySlot;
	if (slot < 0) return NULL;
	{
		WUI_FONT_ENTRY* entry = &g_wuiFonts[slot];
		delete entry->font;
		delete entry->family;
		ZeroMemory(entry, sizeof(*entry));
		lstrcpynW(entry->face, resolvedFace, _countof(entry->face));
		entry->pixelHeight = pixelHeight;
		entry->fontStyle = fontStyle;
		entry->family = new Gdiplus::FontFamily(resolvedFace);
		if (!entry->family || !entry->family->IsAvailable()) {
			delete entry->family;
			entry->family = new Gdiplus::FontFamily(L"Segoe UI");
		}
		if (!entry->family || !entry->family->IsAvailable()) goto fail;
		entry->font = new Gdiplus::Font(entry->family, (Gdiplus::REAL)pixelHeight, fontStyle, Gdiplus::UnitPixel);
		if (!entry->font || entry->font->GetLastStatus() != Gdiplus::Ok) goto fail;
		entry->lastUse = ++g_wuiFontClock;
		entry->frameUse = g_wuiFontFrame;
		return entry->font;

fail:
		delete entry->font;
		delete entry->family;
		ZeroMemory(entry, sizeof(*entry));
	}
	return NULL;
}

static int wui_tip_width(const WCHAR* text, HFONT font)
{
	UNREFERENCED_PARAMETER(text);
	UNREFERENCED_PARAMETER(font);
	int limitWidth;

	limitWidth = GetSystemMetrics(SM_CXSCREEN) - 64;
	if (limitWidth < 120) limitWidth = 120;
	return limitWidth;
}

static BOOL wui_draw_styled(Gdiplus::Graphics& graphics, const RECT& rcClient)
{
	Gdiplus::Font* fonts[TC_WUI_MAX_STYLES] = {};
	Gdiplus::StringFormat format;
	DWORD i;
	BOOL ok = FALSE;
	if (g_wuiState.styleVersion != TC_WUI_STYLE_VERSION || !g_wuiState.styleCount ||
		!g_wuiState.runCount || g_wuiState.styleCount > TC_WUI_MAX_STYLES ||
		g_wuiState.runCount > TC_WUI_MAX_RUNS) return FALSE;
	format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
	graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
	graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
	graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	wui_begin_font_frame();
	for (i = 0; i < g_wuiState.styleCount; i++) {
		const TC_DISPLAY_BACKEND_STYLE& style = g_wuiState.styles[i];
		INT fontStyle = Gdiplus::FontStyleRegular;
		int pixelHeight = abs(style.fontHeight);
		if (style.fontWeight >= FW_BOLD) fontStyle |= Gdiplus::FontStyleBold;
		if (style.fontItalic) fontStyle |= Gdiplus::FontStyleItalic;
		if (pixelHeight <= 0) pixelHeight = 12;
		fonts[i] = wui_get_font(style.fontFace, pixelHeight, fontStyle);
		if (!fonts[i]) goto cleanup;
	}
	for (i = 0; i < g_wuiState.runCount; i++) {
		const TC_DISPLAY_BACKEND_RUN& run = g_wuiState.runs[i];
		const TC_DISPLAY_BACKEND_STYLE* style;
		Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
		Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(255, 0, 0, 0));
		Gdiplus::RectF rect;
		if (run.styleIndex >= g_wuiState.styleCount || run.textStart < 0 || run.textLength <= 0 ||
			run.textStart + run.textLength >= (LONG)_countof(g_wuiState.text)) goto cleanup;
		style = &g_wuiState.styles[run.styleIndex];
		textBrush.SetColor(wui_argb(style->textColor));
		shadowBrush.SetColor(wui_argb(style->shadowColor));
		rect = Gdiplus::RectF((Gdiplus::REAL)(rcClient.left + run.x), (Gdiplus::REAL)run.y,
			(Gdiplus::REAL)(rcClient.right - rcClient.left + 32), (Gdiplus::REAL)(abs(style->fontHeight) + 32));
		if (style->clockShadow) {
			Gdiplus::RectF shadowRect = rect;
			shadowRect.X += (Gdiplus::REAL)style->shadowRange;
			shadowRect.Y += (Gdiplus::REAL)style->shadowRange;
			graphics.DrawString(g_wuiState.text + run.textStart, run.textLength, fonts[run.styleIndex], shadowRect, &format, &shadowBrush);
		}
		if (style->clockBorder) {
			static const int offsets[][2] = { {-1, 1}, {1, -1}, {1, 1}, {0, -1}, {1, 0}, {-1, -1} };
			int offsetIndex;
			for (offsetIndex = 0; offsetIndex < (int)_countof(offsets); offsetIndex++) {
				Gdiplus::RectF borderRect = rect;
				borderRect.X += (Gdiplus::REAL)offsets[offsetIndex][0];
				borderRect.Y += (Gdiplus::REAL)offsets[offsetIndex][1];
				graphics.DrawString(g_wuiState.text + run.textStart, run.textLength, fonts[run.styleIndex], borderRect, &format, &shadowBrush);
			}
		}
		graphics.DrawString(g_wuiState.text + run.textStart, run.textLength, fonts[run.styleIndex], rect, &format, &textBrush);
	}
	ok = TRUE;

cleanup:
	return ok;
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
	if (wui_draw_styled(graphics, rcClient)) return;

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
	if (g_wuiState.contentVersion == 1) y += rcClient.top;

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
		if (g_wuiState.contentVersion == 1) x += rcClient.left;
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

static void wui_clear_layer(void)
{
	if (g_wuiLayerPixels) HeapFree(GetProcessHeap(), 0, g_wuiLayerPixels);
	g_wuiLayerPixels = NULL;
	g_wuiLayerCapacity = 0;
	g_wuiLayerWidth = g_wuiLayerHeight = 0;
}

static void wui_copy_layer(const TC_DISPLAY_BACKEND_RENDER_STATE& state)
{
	LONG height;
	SIZE_T stride, bytes;
	if (!state.layerPixels || state.layerWidth <= 0 || !state.layerHeight ||
		state.layerHeight == LONG_MIN) {
		wui_clear_layer();
		return;
	}
	height = state.layerHeight < 0 ? -state.layerHeight : state.layerHeight;
	stride = (SIZE_T)state.layerWidth * 4u;
	if (stride / 4u != (SIZE_T)state.layerWidth || (SIZE_T)height > SIZE_MAX / stride) {
		wui_clear_layer();
		return;
	}
	bytes = stride * (SIZE_T)height;
	if (bytes > g_wuiLayerCapacity) {
		BYTE* pixels = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
		if (!pixels) {
			wui_clear_layer();
			return;
		}
		wui_clear_layer();
		g_wuiLayerPixels = pixels;
		g_wuiLayerCapacity = bytes;
	}
	for (LONG y = 0; y < height; ++y) {
		LONG sourceY = state.layerHeight > 0 ? height - 1 - y : y;
		CopyMemory(g_wuiLayerPixels + (SIZE_T)y * stride,
			state.layerPixels + (SIZE_T)sourceY * stride, stride);
	}
	g_wuiLayerWidth = state.layerWidth;
	g_wuiLayerHeight = height;
}

static void wui_blend_layer(BYTE* pixels, LONG width, LONG height, LONG left)
{
	if (!g_wuiLayerPixels) return;
	for (LONG y = 0; y < height && y < g_wuiLayerHeight; ++y) {
		for (LONG x = 0; x < g_wuiLayerWidth; ++x) {
			LONG targetX = x + left;
			if (targetX < 0 || targetX >= width) continue;
			BYTE* foreground = pixels + ((SIZE_T)y * width + targetX) * 4u;
			const BYTE* background = g_wuiLayerPixels + ((SIZE_T)y * g_wuiLayerWidth + x) * 4u;
			// GDI+ on an HDC may leave RGB coverage without matching alpha.
			UINT alpha = max(max(foreground[3], foreground[0]), max(foreground[1], foreground[2]));
			UINT inverse = 255u - alpha;
			for (int channel = 0; channel < 3; ++channel) {
				foreground[channel] = (BYTE)(foreground[channel] + (background[channel] * inverse + 127u) / 255u);
			}
			foreground[3] = (BYTE)(alpha + (background[3] * inverse + 127u) / 255u);
		}
	}
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
	HWND edgeTaskbar = NULL;

	if (!hwnd || !IsWindow(hwnd)) return;
	{
		WCHAR targetClass[80];
		if (GetClassNameW(g_wuiTarget, targetClass, _countof(targetClass)) && !lstrcmpW(targetClass, L"TClockMain"))
		{
			edgeTaskbar = tbe_find_taskbar(g_wuiTarget);
			tbe_update_region(hwnd, edgeTaskbar, &g_wuiEdge);
		}
	}
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

    {
        RECT textArea = {g_wuiContentLeft, 0, g_wuiContentLeft + g_wuiContentWidth, sizeWindow.cy};
        if (g_wuiState.contentVersion == 1 && !g_wuiState.runCount) {
            textArea = g_wuiState.contentRect;
            OffsetRect(&textArea, g_wuiContentLeft, 0);
        }
        if (!g_wuiState.textRenderer || !wui_render_text(g_wuiState, textArea,
            sizeWindow.cx, sizeWindow.cy, pixels)) {
            pGraphics = new Gdiplus::Graphics(hdcMem);
            wui_draw_text(*pGraphics, textArea);
        }
    }

	if (pGraphics) {
		pGraphics->Flush(Gdiplus::FlushIntentionSync);
		delete pGraphics;
		pGraphics = NULL;
	}
	GdiFlush();
	wui_blend_layer(pixels, sizeWindow.cx, sizeWindow.cy, g_wuiContentLeft);
	{
		RECT cut;
		COLORREF edgeColor = tbe_sample_strip(hdcScreen, hwnd, edgeTaskbar, &cut);
		if (edgeColor != CLR_INVALID) {
			for (LONG y = cut.top; y < cut.bottom; ++y) {
				for (LONG x = cut.left; x < cut.right; ++x) {
					BYTE* pixel = pixels + ((SIZE_T)y * sizeWindow.cx + x) * 4u;
					pixel[0] = GetBValue(edgeColor); pixel[1] = GetGValue(edgeColor);
					pixel[2] = GetRValue(edgeColor); pixel[3] = 255;
				}
			}
		}
	}

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

static void wui_hide_tip(void);

static BOOL wui_window_above(HWND first, HWND second)
{
	HWND current = first;
	int count;

	for (count = 0; count < 1024 && current; count++) {
		if (current == second) return TRUE;
		current = GetWindow(current, GW_HWNDNEXT);
	}
	return FALSE;
}

static BOOL wui_covering_foreground(HWND hwnd, HWND owner)
{
	HWND foreground;
	RECT hostRect;
	RECT foregroundRect;
	POINT hostCenter;
	HMONITOR hostMonitor;
	HMONITOR foregroundMonitor;
	MONITORINFO foregroundInfo = { sizeof(foregroundInfo) };
	WCHAR foregroundClass[64];
	BOOL remoteDesktop;
	LONG_PTR ownerExStyle;

	foreground = GetForegroundWindow();
	if (!foreground || foreground == hwnd || foreground == g_wuiTarget || foreground == owner) return FALSE;
	if (!IsWindowVisible(foreground)) return FALSE;
	foregroundClass[0] = L'\0';
	if (GetClassNameW(foreground, foregroundClass, _countof(foregroundClass)) <= 0) return FALSE;
	if (lstrcmpW(foregroundClass, L"Progman") == 0 || lstrcmpW(foregroundClass, L"WorkerW") == 0
	 || lstrcmpW(foregroundClass, L"Shell_TrayWnd") == 0) return FALSE;
	remoteDesktop = lstrcmpW(foregroundClass, L"TscShellContainerClass") == 0;
	if (!GetWindowRect(hwnd, &hostRect) || !GetWindowRect(foreground, &foregroundRect)) return FALSE;
	hostCenter.x = (hostRect.left + hostRect.right) / 2;
	hostCenter.y = (hostRect.top + hostRect.bottom) / 2;
	if (hostCenter.x < foregroundRect.left || hostCenter.x >= foregroundRect.right
	 || hostCenter.y < foregroundRect.top || hostCenter.y >= foregroundRect.bottom) return FALSE;
	hostMonitor = MonitorFromWindow(g_wuiTarget, MONITOR_DEFAULTTONEAREST);
	foregroundMonitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
	if (!hostMonitor || hostMonitor != foregroundMonitor) return FALSE;
	if (!GetMonitorInfoW(foregroundMonitor, &foregroundInfo)) return FALSE;
	if (foregroundRect.left > foregroundInfo.rcMonitor.left || foregroundRect.top > foregroundInfo.rcMonitor.top
	 || foregroundRect.right < foregroundInfo.rcMonitor.right || foregroundRect.bottom < foregroundInfo.rcMonitor.bottom) return FALSE;
	// Remote Desktop uses a full-monitor shell container while the local taskbar remains visible.
	if (remoteDesktop) return TRUE;
	if (wui_window_above(foreground, hwnd)) return FALSE;
	ownerExStyle = GetWindowLongPtrW(owner, GWL_EXSTYLE);
	// Remote desktop fullscreen can leave the taskbar topmost while covering the clock.
	// Hide only for actual same-monitor coverage; ordinary fullscreen relies on stacking.
	return (ownerExStyle & WS_EX_TOPMOST) != 0;
}

static BOOL wui_sync_order(HWND hwnd)
{
	HWND owner = GetAncestor(g_wuiTarget, GA_ROOT);
	HWND previous;
	if (!owner || !IsWindowVisible(owner) || !IsWindowVisible(g_wuiTarget)) {
		if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
		return FALSE;
	}
	if (wui_covering_foreground(hwnd, owner)) {
		if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
		wui_hide_tip();
		return FALSE;
	}
	previous = GetWindow(owner, GW_HWNDPREV);
	// Keep the host directly above the taskbar without promoting its owner.
	if (previous == hwnd && IsWindowVisible(hwnd)) return TRUE;
	if (previous == hwnd) previous = GetWindow(hwnd, GW_HWNDPREV);
	SetWindowPos(hwnd, previous ? previous : HWND_TOP, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
	return TRUE;
}

static void wui_place(HWND hwnd)
{
	RECT rcTarget;
	RECT rcPlace;
	int width;
	int height;

	if (!hwnd || !IsWindow(hwnd)) return;
	if (!g_wuiState.text[0] && !g_wuiLayerPixels) return;
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
		NULL,
		rcPlace.left,
		rcPlace.top,
		width,
		height,
		SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
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
	if (g_wuiHost) KillTimer(g_wuiHost, WUI_TIP_LEAVE_TIMER_ID);
	g_wuiTipPending = FALSE;
	g_wuiTipVisible = FALSE;
	g_wuiTipShownOnce = FALSE;
	if (!g_wuiTooltip || !g_wuiTarget) return;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
	ShowWindow(g_wuiTooltip, SW_HIDE);
}

static BOOL wui_make_tip(void)
{
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
	TOOLINFOW ti;

	if (g_wuiTooltip && IsWindow(g_wuiTooltip)) return TRUE;
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return FALSE;
	InitCommonControlsEx(&icc);
	g_wuiTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, g_wuiTarget, NULL, g_wuiInst, NULL);
	if (!g_wuiTooltip) return FALSE;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	SendMessageW(g_wuiTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	return TRUE;
}

static void wui_fit_tip(void)
{
	TOOLINFOW ti;
	RECT rcTip;
	DWORD bubble;
	int width;
	int height;

	if (!g_wuiTooltip || !IsWindow(g_wuiTooltip) || !g_wuiTarget || !g_wuiTooltipText[0]) return;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	bubble = (DWORD)SendMessageW(g_wuiTooltip, TTM_GETBUBBLESIZE, 0, (LPARAM)&ti);
	width = (int)(short)LOWORD(bubble);
	height = (int)(short)HIWORD(bubble);
	if (width <= 0 || height <= 0) return;
	height += 4;
	// The title is custom drawn, so the control's body measurement excludes its width.
	if (g_wuiTipTitle[0]) {
		HDC dc = GetDC(g_wuiTooltip);
		if (dc) {
			HGDIOBJ oldFont = g_wuiTipTitleFont ? SelectObject(dc, g_wuiTipTitleFont) : NULL;
			RECT title = {};
			DrawTextW(dc, g_wuiTipTitle, -1, &title, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
			if (SendMessageW(g_wuiTooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&title))
				width = max(width, title.right - title.left);
			if (oldFont) SelectObject(dc, oldFont);
			ReleaseDC(g_wuiTooltip, dc);
		}
	}
	if (!GetWindowRect(g_wuiTooltip, &rcTip)) return;
	RECT target;
	MONITORINFO monitor = { sizeof(monitor) };
	int x = rcTip.left, y = rcTip.top;
	if (GetWindowRect(g_wuiTarget, &target)
	 && GetMonitorInfoW(MonitorFromWindow(g_wuiTarget, MONITOR_DEFAULTTONEAREST), &monitor)) {
		if (target.top <= monitor.rcMonitor.top) { x = target.left + 8; y = target.bottom + 8; }
		else if (target.bottom >= monitor.rcMonitor.bottom) { x = target.left + 8; y = target.top - height - 8; }
		else { x = target.right + 8; y = target.bottom - height;
			if (x + width > monitor.rcWork.right) x = target.left - width - 8; }
		x = max(monitor.rcWork.left, min(x, monitor.rcWork.right - width));
		y = max(monitor.rcWork.top, min(y, monitor.rcWork.bottom - height));
	}
	if (rcTip.left == x && rcTip.top == y
	 && rcTip.right - rcTip.left == width && rcTip.bottom - rcTip.top == height) return;
	SetWindowPos(g_wuiTooltip, NULL, x, y, width, height,
		SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	// The native tooltip region retains its old size after manual resizing.
	if (rcTip.right - rcTip.left != width || rcTip.bottom - rcTip.top != height)
		SetWindowRgn(g_wuiTooltip, NULL, TRUE);
}

static void wui_activate_tip(void)
{
	TOOLINFOW ti;
	RECT rcTarget;
	RECT rcWork;
	MONITORINFO monitor = { sizeof(monitor) };
	int gap = 8;
	int x;
	int y;

	if (!g_wuiTooltip || !g_wuiTarget || !g_wuiTooltipText[0]) return;
	if (!GetWindowRect(g_wuiTarget, &rcTarget)) return;
	if (!GetMonitorInfoW(MonitorFromWindow(g_wuiTarget, MONITOR_DEFAULTTONEAREST), &monitor)) return;
	rcWork = monitor.rcWork;
	if (rcTarget.top <= monitor.rcMonitor.top) {
		x = rcTarget.left + gap;
		y = rcTarget.bottom + gap;
		if (x > rcWork.right - gap) x = rcWork.right - gap;
		if (x < rcWork.left) x = rcWork.left;
		if (y > rcWork.bottom - gap) y = rcWork.bottom - gap;
	}
	else if (rcTarget.bottom >= monitor.rcMonitor.bottom) {
		x = rcTarget.left + gap;
		y = rcTarget.top - gap;
		if (x > rcWork.right - gap) x = rcWork.right - gap;
		if (x < rcWork.left) x = rcWork.left;
		if (y < rcWork.top) y = rcWork.top;
	}
	else {
		x = rcTarget.right + gap;
		if (x > rcWork.right - gap) x = rcTarget.left - gap;
		if (x < rcWork.left) x = rcWork.left;
		y = rcTarget.bottom - gap;
		if (y > rcWork.bottom - gap) y = rcWork.bottom - gap;
		if (y < rcWork.top) y = rcWork.top;
	}
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	SendMessageW(g_wuiTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
	wui_fit_tip();
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

	if (!g_wuiHost || !IsWindow(g_wuiHost)) return FALSE;
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return FALSE;
	if (!GetCursorPos(&point)) return FALSE;
	if (!GetWindowRect(g_wuiHost, &rcHost)) return FALSE;
	if (!PtInRect(&rcHost, point)) return FALSE;
	ScreenToClient(g_wuiTarget, &point);
	SendMessageW(g_wuiTarget, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
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
		KillTimer(hwnd, WUI_TIP_LEAVE_TIMER_ID);
		wui_track_leave(hwnd);
		if (g_wuiTipVisible) return 0;
		if (g_wuiTipSuppressed) return 0;
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		wui_forward_mouse(hwnd, msg, wParam, lParam);
		return 0;
	case WM_MOUSELEAVE:
		g_wuiHoverInside = FALSE;
		g_wuiTipSuppressed = FALSE;
		if (g_wuiTipVisible) SetTimer(hwnd, WUI_TIP_LEAVE_TIMER_ID, WUI_TIP_LEAVE_MS, NULL);
		else wui_hide_tip();
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
		if (msg == WM_RBUTTONDOWN) {
			g_wuiTipSuppressed = TRUE;
			wui_hide_tip();
		}
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
			if (!g_wuiHoverInside) wui_hide_tip();
			return 0;
		}
		if (wParam == WUI_TIP_TIMER_ID) {
			KillTimer(hwnd, WUI_TIP_TIMER_ID);
			if (g_wuiTipPending && g_wuiHoverInside) {
				if (wui_post_tip_move()) wui_show_tip();
				else wui_hide_tip();
			}
			else if (g_wuiTipVisible) {
				wui_hide_tip();
				g_wuiTipSuppressed = TRUE;
			}
			return 0;
		}
		if (wParam == WUI_TIMER_ID) {
			wui_place(hwnd);
			if (wui_sync_order(hwnd)) wui_present(hwnd);
			else wui_hide_tip();
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
		ZeroMemory(&g_wuiEdge, sizeof(g_wuiEdge));
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
	if (!g_wuiHost) return FALSE;
	ShowWindow(g_wuiHost, SW_HIDE);
	wui_place(g_wuiHost);
	wui_present(g_wuiHost);
	SetTimer(g_wuiHost, WUI_TIMER_ID, WUI_TIMER_MS, NULL);
	return TRUE;
}

extern "C" void WINAPI WuiDestroyHost(void)
{
	wui_clear_layer();
	wui_reset_text();
	wui_hide_tip();
	if (g_wuiTooltip && IsWindow(g_wuiTooltip)) {
		DestroyWindow(g_wuiTooltip);
	}
	g_wuiTooltip = NULL;
	g_wuiTipHasStyle = FALSE;
	g_wuiTipTitle[0] = L'\0';
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
		wui_clear_fonts();
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
	if (cb < offsetof(TC_DISPLAY_BACKEND_RENDER_STATE, contentVersion)) {
		g_wuiState.layerPixels = NULL;
		g_wuiState.layerWidth = g_wuiState.layerHeight = 0;
	}
	wui_copy_layer(g_wuiState);
	g_wuiState.layerPixels = NULL;
	if (g_wuiHost && IsWindow(g_wuiHost)) {
		wui_present(g_wuiHost);
	}
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefresh(void)
{
	if (!g_wuiHost || !IsWindow(g_wuiHost)) return FALSE;
	wui_place(g_wuiHost);
	if (wui_sync_order(g_wuiHost)) wui_present(g_wuiHost);
	else wui_hide_tip();
	return TRUE;
}

extern "C" BOOL WINAPI WuiSetTooltip(const WCHAR* text, BOOL visible, HFONT font, COLORREF backColor,
	UINT initialDelay, UINT reshowDelay, UINT autoPopDelay)
{
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
	if (g_wuiTipVisible || g_wuiTipPending) {
		// Preserve the previous snapshot until refresh has compared and fitted it.
		WUI_TOOLTIP_STATE state = { sizeof(state) };
		state.text = text;
		state.title = g_wuiTipTitle;
		state.font = font;
		state.titleFont = g_wuiTipTitleFont;
		state.backColor = backColor;
		state.textColor = g_wuiTipTextColor;
		state.titleColor = g_wuiTipTitleColor;
		g_wuiTipAutoPopDelay = autoPopDelay;
		return WuiRefreshTooltip(&state);
	}
	BOOL textChanged = lstrcmpW(g_wuiTooltipText, text) != 0;
	if (textChanged) lstrcpynW(g_wuiTooltipText, text, _countof(g_wuiTooltipText));
	if (!wui_make_tip()) return FALSE;
	if (!g_wuiTipHasStyle || g_wuiTipFont != font)
		SendMessageW(g_wuiTooltip, WM_SETFONT, (WPARAM)font, FALSE);
	if (!g_wuiTipHasStyle || g_wuiTipBackColor != backColor) {
		SendMessageW(g_wuiTooltip, TTM_SETTIPBKCOLOR, backColor, 0);
		SendMessageW(g_wuiTooltip, TTM_SETTIPTEXTCOLOR, backColor, 0);
	}
	g_wuiTipFont = font;
	g_wuiTipBackColor = backColor;
	if (!g_wuiTipVisible)
		SendMessageW(g_wuiTooltip, TTM_SETMAXTIPWIDTH, 0, wui_tip_width(g_wuiTooltipText, font));
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	if (textChanged) SendMessageW(g_wuiTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	g_wuiTipAutoPopDelay = autoPopDelay;
	if (!g_wuiTipPending && !g_wuiTipVisible) {
		UINT delay = g_wuiTipShownOnce ? reshowDelay : initialDelay;
		g_wuiTipPending = TRUE;
		if (delay) SetTimer(g_wuiHost, WUI_TIP_TIMER_ID, delay, NULL);
		else wui_show_tip();
	}
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefreshTooltip(const WUI_TOOLTIP_STATE* state)
{
	LOGFONTW fontInfo = {}, titleInfo = {};
	UINT dpi;
	BOOL textChanged, titleChanged, fontChanged, styleChanged;
	if (!state || state->cb != sizeof(*state) || !state->text || !state->title) return FALSE;
	if (!g_wuiTooltip || (!g_wuiTipVisible && !g_wuiTipPending)) return FALSE;
	if (state->font) GetObjectW(state->font, sizeof(fontInfo), &fontInfo);
	if (state->titleFont) GetObjectW(state->titleFont, sizeof(titleInfo), &titleInfo);
	dpi = GetDpiForWindow(g_wuiTooltip);
	textChanged = lstrcmpW(g_wuiTooltipText, state->text) != 0;
	titleChanged = lstrcmpW(g_wuiTipTitle, state->title) != 0;
	fontChanged = !g_wuiTipHasStyle || g_wuiTipFont != state->font
		|| memcmp(&g_wuiTipFontInfo, &fontInfo, sizeof(fontInfo)) != 0;
	styleChanged = fontChanged || g_wuiTipDpi != dpi
		|| g_wuiTipTitleFont != state->titleFont
		|| memcmp(&g_wuiTipTitleInfo, &titleInfo, sizeof(titleInfo)) != 0
		|| g_wuiTipBackColor != state->backColor
		|| g_wuiTipTextColor != state->textColor || g_wuiTipTitleColor != state->titleColor;
	// Enabled live updates retain the classic tooltip's extended display duration.
	if (g_wuiTipVisible && g_wuiTipAutoPopDelay) SetTimer(g_wuiHost, WUI_TIP_TIMER_ID, g_wuiTipAutoPopDelay, NULL);
	if (!textChanged && !titleChanged && !styleChanged) return TRUE;
	lstrcpynW(g_wuiTooltipText, state->text, _countof(g_wuiTooltipText));
	lstrcpynW(g_wuiTipTitle, state->title, _countof(g_wuiTipTitle));
	if (fontChanged) SendMessageW(g_wuiTooltip, WM_SETFONT, (WPARAM)state->font, FALSE);
	if (!g_wuiTipHasStyle || g_wuiTipBackColor != state->backColor) {
		SendMessageW(g_wuiTooltip, TTM_SETTIPBKCOLOR, state->backColor, 0);
		SendMessageW(g_wuiTooltip, TTM_SETTIPTEXTCOLOR, state->backColor, 0);
	}
	if (!g_wuiTipHasStyle || g_wuiTipDpi != dpi)
		SendMessageW(g_wuiTooltip, TTM_SETMAXTIPWIDTH, 0, wui_tip_width(state->text, state->font));
	g_wuiTipFont = state->font;
	g_wuiTipTitleFont = state->titleFont;
	g_wuiTipFontInfo = fontInfo;
	g_wuiTipTitleInfo = titleInfo;
	g_wuiTipBackColor = state->backColor;
	g_wuiTipTextColor = state->textColor;
	g_wuiTipTitleColor = state->titleColor;
	g_wuiTipDpi = dpi;
	g_wuiTipHasStyle = TRUE;
	if (!g_wuiTipVisible) return TRUE;
	wui_fit_tip();
	// Repaint content without restarting tracking or erasing the entire popup.
	InvalidateRect(g_wuiTooltip, NULL, FALSE);
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefreshTooltipText(const WCHAR* text)
{
	WUI_TOOLTIP_STATE state = { sizeof(state) };
	state.text = text;
	state.title = g_wuiTipTitle;
	state.font = g_wuiTipFont;
	state.titleFont = g_wuiTipTitleFont;
	state.backColor = g_wuiTipBackColor;
	state.textColor = g_wuiTipTextColor;
	state.titleColor = g_wuiTipTitleColor;
	return WuiRefreshTooltip(&state);
}

extern "C" BOOL WINAPI WuiIsTooltip(HWND hwnd)
{
	return hwnd && hwnd == g_wuiTooltip;
}
