#pragma once

#include <string>

#include "task_store.h"

namespace tcalendar {

struct HostConfig {
    std::wstring ini_file_path;
    std::wstring default_template_path;
    std::wstring user_template_path;
    bool block_external_navigation = true;
    bool enable_webview2_bootstrap = true;
    std::wstring storage_db_path;
    bool test_force_storage_write_failure = false;
    std::wstring default_view_mode = L"list"; // "list" or "timeline"
    int default_range_preset_days = 1;        // 1/7/14/30
    int default_custom_range_days = 7;        // for custom mode
    bool default_use_custom_range = false;
};

class TCalendarHost {
public:
    explicit TCalendarHost(const HostConfig& config);
    ~TCalendarHost();

    bool Initialize();
    void Shutdown();

    // Phase 1: wire this to WebView2 message bridge.
    bool HandleWebMessage(const std::wstring& request_json, std::wstring& response_json);

    bool IsWebView2BootstrapReady() const;
    const std::wstring& GetLastInitializeError() const;

private:
    HostConfig config_;
    bool initialized_ = false;
    TaskStore store_;

    void* webview2_loader_module_ = nullptr;
    bool webview2_bootstrap_ready_ = false;
    std::wstring last_initialize_error_;

    bool InitializeWebView2Bootstrap();
    void ShutdownWebView2Bootstrap();

    static std::wstring BuildResponse(bool ok, const wchar_t* code, const wchar_t* message,
                                      const std::wstring& request_id, const wchar_t* data_json);
};

} // namespace tcalendar
