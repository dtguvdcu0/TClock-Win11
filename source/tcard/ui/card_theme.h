#pragma once

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <string>
#include <cwchar>

#pragma comment(lib, "comctl32.lib")

namespace tcard_ui {

constexpr int kPaperInsetDip = 16;
constexpr int kPaperHeaderDip = 36;
constexpr int kEditorCommandDip = 48;
constexpr float kNoteTitleSizeDip = 16.0f;
constexpr float kNoteBodySizeDip = 15.0f;

inline COLORREF shade_color(COLORREF color, float factor)
{
    const int red = min(255, max(0, static_cast<int>(GetRValue(color) * factor)));
    const int green = min(255, max(0, static_cast<int>(GetGValue(color) * factor)));
    const int blue = min(255, max(0, static_cast<int>(GetBValue(color) * factor)));
    return RGB(red, green, blue);
}

inline int dip_to_px(int dip, UINT dpi)
{
    return MulDiv(dip, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
}

// Shared settings rows keep labels centered and controls within the paper.
inline void layout_setting(HWND label, HWND control, int x, int y, int width, bool combo)
{
    const int labelWidth = 84;
    MoveWindow(label, x, y + 3, labelWidth, 22, TRUE);
    MoveWindow(control, x + labelWidth + 8, y, max(60, width - labelWidth - 8), combo ? 240 : 28, TRUE);
}

inline HWND create_label(HWND parent, int id, const wchar_t* text, HFONT font)
{
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_CENTERIMAGE,
        0, 0, 80, 22, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return label;
}

inline bool valid_size(const std::wstring& text, double& value)
{
    wchar_t* end = nullptr;
    value = wcstod(text.c_str(), &end);
    return end != text.c_str() && *end == L'\0' && value >= 8.0 && value <= 48.0;
}

// Preserve the native scrollbar gutter so hover never changes text wrapping.
struct ScrollHover { COLORREF paper; bool hovered = false; };
inline constexpr UINT_PTR scroll_id = 0x54435348;
inline bool hover_card(HWND control)
{
    POINT point{};
    if (!GetCursorPos(&point)) return false;
    const HWND root = GetAncestor(control, GA_ROOT);
    const HWND hit = WindowFromPoint(point);
    const HWND capture = GetCapture();
    return (hit && GetAncestor(hit, GA_ROOT) == root) ||
        (capture && GetAncestor(capture, GA_ROOT) == root);
}
inline void conceal_scroll(HWND control, COLORREF paper)
{
    RECT window{}; GetWindowRect(control, &window);
    HDC dc = GetWindowDC(control);
    if (!dc) return;
    HBRUSH brush = CreateSolidBrush(paper);
    for (LONG object : {OBJID_VSCROLL, OBJID_HSCROLL}) {
        SCROLLBARINFO info{}; info.cbSize = sizeof(info);
        if (GetScrollBarInfo(control, object, &info) && !(info.rgstate[0] & STATE_SYSTEM_INVISIBLE)) {
            OffsetRect(&info.rcScrollBar, -window.left, -window.top);
            FillRect(dc, &info.rcScrollBar, brush);
        }
    }
    DeleteObject(brush); ReleaseDC(control, dc);
}
inline LRESULT CALLBACK scroll_proc(HWND control, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    auto* state = reinterpret_cast<ScrollHover*>(data);
    if (message == WM_NCDESTROY) {
        KillTimer(control, id); RemoveWindowSubclass(control, scroll_proc, id); delete state;
        return DefSubclassProc(control, message, wp, lp);
    }
    if (message == WM_TIMER && wp == id) {
        const bool hovered = hover_card(control);
        if (hovered != state->hovered) {
            state->hovered = hovered;
            RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
        }
        return 0;
    }
    if (message == EM_SETBKGNDCOLOR) state->paper = static_cast<COLORREF>(lp);
    const auto result = DefSubclassProc(control, message, wp, lp);
    if (!state->hovered && (message == WM_NCPAINT || message == WM_PAINT || message == WM_VSCROLL ||
        message == WM_HSCROLL || message == WM_MOUSEWHEEL || message == WM_SIZE)) conceal_scroll(control, state->paper);
    return result;
}
inline void attach_scroll(HWND control, COLORREF paper)
{
    auto* state = new ScrollHover{paper, hover_card(control)};
    if (!SetWindowSubclass(control, scroll_proc, scroll_id, reinterpret_cast<DWORD_PTR>(state))) { delete state; return; }
    SetTimer(control, scroll_id, 100, nullptr);
}

inline void update_scroll(HWND control)
{
    if (!control) return;
    wchar_t name[32]{};
    GetClassNameW(control, name, ARRAYSIZE(name));
    bool needed = false;
    if (_wcsicmp(name, L"EDIT") == 0 || (_wcsicmp(name, L"RICHEDIT50W") == 0 &&
        (SendMessageW(control, EM_GETTEXTMODE, 0, 0) & TM_PLAINTEXT))) {
        RECT rect{};
        SendMessageW(control, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
        HDC dc = GetDC(control);
        if (!dc) return;
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
        HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(SYSTEM_FONT));
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, previous);
        ReleaseDC(control, dc);
        const LRESULT lines = SendMessageW(control, EM_GETLINECOUNT, 0, 0);
        needed = GetWindowTextLengthW(control) > 0 && lines * metrics.tmHeight > rect.bottom - rect.top;
    } else {
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
        if (GetScrollInfo(control, SB_VERT, &info))
            needed = info.nPage > 0 && static_cast<long long>(info.nMax) - info.nMin + 1 > info.nPage;
    }
    const bool visible = (GetWindowLongPtrW(control, GWL_STYLE) & WS_VSCROLL) != 0;
    if (visible != needed) ShowScrollBar(control, SB_VERT, needed);
}

inline LRESULT CALLBACK button_proc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE || message == WM_ENABLE || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, button_proc, 1);
    }
    return DefSubclassProc(window, message, w, l);
}

inline void paint_button(const DRAWITEMSTRUCT& item, COLORREF paper, COLORREF ink, HFONT font, bool primary)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(item.hwndItem, &cursor);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hot = !disabled && PtInRect(&item.rcItem, cursor);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF fill = primary && !disabled ? shade_color(ink, pressed ? 0.85f : hot ? 1.25f : 1.0f) : shade_color(paper, pressed ? 0.90f : hot ? 0.94f : 0.98f);
    HBRUSH background = CreateSolidBrush(paper);
    FillRect(item.hDC, &item.rcItem, background);
    DeleteObject(background);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary && !disabled ? fill : shade_color(paper, hot ? 0.82f : 0.91f));
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush), oldPen = SelectObject(item.hDC, pen);
    const int radius = dip_to_px(10, GetDpiForWindow(item.hwndItem));
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, radius, radius);
    SelectObject(item.hDC, oldBrush); SelectObject(item.hDC, oldPen);
    DeleteObject(brush); DeleteObject(pen);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? RGB(130,128,120) : primary ? paper : ink);
    HGDIOBJ oldFont = SelectObject(item.hDC, font);
    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, 128);
    RECT label = item.rcItem;
    InflateRect(&label, -6, 0);
    if (pressed && !disabled) OffsetRect(&label, 0, 1);
    DrawTextW(item.hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(item.hDC, oldFont);
    if (item.itemState & ODS_FOCUS) { InflateRect(&label, -3, -3); DrawFocusRect(item.hDC, &label); }
}

}
