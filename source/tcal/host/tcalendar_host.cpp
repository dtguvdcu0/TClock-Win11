#include "tcalendar_host.h"
#include "bridge_contract.h"

#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace tcalendar {

namespace {

using CreateCoreWebView2EnvironmentWithOptionsFn = HRESULT (STDAPICALLTYPE*)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    void* environmentOptions,
    void* environment_created_handler);

std::wstring EscapeJsonString(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        switch (ch) {
            case 0x5C:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(static_cast<wchar_t>(0x5C));
                break;
            case 0x22:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(static_cast<wchar_t>(0x22));
                break;
            case 0x08:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(L'b');
                break;
            case 0x0C:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(L'f');
                break;
            case 0x0A:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(L'n');
                break;
            case 0x0D:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(L'r');
                break;
            case 0x09:
                out.push_back(static_cast<wchar_t>(0x5C));
                out.push_back(L't');
                break;
            default:
                if (ch >= 0 && ch < 0x20) {
                    wchar_t buf[7] = {0};
                    swprintf(buf, 7, L"%lc%04X", static_cast<wchar_t>(0x5C), static_cast<unsigned int>(ch));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

bool ParseTimeToMinutes(const std::wstring& value, int& out_minutes) {
    if (value.empty()) return false;
    if (value.size() != 5 || value[2] != L':') return false;
    if (value[0] < L'0' || value[0] > L'9' || value[1] < L'0' || value[1] > L'9' ||
        value[3] < L'0' || value[3] > L'9' || value[4] < L'0' || value[4] > L'9') {
        return false;
    }
    const int hh = (value[0] - L'0') * 10 + (value[1] - L'0');
    const int mm = (value[3] - L'0') * 10 + (value[4] - L'0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    out_minutes = hh * 60 + mm;
    return true;
}

bool ValidateTaskTimes(const std::wstring& start_time, const std::wstring& end_time, std::wstring& out_message) {
    out_message.clear();
    if (start_time.empty() && end_time.empty()) return true;
    if (!start_time.empty() && end_time.empty()) {
        int dummy = 0;
        if (!ParseTimeToMinutes(start_time, dummy)) {
            out_message = L"Invalid startTime format";
            return false;
        }
        return true;
    }
    if (start_time.empty() && !end_time.empty()) {
        out_message = L"endTime requires startTime";
        return false;
    }

    int start_minutes = 0;
    int end_minutes = 0;
    if (!ParseTimeToMinutes(start_time, start_minutes)) {
        out_message = L"Invalid startTime format";
        return false;
    }
    if (!ParseTimeToMinutes(end_time, end_minutes)) {
        out_message = L"Invalid endTime format";
        return false;
    }
    if (end_minutes < start_minutes) {
        out_message = L"endTime must be after startTime";
        return false;
    }
    return true;
}

} // namespace

TCalendarHost::TCalendarHost(const HostConfig& config) : config_(config) {}

TCalendarHost::~TCalendarHost() {
    Shutdown();
}

bool TCalendarHost::Initialize() {
    if (initialized_) return true;

    last_initialize_error_.clear();

    if (!config_.default_template_path.empty() &&
        !std::filesystem::exists(config_.default_template_path)) {
        last_initialize_error_ = L"default template not found: " + config_.default_template_path;
        return false;
    }

    if (config_.enable_webview2_bootstrap && !InitializeWebView2Bootstrap()) {
        last_initialize_error_ = L"WebView2 bootstrap failed (WebView2Loader.dll not available)";
        return false;
    }

    if (config_.storage_db_path.empty()) {
        last_initialize_error_ = L"storage_db_path is empty";
        return false;
    }

    if (!store_.Initialize(config_.storage_db_path)) {
        last_initialize_error_ = L"task store initialize failed: " + store_.GetLastError();
        return false;
    }
    store_.SetTestForceWriteFailure(config_.test_force_storage_write_failure);

    // Phase 1 implementation target:
    // 1) Create WebView2 environment/controller.
    // 2) Load template from default/user path.
    // 3) Register bridge callback.
    initialized_ = true;
    return true;
}

void TCalendarHost::Shutdown() {
    if (!initialized_) return;

    // Phase 1 implementation target:
    // release WebView2 resources and flush pending state.
    store_.Shutdown();
    ShutdownWebView2Bootstrap();
    initialized_ = false;
}

bool TCalendarHost::InitializeWebView2Bootstrap() {
    if (webview2_bootstrap_ready_) return true;

    if (webview2_loader_module_) {
        FreeLibrary(static_cast<HMODULE>(webview2_loader_module_));
        webview2_loader_module_ = nullptr;
    }

    HMODULE module = LoadLibraryW(L"WebView2Loader.dll");
    if (!module) {
        return false;
    }

    const FARPROC proc = GetProcAddress(module, "CreateCoreWebView2EnvironmentWithOptions");
    if (!proc) {
        FreeLibrary(module);
        return false;
    }

    const auto create_env = reinterpret_cast<CreateCoreWebView2EnvironmentWithOptionsFn>(proc);
    if (!create_env) {
        FreeLibrary(module);
        return false;
    }

    webview2_loader_module_ = module;
    webview2_bootstrap_ready_ = true;
    return true;
}

void TCalendarHost::ShutdownWebView2Bootstrap() {
    if (webview2_loader_module_) {
        FreeLibrary(static_cast<HMODULE>(webview2_loader_module_));
        webview2_loader_module_ = nullptr;
    }
    webview2_bootstrap_ready_ = false;
}

bool TCalendarHost::IsWebView2BootstrapReady() const {
    return webview2_bootstrap_ready_;
}

const std::wstring& TCalendarHost::GetLastInitializeError() const {
    return last_initialize_error_;
}

std::wstring TCalendarHost::BuildResponse(bool ok, const wchar_t* code, const wchar_t* message,
                                          const std::wstring& request_id, const wchar_t* data_json) {
    const std::wstring code_safe = EscapeJsonString(code ? code : L"");
    const std::wstring message_safe = EscapeJsonString(message ? message : L"");
    const std::wstring request_safe = EscapeJsonString(request_id);
    return std::wstring(L"{\"ok\":") + (ok ? L"true" : L"false") +
           L",\"code\":\"" + code_safe +
           L"\",\"message\":\"" + message_safe +
           L"\",\"requestId\":\"" + request_safe +
           L"\",\"data\":" + (data_json ? data_json : L"null") + L"}";
}

bool TCalendarHost::HandleWebMessage(const std::wstring& request_json, std::wstring& response_json) {
    if (!initialized_) return false;

    BridgeRequest req;
    std::wstring err_code;
    std::wstring err_message;
    if (!ValidateBridgeRequest(request_json, req, err_code, err_message)) {
        response_json = BuildResponse(false, err_code.c_str(), err_message.c_str(), L"", L"null");
        return true;
    }

    if (req.api_version != L"1.0") {
        response_json = BuildResponse(false, L"UNSUPPORTED_API_VERSION", L"Unsupported apiVersion", req.request_id, L"null");
        return true;
    }

    if (req.method == L"system.getVersion") {
        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"version\":\"1.0\"}");
        return true;
    }

    if (req.method == L"system.getViewConfig") {
        const std::wstring data =
            std::wstring(L"{\"defaultViewMode\":\"") + EscapeJsonString(config_.default_view_mode) +
            L"\",\"defaultRangePresetDays\":" + std::to_wstring(config_.default_range_preset_days) +
            L",\"defaultCustomRangeDays\":" + std::to_wstring(config_.default_custom_range_days) +
            L",\"defaultUseCustomRange\":" + (config_.default_use_custom_range ? L"true" : L"false") +
            L",\"uiFontFamily\":\"" + EscapeJsonString(config_.ui_font_family) +
            L"\",\"uiBaseFontSize\":" + std::to_wstring(config_.ui_base_font_size) +
            L",\"uiCalendarDateFontSize\":" + std::to_wstring(config_.ui_calendar_date_font_size) +
            L",\"uiTaskFontSize\":" + std::to_wstring(config_.ui_task_font_size) +
            L",\"uiPanelRightWidth\":" + std::to_wstring(config_.ui_panel_right_width) +
            L",\"uiCalendarHeight\":" + std::to_wstring(config_.ui_calendar_height) +
            L",\"uiShowTaskPanel\":" + (config_.ui_show_task_panel ? L"true" : L"false") +
            L",\"tclockAlertEnabled\":" + (config_.tclock_alert_enabled ? L"true" : L"false") +
            L",\"alertSoundEnabled\":" + (config_.alert_sound_enabled ? L"true" : L"false") +
            L",\"alertSoundPath\":\"" + EscapeJsonString(config_.alert_sound_path) + L"\"" +
            L"}";
        response_json = BuildResponse(true, L"OK", L"", req.request_id, data.c_str());
        return true;
    }

    const JsonObject* params = nullptr;
    if (!GetObjectField(req.root, L"params", params)) {
        response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing or invalid params object", req.request_id, L"null");
        return true;
    }

    if (req.method == L"system.setViewConfig") {
        std::wstring view_mode;
        std::wstring range_preset;
        std::wstring custom_days_raw;
        std::wstring ui_font_family;
        std::wstring ui_base_font_size_raw;
        std::wstring ui_calendar_date_font_size_raw;
        std::wstring ui_task_font_size_raw;
        std::wstring ui_panel_right_width_raw;
        std::wstring ui_calendar_height_raw;
        std::wstring ui_show_task_panel_raw;
        std::wstring tclock_alert_enabled_raw;
        std::wstring alert_sound_enabled_raw;
        std::wstring alert_sound_path_raw;
        if (!GetStringField(*params, L"defaultViewMode", view_mode) ||
            !GetStringField(*params, L"rangePreset", range_preset)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing defaultViewMode/rangePreset in params", req.request_id, L"null");
            return true;
        }

        if (view_mode != L"list" && view_mode != L"timeline") {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Invalid defaultViewMode", req.request_id, L"null");
            return true;
        }

        bool use_custom_range = false;
        int preset_days = 1;
        if (range_preset == L"custom") {
            use_custom_range = true;
        } else {
            const int candidate = _wtoi(range_preset.c_str());
            if (candidate != 1 && candidate != 7 && candidate != 14 && candidate != 30) {
                response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Invalid rangePreset", req.request_id, L"null");
                return true;
            }
            preset_days = candidate;
        }

        int custom_days = config_.default_custom_range_days;
        if (GetStringField(*params, L"customRangeDays", custom_days_raw)) {
            const int parsed_custom_days = _wtoi(custom_days_raw.c_str());
            if (parsed_custom_days >= 1 && parsed_custom_days <= 365) {
                custom_days = parsed_custom_days;
            }
        }

        config_.default_view_mode = view_mode;
        config_.default_range_preset_days = preset_days;
        config_.default_use_custom_range = use_custom_range;
        config_.default_custom_range_days = custom_days;

        if (GetStringField(*params, L"uiFontFamily", ui_font_family) && !ui_font_family.empty()) {
            config_.ui_font_family = ui_font_family;
        }
        if (GetStringField(*params, L"uiBaseFontSize", ui_base_font_size_raw)) {
            int n = _wtoi(ui_base_font_size_raw.c_str());
            if (n < 9) n = 9;
            if (n > 28) n = 28;
            if (n > 0) config_.ui_base_font_size = n;
        }
        if (GetStringField(*params, L"uiCalendarDateFontSize", ui_calendar_date_font_size_raw)) {
            int n = _wtoi(ui_calendar_date_font_size_raw.c_str());
            if (n < 9) n = 9;
            if (n > 28) n = 28;
            if (n > 0) config_.ui_calendar_date_font_size = n;
        }
        if (GetStringField(*params, L"uiTaskFontSize", ui_task_font_size_raw)) {
            int n = _wtoi(ui_task_font_size_raw.c_str());
            if (n < 9) n = 9;
            if (n > 28) n = 28;
            if (n > 0) config_.ui_task_font_size = n;
        }
        if (GetStringField(*params, L"uiPanelRightWidth", ui_panel_right_width_raw)) {
            int n = _wtoi(ui_panel_right_width_raw.c_str());
            if (n < 320) n = 320;
            if (n > 1600) n = 1600;
            if (n > 0) config_.ui_panel_right_width = n;
        }
        if (GetStringField(*params, L"uiCalendarHeight", ui_calendar_height_raw)) {
            int n = _wtoi(ui_calendar_height_raw.c_str());
            if (n < 280) n = 280;
            if (n > 1200) n = 1200;
            if (n > 0) config_.ui_calendar_height = n;
        }
        if (GetStringField(*params, L"uiShowTaskPanel", ui_show_task_panel_raw)) {
            config_.ui_show_task_panel = (ui_show_task_panel_raw != L"0" && ui_show_task_panel_raw != L"false");
        }
        if (GetStringField(*params, L"tclockAlertEnabled", tclock_alert_enabled_raw)) {
            config_.tclock_alert_enabled = (tclock_alert_enabled_raw != L"0" && tclock_alert_enabled_raw != L"false");
        }
        if (GetStringField(*params, L"alertSoundEnabled", alert_sound_enabled_raw)) {
            config_.alert_sound_enabled = (alert_sound_enabled_raw != L"0" && alert_sound_enabled_raw != L"false");
        }
        if (GetStringField(*params, L"alertSoundPath", alert_sound_path_raw)) {
            config_.alert_sound_path = alert_sound_path_raw.empty()
                ? L"C:\\Windows\\Media\\notify.wav"
                : alert_sound_path_raw;
        }

        if (!config_.ini_file_path.empty()) {
            wchar_t preset_buf[16] = {0};
            wchar_t custom_buf[16] = {0};
            wchar_t base_font_buf[16] = {0};
            wchar_t calendar_font_buf[16] = {0};
            wchar_t task_font_buf[16] = {0};
            wchar_t panel_right_buf[16] = {0};
            wchar_t calendar_height_buf[16] = {0};
            _snwprintf_s(preset_buf, _countof(preset_buf), _TRUNCATE, L"%d", config_.default_range_preset_days);
            _snwprintf_s(custom_buf, _countof(custom_buf), _TRUNCATE, L"%d", config_.default_custom_range_days);
            _snwprintf_s(base_font_buf, _countof(base_font_buf), _TRUNCATE, L"%d", config_.ui_base_font_size);
            _snwprintf_s(calendar_font_buf, _countof(calendar_font_buf), _TRUNCATE, L"%d", config_.ui_calendar_date_font_size);
            _snwprintf_s(task_font_buf, _countof(task_font_buf), _TRUNCATE, L"%d", config_.ui_task_font_size);
            _snwprintf_s(panel_right_buf, _countof(panel_right_buf), _TRUNCATE, L"%d", config_.ui_panel_right_width);
            _snwprintf_s(calendar_height_buf, _countof(calendar_height_buf), _TRUNCATE, L"%d", config_.ui_calendar_height);

            const wchar_t* use_custom = config_.default_use_custom_range ? L"1" : L"0";
            const wchar_t* show_task_panel = config_.ui_show_task_panel ? L"1" : L"0";
            const wchar_t* alert_sound_enabled = config_.alert_sound_enabled ? L"1" : L"0";
            bool write_ok = true;
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"DefaultViewMode", config_.default_view_mode.c_str(), config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"DefaultRangePresetDays", preset_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"DefaultCustomRangeDays", custom_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"DefaultUseCustomRange", use_custom, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiFontFamily", config_.ui_font_family.c_str(), config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiBaseFontSize", base_font_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiCalendarDateFontSize", calendar_font_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiTaskFontSize", task_font_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiPanelRightWidth", panel_right_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiCalendarHeight", calendar_height_buf, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"UiShowTaskPanel", show_task_panel, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"AlertSoundEnabled", alert_sound_enabled, config_.ini_file_path.c_str()) != FALSE);
            write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"AlertSoundPath", config_.alert_sound_path.c_str(), config_.ini_file_path.c_str()) != FALSE);
            if (!config_.tclock_ini_file_path.empty()) {
                const wchar_t* tclock_alert = config_.tclock_alert_enabled ? L"1" : L"0";
                // Persist alert startup toggle to tclock-win11.ini [TCalendar].
                write_ok = write_ok && (WritePrivateProfileStringW(L"TCalendar", L"Alart", tclock_alert, config_.tclock_ini_file_path.c_str()) != FALSE);
            }
            if (!write_ok) {
                response_json = BuildResponse(false, L"STORAGE_ERROR", L"Failed to write view config to ini", req.request_id, L"null");
                return true;
            }
        }

        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"saved\":true}");
        return true;
    }

    if (req.method == L"task.create") {
        std::wstring date;
        std::wstring title;
        std::wstring detail;
        std::wstring start_time;
        std::wstring end_time;
        bool alert_enabled = false;
        TaskItem t{};
        if (!GetStringField(*params, L"date", date) ||
            !GetStringField(*params, L"title", title)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing date/title in params", req.request_id, L"null");
            return true;
        }
        GetStringField(*params, L"detail", detail);
        GetStringField(*params, L"startTime", start_time);
        GetStringField(*params, L"endTime", end_time);
        GetBoolField(*params, L"alertEnabled", alert_enabled);
        std::wstring time_error;
        if (!ValidateTaskTimes(start_time, end_time, time_error)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", time_error.c_str(), req.request_id, L"null");
            return true;
        }
        if (!store_.CreateTask(date, title, detail, start_time, end_time, alert_enabled, t)) {
            const std::wstring store_error = store_.GetLastError();
            if (store_error == L"invalid task payload") {
                response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Invalid task payload", req.request_id, L"null");
            } else {
                const std::wstring message = store_error.empty() ? L"Task store error" : store_error;
                response_json = BuildResponse(false, L"STORAGE_ERROR", message.c_str(), req.request_id, L"null");
            }
            return true;
        }
        std::wstring data = std::wstring(L"{\"id\":\"") + EscapeJsonString(t.id) +
                            L"\",\"date\":\"" + EscapeJsonString(t.date) +
                            L"\",\"title\":\"" + EscapeJsonString(t.title) +
                            L"\",\"detail\":\"" + EscapeJsonString(t.detail) +
                            L"\",\"startTime\":\"" + EscapeJsonString(t.start_time) +
                            L"\",\"endTime\":\"" + EscapeJsonString(t.end_time) +
                            L"\",\"done\":false" +
                            L",\"alertEnabled\":" + (t.alert_enabled ? L"true" : L"false") + L"}";
        response_json = BuildResponse(true, L"OK", L"", req.request_id, data.c_str());
        return true;
    }

    if (req.method == L"task.toggleDone") {
        std::wstring id;
        bool done = false;
        if (!GetStringField(*params, L"id", id) ||
            !GetBoolField(*params, L"done", done)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing id/done in params", req.request_id, L"null");
            return true;
        }
        if (!store_.ToggleDone(id, done)) {
            const std::wstring store_error = store_.GetLastError();
            if (store_error == L"task not found") {
                response_json = BuildResponse(false, L"NOT_FOUND", L"Task not found", req.request_id, L"null");
            } else {
                const std::wstring message = store_error.empty() ? L"Task store error" : store_error;
                response_json = BuildResponse(false, L"STORAGE_ERROR", message.c_str(), req.request_id, L"null");
            }
            return true;
        }
        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"id\":\"updated\"}");
        return true;
    }

    if (req.method == L"task.delete") {
        std::wstring id;
        if (!GetStringField(*params, L"id", id)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing id in params", req.request_id, L"null");
            return true;
        }
        if (!store_.DeleteTask(id)) {
            const std::wstring store_error = store_.GetLastError();
            if (store_error == L"task not found") {
                response_json = BuildResponse(false, L"NOT_FOUND", L"Task not found", req.request_id, L"null");
            } else {
                const std::wstring message = store_error.empty() ? L"Task store error" : store_error;
                response_json = BuildResponse(false, L"STORAGE_ERROR", message.c_str(), req.request_id, L"null");
            }
            return true;
        }
        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"id\":\"deleted\"}");
        return true;
    }

    if (req.method == L"task.update") {
        std::wstring id;
        std::wstring title;
        std::wstring detail;
        std::wstring start_time;
        std::wstring end_time;
        bool alert_enabled = false;
        if (!GetStringField(*params, L"id", id) ||
            !GetStringField(*params, L"title", title)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing id/title in params", req.request_id, L"null");
            return true;
        }
        GetStringField(*params, L"detail", detail);
        GetStringField(*params, L"startTime", start_time);
        GetStringField(*params, L"endTime", end_time);
        GetBoolField(*params, L"alertEnabled", alert_enabled);
        std::wstring time_error;
        if (!ValidateTaskTimes(start_time, end_time, time_error)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", time_error.c_str(), req.request_id, L"null");
            return true;
        }
        if (!store_.UpdateTask(id, title, detail, start_time, end_time, alert_enabled)) {
            const std::wstring store_error = store_.GetLastError();
            if (store_error == L"task not found") {
                response_json = BuildResponse(false, L"NOT_FOUND", L"Task not found", req.request_id, L"null");
            } else if (store_error == L"invalid task payload") {
                response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Invalid task payload", req.request_id, L"null");
            } else {
                const std::wstring message = store_error.empty() ? L"Task store error" : store_error;
                response_json = BuildResponse(false, L"STORAGE_ERROR", message.c_str(), req.request_id, L"null");
            }
            return true;
        }
        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"id\":\"updated\"}");
        return true;
    }

    if (req.method == L"task.updateTitle") {
        std::wstring id;
        std::wstring title;
        if (!GetStringField(*params, L"id", id) ||
            !GetStringField(*params, L"title", title)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing id/title in params", req.request_id, L"null");
            return true;
        }
        if (!store_.UpdateTitle(id, title)) {
            const std::wstring store_error = store_.GetLastError();
            if (store_error == L"task not found") {
                response_json = BuildResponse(false, L"NOT_FOUND", L"Task not found", req.request_id, L"null");
            } else if (store_error == L"invalid task payload") {
                response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Invalid task payload", req.request_id, L"null");
            } else {
                const std::wstring message = store_error.empty() ? L"Task store error" : store_error;
                response_json = BuildResponse(false, L"STORAGE_ERROR", message.c_str(), req.request_id, L"null");
            }
            return true;
        }
        response_json = BuildResponse(true, L"OK", L"", req.request_id, L"{\"id\":\"updated\"}");
        return true;
    }

    if (req.method == L"calendar.getDayTasks") {
        std::wstring date;
        if (!GetStringField(*params, L"date", date)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing date in params", req.request_id, L"null");
            return true;
        }
        const auto tasks = store_.GetDayTasks(date);
        std::wstring data = L"{\"items\":[";
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (i) data += L",";
            data += std::wstring(L"{\"id\":\"") + EscapeJsonString(tasks[i].id) + L"\",\"date\":\"" + EscapeJsonString(tasks[i].date) +
                    L"\",\"title\":\"" + EscapeJsonString(tasks[i].title) +
                    L"\",\"detail\":\"" + EscapeJsonString(tasks[i].detail) +
                    L"\",\"startTime\":\"" + EscapeJsonString(tasks[i].start_time) +
                    L"\",\"endTime\":\"" + EscapeJsonString(tasks[i].end_time) +
                    L"\",\"done\":" + (tasks[i].done ? L"true" : L"false") +
                    L",\"alertEnabled\":" + (tasks[i].alert_enabled ? L"true" : L"false") + L"}";
        }
        data += L"]}";
        response_json = BuildResponse(true, L"OK", L"", req.request_id, data.c_str());
        return true;
    }

    if (req.method == L"calendar.getRangeTasks") {
        std::wstring date_from;
        std::wstring date_to;
        if (!GetStringField(*params, L"dateFrom", date_from) ||
            !GetStringField(*params, L"dateTo", date_to)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing dateFrom/dateTo in params", req.request_id, L"null");
            return true;
        }
        const auto tasks = store_.GetTasksInRange(date_from, date_to);
        std::wstring data = L"{\"items\":[";
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (i) data += L",";
            data += std::wstring(L"{\"id\":\"") + EscapeJsonString(tasks[i].id) + L"\",\"date\":\"" + EscapeJsonString(tasks[i].date) +
                    L"\",\"title\":\"" + EscapeJsonString(tasks[i].title) +
                    L"\",\"detail\":\"" + EscapeJsonString(tasks[i].detail) +
                    L"\",\"startTime\":\"" + EscapeJsonString(tasks[i].start_time) +
                    L"\",\"endTime\":\"" + EscapeJsonString(tasks[i].end_time) +
                    L"\",\"done\":" + (tasks[i].done ? L"true" : L"false") +
                    L",\"alertEnabled\":" + (tasks[i].alert_enabled ? L"true" : L"false") + L"}";
        }
        data += L"]}";
        response_json = BuildResponse(true, L"OK", L"", req.request_id, data.c_str());
        return true;
    }

    response_json = BuildResponse(false, L"NOT_IMPLEMENTED", L"Method not implemented", req.request_id, L"null");
    return true;
}

} // namespace tcalendar
