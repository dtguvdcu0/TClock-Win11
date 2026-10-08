#pragma once
#include <windows.h>

// Clock-owned Shell-relative notification anchor in pixels, encoded as Y + 1.
#define WUI_TASKBAR_ANCHOR_PROPERTY L"TClock.WinUI.NotifyAnchorY"

// Calls never wait for the taskbar UI thread or the diagnostics worker.
void wui_reserve_taskbar(HWND taskbar, HWND clock);
void wui_release_taskbar(void);
