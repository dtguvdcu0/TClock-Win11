#include "tcalendar_host.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include "WebView2.h"
#include <wrl.h>

#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cwchar>
#include <cwctype>

#include "task_store.h"
#include "resource.h"

namespace {

constexpr const wchar_t* kTCalendarWindowClassName = L"TCalendarStandaloneWindowClass";
constexpr const wchar_t* kTCalendarUiMutexName = L"Local\\TCalendar.Win10mod.UIInstance";
constexpr const wchar_t* kTCalendarAlertMutexName = L"Local\\TCalendar.Win10mod.AlertInstance";
constexpr const wchar_t* kTCalendarIniFileName = L"tcalendar.ini";

struct WindowContext {
    tcalendar::TCalendarHost* host = nullptr;
    std::wstring initial_uri;
    std::wstring webview_user_data_dir;
    std::wstring ini_file_path;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    bool webview_ready = false;
    HRESULT init_hr = E_PENDING;
};

enum class RuntimeMode {
    Ui,
    Alert,
    Smoke,
};

struct RuntimeArgs {
    RuntimeMode mode = RuntimeMode::Ui;
    bool smoke_storage_error_mode = false;
    bool smoke_strict_mode = false;
};

bool ParseDate(const std::wstring& value, int& y, int& m, int& d) {
    if (value.size() != 10 || value[4] != L'-' || value[7] != L'-') return false;
    if (!iswdigit(value[0]) || !iswdigit(value[1]) || !iswdigit(value[2]) || !iswdigit(value[3]) ||
        !iswdigit(value[5]) || !iswdigit(value[6]) || !iswdigit(value[8]) || !iswdigit(value[9])) {
        return false;
    }
    y = (value[0] - L'0') * 1000 + (value[1] - L'0') * 100 + (value[2] - L'0') * 10 + (value[3] - L'0');
    m = (value[5] - L'0') * 10 + (value[6] - L'0');
    d = (value[8] - L'0') * 10 + (value[9] - L'0');
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    return true;
}

bool ParseTimeHM(const std::wstring& value, int& hh, int& mm) {
    if (value.size() != 5 || value[2] != L':') return false;
    if (!iswdigit(value[0]) || !iswdigit(value[1]) || !iswdigit(value[3]) || !iswdigit(value[4])) return false;
    hh = (value[0] - L'0') * 10 + (value[1] - L'0');
    mm = (value[3] - L'0') * 10 + (value[4] - L'0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    return true;
}

time_t FloorToMinute(time_t t) {
    std::tm local_tm{};
    localtime_s(&local_tm, &t);
    local_tm.tm_sec = 0;
    return mktime(&local_tm);
}

bool BuildTaskStartLocalMinute(const tcalendar::TaskItem& task, time_t& out_local_minute) {
    int y = 0, m = 0, d = 0;
    int hh = 0, mm = 0;
    if (!ParseDate(task.date, y, m, d)) return false;
    if (!ParseTimeHM(task.start_time, hh, mm)) return false;

    std::tm tm_local{};
    tm_local.tm_year = y - 1900;
    tm_local.tm_mon = m - 1;
    tm_local.tm_mday = d;
    tm_local.tm_hour = hh;
    tm_local.tm_min = mm;
    tm_local.tm_sec = 0;
    tm_local.tm_isdst = -1;

    const time_t t = mktime(&tm_local);
    if (t == static_cast<time_t>(-1)) return false;
    out_local_minute = t;
    return true;
}

bool IsTaskDateToday(const tcalendar::TaskItem& task, const std::tm& now_tm) {
    int y = 0, m = 0, d = 0;
    if (!ParseDate(task.date, y, m, d)) return false;
    return (y == now_tm.tm_year + 1900) && (m == now_tm.tm_mon + 1) && (d == now_tm.tm_mday);
}

bool ParseRuntimeArgs(int argc, wchar_t** argv, RuntimeArgs& out, std::wstring& out_error) {
    out = RuntimeArgs{};
    out_error.clear();

    bool saw_ui = false;
    bool saw_alert = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg == L"--smoke") {
            out.mode = RuntimeMode::Smoke;
            continue;
        }
        if (arg == L"--smoke-storage-error") {
            out.mode = RuntimeMode::Smoke;
            out.smoke_storage_error_mode = true;
            continue;
        }
        if (arg == L"--smoke-storage-error-strict") {
            out.mode = RuntimeMode::Smoke;
            out.smoke_storage_error_mode = true;
            out.smoke_strict_mode = true;
            continue;
        }
        if (arg == L"--ui") {
            saw_ui = true;
            continue;
        }
        if (arg == L"--alert") {
            saw_alert = true;
            continue;
        }
        out_error = L"Unknown option: " + arg;
        return false;
    }

    if (out.mode == RuntimeMode::Smoke) {
        if (saw_ui || saw_alert) {
            out_error = L"Smoke mode cannot be combined with --ui/--alert";
            return false;
        }
        return true;
    }

    if (saw_ui && saw_alert) {
        out_error = L"--ui and --alert cannot be combined";
        return false;
    }
    if (saw_alert) {
        out.mode = RuntimeMode::Alert;
    } else {
        out.mode = RuntimeMode::Ui;
    }
    return true;
}

std::wstring BuildNotifyBody(const tcalendar::TaskItem& task) {
    std::wstring body;
    body.reserve(512);
    body += task.date;
    if (!task.start_time.empty()) {
        body += L" ";
        body += task.start_time;
    }
    body += L"\n";
    body += task.title;
    if (!task.detail.empty()) {
        body += L"\n";
        body += task.detail;
    }
    return body;
}

std::wstring BuildNotifyKey(const tcalendar::TaskItem& task) {
    std::wstring key;
    key.reserve(task.id.size() + task.date.size() + task.start_time.size() + 3);
    key += task.id;
    key += L"|";
    key += task.date;
    key += L"|";
    key += task.start_time;
    return key;
}

int RunAlertMode(const tcalendar::HostConfig& config) {
    tcalendar::TaskStore store;
    if (!store.Initialize(config.storage_db_path)) {
        std::wstring text = L"TCalendar alert storage init failed.\n\n" + store.GetLastError();
        MessageBoxW(nullptr, text.c_str(), L"TCalendar", MB_OK | MB_ICONERROR);
        return 1;
    }

    int window_minutes = config.alert_scan_window_minutes;
    if (window_minutes < 30) window_minutes = 30;

    int tick_seconds = config.alert_dispatch_tick_seconds;
    if (tick_seconds < 30) tick_seconds = 30;

    int refresh_minutes = config.alert_refresh_minutes;
    if (refresh_minutes < 1) refresh_minutes = 1;

    int grace_minutes = config.alert_grace_minutes;
    if (grace_minutes < 0) grace_minutes = 0;
    if (grace_minutes > 5) grace_minutes = 5;

    std::unordered_set<std::wstring> delivered;
    std::vector<tcalendar::TaskItem> due_queue;

    auto refresh_due_queue = [&]() {
        if (!store.ReloadFromDb()) {
            return;
        }

        const time_t now = time(nullptr);
        const time_t now_minute = FloorToMinute(now);
        const time_t to_time = now_minute + static_cast<time_t>(window_minutes) * 60;

        std::tm from_tm{};
        localtime_s(&from_tm, &now_minute);
        wchar_t from_buf[16] = {0};
        wcsftime(from_buf, _countof(from_buf), L"%Y-%m-%d", &from_tm);

        std::tm to_tm{};
        localtime_s(&to_tm, &to_time);
        wchar_t to_buf[16] = {0};
        wcsftime(to_buf, _countof(to_buf), L"%Y-%m-%d", &to_tm);

        due_queue.clear();
        const auto all = store.GetTasksInRange(from_buf, to_buf);
        due_queue.reserve(all.size());
        for (const auto& t : all) {
            if (t.done) continue;
            if (!t.alert_enabled) continue;
            due_queue.push_back(t);
        }
    };

    std::filesystem::file_time_type last_write = (std::filesystem::file_time_type::min)();
    std::error_code ec;
    if (std::filesystem::exists(config.storage_db_path, ec)) {
        last_write = std::filesystem::last_write_time(config.storage_db_path, ec);
    }

    auto last_full_refresh = std::chrono::steady_clock::now() - std::chrono::minutes(refresh_minutes);
    refresh_due_queue();

    while (true) {
        const auto now_steady = std::chrono::steady_clock::now();
        bool need_refresh = false;

        if (now_steady - last_full_refresh >= std::chrono::minutes(refresh_minutes)) {
            need_refresh = true;
            last_full_refresh = now_steady;
        }

        ec.clear();
        if (std::filesystem::exists(config.storage_db_path, ec)) {
            const auto current_write = std::filesystem::last_write_time(config.storage_db_path, ec);
            if (!ec && current_write != last_write) {
                last_write = current_write;
                need_refresh = true;
            }
        }

        if (need_refresh) {
            refresh_due_queue();
        }

        const time_t now = time(nullptr);
        const time_t now_minute = FloorToMinute(now);
        const time_t floor_min = now_minute - static_cast<time_t>(grace_minutes) * 60;
        std::tm now_tm{};
        localtime_s(&now_tm, &now);

        for (const auto& task : due_queue) {
            const std::wstring key = BuildNotifyKey(task);
            if (delivered.find(key) != delivered.end()) continue;

            if (task.start_time.empty()) {
                if (!IsTaskDateToday(task, now_tm)) continue;
            } else {
                time_t task_minute = 0;
                if (!BuildTaskStartLocalMinute(task, task_minute)) continue;
                if (task_minute < floor_min || task_minute > now_minute) continue;
            }

            const std::wstring body = BuildNotifyBody(task);
            if (config.alert_sound_enabled) {
                const wchar_t* sound_path = config.alert_sound_path.empty()
                    ? L"C:\\Windows\\Media\\notify.wav"
                    : config.alert_sound_path.c_str();
                PlaySoundW(sound_path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            }
            MessageBoxW(nullptr, body.c_str(), L"TClock-Win11", MB_OK | MB_SETFOREGROUND | MB_ICONINFORMATION);
            delivered.insert(key);
        }

        std::this_thread::sleep_for(std::chrono::seconds(tick_seconds));
    }
}

std::wstring BuildFileUriFromPath(const std::wstring& path) {
    std::wstring uri = L"file:///";
    uri.reserve(uri.size() + path.size() + 16);

    for (const wchar_t ch : path) {
        if (ch == L'\\') {
            uri.push_back(L'/');
        } else if (ch == L' ') {
            uri += L"%20";
        } else {
            uri.push_back(ch);
        }
    }
    return uri;
}


std::filesystem::path GetExecutableDirectory() {
    wchar_t module_path[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(module_path).parent_path();
}

std::filesystem::path ResolvePathFromExe(const std::filesystem::path& exe_dir, const std::wstring& raw_path) {
    if (raw_path.empty()) {
        return std::filesystem::path();
    }
    const std::filesystem::path p(raw_path);
    if (p.is_absolute()) {
        return p;
    }
    return exe_dir / p;
}

void EnsureTCalendarIni(const std::filesystem::path& exe_dir) {
    const std::filesystem::path ini_path = exe_dir / kTCalendarIniFileName;
    if (std::filesystem::exists(ini_path)) {
        return;
    }

    WritePrivateProfileStringW(L"TCalendar", L"Skin", L"default", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"TemplateDefault", L"tcalendar\\template\\default\\index.html", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"TemplateUser", L"tcalendar\\template\\user\\index.html", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"StorageDbPath", L"tcalendar\\data\\tasks.db", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"WindowWidth", L"960", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"WindowHeight", L"640", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"BlockExternalNavigation", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"EnableWebView2Bootstrap", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"DefaultViewMode", L"list", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"DefaultRangePresetDays", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"DefaultCustomRangeDays", L"7", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"DefaultUseCustomRange", L"0", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiFontFamily", L"Segoe UI", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiBaseFontSize", L"14", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiCalendarDateFontSize", L"13", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiTaskFontSize", L"14", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiPanelRightWidth", L"420", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiCalendarHeight", L"420", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"UiShowTaskPanel", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertScanWindowMinutes", L"120", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertDispatchTickSeconds", L"60", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertRefreshMinutes", L"10", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertGraceMinutes", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertSoundEnabled", L"1", ini_path.wstring().c_str());
    WritePrivateProfileStringW(L"TCalendar", L"AlertSoundPath", L"C:\\Windows\\Media\\notify.wav", ini_path.wstring().c_str());
}

void LoadHostConfigFromIni(const std::filesystem::path& exe_dir, bool smoke_mode, bool smoke_storage_error_mode, tcalendar::HostConfig& out_config) {
    EnsureTCalendarIni(exe_dir);

    const std::filesystem::path runtime_root = exe_dir / L"tcalendar";
    std::error_code ec;
    std::filesystem::create_directories(runtime_root / L"data", ec);
    std::filesystem::create_directories(runtime_root / L"template" / L"default", ec);
    std::filesystem::create_directories(runtime_root / L"template" / L"user", ec);

    const std::filesystem::path ini_path = exe_dir / kTCalendarIniFileName;
    const std::wstring ini_file = ini_path.wstring();
    out_config.ini_file_path = ini_file;

    // Migration cleanup: alert startup ownership moved to tclock-win11.ini [TCalendar].
    WritePrivateProfileStringW(L"TCalendar", L"Enable", nullptr, ini_file.c_str());
    WritePrivateProfileStringW(L"TCalendar", L"Alart", nullptr, ini_file.c_str());
    auto ensure_ini_key = [&](const wchar_t* key, const wchar_t* value) {
        wchar_t current[MAX_PATH] = {0};
        GetPrivateProfileStringW(L"TCalendar", key, L"", current, MAX_PATH, ini_file.c_str());
        if (current[0] == L'\0') {
            WritePrivateProfileStringW(L"TCalendar", key, value, ini_file.c_str());
        }
    };
    ensure_ini_key(L"Skin", L"default");
    ensure_ini_key(L"WindowWidth", L"960");
    ensure_ini_key(L"WindowHeight", L"640");
    ensure_ini_key(L"DefaultViewMode", L"list");
    ensure_ini_key(L"DefaultRangePresetDays", L"1");
    ensure_ini_key(L"DefaultCustomRangeDays", L"7");
    ensure_ini_key(L"DefaultUseCustomRange", L"0");
    ensure_ini_key(L"UiFontFamily", L"Segoe UI");
    ensure_ini_key(L"UiBaseFontSize", L"14");
    ensure_ini_key(L"UiCalendarDateFontSize", L"13");
    ensure_ini_key(L"UiTaskFontSize", L"14");
    ensure_ini_key(L"UiPanelRightWidth", L"420");
    ensure_ini_key(L"UiCalendarHeight", L"420");
    ensure_ini_key(L"UiShowTaskPanel", L"1");
    ensure_ini_key(L"AlertScanWindowMinutes", L"120");
    ensure_ini_key(L"AlertDispatchTickSeconds", L"60");
    ensure_ini_key(L"AlertRefreshMinutes", L"10");
    ensure_ini_key(L"AlertGraceMinutes", L"1");
    ensure_ini_key(L"AlertSoundEnabled", L"1");
    ensure_ini_key(L"AlertSoundPath", L"C:\\Windows\\Media\\notify.wav");

    wchar_t buf[MAX_PATH] = {0};
    wchar_t skin[MAX_PATH] = {0};
    GetPrivateProfileStringW(L"TCalendar", L"Skin", L"default", skin, MAX_PATH, ini_file.c_str());

    GetPrivateProfileStringW(L"TCalendar", L"TemplateDefault", L"", buf, MAX_PATH, ini_file.c_str());
    if (buf[0] == L'\0') {
        std::wstring skin_name = skin[0] ? skin : L"default";
        const std::wstring default_template = std::wstring(L"tcalendar\\template\\") + skin_name + L"\\index.html";
        out_config.default_template_path = ResolvePathFromExe(exe_dir, default_template).wstring();
    } else {
        out_config.default_template_path = ResolvePathFromExe(exe_dir, buf).wstring();
    }

    GetPrivateProfileStringW(L"TCalendar", L"TemplateUser", L"tcalendar\\template\\user\\index.html", buf, MAX_PATH, ini_file.c_str());
    out_config.user_template_path = ResolvePathFromExe(exe_dir, buf).wstring();

    GetPrivateProfileStringW(L"TCalendar", L"StorageDbPath", L"tcalendar\\data\\tasks.db", buf, MAX_PATH, ini_file.c_str());
    std::filesystem::path db_path = ResolvePathFromExe(exe_dir, buf);
    if (smoke_mode) {
        db_path = runtime_root / L"data" / L"tasks-smoke.db";
    }
    out_config.storage_db_path = db_path.wstring();

    out_config.block_external_navigation = GetPrivateProfileIntW(L"TCalendar", L"BlockExternalNavigation", 1, ini_file.c_str()) != 0;
    out_config.enable_webview2_bootstrap = GetPrivateProfileIntW(L"TCalendar", L"EnableWebView2Bootstrap", 1, ini_file.c_str()) != 0;
    out_config.test_force_storage_write_failure = smoke_storage_error_mode;

    GetPrivateProfileStringW(L"TCalendar", L"DefaultViewMode", L"list", buf, MAX_PATH, ini_file.c_str());
    std::wstring view_mode = buf;
    if (view_mode != L"list" && view_mode != L"timeline") {
        view_mode = L"list";
    }
    out_config.default_view_mode = view_mode;

    int preset_days = GetPrivateProfileIntW(L"TCalendar", L"DefaultRangePresetDays", 1, ini_file.c_str());
    if (preset_days != 1 && preset_days != 7 && preset_days != 14 && preset_days != 30) {
        preset_days = 1;
    }
    out_config.default_range_preset_days = preset_days;

    int custom_days = GetPrivateProfileIntW(L"TCalendar", L"DefaultCustomRangeDays", 7, ini_file.c_str());
    if (custom_days < 1) custom_days = 1;
    if (custom_days > 365) custom_days = 365;
    out_config.default_custom_range_days = custom_days;
    out_config.default_use_custom_range = GetPrivateProfileIntW(L"TCalendar", L"DefaultUseCustomRange", 0, ini_file.c_str()) != 0;

    GetPrivateProfileStringW(L"TCalendar", L"UiFontFamily", L"Segoe UI", buf, MAX_PATH, ini_file.c_str());
    if (buf[0] == L'\0') {
        out_config.ui_font_family = L"Segoe UI";
    } else {
        out_config.ui_font_family = buf;
    }

    int ui_base_font_size = GetPrivateProfileIntW(L"TCalendar", L"UiBaseFontSize", 14, ini_file.c_str());
    if (ui_base_font_size < 9) ui_base_font_size = 9;
    if (ui_base_font_size > 28) ui_base_font_size = 28;
    out_config.ui_base_font_size = ui_base_font_size;

    int ui_calendar_date_font_size = GetPrivateProfileIntW(L"TCalendar", L"UiCalendarDateFontSize", 13, ini_file.c_str());
    if (ui_calendar_date_font_size < 9) ui_calendar_date_font_size = 9;
    if (ui_calendar_date_font_size > 28) ui_calendar_date_font_size = 28;
    out_config.ui_calendar_date_font_size = ui_calendar_date_font_size;

    int ui_task_font_size = GetPrivateProfileIntW(L"TCalendar", L"UiTaskFontSize", 14, ini_file.c_str());
    if (ui_task_font_size < 9) ui_task_font_size = 9;
    if (ui_task_font_size > 28) ui_task_font_size = 28;
    out_config.ui_task_font_size = ui_task_font_size;

    int ui_panel_right_width = GetPrivateProfileIntW(L"TCalendar", L"UiPanelRightWidth", 420, ini_file.c_str());
    if (ui_panel_right_width < 320) ui_panel_right_width = 320;
    if (ui_panel_right_width > 1600) ui_panel_right_width = 1600;
    out_config.ui_panel_right_width = ui_panel_right_width;

    int ui_calendar_height = GetPrivateProfileIntW(L"TCalendar", L"UiCalendarHeight", 420, ini_file.c_str());
    if (ui_calendar_height < 280) ui_calendar_height = 280;
    if (ui_calendar_height > 1200) ui_calendar_height = 1200;
    out_config.ui_calendar_height = ui_calendar_height;
    out_config.ui_show_task_panel = GetPrivateProfileIntW(L"TCalendar", L"UiShowTaskPanel", 1, ini_file.c_str()) != 0;

    // Ownership note: Alart toggle is stored in tclock-win11.ini [TCalendar], not in tcalendar.ini.
    const std::filesystem::path tclock_ini_path = exe_dir / L"tclock-win11.ini";
    out_config.tclock_ini_file_path = tclock_ini_path.wstring();
    out_config.tclock_alert_enabled =
        GetPrivateProfileIntW(L"TCalendar", L"Alart", 0, out_config.tclock_ini_file_path.c_str()) != 0;

    out_config.enable_task_start_notify = out_config.tclock_alert_enabled;

    out_config.alert_sound_enabled =
        GetPrivateProfileIntW(L"TCalendar", L"AlertSoundEnabled", 1, ini_file.c_str()) != 0;
    GetPrivateProfileStringW(L"TCalendar", L"AlertSoundPath", L"C:\\Windows\\Media\\notify.wav", buf, MAX_PATH, ini_file.c_str());
    if (buf[0] == L'\0') {
        out_config.alert_sound_path = L"C:\\Windows\\Media\\notify.wav";
    } else {
        out_config.alert_sound_path = buf;
    }

    int alert_scan_window_minutes = GetPrivateProfileIntW(L"TCalendar", L"AlertScanWindowMinutes", 120, ini_file.c_str());
    if (alert_scan_window_minutes < 30) alert_scan_window_minutes = 30;
    if (alert_scan_window_minutes > 1440) alert_scan_window_minutes = 1440;
    out_config.alert_scan_window_minutes = alert_scan_window_minutes;

    int alert_dispatch_tick_seconds = GetPrivateProfileIntW(L"TCalendar", L"AlertDispatchTickSeconds", 60, ini_file.c_str());
    if (alert_dispatch_tick_seconds < 30) alert_dispatch_tick_seconds = 30;
    if (alert_dispatch_tick_seconds > 3600) alert_dispatch_tick_seconds = 3600;
    out_config.alert_dispatch_tick_seconds = alert_dispatch_tick_seconds;

    int alert_refresh_minutes = GetPrivateProfileIntW(L"TCalendar", L"AlertRefreshMinutes", 10, ini_file.c_str());
    if (alert_refresh_minutes < 1) alert_refresh_minutes = 1;
    if (alert_refresh_minutes > 1440) alert_refresh_minutes = 1440;
    out_config.alert_refresh_minutes = alert_refresh_minutes;

    int alert_grace_minutes = GetPrivateProfileIntW(L"TCalendar", L"AlertGraceMinutes", 1, ini_file.c_str());
    if (alert_grace_minutes < 0) alert_grace_minutes = 0;
    if (alert_grace_minutes > 5) alert_grace_minutes = 5;
    out_config.alert_grace_minutes = alert_grace_minutes;
}

void LoadWindowSizeFromIni(const std::wstring& ini_file, int& width, int& height) {
    int w = GetPrivateProfileIntW(L"TCalendar", L"WindowWidth", width, ini_file.c_str());
    int h = GetPrivateProfileIntW(L"TCalendar", L"WindowHeight", height, ini_file.c_str());
    if (w >= 320 && w <= 3840) {
        width = w;
    }
    if (h >= 240 && h <= 2160) {
        height = h;
    }
}

void SaveWindowSizeToIni(HWND hwnd, const std::wstring& ini_file) {
    if (!hwnd || ini_file.empty() || IsZoomed(hwnd) || IsIconic(hwnd)) {
        return;
    }

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) {
        return;
    }

    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    if (width < 320 || height < 240) {
        return;
    }

    wchar_t wbuf[16] = {0};
    wchar_t hbuf[16] = {0};
    _snwprintf_s(wbuf, _countof(wbuf), _TRUNCATE, L"%d", width);
    _snwprintf_s(hbuf, _countof(hbuf), _TRUNCATE, L"%d", height);

    WritePrivateProfileStringW(L"TCalendar", L"WindowWidth", wbuf, ini_file.c_str());
    WritePrivateProfileStringW(L"TCalendar", L"WindowHeight", hbuf, ini_file.c_str());
}

void ResizeWebViewToClient(WindowContext* context, HWND hwnd) {
    if (!context || !context->controller) return;

    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    context->controller->put_Bounds(bounds);
}

bool StartWebView2ForWindow(WindowContext* context, HWND hwnd) {
    if (!context) return false;

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        context->webview_user_data_dir.empty() ? nullptr : context->webview_user_data_dir.c_str(),
        nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [context, hwnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                context->init_hr = result;
                if (FAILED(result) || !environment) {
                    return S_OK;
                }

                context->environment = environment;
                return environment->CreateCoreWebView2Controller(
                    hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [context, hwnd](HRESULT controller_result, ICoreWebView2Controller* controller) -> HRESULT {
                            context->init_hr = controller_result;
                            if (FAILED(controller_result) || !controller) {
                                return S_OK;
                            }

                            context->controller = controller;
                            context->controller->get_CoreWebView2(&context->webview);
                            ResizeWebViewToClient(context, hwnd);

                            if (context->webview) {
                                EventRegistrationToken web_message_token{};
                                context->webview->add_WebMessageReceived(
                                    Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [context](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            if (!context || !context->host || !sender || !args) {
                                                return S_OK;
                                            }

                                            LPWSTR message_text = nullptr;
                                            const HRESULT message_hr = args->TryGetWebMessageAsString(&message_text);
                                            if (FAILED(message_hr) || !message_text) {
                                                return S_OK;
                                            }

                                            std::wstring request_json(message_text);
                                            CoTaskMemFree(message_text);

                                            std::wstring response_json;
                                            if (!context->host->HandleWebMessage(request_json, response_json)) {
                                                return S_OK;
                                            }

                                            sender->PostWebMessageAsString(response_json.c_str());
                                            return S_OK;
                                        })
                                        .Get(),
                                    &web_message_token);

                                context->webview->Navigate(context->initial_uri.c_str());
                                context->webview_ready = true;
                            }
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    context->init_hr = hr;
    return SUCCEEDED(hr);
}

void ActivateExistingTCalendarWindow() {
    HWND hwnd = FindWindowW(kTCalendarWindowClassName, nullptr);
    if (!hwnd) {
        return;
    }

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }
    SetForegroundWindow(hwnd);
}

LRESULT CALLBACK TCalendarWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* context = reinterpret_cast<WindowContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }

    auto* context = reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE:
            ResizeWebViewToClient(context, hwnd);
            return 0;

        case WM_EXITSIZEMOVE:
            if (context) {
                SaveWindowSizeToIni(hwnd, context->ini_file_path);
            }
            return 0;

        case WM_SYSCOMMAND:
            if ((wparam & 0xFFF0) == SC_MAXIMIZE) {
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);

        case WM_DESTROY:
            if (context) {
                SaveWindowSizeToIni(hwnd, context->ini_file_path);
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

int RunSmokeMode(tcalendar::TCalendarHost& host, bool force_storage_error_test, bool strict_assert) {
    std::wstring response;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-1","method":"system.getVersion","params":{}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-2","method":"task.create","params":{"date":"2026-02-23","title":"hello"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-2b","method":"task.create","params":{"date":"2026-02-23","title":"quote \"x\" and slash \\"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-3","method":"calendar.getDayTasks","params":{"date":"2026-02-23"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-4","method":"task.toggleDone","params":{"id":"t-1","done":true}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-5","method":"task.create","params":{"date":true,"title":"bad"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-6","method":"task.create","params":{"date":"2026-02-23","title":"broken"})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-7","method":"task.create"})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-8","method":"task.updateTitle","params":{"id":"t-1","title":"hello2"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-9","method":"task.delete","params":{"id":"t-1"}})", response);
    std::wcout << response << std::endl;

    host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-10","method":"calendar.getDayTasks","params":{"date":"2026-02-23"}})", response);
    std::wcout << response << std::endl;

    if (force_storage_error_test) {
        host.HandleWebMessage(LR"({"apiVersion":"1.0","requestId":"smoke-11","method":"task.create","params":{"date":"2026-02-23","title":"forced-storage-error-check"}})", response);
        std::wcout << response << std::endl;

        if (strict_assert) {
            const bool ok_storage = response.find(L"\"requestId\":\"smoke-11\"") != std::wstring::npos &&
                                    response.find(L"\"code\":\"STORAGE_ERROR\"") != std::wstring::npos;
            if (!ok_storage) {
                std::wcerr << L"smoke strict check failed: expected STORAGE_ERROR for smoke-11" << std::endl;
                return 21;
            }
        }
    }

    return 0;
}

int RunStandaloneWindowMode(tcalendar::TCalendarHost& host, const tcalendar::HostConfig& config, const std::filesystem::path& exe_dir, HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TCalendarWndProc;
    wc.hInstance = instance;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kTCalendarWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return 2;
    }

    WindowContext context{};
    context.host = &host;
    context.initial_uri = BuildFileUriFromPath(std::filesystem::absolute(config.default_template_path).wstring());
    context.webview_user_data_dir = (std::filesystem::absolute(config.storage_db_path).parent_path() / L"webview2").wstring();
    context.ini_file_path = (exe_dir / kTCalendarIniFileName).wstring();

    int window_width = 960;
    int window_height = 640;
    LoadWindowSizeFromIni(context.ini_file_path, window_width, window_height);

    HWND hwnd = CreateWindowExW(
        0,
        kTCalendarWindowClassName,
        L"TCalendar",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance,
        &context);

    if (!hwnd) {
        UnregisterClassW(kTCalendarWindowClassName, instance);
        return 3;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    if (!StartWebView2ForWindow(&context, hwnd)) {
        DestroyWindow(hwnd);
        UnregisterClassW(kTCalendarWindowClassName, instance);
        return 4;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    context.webview.Reset();
    context.controller.Reset();
    context.environment.Reset();

    UnregisterClassW(kTCalendarWindowClassName, instance);
    return static_cast<int>(msg.wParam);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    RuntimeArgs args{};
    std::wstring arg_error;
    if (!ParseRuntimeArgs(argc, argv, args, arg_error)) {
        MessageBoxW(nullptr, arg_error.c_str(), L"TCalendar", MB_OK | MB_ICONERROR);
        return 2;
    }

    HANDLE single_instance_mutex = nullptr;
    if (args.mode != RuntimeMode::Smoke) {
        HWND console = GetConsoleWindow();
        if (console) {
            ShowWindow(console, SW_HIDE);
        }
        // UI/alert both run without visible console in production launch.
        FreeConsole();

        const wchar_t* mutex_name = (args.mode == RuntimeMode::Alert)
            ? kTCalendarAlertMutexName
            : kTCalendarUiMutexName;

        single_instance_mutex = CreateMutexW(nullptr, FALSE, mutex_name);
        if (single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            if (args.mode == RuntimeMode::Ui) {
                ActivateExistingTCalendarWindow();
            }
            CloseHandle(single_instance_mutex);
            return 0;
        }
    }

    const bool smoke_mode = (args.mode == RuntimeMode::Smoke);

    const std::filesystem::path exe_dir = GetExecutableDirectory();
    tcalendar::HostConfig config{};
    LoadHostConfigFromIni(exe_dir, smoke_mode, args.smoke_storage_error_mode, config);

    int rc = 0;
    if (args.mode == RuntimeMode::Alert) {
        rc = RunAlertMode(config);
        if (single_instance_mutex) {
            CloseHandle(single_instance_mutex);
        }
        return rc;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (smoke_mode) {
        std::error_code ec;
        std::filesystem::remove(config.storage_db_path, ec);
    }

    tcalendar::TCalendarHost host(config);
    if (!host.Initialize()) {
        if (!smoke_mode) {
            const std::wstring detail = host.GetLastInitializeError();
            const std::wstring text = detail.empty()
                ? L"TCalendar initialization failed."
                : (L"TCalendar initialization failed.\n\n" + detail);
            MessageBoxW(nullptr, text.c_str(), L"TCalendar", MB_OK | MB_ICONERROR);
        }
        CoUninitialize();
        if (single_instance_mutex) {
            CloseHandle(single_instance_mutex);
        }
        return 1;
    }

    if (smoke_mode) {
        rc = RunSmokeMode(host, args.smoke_storage_error_mode, args.smoke_strict_mode);
    } else {
        rc = RunStandaloneWindowMode(host, config, exe_dir, GetModuleHandleW(nullptr));
    }

    host.Shutdown();
    CoUninitialize();
    if (single_instance_mutex) {
        CloseHandle(single_instance_mutex);
    }
    return rc;
}
