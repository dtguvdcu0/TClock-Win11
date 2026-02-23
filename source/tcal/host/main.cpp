#include "tcalendar_host.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include "WebView2.h"
#include <wrl.h>

#include <iostream>
#include <string>
#include <filesystem>

namespace {

struct WindowContext {
    tcalendar::TCalendarHost* host = nullptr;
    std::wstring initial_uri;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    bool webview_ready = false;
    HRESULT init_hr = E_PENDING;
};

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
        nullptr,
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

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

int RunSmokeMode(tcalendar::TCalendarHost& host, bool force_storage_error_test, bool strict_assert) {
    std::wcout << L"bootstrapReady=" << (host.IsWebView2BootstrapReady() ? L"true" : L"false") << std::endl;

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

int RunStandaloneWindowMode(tcalendar::TCalendarHost& host, const tcalendar::HostConfig& config, HINSTANCE instance) {
    const wchar_t* kClassName = L"TCalendarStandaloneWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TCalendarWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        return 2;
    }

    WindowContext context{};
    context.host = &host;
    context.initial_uri = BuildFileUriFromPath(std::filesystem::absolute(config.default_template_path).wstring());

    HWND hwnd = CreateWindowExW(
        0,
        kClassName,
        L"TCalendar",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        960,
        640,
        nullptr,
        nullptr,
        instance,
        &context);

    if (!hwnd) {
        UnregisterClassW(kClassName, instance);
        return 3;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    std::wcout << L"bootstrapReady=" << (host.IsWebView2BootstrapReady() ? L"true" : L"false") << std::endl;

    if (!StartWebView2ForWindow(&context, hwnd)) {
        DestroyWindow(hwnd);
        UnregisterClassW(kClassName, instance);
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

    UnregisterClassW(kClassName, instance);
    return static_cast<int>(msg.wParam);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool smoke_mode = false;
    bool smoke_storage_error_mode = false;
    bool smoke_strict_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--smoke") {
            smoke_mode = true;
        } else if (std::wstring(argv[i]) == L"--smoke-storage-error") {
            smoke_mode = true;
            smoke_storage_error_mode = true;
        } else if (std::wstring(argv[i]) == L"--smoke-storage-error-strict") {
            smoke_mode = true;
            smoke_storage_error_mode = true;
            smoke_strict_mode = true;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const std::filesystem::path exe_dir = GetExecutableDirectory();

    tcalendar::HostConfig config{};
    config.default_template_path = (exe_dir / "web" / "default" / "index.html").wstring();
    config.user_template_path = (exe_dir / "web" / "user" / "index.html").wstring();
    config.block_external_navigation = true;
    config.enable_webview2_bootstrap = true;
    config.storage_db_path = (exe_dir / "data" / (smoke_mode ? "tasks-smoke.db" : "tasks.db")).wstring();
    config.test_force_storage_write_failure = smoke_storage_error_mode;

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
        return 1;
    }

    int rc = 0;
    if (smoke_mode) {
        rc = RunSmokeMode(host, smoke_storage_error_mode, smoke_strict_mode);
    } else {
        rc = RunStandaloneWindowMode(host, config, GetModuleHandleW(nullptr));
    }

    host.Shutdown();
    CoUninitialize();
    return rc;
}
