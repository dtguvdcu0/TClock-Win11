#pragma once

#include <windows.h>

#ifdef WUI_DLL_EXPORTS
#define WUI_API __declspec(dllexport)
#else
#define WUI_API __declspec(dllimport)
#endif

#define TC_WUI_STYLE_VERSION 1
#define TC_WUI_MAX_STYLES 64
#define TC_WUI_MAX_RUNS 256

typedef struct TC_DISPLAY_BACKEND_STYLE {
	COLORREF textColor;
	COLORREF shadowColor;
	LONG shadowRange;
	LONG fontHeight;
	LONG fontWeight;
	BYTE fontItalic;
	BYTE fontCharSet;
	BYTE clockShadow;
	BYTE clockBorder;
	WCHAR fontFace[LF_FACESIZE];
} TC_DISPLAY_BACKEND_STYLE;

typedef struct TC_DISPLAY_BACKEND_RUN {
	LONG textStart;
	LONG textLength;
	LONG x;
	LONG y;
	WORD styleIndex;
	WORD reserved;
} TC_DISPLAY_BACKEND_RUN;

typedef struct TC_DISPLAY_BACKEND_RENDER_STATE {
	DWORD cb;
	LONG textPos;
	LONG vertPos;
	LONG lineHeight;
	LONG shadowRange;
	LONG fontHeight;
	LONG fontWeight;
	BYTE fontItalic;
	BYTE fontCharSet;
	BYTE clockShadow;
	BYTE clockBorder;
	COLORREF textColor;
	COLORREF shadowColor;
	WCHAR fontFace[LF_FACESIZE];
	WCHAR text[4096];
	DWORD styleVersion;
	DWORD styleCount;
	DWORD runCount;
	TC_DISPLAY_BACKEND_STYLE styles[TC_WUI_MAX_STYLES];
	TC_DISPLAY_BACKEND_RUN runs[TC_WUI_MAX_RUNS];
} TC_DISPLAY_BACKEND_RENDER_STATE;

typedef struct WUI_TOOLTIP_STATE {
	DWORD cb;
	const WCHAR* text;
	const WCHAR* title;
	HFONT font;
	HFONT titleFont;
	COLORREF backColor;
	COLORREF textColor;
	COLORREF titleColor;
} WUI_TOOLTIP_STATE;

#ifdef __cplusplus
extern "C" {
#endif

WUI_API BOOL WINAPI WuiCreateHost(HWND hwndTargetClock);
WUI_API void WINAPI WuiDestroyHost(void);
WUI_API BOOL WINAPI WuiUpdateState(const TC_DISPLAY_BACKEND_RENDER_STATE* state);
WUI_API BOOL WINAPI WuiRefresh(void);
WUI_API BOOL WINAPI WuiSetTooltip(const WCHAR* text, BOOL visible, HFONT font, COLORREF backColor,
	UINT initialDelay, UINT reshowDelay, UINT autoPopDelay);
WUI_API BOOL WINAPI WuiRefreshTooltipText(const WCHAR* text);
WUI_API BOOL WINAPI WuiRefreshTooltip(const WUI_TOOLTIP_STATE* state);
WUI_API BOOL WINAPI WuiIsTooltip(HWND hwnd);

#ifdef __cplusplus
}
#endif
