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

    const JsonObject* params = nullptr;
    if (!GetObjectField(req.root, L"params", params)) {
        response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing or invalid params object", req.request_id, L"null");
        return true;
    }

    if (req.method == L"task.create") {
        std::wstring date;
        std::wstring title;
        TaskItem t{};
        if (!GetStringField(*params, L"date", date) ||
            !GetStringField(*params, L"title", title)) {
            response_json = BuildResponse(false, L"VALIDATION_ERROR", L"Missing date/title in params", req.request_id, L"null");
            return true;
        }
        if (!store_.CreateTask(date, title, t)) {
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
                            L"\",\"done\":false}";
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
                    L"\",\"title\":\"" + EscapeJsonString(tasks[i].title) + L"\",\"done\":" + (tasks[i].done ? L"true" : L"false") + L"}";
        }
        data += L"]}";
        response_json = BuildResponse(true, L"OK", L"", req.request_id, data.c_str());
        return true;
    }

    response_json = BuildResponse(false, L"NOT_IMPLEMENTED", L"Method not implemented", req.request_id, L"null");
    return true;
}

} // namespace tcalendar
