#pragma once

constexpr int kSortField = 1201;
int g_sort_field = 1;
bool g_sort_descending = true;

void fill_list();
void refresh_preview();
void update_open_card();

std::wstring preferences_path() { return module_dir() + L"\\TCard.ini"; }

void create_sort(HWND hwnd)
{
    const auto ini = preferences_path();
    g_sort_field = (std::min)(2, static_cast<int>(GetPrivateProfileIntW(L"TCard", L"SortField", 1, ini.c_str())));
    if (g_sort_field < 0) g_sort_field = 1;
    g_sort_descending = GetPrivateProfileIntW(L"TCard", L"SortDescending", 1, ini.c_str()) != 0;
    HWND sort = CreateWindowExW(0, L"BUTTON", L"Sort", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 96, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSortField)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(sort, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui_font), TRUE);
    SetWindowSubclass(sort, tcard_ui::button_proc, 1, 0);
}

void apply_sort(int field, bool descending)
{
    if (!WritePrivateProfileStringW(L"TCard", L"SortField", std::to_wstring(field).c_str(), preferences_path().c_str()) ||
        !WritePrivateProfileStringW(L"TCard", L"SortDescending", descending ? L"1" : L"0", preferences_path().c_str())) {
        MessageBoxW(g_main, L"Could not save sort settings.", L"TCard", MB_OK | MB_ICONERROR);
        return;
    }
    g_sort_field = field; g_sort_descending = descending;
    fill_list(); refresh_preview();
}

void show_sort()
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const wchar_t* fields[] = {L"Date created", L"Date modified", L"Title"};
    for (int field = 0; field < 3; ++field) {
        HMENU child = CreatePopupMenu();
        if (!child) { DestroyMenu(menu); return; }
        for (int direction = 0; direction < 2; ++direction) {
            const wchar_t* label = field == 2 ? (direction ? L"Z to A" : L"A to Z") :
                (direction ? L"Newest first" : L"Oldest first");
            AppendMenuW(child, MF_STRING, 1400 + field * 2 + direction, label);
        }
        if (field == g_sort_field) CheckMenuRadioItem(child, 0, 1, g_sort_descending ? 1 : 0, MF_BYPOSITION);
        if (!AppendMenuW(menu, MF_POPUP | (field == g_sort_field ? MF_CHECKED : 0), reinterpret_cast<UINT_PTR>(child), fields[field])) {
            DestroyMenu(child); DestroyMenu(menu); return;
        }
    }
    RECT anchor{};
    GetWindowRect(GetDlgItem(g_main, kSortField), &anchor);
    TPMPARAMS placement{}; placement.cbSize = sizeof(placement); placement.rcExclude = anchor;
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
        anchor.left, anchor.top, g_main, &placement);
    DestroyMenu(menu);
    if (command >= 1400 && command < 1406) apply_sort(static_cast<int>((command - 1400) / 2), (command - 1400) % 2 != 0);
}
