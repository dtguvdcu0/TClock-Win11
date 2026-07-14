#pragma once

#include <windows.h>

#ifdef WUI_DLL_EXPORTS
#define WUI_API __declspec(dllexport)
#else
#define WUI_API __declspec(dllimport)
#endif

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
} TC_DISPLAY_BACKEND_RENDER_STATE;

#ifdef __cplusplus
extern "C" {
#endif

WUI_API BOOL WINAPI WuiCreateHost(HWND hwndTargetClock);
WUI_API void WINAPI WuiDestroyHost(void);
WUI_API BOOL WINAPI WuiUpdateState(const TC_DISPLAY_BACKEND_RENDER_STATE* state);
WUI_API BOOL WINAPI WuiRefresh(void);
WUI_API BOOL WINAPI WuiSetTooltip(const WCHAR* text, BOOL visible, HFONT font, COLORREF backColor);
WUI_API BOOL WINAPI WuiIsTooltip(HWND hwnd);

#ifdef __cplusplus
}
#endif
