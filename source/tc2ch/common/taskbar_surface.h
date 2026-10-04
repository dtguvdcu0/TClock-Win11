#pragma once

#include <windows.h>

// Shared visibility contract for layered surfaces belonging to the native taskbar.
static __inline BOOL tbs_can_present(HWND target, HWND taskbar)
{
 HWND foreground = GetAncestor(GetForegroundWindow(), GA_ROOTOWNER);
 RECT rect, client;
 MONITORINFO monitor = { sizeof(monitor) };
 WCHAR className[64];
 if (!IsWindowVisible(taskbar) || !IsWindowVisible(target) ||
     !GetClientRect(target, &client) || client.right <= 0 || client.bottom <= 0) return FALSE;
 if (!foreground || foreground == GetShellWindow() || foreground == taskbar ||
     IsIconic(foreground) || !IsWindowVisible(foreground)) return TRUE;
 className[0] = L'\0';
 if (GetClassNameW(foreground, className, _countof(className)) > 0 &&
     (lstrcmpW(className, L"Progman") == 0 || lstrcmpW(className, L"WorkerW") == 0)) return TRUE;
 if (GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &monitor) &&
     GetWindowRect(foreground, &rect) && rect.left <= monitor.rcMonitor.left &&
     rect.top <= monitor.rcMonitor.top && rect.right >= monitor.rcMonitor.right &&
     rect.bottom >= monitor.rcMonitor.bottom) return FALSE;
 return TRUE;
}

// Preserve the shell's stacking instead of giving each surface an independent topmost rank.
static __inline BOOL tbs_sync_order(HWND surface, HWND taskbar)
{
 HWND preceding = GetWindow(taskbar, GW_HWNDPREV);
 if (preceding == surface) preceding = GetWindow(surface, GW_HWNDPREV);
 return SetWindowPos(surface, preceding ? preceding : HWND_TOP, 0, 0, 0, 0,
  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}
