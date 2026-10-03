#pragma once

// Preserve vertical native edges; extend horizontal edges from the adjacent OS surface.
typedef struct TBE_REGION {
 HWND window;
 RECT cut;
 SIZE size;
 BOOL applied;
 BOOL busy;
} TBE_REGION;

static __inline BOOL tbe_get_strip(const RECT* task, const RECT* monitor, RECT* strip)
{
 LONG width = task->right - task->left, height = task->bottom - task->top;
 if (width <= 0 || height <= 0 || width == height || monitor->right <= monitor->left || monitor->bottom <= monitor->top) return FALSE;
 *strip = *task;
 if (height > width) {
  if (task->left + width / 2 < monitor->left + (monitor->right - monitor->left) / 2) strip->left = task->right - 1;
  else strip->right = task->left + 1;
 } else {
  if (task->top + height / 2 < monitor->top + (monitor->bottom - monitor->top) / 2) strip->top = task->bottom - 1;
  else strip->bottom = task->top + 1;
 }
 return TRUE;
}

static __inline HWND tbe_find_taskbar(HWND clock)
{
 HWND window = clock;
 HMONITOR monitor = MonitorFromWindow(clock, MONITOR_DEFAULTTONEAREST);
 while (window) {
  WCHAR name[80];
  if (GetClassNameW(window, name, _countof(name)) &&
      (!lstrcmpW(name, L"Shell_TrayWnd") || !lstrcmpW(name, L"Shell_SecondaryTrayWnd"))) return window;
  window = GetParent(window);
 }
 window = FindWindowW(L"Shell_TrayWnd", NULL);
 if (window && MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor) return window;
 window = NULL;
 while ((window = FindWindowExW(NULL, window, L"Shell_SecondaryTrayWnd", NULL)) != NULL)
  if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor) return window;
 return NULL;
}

// Only call for TClock-owned windows. Region ownership transfers to USER on success.
static __inline void tbe_update_region(HWND window, HWND taskbar, TBE_REGION* state)
{
 RECT bounds, task, strip, cut = {0, 0, 0, 0};
 MONITORINFO monitor = { sizeof(monitor) };
 SIZE size;
 HRGN region, excluded;
 BOOL hasCut = FALSE;
 if (state->busy || !window || !GetWindowRect(window, &bounds)) return;
 size.cx = bounds.right - bounds.left; size.cy = bounds.bottom - bounds.top;
 if (size.cx <= 0 || size.cy <= 0) return;
 if (taskbar && GetWindowRect(taskbar, &task) &&
     GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &monitor) &&
     task.bottom - task.top > task.right - task.left &&
     tbe_get_strip(&task, &monitor.rcMonitor, &strip) && IntersectRect(&cut, &bounds, &strip)) {
  OffsetRect(&cut, -bounds.left, -bounds.top); hasCut = TRUE;
 }
 if (state->window != window) { state->window = window; state->applied = FALSE; }
 if (!hasCut) {
  if (state->applied) {
   state->busy = TRUE;
   if (SetWindowRgn(window, NULL, TRUE)) state->applied = FALSE;
   state->busy = FALSE;
  }
  return;
 }
 if (state->applied && state->size.cx == size.cx && state->size.cy == size.cy && EqualRect(&state->cut, &cut)) return;
 region = CreateRectRgn(0, 0, size.cx, size.cy);
 excluded = CreateRectRgn(cut.left, cut.top, cut.right, cut.bottom);
 if (!region || !excluded || CombineRgn(region, region, excluded, RGN_DIFF) == ERROR) {
  if (region) DeleteObject(region);
  if (excluded) DeleteObject(excluded);
  return;
 }
 DeleteObject(excluded);
 state->busy = TRUE;
 if (SetWindowRgn(window, region, TRUE)) { state->applied = TRUE; state->cut = cut; state->size = size; }
 else DeleteObject(region);
 state->busy = FALSE;
}

// Horizontal composition ends beside the clock, so transparency exposes wallpaper.
// Sample outside both clock surfaces and paint the OS border after content rendering.
static __inline COLORREF tbe_sample_strip(HDC screen, HWND window, HWND taskbar, RECT* cut)
{
 RECT bounds, task, strip;
 MONITORINFO monitor = { sizeof(monitor) };
 LONG sampleX;
 COLORREF color;
 if (!screen || !taskbar || !GetWindowRect(window, &bounds) || !GetWindowRect(taskbar, &task) ||
     task.right - task.left <= task.bottom - task.top ||
     !GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &monitor) ||
     !tbe_get_strip(&task, &monitor.rcMonitor, &strip) || !IntersectRect(cut, &bounds, &strip)) return CLR_INVALID;
 sampleX = bounds.left - 4;
 if (sampleX < task.left) sampleX = bounds.right + 4;
 if (sampleX >= task.right) return CLR_INVALID;
 color = GetPixel(screen, sampleX, strip.top);
 OffsetRect(cut, -bounds.left, -bounds.top);
 return color;
}

static __inline void tbe_draw_strip(HDC target, HWND window, HWND taskbar)
{
 RECT cut;
 HDC screen = GetDC(NULL);
 COLORREF color = tbe_sample_strip(screen, window, taskbar, &cut);
 if (screen) ReleaseDC(NULL, screen);
 if (color != CLR_INVALID) {
  HBRUSH brush = CreateSolidBrush(color);
  if (brush) { FillRect(target, &cut, brush); DeleteObject(brush); }
 }
}
