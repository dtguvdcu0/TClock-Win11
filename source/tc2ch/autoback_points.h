#pragma once

// Shared by the settings preview and clock renderer. Positions use 1/100 percent.
typedef struct AB_POINTS {
 int position[4];
 int balance;
} AB_POINTS;

static __inline int ab_clamp(int value, int high)
{
 return value < 0 ? 0 : (value > high ? high : value);
}

enum { AB_HORIZONTAL, AB_SIDE, AB_PROFILE_COUNT };
enum { AB_INVALID = -2, AB_FUTURE = -1, AB_LEGACY, AB_POINTS_SAVED, AB_DEFAULTS };
#define AB_QUERY_MESSAGE L"TClock.AutoBack.LegacyPoint.v1"

typedef struct AB_PROFILE {
 const char* keys[4];
 const char* balance;
 const char* snapshots[2];
 int defaults[4];
} AB_PROFILE;

static const AB_PROFILE ab_profiles[AB_PROFILE_COUNT] = {
 { { "AutoBackHorizontalPoint1X", "AutoBackHorizontalPoint1Y", "AutoBackHorizontalPoint2X", "AutoBackHorizontalPoint2Y" },
   "AutoBackHorizontalBalance", { "AutoBackHorizontalSnapshotColor", "AutoBackHorizontalSnapshotColor2" }, { 2000, 5000, 6000, 5000 } },
 { { "AutoBackSidePoint1X", "AutoBackSidePoint1Y", "AutoBackSidePoint2X", "AutoBackSidePoint2Y" },
   "AutoBackSideBalance", { "AutoBackSideSnapshotColor", "AutoBackSideSnapshotColor2" }, { 500, 2000, 500, 6000 } }
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
 points->balance = ab_clamp((int)GetMyRegLong("Color_Font", description->balance, 50), 100);
 if (profile == AB_SIDE) return AB_POINTS_SAVED;
 if (version != 0xFFFFFFFF && version > 1) return AB_FUTURE;
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
 if (points->balance < 0 || points->balance > 100) return FALSE;
 for (i = 0; i < 4; ++i) SetMyRegLong("Color_Font", description->keys[i], points->position[i]);
 SetMyRegLong("Color_Font", description->balance, points->balance);
 for (i = 0; i < 4; ++i)
  if ((DWORD)GetMyRegLong("Color_Font", description->keys[i], 0xFFFFFFFF) != (DWORD)points->position[i]) return FALSE;
 if ((DWORD)GetMyRegLong("Color_Font", description->balance, 0xFFFFFFFF) != (DWORD)points->balance) return FALSE;
 if (profile == AB_HORIZONTAL) {
  SetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 1);
  if (GetMyRegLong("Color_Font", "AutoBackHorizontalPointsVersion", 0) != 1) return FALSE;
 }
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
 colors[0] = colors[1] = CLR_INVALID;
 if (!GetWindowRect(taskbar, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) return FALSE;
 for (i = 0; i < 2; ++i) {
  positions[i] = ab_get_point(&rect, points->position[i * 2], points->position[i * 2 + 1]);
  if (ab_contains(clock, positions[i]) || ab_contains(overlay, positions[i])) return FALSE;
 }
 dc = GetDC(NULL);
 if (!dc) return FALSE;
 for (i = 0; i < 2; ++i) colors[i] = GetPixel(dc, positions[i].x, positions[i].y);
 ReleaseDC(NULL, dc);
 return colors[0] != CLR_INVALID && colors[1] != CLR_INVALID;
}
