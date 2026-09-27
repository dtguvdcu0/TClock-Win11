#pragma once

// Presentation-only adapter for the existing property controls.
typedef struct PL_ITEM {
 int page, id, kind, x, y, width, height;
 const WCHAR* text;
 const WCHAR* english;
} PL_ITEM;
#include "property_layout_data.h"

typedef struct PL_WINDOW {
 HWND hwnd;
 int x, y, width, height;
 int layoutY;
 BOOL heading;
 BOOL combo;
} PL_WINDOW;
typedef struct PL_STATE {
 int page, dpi, offset, count, extent;
 BOOL positioning;
 BOOL dirty, refreshQueued;
 int paintedOffset;
 int suppressedCount;
 HWND suppressed[128];
 PL_WINDOW windows[256];
} PL_STATE;
static HFONT pl_font;
static HFONT pl_heading;
#define PL_REFRESH (WM_APP + 177)
#define PL_TITLE 29900

static UINT pl_dpi(HWND hwnd)
{
 typedef UINT (WINAPI *DpiProc)(HWND);
 DpiProc getDpi = (DpiProc)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
 HDC dc; UINT dpi;
 if (getDpi) return getDpi(hwnd);
 dc = GetDC(hwnd); dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
 if (dc) ReleaseDC(hwnd, dc);
 return dpi;
}

static int pl_scale(int value, int dpi) { return MulDiv(value, dpi, 96); }

static void pl_place(HWND hwnd, int x, int y, int width, int height, int dpi)
{
 SetWindowPos(hwnd, NULL, pl_scale(x, dpi), pl_scale(y, dpi),
  pl_scale(width, dpi), pl_scale(height, dpi), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void pl_add(PL_STATE* state, HWND hwnd, int x, int y, int width, int height)
{
 PL_WINDOW* entry;
 WCHAR name[40];
 if (!hwnd || state->count >= _countof(state->windows)) return;
 entry = &state->windows[state->count++];
 entry->hwnd = hwnd; entry->x = x; entry->y = y;
 entry->width = width; entry->height = height == 30 ? 28 : height;
 GetClassNameW(hwnd, name, _countof(name));
 entry->combo = !lstrcmpW(name, L"ComboBox");
 if (GetDlgCtrlID(hwnd) != IDC_FONTSAMPLE && GetDlgCtrlID(hwnd) != IDC_FONTSAMPLE_TOOLTIP)
  SendMessageW(hwnd, WM_SETFONT, (WPARAM)pl_font, FALSE);
 if (entry->combo) SendMessageW(hwnd, CB_SETITEMHEIGHT, (WPARAM)-1, pl_scale(max(14, height - 6), state->dpi));
}

static PL_WINDOW* pl_find(PL_STATE* state, HWND hwnd)
{
 int i;
 for (i = 0; i < state->count; ++i)
  if (state->windows[i].hwnd == hwnd) return &state->windows[i];
 return NULL;
}

static int pl_y(PL_STATE* state, PL_WINDOW* entry)
{
 static const int optional[] = { 1413, 1429, 1425, 1431, 1434, 1427, 1436, 1439, 1403 };
 int y = entry->y, i, j;
 if (state->page != 16) return y;
 for (i = 0; i < _countof(optional); ++i) {
  for (j = 0; j < state->count; ++j) {
   PL_WINDOW* candidate = &state->windows[j];
   if (GetDlgCtrlID(candidate->hwnd) == optional[i] &&
       !(GetWindowLongPtrW(candidate->hwnd, GWL_STYLE) & WS_VISIBLE) && entry->y >= candidate->y + 30) {
    y -= 36; break;
   }
  }
 }
 return y;
}

// Remove excess blank bands without shrinking previews or multiline editors.
static void pl_compact(PL_STATE* state)
{
 int order[256], count = 0, i, j, bottom = 0, removed = 0;
 for (i = 0; i < state->count; ++i) {
  PL_WINDOW* entry = &state->windows[i];
  entry->layoutY = pl_y(state, entry);
  if (!(GetWindowLongPtrW(entry->hwnd, GWL_STYLE) & WS_VISIBLE)) continue;
  j = count;
  while (j > 0 && state->windows[order[j - 1]].layoutY > entry->layoutY) {
   order[j] = order[j - 1]; --j;
  }
  order[j] = i; ++count;
 }
 for (i = 0; i < count; ++i) {
  PL_WINDOW* entry = &state->windows[order[i]];
  int y = entry->layoutY;
  int gap = entry->heading ? 12 : 4;
  if (y > bottom + gap) removed += y - bottom - gap;
  bottom = max(bottom, y + entry->height);
  entry->layoutY = y - removed;
 }
}

static void pl_arrange(HWND page, PL_STATE* state)
{
 RECT client;
 SCROLLINFO info = { sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS };
 int i, extent = 0, height;
 HDWP batch;
 BOOL relayout = state->dirty;
 if (state->positioning) return;
 state->positioning = TRUE;
 if (relayout) {
  for (i = 0; i < state->suppressedCount; ++i) ShowWindow(state->suppressed[i], SW_HIDE);
  pl_compact(state); state->dirty = FALSE;
 }
 GetClientRect(page, &client);
 height = MulDiv(client.bottom, 96, state->dpi);
 for (i = 0; i < state->count; ++i) {
  PL_WINDOW* entry = &state->windows[i];
  if (GetWindowLongPtrW(entry->hwnd, GWL_STYLE) & WS_VISIBLE)
   extent = max(extent, entry->layoutY + entry->height + 8);
 }
 state->extent = extent;
 state->offset = max(0, min(state->offset, max(0, extent - height)));
 if (!relayout && state->paintedOffset == state->offset) { state->positioning = FALSE; return; }
 info.nMin = 0; info.nMax = max(0, extent - 1); info.nPage = height; info.nPos = state->offset;
 SetScrollInfo(page, SB_VERT, &info, TRUE);
 // Move children together and finish painting before returning from a scroll.
 batch = BeginDeferWindowPos(state->count);
 for (i = 0; i < state->count && batch; ++i) {
  PL_WINDOW* entry = &state->windows[i];
  batch = DeferWindowPos(batch, entry->hwnd, NULL,
   pl_scale(entry->x, state->dpi), pl_scale(entry->layoutY - state->offset, state->dpi),
   pl_scale(entry->width, state->dpi), pl_scale(entry->height + (entry->combo ? 180 : 0), state->dpi),
   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
 }
 if (batch) EndDeferWindowPos(batch);
 else {
  for (i = 0; i < state->count; ++i) {
   PL_WINDOW* entry = &state->windows[i];
   pl_place(entry->hwnd, entry->x, entry->layoutY - state->offset, entry->width,
    entry->height + (entry->combo ? 180 : 0), state->dpi);
  }
 }
 state->paintedOffset = state->offset;
 RedrawWindow(page, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
 state->positioning = FALSE;
}

static void pl_reveal(HWND page, PL_STATE* state, HWND focus)
{
 PL_WINDOW* entry = pl_find(state, focus);
 RECT rect;
 int height;
 if (!entry) return;
 GetClientRect(page, &rect);
 height = MulDiv(rect.bottom, 96, state->dpi);
 if (entry->layoutY < state->offset) state->offset = entry->layoutY;
 else if (entry->layoutY + entry->height > state->offset + height)
  state->offset = entry->layoutY + entry->height - height + 6;
 else return;
 pl_arrange(page, state);
}

static LRESULT CALLBACK pl_handle_field(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
 UINT_PTR id, DWORD_PTR reference)
{
 PL_STATE* state = (PL_STATE*)reference;
 if (message == WM_SETFOCUS) pl_reveal(GetParent(hwnd), state, hwnd);
 if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, pl_handle_field, id);
 return DefSubclassProc(hwnd, message, wp, lp);
}

static LRESULT CALLBACK pl_handle_page(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
 UINT_PTR id, DWORD_PTR reference)
{
 PL_STATE* state = (PL_STATE*)reference;
 if (message == WM_VSCROLL && !lp) {
  SCROLLINFO info = { sizeof(info), SIF_TRACKPOS | SIF_PAGE };
  GetScrollInfo(hwnd, SB_VERT, &info);
  switch (LOWORD(wp)) {
  case SB_LINEUP: state->offset -= 36; break;
  case SB_LINEDOWN: state->offset += 36; break;
  case SB_PAGEUP: state->offset -= info.nPage; break;
  case SB_PAGEDOWN: state->offset += info.nPage; break;
  case SB_THUMBTRACK: case SB_THUMBPOSITION: state->offset = info.nTrackPos; break;
  case SB_TOP: state->offset = 0; break;
  case SB_BOTTOM: state->offset = state->extent; break;
  }
  pl_arrange(hwnd, state); return 0;
 }
 if (message == WM_MOUSEWHEEL) {
  state->offset -= GET_WHEEL_DELTA_WPARAM(wp) * 36 / WHEEL_DELTA;
  pl_arrange(hwnd, state); return 0;
 }
 if (message == PL_REFRESH) {
  state->refreshQueued = FALSE; state->dirty = TRUE;
  pl_arrange(hwnd, state); return 0;
 }
 if (message == WM_COMMAND) {
  LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
  if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == CBN_SETFOCUS)
   pl_reveal(hwnd, state, (HWND)lp);
  else if (!state->positioning && !state->refreshQueued &&
    (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE)) {
   state->refreshQueued = TRUE; PostMessageW(hwnd, PL_REFRESH, 0, 0);
  }
  return result;
 }
 if (message == WM_NCDESTROY) {
  LRESULT result;
  RemoveWindowSubclass(hwnd, pl_handle_page, id);
  result = DefSubclassProc(hwnd, message, wp, lp);
  GlobalFree(state);
  return result;
 }
 return DefSubclassProc(hwnd, message, wp, lp);
}

static void pl_attach(HWND page, int index)
{
 PL_STATE* state;
 HWND child;
 int i, bottom = 0, fallbackTop = 0, aboutY = 100;
 state = (PL_STATE*)GlobalAlloc(GPTR, sizeof(PL_STATE));
 if (!state) return;
 state->page = index; state->dpi = pl_dpi(page); state->dirty = TRUE;
 for (i = 0; i < _countof(pl_items); ++i) {
  const PL_ITEM* item = &pl_items[i];
  if (item->page != index) continue;
  bottom = max(bottom, item->y + item->height);
 }
 fallbackTop = bottom + 30;
 // Move existing controls first; runtime values and enable/visibility state stay owned by each page.
 for (child = GetWindow(page, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
  int control = GetDlgCtrlID(child);
  WCHAR name[40];
  BOOL mapped = FALSE;
  GetClassNameW(child, name, _countof(name));
  if (!lstrcmpW(name, L"msctls_updown32")) continue;
  for (i = 0; i < _countof(pl_items); ++i) {
   const PL_ITEM* item = &pl_items[i];
   if (item->page != index || item->kind != 1 || item->id != control) continue;
   pl_add(state, child, item->x, item->y, item->width, item->height);
   if (item->text && (!lstrcmpW(name, L"Button") || !lstrcmpW(name, L"Static")) && (!b_EnglishMenu || item->english))
    SetWindowTextW(child, b_EnglishMenu && item->english ? item->english : (!b_EnglishMenu ? item->text : L""));
   mapped = TRUE; break;
  }
  if (mapped) continue;
  if (index == 14 && !lstrcmpW(name, L"Static") && (control == -1 || control == 65535)) {
   WCHAR text[512]; GetWindowTextW(child, text, _countof(text));
   if (text[0]) { pl_add(state, child, 0, aboutY, 500, 22); aboutY += 27; continue; }
  }
  if ((!lstrcmpW(name, L"Static") && (index != 19 || control == 10344)) ||
      control == -1 || control == 65535 ||
      (!lstrcmpW(name, L"Button") && (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)) {
   ShowWindow(child, SW_HIDE);
   if (state->suppressedCount < _countof(state->suppressed)) state->suppressed[state->suppressedCount++] = child;
   continue;
  }
  // Retain controls absent from the captured state (for example action/alarm details).
  {
   RECT rect; int x, y, width, height;
   GetWindowRect(child, &rect); MapWindowPoints(NULL, page, (POINT*)&rect, 2);
   x = MulDiv(rect.left, 96, state->dpi);
   y = MulDiv(rect.top, 96, state->dpi);
   width = MulDiv(rect.right - rect.left, 96, state->dpi);
   height = MulDiv(rect.bottom - rect.top, 96, state->dpi);
   pl_add(state, child, max(0, x), fallbackTop + y, min(width + 30, 490 - x), max(height, 24));
  }
 }
 // Spin controls keep their original buddy; give the combined editor a single aligned footprint.
 for (child = GetWindow(page, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
  WCHAR name[40];
  GetClassNameW(child, name, _countof(name));
  if (!lstrcmpW(name, L"msctls_updown32")) {
   PL_WINDOW* buddy = pl_find(state, (HWND)SendMessageW(child, UDM_GETBUDDY, 0, 0));
   if (buddy) {
    int spinX = buddy->x + buddy->width - 16;
    buddy->width = max(24, buddy->width - 16);
    pl_add(state, child, spinX, buddy->y, 16, buddy->height);
   }
  }
 }
 for (i = 0; i < _countof(pl_items); ++i) {
  const PL_ITEM* item = &pl_items[i];
  const WCHAR* text;
  HWND label;
  int labelWidth;
  if (item->page != index || item->kind == 1) continue;
  text = b_EnglishMenu ? item->english : item->text;
  if (!text) continue;
  label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_LEFTNOWORDWRAP,
   0, 0, 0, 0, page, NULL, g_hInst, NULL);
  labelWidth = item->width;
  if (item->kind == 3 || labelWidth < 100) {
   HDC dc = GetDC(page); SIZE size; HGDIOBJ oldFont = SelectObject(dc, item->kind == 3 ? pl_heading : pl_font);
   if (GetTextExtentPoint32W(dc, text, lstrlenW(text), &size))
    labelWidth = min(500 - item->x, max(item->kind == 3 ? 0 : labelWidth, MulDiv(size.cx, 96, state->dpi) + 4));
   SelectObject(dc, oldFont); ReleaseDC(page, dc);
  }
  pl_add(state, label, item->x, item->y, labelWidth, max(20, item->height));
  if (item->kind == 3) {
   HWND line = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
    0, 0, 0, 0, page, NULL, g_hInst, NULL);
   pl_find(state, label)->heading = TRUE;
   SendMessageW(label, WM_SETFONT, (WPARAM)pl_heading, FALSE);
   pl_add(state, line, item->x + labelWidth + 8, item->y + 9, max(0, item->width - labelWidth - 8), 2);
  }
 }
 // Keep keyboard navigation in visual row order after regrouping the controls.
 for (i = 1; i < state->count; ++i) {
  PL_WINDOW entry = state->windows[i]; int j = i;
  while (j > 0 && (state->windows[j - 1].y > entry.y ||
   (state->windows[j - 1].y == entry.y && state->windows[j - 1].x > entry.x))) {
   state->windows[j] = state->windows[j - 1]; --j;
  }
  state->windows[j] = entry;
 }
 for (i = 0; i < state->count; ++i) {
  SetWindowPos(state->windows[i].hwnd, i ? state->windows[i - 1].hwnd : HWND_TOP,
   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowSubclass(state->windows[i].hwnd, pl_handle_field, 1, (DWORD_PTR)state);
 }
 SetWindowLongPtrW(page, GWL_STYLE, GetWindowLongPtrW(page, GWL_STYLE) | WS_VSCROLL | WS_CLIPCHILDREN);
 SetWindowPos(page, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
 SetWindowSubclass(page, pl_handle_page, 1, (DWORD_PTR)state);
 pl_arrange(page, state);
}

static void pl_frame(HWND parent)
{
 int dpi = pl_dpi(parent);
 RECT rect = { 0, 0, pl_scale(744, dpi), pl_scale(644, dpi) };
 HWND tree = GetDlgItem(parent, IDC_TREE);
 LOGFONTW font = { 0 };
 font.lfHeight = -pl_scale(12, dpi); font.lfWeight = FW_NORMAL;
 lstrcpyW(font.lfFaceName, L"Segoe UI");
 pl_font = CreateFontIndirectW(&font);
 font.lfWeight = FW_SEMIBOLD; pl_heading = CreateFontIndirectW(&font);
 AdjustWindowRectEx(&rect, (DWORD)GetWindowLongPtrW(parent, GWL_STYLE), FALSE,
  (DWORD)GetWindowLongPtrW(parent, GWL_EXSTYLE));
 SetWindowPos(parent, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
 pl_place(tree, 14, 16, 166, 558, dpi);
 SetWindowLongPtrW(tree, GWL_STYLE, (GetWindowLongPtrW(tree, GWL_STYLE) &
  ~(TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS)) | TVS_FULLROWSELECT);
 SendMessageW(tree, WM_SETFONT, (WPARAM)pl_font, FALSE);
 SendMessageW(tree, TVM_SETITEMHEIGHT, pl_scale(27, dpi), 0);
 SendMessageW(tree, TVM_SETINDENT, pl_scale(8, dpi), 0);
 pl_place(GetDlgItem(parent, IDOK), 444, 599, 88, 30, dpi);
 pl_place(GetDlgItem(parent, IDCANCEL), 542, 599, 88, 30, dpi);
 pl_place(GetDlgItem(parent, ID_APPLY), 640, 599, 88, 30, dpi);
 SendDlgItemMessageW(parent, IDOK, WM_SETFONT, (WPARAM)pl_font, FALSE);
 SendDlgItemMessageW(parent, IDCANCEL, WM_SETFONT, (WPARAM)pl_font, FALSE);
 SendDlgItemMessageW(parent, ID_APPLY, WM_SETFONT, (WPARAM)pl_font, FALSE);
 {
  HWND title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
   0, 0, 0, 0, parent, (HMENU)(INT_PTR)PL_TITLE, g_hInst, NULL);
  pl_place(title, 202, 20, 520, 26, dpi);
  SendMessageW(title, WM_SETFONT, (WPARAM)pl_heading, FALSE);
 }
}

static HTREEITEM pl_insert(HWND tree, HTREEITEM parent, int value, const WCHAR* text)
{
 TVINSERTSTRUCTW item = { 0 };
 item.hParent = parent; item.hInsertAfter = TVI_LAST;
 item.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
 item.item.pszText = (LPWSTR)text; item.item.lParam = value;
 item.item.state = TVIS_EXPANDED | (value < 0 ? TVIS_BOLD : 0);
 item.item.stateMask = TVIS_EXPANDED | TVIS_BOLD;
 return (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&item);
}

static void pl_tree(HWND tree, HTREEITEM roots[], HTREEITEM children[])
{
 HTREEITEM group;
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Display" : L"\u8868\u793a");
 children[0] = pl_insert(tree, group, 100, MyStringW(IDS_PROP_COLOR));
 children[1] = pl_insert(tree, group, 101, MyStringW(IDS_PROP_FORMAT));
 children[3] = pl_insert(tree, group, 103, MyStringW(IDS_PROP_GRAPH));
 children[5] = pl_insert(tree, group, 105, MyStringW(IDS_BARMETER));
 children[4] = pl_insert(tree, group, 104, MyStringW(IDS_PROP_ANALOG));
 roots[1] = pl_insert(tree, group, 1, MyStringW(IDS_TOOLTIP));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Interaction" : L"\u64cd\u4f5c\u30fb\u901a\u77e5");
 roots[6] = pl_insert(tree, group, 6, MyStringW(IDS_PROP_MOUSE));
 roots[9] = pl_insert(tree, group, 9, MyStringW(IDS_PROP_RCLICK_MENU));
 children[7] = pl_insert(tree, group, 107, MyStringW(IDS_PROP_CHIME));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Integration / system" : L"\u9023\u643a\u30fb\u30b7\u30b9\u30c6\u30e0");
 roots[8] = pl_insert(tree, group, 8, MyStringW(IDS_PROP_CUSTOMVARS));
 roots[2] = pl_insert(tree, group, 2, MyStringW(IDS_PROP_KEYWORDS));
 roots[7] = pl_insert(tree, group, 7, MyStringW(IDS_PROP_ETC));
 roots[5] = pl_insert(tree, group, 5, MyStringW(IDS_PROP_WIN11));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Information" : L"\u60c5\u5831");
 roots[4] = pl_insert(tree, group, 4, MyStringW(IDS_MISC));
}
