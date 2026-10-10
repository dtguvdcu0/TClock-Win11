#pragma once
#include <windows.h>
#define UPDATE_NOTICE_COMMAND 49010
#define UPDATE_NOTICE_TIMER 49011
#ifdef __cplusplus
extern "C" {
#endif
void update_Start(HWND owner, BOOL english);
void update_Stop(void);
void update_Drain(void);
void update_Tick(void);
void update_InsertMenu(HMENU menu, BOOL english);
BOOL update_MeasureMenu(MEASUREITEMSTRUCT* item);
BOOL update_DrawItem(DRAWITEMSTRUCT* item);
void update_AttachPage(HWND page, BOOL english);
void update_DetachPage(HWND page);
void update_OpenRelease(HWND owner);
#ifdef __cplusplus
}
#endif
