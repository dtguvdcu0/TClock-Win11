#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <commctrl.h>
#include <gdiplus.h>
#include "wui_api.h"
#include "wui_text.h"
#include "wui_taskbar.h"
#include "../common/taskbar_edge.h"
#include "../common/taskbar_surface.h"
#include "../common/tooltip_hover.h"
#include "../common/tooltip_fade.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

#define WUI_TIMER_ID 1
#define WUI_TIMER_MS 100
#define WUI_TIP_TIMER_ID 2
#define WUI_TIP_FADE_TIMER_ID 6

static HINSTANCE g_wuiInst = NULL;
static HWND g_wuiTarget = NULL;
static HWND g_wuiHost = NULL;
static TBE_REGION g_wuiEdge = {0};
static TBE_FRAME g_wuiFrame = {0};
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
static TIP_HOVER_STATE g_wuiHover = {};
static TIP_FADE_STATE g_wuiFade = {};
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
	HDC hdcScreen = NULL;
	HDC hdcMem = NULL;
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
	if (!tbe_ensure_frame(&g_wuiFrame, sizeWindow.cx, sizeWindow.cy)) goto cleanup;
	hdcMem = g_wuiFrame.dc;
	pixels = (BYTE*)g_wuiFrame.pixels;
	ZeroMemory(pixels, (SIZE_T)sizeWindow.cx * (SIZE_T)sizeWindow.cy * 4u);
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
		tbe_write_strip((RGBQUAD*)pixels, sizeWindow.cx, sizeWindow.cy, &cut, edgeColor);
	}

	blend.BlendOp = AC_SRC_OVER;
	blend.BlendFlags = 0;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;
	UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWindow, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

cleanup:
	delete pGraphics;
	if (hdcScreen) ReleaseDC(NULL, hdcScreen);
}

static void wui_hide_tip(void);

static BOOL wui_keep_order(HWND hwnd, HWND owner)
{
    HWND next;
    RECT surface, taskbar, sibling, overlap;
    WCHAR className[32];
    if (!IsWindowVisible(hwnd) || !GetWindowRect(hwnd, &surface)) return FALSE;
    next = GetWindow(hwnd, GW_HWNDNEXT);
    if (next == owner) return TRUE;
    // Horizontal taskbars place their nonoverlapping desktop button before Shell.
    if (!next || GetWindow(next, GW_HWNDNEXT) != owner || GetWindow(next, GW_OWNER) != owner
     || !GetWindowRect(owner, &taskbar) || taskbar.right - taskbar.left <= taskbar.bottom - taskbar.top
     || !GetClassNameW(next, className, _countof(className)) || lstrcmpW(className, L"Static") != 0
     || !GetWindowRect(next, &sibling) || IntersectRect(&overlap, &surface, &sibling)) return FALSE;
    return TRUE;
}

static BOOL wui_sync_order(HWND hwnd)
{
	HWND owner = GetAncestor(g_wuiTarget, GA_ROOT);
	if (!owner || !IsWindowVisible(owner) || !IsWindowVisible(g_wuiTarget)) {
		if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
		return FALSE;
	}
	if (!tbs_can_present(g_wuiTarget, owner)) {
		if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
		wui_hide_tip();
		return FALSE;
	}
	if (wui_keep_order(hwnd, owner)) return TRUE;
	return tbs_sync_order(hwnd, owner);
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
	wui_reserve_taskbar(GetAncestor(g_wuiTarget, GA_ROOT), g_wuiTarget);
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

static void wui_conceal_tip(void)
{
	TOOLINFOW ti;
	if (!g_wuiTooltip || !g_wuiTarget) return;
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
	ShowWindow(g_wuiTooltip, SW_HIDE);
	tip_stop_fade(&g_wuiFade, g_wuiTooltip, g_wuiHost, WUI_TIP_FADE_TIMER_ID);
}

static void wui_hide_tip(void)
{
	if (g_wuiHost) KillTimer(g_wuiHost, WUI_TIP_TIMER_ID);
	g_wuiHover.armed = 0;
	tip_hide_hover(&g_wuiHover);
	wui_conceal_tip();
}

static void wui_schedule_tip(void)
{
	if (!tip_schedule_hover(&g_wuiHover, g_wuiHost, WUI_TIP_TIMER_ID, GetTickCount64()))
		wui_hide_tip();
}

static BOOL wui_make_tip(void)
{
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
	TOOLINFOW ti;

	if (g_wuiTooltip && IsWindow(g_wuiTooltip)) return TRUE;
	if (!g_wuiTarget || !IsWindow(g_wuiTarget)) return FALSE;
	InitCommonControlsEx(&icc);
	g_wuiTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_NOANIMATE | TTS_NOFADE,
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
	 && GetMonitorInfoW(MonitorFromPoint(g_wuiHover.point, MONITOR_DEFAULTTONEAREST), &monitor)) {
		// Keep the hover anchor while the text or title changes size.
		if (target.top <= monitor.rcMonitor.top) { x = g_wuiHover.point.x - width / 2; y = target.bottom + 8; }
		else if (target.bottom >= monitor.rcMonitor.bottom) { x = g_wuiHover.point.x - width / 2; y = target.top - height - 8; }
		else { x = target.right + 8; y = g_wuiHover.point.y - height / 2;
			if (x + width > monitor.rcWork.right) x = target.left - width - 8; }
		x = max(monitor.rcWork.left, min(x, monitor.rcWork.right - width));
		y = max(monitor.rcWork.top, min(y, monitor.rcWork.bottom - height));
	}
	if (rcTip.left != x || rcTip.top != y
	 || rcTip.right - rcTip.left != width || rcTip.bottom - rcTip.top != height) {
		SetWindowPos(g_wuiTooltip, NULL, x, y, width, height,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	}
	// Native placement can leave a stale region even when the outer size is correct.
	// Clear it after every fit, including position-only and already-sized refreshes.
	SetWindowRgn(g_wuiTooltip, NULL, TRUE);
}

static void wui_activate_tip(void)
{
	TOOLINFOW ti;
	RECT target;

	if (!g_wuiTooltip || !g_wuiTarget || !g_wuiTooltipText[0]) return;
	if (!GetWindowRect(g_wuiTarget, &target)) return;
	if (!g_wuiHover.hasPoint) {
		if (!GetCursorPos(&g_wuiHover.point)) {
			g_wuiHover.point.x = target.left + (target.right - target.left) / 2;
			g_wuiHover.point.y = target.top + (target.bottom - target.top) / 2;
		}
		g_wuiHover.hasPoint = TRUE;
	}
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK;
	ti.hwnd = g_wuiTarget;
	ti.uId = 1;
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	SendMessageW(g_wuiTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(g_wuiHover.point.x, g_wuiHover.point.y));
	SendMessageW(g_wuiTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
	wui_fit_tip();
}

static int wui_scale_tip(int value)
{
	UINT dpi = g_wuiHost ? GetDpiForWindow(g_wuiHost) : 96;
	return max(1, MulDiv(value, dpi ? dpi : 96, 96));
}

static int wui_pick_tip(POINT point, int previous)
{
	RECT target;
	int slots = (int)(INT_PTR)GetPropW(g_wuiTarget, WUI_TIP_SLOTS_PROP);
	BOOL vertical = GetPropW(g_wuiTarget, WUI_TIP_AXIS_PROP) != NULL;
	if (!GetWindowRect(g_wuiTarget, &target)) return 0;
	return tip_pick_slot(&target, point, vertical, slots, previous, wui_scale_tip(TIP_HOVER_MARGIN));
}

static BOOL wui_move_tip(POINT point)
{
	BOOL wasVisible = g_wuiHover.visible;
	if (tip_move_hover(&g_wuiHover, point, wui_pick_tip(point, g_wuiHover.slot),
		wui_scale_tip(TIP_HOVER_DISTANCE), wui_scale_tip(TIP_HOVER_STILL), GetTickCount64())) return TRUE;
	if (wasVisible) wui_conceal_tip();
	wui_schedule_tip();
	return FALSE;
}

static void wui_show_tip(void)
{
	if (!g_wuiHover.pending) return;
	tip_begin_fade(&g_wuiFade, g_wuiTooltip, g_wuiHost, WUI_TIP_FADE_TIMER_ID,
		!GetPropW(g_wuiTarget, WUI_TIP_FADE_PROP), GetTickCount64());
	wui_activate_tip();
	tip_step_fade(&g_wuiFade, g_wuiTooltip, g_wuiHost, WUI_TIP_FADE_TIMER_ID, GetTickCount64());
	tip_show_hover(&g_wuiHover, wui_pick_tip(g_wuiHover.point, -1), GetTickCount64());
	wui_schedule_tip();
}

static void wui_track_leave(HWND hwnd)
{
	TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };

	if (g_wuiHover.inside) return;
	TrackMouseEvent(&tme);
	tip_enter_hover(&g_wuiHover);
}

static void wui_wait_tip(void)
{
	POINT point = {};
	RECT target;
	BOOL valid = g_wuiTarget && IsWindow(g_wuiTarget)
		&& GetCursorPos(&point) && GetWindowRect(g_wuiTarget, &target) && PtInRect(&target, point);
	KillTimer(g_wuiHost, WUI_TIP_TIMER_ID);
	g_wuiHover.armed = 0;
	TIP_HOVER_ACTION action = tip_poll_hover(&g_wuiHover, point, valid,
		wui_scale_tip(TIP_HOVER_STILL), GetTickCount64());
	if (action == TIP_HOVER_HIDE) wui_hide_tip();
	else if (action == TIP_HOVER_SHOW) {
		ScreenToClient(g_wuiTarget, &point);
		SendMessageW(g_wuiTarget, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
		if (g_wuiHover.pending && g_wuiHover.inside && !g_wuiHover.suppressed) wui_show_tip();
	}
	wui_schedule_tip();
}

static LRESULT CALLBACK wui_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_MOUSEMOVE:
		wui_track_leave(hwnd);
		tip_enter_hover(&g_wuiHover);
		wui_schedule_tip();
		if (g_wuiHover.suppressed) return 0;
		{
			POINT point = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
			if (ClientToScreen(hwnd, &point)) {
				if (wui_move_tip(point)) return 0;
			}
		}
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
		wui_forward_mouse(hwnd, msg, wParam, lParam);
		return 0;
	case WM_MOUSELEAVE:
		tip_leave_hover(&g_wuiHover, GetTickCount64());
		if (!g_wuiHover.visible) wui_conceal_tip();
		wui_schedule_tip();
		return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
		if (msg == WM_RBUTTONDOWN) {
			g_wuiHover.suppressed = TRUE;
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
		if (wParam == WUI_TIP_FADE_TIMER_ID) {
			tip_step_fade(&g_wuiFade, g_wuiTooltip, g_wuiHost, WUI_TIP_FADE_TIMER_ID, GetTickCount64());
			return 0;
		}
		if (wParam == WUI_TIP_TIMER_ID) {
			wui_wait_tip();
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
	case WM_THEMECHANGED:
	case WM_DWMCOLORIZATIONCOLORCHANGED:
	case WM_SETTINGCHANGE:
	case WM_DISPLAYCHANGE:
	case WM_DPICHANGED:
		tbe_reset_color();
		break;
	case WM_DESTROY:
		wui_release_taskbar();
		tbe_release_frame(&g_wuiFrame);
		tbe_reset_color();
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
	wui_release_taskbar();
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
	tbe_release_frame(&g_wuiFrame);
	tbe_reset_color();
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

static BOOL wui_store_state(const TC_DISPLAY_BACKEND_RENDER_STATE* state)
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
	return TRUE;
}

extern "C" BOOL WINAPI WuiUpdateState(const TC_DISPLAY_BACKEND_RENDER_STATE* state)
{
	if (!wui_store_state(state)) return FALSE;
	if (g_wuiHost && IsWindow(g_wuiHost)) wui_present(g_wuiHost);
	return TRUE;
}

extern "C" BOOL WINAPI WuiApplyState(const TC_DISPLAY_BACKEND_RENDER_STATE* state)
{
	if (!wui_store_state(state)) return FALSE;
	if (!g_wuiHost || !IsWindow(g_wuiHost)) return FALSE;
	wui_place(g_wuiHost);
	if (wui_sync_order(g_wuiHost)) wui_present(g_wuiHost);
	else wui_hide_tip();
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
	if (g_wuiHover.suppressed) return TRUE;
	tip_config_hover(&g_wuiHover, initialDelay, reshowDelay, autoPopDelay);
	if (g_wuiHover.visible || g_wuiHover.pending) {
		// Preserve the previous snapshot until refresh has compared and fitted it.
		WUI_TOOLTIP_STATE state = { sizeof(state) };
		state.text = text;
		state.title = g_wuiTipTitle;
		state.font = font;
		state.titleFont = g_wuiTipTitleFont;
		state.backColor = backColor;
		state.textColor = g_wuiTipTextColor;
		state.titleColor = g_wuiTipTitleColor;
		g_wuiHover.autoDelay = autoPopDelay;
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
	if (!g_wuiHover.visible)
		SendMessageW(g_wuiTooltip, TTM_SETMAXTIPWIDTH, 0, wui_tip_width(g_wuiTooltipText, font));
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	if (textChanged) SendMessageW(g_wuiTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	g_wuiHover.autoDelay = autoPopDelay;
	tip_begin_hover(&g_wuiHover, GetTickCount64(), initialDelay);
	if (!initialDelay) wui_show_tip();
	else wui_schedule_tip();
	return TRUE;
}

extern "C" BOOL WINAPI WuiRefreshTooltip(const WUI_TOOLTIP_STATE* state)
{
	LOGFONTW fontInfo = {}, titleInfo = {};
	UINT dpi;
	BOOL textChanged, titleChanged, fontChanged, styleChanged;
	if (!state || state->cb != sizeof(*state) || !state->text || !state->title) return FALSE;
	if (!g_wuiTooltip || (!g_wuiHover.visible && !g_wuiHover.pending)) return FALSE;
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
	tip_refresh_hover(&g_wuiHover, GetTickCount64());
	wui_schedule_tip();
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
	if (!g_wuiHover.visible) return TRUE;
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
