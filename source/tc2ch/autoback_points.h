#pragma once
#include <stdio.h>
#include "common/ini_io_utf8.h"

// Shared by the settings preview and clock renderer. Positions use 1/100 percent.
typedef struct AB_POINTS {
 int position[4];
 int balance;
 int count;
} AB_POINTS;

static __inline int ab_clamp(int value, int high)
{
 return value < 0 ? 0 : (value > high ? high : value);
}

enum { AB_HORIZONTAL, AB_SIDE, AB_PROFILE_COUNT };
enum { AB_INVALID = -2, AB_FUTURE = -1, AB_LEGACY, AB_POINTS_SAVED, AB_DEFAULTS };
#define AB_QUERY_MESSAGE L"TClock.AutoBack.LegacyPoint.v1"
#define AB_OFFSET_QUERY_MESSAGE L"TClock.AutoBack.OffsetColor.v1"
#define AB_MODE_KEY "AutoBackHorizontalSamplingMode"

typedef struct AB_OFFSETS {
 int clock;
 int desktop;
 int balance;
} AB_OFFSETS;

static __inline void ab_load_offsets(AB_OFFSETS* offsets)
{
 offsets->clock = max(-200, min(200, (int)GetMyRegLong("Color_Font", "AutoBackSampleClockOffset", 0)));
 offsets->desktop = max(-200, min(200, (int)GetMyRegLong("Color_Font", "AutoBackSampleShowDesktopOffset", 0)));
 offsets->balance = ab_clamp((int)GetMyRegLong("Color_Font", "AutoBackBlendRatio", 50), 100);
}

typedef struct AB_PROFILE {
 const char* keys[4];
 const char* balance;
 const char* count;
 const char* snapshots[2];
 int defaults[4];
} AB_PROFILE;

static const AB_PROFILE ab_profiles[AB_PROFILE_COUNT] = {
 { { "AutoBackHorizontalPoint1X", "AutoBackHorizontalPoint1Y", "AutoBackHorizontalPoint2X", "AutoBackHorizontalPoint2Y" },
   "AutoBackHorizontalBalance", "AutoBackHorizontalPointCount", { "AutoBackHorizontalSnapshotColor", "AutoBackHorizontalSnapshotColor2" }, { 2000, 5000, 6000, 5000 } },
 { { "AutoBackSidePoint1X", "AutoBackSidePoint1Y", "AutoBackSidePoint2X", "AutoBackSidePoint2Y" },
   "AutoBackSideBalance", "AutoBackSidePointCount", { "AutoBackSideSnapshotColor", "AutoBackSideSnapshotColor2" }, { 500, 2000, 500, 6000 } }
};

static __inline BOOL ab_has_legacy(void)
{
 static const char* keys[] = { "AutoBackSampleClockOffset", "AutoBackSampleShowDesktopOffset", "AutoBackBlendRatio", "AutoBackSnapshotColor", "AutoBackSnapshotColor2" };
 int i;
 for (i = 0; i < _countof(keys); ++i)
  if (GetMyRegLong("Color_Font", keys[i], 0xFFFFFFFF) != 0xFFFFFFFF) return TRUE;
 return FALSE;
}

static __inline int ab_load(int profile, AB_POINTS* points)
{
 const AB_PROFILE* description = &ab_profiles[profile];
 DWORD version = GetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 0xFFFFFFFF);
 BOOL complete = TRUE, any = FALSE;
 int i;
 for (i = 0; i < 4; ++i) {
  DWORD value = GetMyRegLong("Color_Font", description->keys[i], 0xFFFFFFFF);
  if (value != 0xFFFFFFFF) any = TRUE;
  if (value > 10000) complete = FALSE;
  points->position[i] = value == 0xFFFFFFFF ? description->defaults[i] : ab_clamp((int)value, 10000);
 }
 points->count = GetMyRegLong("Color_Font", description->count, 2) == 1 ? 1 : 2;
 points->balance = ab_clamp((int)GetMyRegLong("Color_Font", description->balance, 50), 100);
 if (profile == AB_SIDE) return AB_POINTS_SAVED;
 if (version != 0xFFFFFFFF && version > 1) return AB_FUTURE;
 {
  DWORD selection = GetMyRegLong("Color_Font", AB_MODE_KEY, 0xFFFFFFFF);
  if (selection != 0xFFFFFFFF && selection > 1) return AB_FUTURE;
  if (selection == 0) return AB_LEGACY;
  if (selection == 1 && complete) return AB_POINTS_SAVED;
  if (selection == 1) return AB_INVALID;
 }
 if (version == 1 && complete) return AB_POINTS_SAVED;
 if (version != 0xFFFFFFFF || any) return AB_INVALID;
 return ab_has_legacy() ? AB_LEGACY : AB_DEFAULTS;
}

static __inline BOOL ab_save(int profile, const AB_POINTS* points)
{
 const AB_PROFILE* description = &ab_profiles[profile];
 DWORD version = GetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 0xFFFFFFFF);
 int i;
 if (profile == AB_HORIZONTAL && version != 0xFFFFFFFF && version > 1) return FALSE;
 for (i = 0; i < 4; ++i) {
  if (points->position[i] < 0 || points->position[i] > 10000) return FALSE;
 }
 if (points->balance < 0 || points->balance > 100 || (points->count != 1 && points->count != 2)) return FALSE;
 for (i = 0; i < 4; ++i) SetMyRegLong("Color_Font", description->keys[i], points->position[i]);
 SetMyRegLong("Color_Font", description->balance, points->balance);
 SetMyRegLong("Color_Font", description->count, points->count);
 for (i = 0; i < 4; ++i)
  if ((DWORD)GetMyRegLong("Color_Font", description->keys[i], 0xFFFFFFFF) != (DWORD)points->position[i]) return FALSE;
 if ((DWORD)GetMyRegLong("Color_Font", description->balance, 0xFFFFFFFF) != (DWORD)points->balance) return FALSE;
 if ((DWORD)GetMyRegLong("Color_Font", description->count, 0) != (DWORD)points->count) return FALSE;
 if (profile == AB_HORIZONTAL) {
  SetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 1);
  if (GetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 0) != 1) return FALSE;
 }
 return TRUE;
}

// Commit both parameter sets and the selected mode atomically; failed writes leave the INI intact.
static __inline BOOL ab_save_settings(LPCWSTR path, int profile, const AB_POINTS* points, const AB_OFFSETS* offsets, int selection)
{
 DWORD mode = GetMyRegLong("Color_Font", AB_MODE_KEY, 0xFFFFFFFF);
 DWORD version = GetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 0xFFFFFFFF);
 const char* keys[11];
 int values[11], count = 0, i, used = 0;
 char entries[1024] = {0};
 if (profile == AB_SIDE) return ab_save(profile, points);
 if ((mode != 0xFFFFFFFF && mode > 1) || (version != 0xFFFFFFFF && version > 1)) return FALSE;
 if (selection < 0 || selection > 1 || offsets->clock < -200 || offsets->clock > 200 ||
     offsets->desktop < -200 || offsets->desktop > 200 || offsets->balance < 0 || offsets->balance > 100 ||
     points->balance < 0 || points->balance > 100 || (points->count != 1 && points->count != 2)) return FALSE;
 for (i = 0; i < 4; ++i) {
  if (points->position[i] < 0 || points->position[i] > 10000) return FALSE;
  keys[count] = ab_profiles[profile].keys[i]; values[count++] = points->position[i];
 }
 keys[count] = ab_profiles[profile].balance; values[count++] = points->balance;
 keys[count] = ab_profiles[profile].count; values[count++] = points->count;
 keys[count] = "AutoBackHorizontalPointsVersion"; values[count++] = 1;
 keys[count] = "AutoBackSampleClockOffset"; values[count++] = offsets->clock;
 keys[count] = "AutoBackSampleShowDesktopOffset"; values[count++] = offsets->desktop;
 keys[count] = "AutoBackBlendRatio"; values[count++] = offsets->balance;
 keys[count] = AB_MODE_KEY; values[count++] = selection;
 for (i = 0; i < count; ++i) {
  int length = sprintf_s(entries + used, sizeof(entries) - used, "%s=%d", keys[i], values[i]);
  if (length < 0) return FALSE;
  used += length + 1;
 }
 if (!tc_write_batchW(path, "Color_Font", entries, (DWORD)used + 1)) return FALSE;
 for (i = 0; i < count; ++i)
  if ((int)GetMyRegLong("Color_Font", keys[i], 0x7FFFFFFF) != values[i]) return FALSE;
 return TRUE;
}

// Anchors and offsets use screen coordinates, including monitors left of the primary display.
static __inline BOOL ab_place_offsets(const RECT* task, const RECT* clock, int notifyWidth,
 int desktopPosition, BOOL winui, const AB_OFFSETS* offsets, int positions[2])
{
 RECT overlap;
 if (task->right <= task->left || task->bottom <= task->top ||
     clock->right <= clock->left || !IntersectRect(&overlap, task, clock)) return FALSE;
 if (notifyWidth > 0 && desktopPosition > 0)
  positions[0] = clock->right + max(0, min(notifyWidth - 1, desktopPosition + offsets->desktop));
 else positions[0] = clock->left - (notifyWidth > 0 ? notifyWidth / 2 : 10);
 positions[1] = clock->left + offsets->clock - (winui ? 10 : 0);
 positions[0] = max(task->left, min(task->right - 1, positions[0]));
 positions[1] = max(task->left, min(task->right - 1, positions[1]));
 return TRUE;
}


static __inline HWND ab_find_taskbar(HWND clock)
{
 HWND parent = clock;
 while (parent && IsWindow(parent)) {
  WCHAR name[80];
  if (GetClassNameW(parent, name, _countof(name)) &&
      (!lstrcmpW(name, L"Shell_TrayWnd") || !lstrcmpW(name, L"Shell_SecondaryTrayWnd"))) return parent;
  parent = GetParent(parent);
 }
 return FindWindowW(L"Shell_TrayWnd", NULL);
}

static __inline int ab_get_profile(HWND taskbar)
{
 RECT rect;
 if (!IsWindow(taskbar) || !GetWindowRect(taskbar, &rect) || rect.right <= rect.left || rect.bottom <= rect.top || rect.right-rect.left == rect.bottom-rect.top) return -1;
 return rect.bottom - rect.top > rect.right - rect.left ? AB_SIDE : AB_HORIZONTAL;
}

static __inline POINT ab_get_point(const RECT* rect, int x, int y)
{
 POINT point;
 point.x = rect->left + MulDiv(ab_clamp(x, 10000), max(0, rect->right - rect->left - 1), 10000);
 point.y = rect->top + MulDiv(ab_clamp(y, 10000), max(0, rect->bottom - rect->top - 1), 10000);
 return point;
}

static __inline BOOL ab_contains(HWND hwnd, POINT point)
{
 RECT rect;
 return hwnd && IsWindowVisible(hwnd) && GetWindowRect(hwnd, &rect) && PtInRect(&rect, point);
}

static __inline BOOL ab_sample(HWND taskbar, HWND clock, const AB_POINTS* points, COLORREF colors[2])
{
 RECT rect;
 HDC dc;
 HWND overlay = FindWindowW(L"TClockWinUIDllWindow", NULL);
 POINT positions[2];
 int i;
 HWND marker = NULL;
 colors[0] = colors[1] = CLR_INVALID;
 // Keep the last runtime color while a settings marker covers a sampling point.
 while ((marker = FindWindowExW(NULL, marker, L"TClockSamplePoint", NULL)) != NULL)
  if (IsWindowVisible(marker)) return FALSE;
 if (points->count != 1 && points->count != 2) return FALSE;
 if (!GetWindowRect(taskbar, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) return FALSE;
 for (i = 0; i < points->count; ++i) {
  positions[i] = ab_get_point(&rect, points->position[i * 2], points->position[i * 2 + 1]);
  if (ab_contains(clock, positions[i]) || ab_contains(overlay, positions[i])) return FALSE;
 }
 dc = GetDC(NULL);
 if (!dc) return FALSE;
 for (i = 0; i < points->count; ++i) colors[i] = GetPixel(dc, positions[i].x, positions[i].y);
 ReleaseDC(NULL, dc);
 if (points->count == 1) colors[1] = colors[0];
 return colors[0] != CLR_INVALID && colors[1] != CLR_INVALID;
}

static __inline COLORREF ab_blend(COLORREF first, COLORREF second, int balance)
{
 int ratio = ab_clamp(balance, 100);
 return RGB((GetRValue(first) * (100-ratio) + GetRValue(second) * ratio) / 100,
            (GetGValue(first) * (100-ratio) + GetGValue(second) * ratio) / 100,
            (GetBValue(first) * (100-ratio) + GetBValue(second) * ratio) / 100);
}

static __inline BOOL ab_sample_at(HWND taskbar, HWND clock, int screenX, BOOL top, COLORREF colors[2])
{
 RECT rect;
 POINT point[2];
 HDC dc;
 HWND marker = NULL, overlay = FindWindowW(L"TClockWinUIDllWindow", NULL);
 int i;
 colors[0] = colors[1] = CLR_INVALID;
 while ((marker = FindWindowExW(NULL, marker, L"TClockSamplePoint", NULL)) != NULL)
  if (IsWindowVisible(marker)) return FALSE;
 if (!GetWindowRect(taskbar, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) return FALSE;
 point[0].x = point[1].x = max(rect.left, min(rect.right - 1, screenX));
 point[0].y = top ? rect.top + (rect.bottom - rect.top) / 2 : rect.bottom - 1;
 point[1].y = top ? rect.bottom - 1 : rect.top;
 for (i = 0; i < 2; ++i)
  if (ab_contains(clock, point[i]) || ab_contains(overlay, point[i])) return FALSE;
 dc = GetDC(NULL);
 if (!dc) return FALSE;
 for (i = 0; i < 2; ++i) colors[i] = GetPixel(dc, point[i].x, point[i].y);
 ReleaseDC(NULL, dc);
 return colors[0] != CLR_INVALID && colors[1] != CLR_INVALID;
}
