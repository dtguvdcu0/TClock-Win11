// Passive update checking. A cancellable worker owns networking, parsing and cache I/O.
// The UI reads only a small version snapshot; callbacks never reference UI objects.
#define NOMINMAX
#include "update_notice.h"
#include "../version.h"
#include <shellapi.h>
#include <winhttp.h>
#include <strsafe.h>
#include <string>
#include <memory>
#include <new>
#include <process.h>
#include <shlobj.h>
#include <commctrl.h>
#include <set>
#include <algorithm>
#include <stdexcept>
#undef GetObject
#pragma warning(push, 0)
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#pragma warning(pop)
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "comctl32.lib")

using namespace winrt::Windows::Data::Json;
static const wchar_t update_releaseUrl[] = L"https://github.com/dtguvdcu0/TClock-Win11/releases";
static const ULONGLONG update_day = 24ULL * 60 * 60 * 1000;
struct UpdateVersion { unsigned part[4]; };
struct UpdateRequest {
    LONG refs = 1;
    volatile LONG event = 0;
    HANDLE completed = nullptr;
    DWORD received = 0;
    DWORD status = 0, error = 0;
    HINTERNET session = nullptr, connection = nullptr, handle = nullptr;
    size_t limit = 0;
    char buffer[32768];
    std::string body;
    ~UpdateRequest() {
        if (completed) CloseHandle(completed);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};
struct UpdateWorker {
    LONG refs = 1;
    HANDLE cancel = nullptr;
    SRWLOCK lock = SRWLOCK_INIT;
    unsigned generation = 0;
    wchar_t version[24]{};
    ~UpdateWorker() { if (cancel) CloseHandle(cancel); }
};
struct UpdateState {
    HWND owner = nullptr, page = nullptr;
    bool pageEnglish = false, newer = false;
    UpdateVersion current{};
    std::wstring version;
    UpdateWorker* worker = nullptr;
    unsigned generation = 0;
};
static HANDLE update_thread;
// Explicit lifetime: the application has a custom CRT entry point.
static UpdateState* update_state;
static wchar_t update_menuText[160];

static bool update_ParseVersion(const wchar_t* text, UpdateVersion& result) {
    if (!text || !*text) return false;
    UpdateVersion value{};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned digits = 0, n = 0;
        while (*text >= L'0' && *text <= L'9') {
            if (++digits > 5) return false;
            n = n * 10 + (*text++ - L'0');
            if (n > 65535) return false;
        }
        if (!digits) return false;
        value.part[i] = n;
        if (i < 3) { if (*text++ != L'.') return false; }
        else if (*text) return false;
    }
    result = value;
    return true;
}
static bool update_IsNewer(const UpdateVersion& remote, const UpdateVersion& current) {
    for (unsigned i = 0; i < 4; ++i)
        if (remote.part[i] != current.part[i]) return remote.part[i] > current.part[i];
    return false;
}
static void update_ReleaseRequest(UpdateRequest* request) {
    if (InterlockedDecrement(&request->refs) == 0) delete request;
}
static void CALLBACK update_Complete(HINTERNET, DWORD_PTR context, DWORD event, void* information, DWORD bytes) {
    auto request = reinterpret_cast<UpdateRequest*>(context);
    if (!request) return;
    if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        update_ReleaseRequest(request);
        return;
    }
    if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && information)
        request->error = static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
    if (event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) request->received = bytes;
    if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
        event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
        event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ||
        event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
    {
        InterlockedExchange(&request->event, static_cast<LONG>(event));
        SetEvent(request->completed);
    }
}
static void update_CloseRequest(UpdateRequest* request) {
    // The worker alone closes/uses the request handle. The callback reference
    // survives cancellation until HANDLE_CLOSING, including after update_Stop.
    if (request->handle) WinHttpCloseHandle(request->handle);
    update_ReleaseRequest(request);
}
static UpdateRequest* update_Begin(bool release, const wchar_t* version, const std::wstring& etag) {
    std::wstring headers = release
        ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n"
        : L"Accept: application/json\r\nCache-Control: no-cache\r\n";
    if (!etag.empty()) headers += L"If-None-Match: " + etag + L"\r\n";
    auto request = new(std::nothrow) UpdateRequest;
    if (!request) return nullptr;
    request->completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!request->completed) { delete request; return nullptr; }
    request->limit = release ? 256 * 1024 : 64 * 1024;
    request->session = WinHttpOpen(L"TClock-Win11/" TCLOCK_VER_PRODUCT_STR,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!request->session) { update_CloseRequest(request); return nullptr; }
    WinHttpSetTimeouts(request->session, 3000, 3000, 5000, 5000);
    request->connection = WinHttpConnect(request->session,
        release ? L"api.github.com" : L"dtguvdcu0.github.io", INTERNET_DEFAULT_HTTPS_PORT, 0);
    wchar_t path[192];
    if (release) StringCchPrintfW(path, ARRAYSIZE(path), L"/repos/dtguvdcu0/TClock-Win11/releases/tags/v%ls", version);
    else StringCchCopyW(path, ARRAYSIZE(path), L"/TClock-Win11/updates/manifest.json");
    if (request->connection) request->handle = WinHttpOpenRequest(request->connection,
        L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request->handle) { update_CloseRequest(request); return nullptr; }
    // No credentials, cookies, redirects or weakened certificate validation.
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(request);
    if (!WinHttpSetOption(request->handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(request->handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
        !WinHttpSetOption(request->handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
        WinHttpSetStatusCallback(request->handle, update_Complete,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        update_CloseRequest(request); return nullptr;
    }
    InterlockedIncrement(&request->refs);
    if (!WinHttpSendRequest(request->handle, headers.c_str(), static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, context)) {
        update_CloseRequest(request); return nullptr;
    }
    return request;
}
static std::wstring update_Decode(const std::string& data) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(),
        static_cast<int>(data.size()), nullptr, 0);
    if (!length) throw std::invalid_argument("Invalid UTF-8");
    std::wstring text(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data(),
        static_cast<int>(data.size()), &text[0], length))
        throw std::invalid_argument("Invalid UTF-8");
    return text;
}
static JsonObject update_ParseObject(const std::string& data) {
    auto text = update_Decode(data);
    auto json = JsonObject::Parse(text);
    // The OS parser validates JSON grammar. Also reject duplicate top-level keys,
    // including escaped spellings, so ambiguous manifest identity is never accepted.
    std::set<std::wstring> keys;
    unsigned depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t ch = text[i];
        if (ch == L'{' || ch == L'[') { ++depth; continue; }
        if (ch == L'}' || ch == L']') { --depth; continue; }
        if (ch != L'"') continue;
        size_t start = i++;
        for (; i < text.size(); ++i) {
            if (text[i] == L'\\') { ++i; continue; }
            if (text[i] == L'"') break;
        }
        size_t next = i + 1;
        while (next < text.size() && iswspace(text[next])) ++next;
        if (depth == 1 && next < text.size() && text[next] == L':') {
            auto key = JsonValue::Parse(text.substr(start, i - start + 1)).GetString();
            if (!keys.insert(std::wstring(key.c_str(), key.size())).second)
                throw std::invalid_argument("Duplicate JSON key");
        }
    }
    return json;
}

static bool update_ParseManifest(const std::string& data, std::wstring& version, UpdateVersion& number) {
    if (data.size() > 64 * 1024) return false;
    auto json = update_ParseObject(data);
    if (json.GetNamedNumber(L"schema_version") != 1 ||
        json.GetNamedString(L"product") != L"TClock-Win11" ||
        json.GetNamedString(L"channel") != L"release" ||
        json.GetNamedString(L"architecture") != L"x64") return false;
    auto value = json.GetNamedString(L"version");
    if (value.size() > 23 || wcslen(value.c_str()) != value.size() ||
        !update_ParseVersion(value.c_str(), number)) return false;
    version.assign(value.c_str(), value.size());
    return true;
}
static bool update_ParseRelease(const std::string& data, const std::wstring& version, std::wstring& result) {
    if (data.size() > 256 * 1024) return false;
    auto text = update_Decode(data);
    auto json = update_ParseObject(data);
    if (json.GetNamedBoolean(L"draft") ||
        json.GetNamedString(L"tag_name") != (L"v" + version) ||
        json.GetNamedString(L"html_url") != (std::wstring(update_releaseUrl) + L"/tag/v" + version))
        return false;
    if (json.GetNamedNumber(L"id") <= 0) return false;
    (void)json.GetNamedBoolean(L"prerelease");
    (void)json.GetNamedString(L"name");
    (void)json.GetNamedString(L"published_at");
    auto body = json.GetNamedValue(L"body");
    if (body.ValueType() != JsonValueType::Null && body.ValueType() != JsonValueType::String) return false;
    auto assets = json.GetNamedArray(L"assets");
    const std::wstring prefix = std::wstring(update_releaseUrl) + L"/download/v" + version + L"/";
    for (const auto& entry : assets) {
        auto asset = entry.GetObject();
        if (asset.GetNamedNumber(L"id") <= 0 || asset.GetNamedNumber(L"size") < 0) return false;
        (void)asset.GetNamedString(L"name");
        (void)asset.GetNamedString(L"state");
        (void)asset.GetNamedString(L"content_type");
        auto url = asset.GetNamedString(L"browser_download_url");
        if (std::wstring(url.c_str(), url.size()).compare(0, prefix.size(), prefix) != 0) return false;
        if (asset.HasKey(L"digest")) {
            auto digest = asset.GetNamedValue(L"digest");
            if (digest.ValueType() != JsonValueType::Null && digest.ValueType() != JsonValueType::String) return false;
        }
    }

    // Retain the exact version-bound metadata, including body and optional digest.
    // An empty assets array is valid and does not affect the passive notice.
    result.swap(text);
    return true;
}
static void update_MakeLabel(wchar_t* text, size_t count, bool english) {
    if (!update_state) { *text = 0; return; }
    StringCchPrintfW(text, count,
        english ? L"New version available: v%ls" : L"\u65b0\u3057\u3044\u30d0\u30fc\u30b8\u30e7\u30f3 v%ls \u304c\u3042\u308a\u307e\u3059",
        update_state->version.c_str());
}
static void update_RefreshPage() {
    if (!update_state || !update_state->page) return;
    HWND button = GetDlgItem(update_state->page, UPDATE_NOTICE_COMMAND);
    wchar_t text[160];
    update_MakeLabel(text, ARRAYSIZE(text), update_state->pageEnglish);
    SetWindowTextW(button, text);
    ShowWindow(button, update_state->newer ? SW_SHOWNA : SW_HIDE);
    InvalidateRect(button, nullptr, TRUE);
}
static void update_ReleaseWorker(UpdateWorker* worker) {
    if (InterlockedDecrement(&worker->refs) == 0) delete worker;
}
static void update_Publish(UpdateWorker* worker, const std::wstring& version) {
    AcquireSRWLockExclusive(&worker->lock);
    StringCchCopyW(worker->version, ARRAYSIZE(worker->version), version.c_str());
    ++worker->generation;
    ReleaseSRWLockExclusive(&worker->lock);
}
struct UpdateResponse {
    std::string body;
    std::wstring etag;
    ULONGLONG retryAfter = 0;
    DWORD status = 0, error = 0;
    bool success = false;
};
static std::wstring update_ReadHeader(HINTERNET handle, const wchar_t* name) {
    wchar_t text[1024]{};
    DWORD bytes = sizeof(text);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_CUSTOM, name, text, &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    return text;
}
static ULONGLONG update_GetEpoch() {
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10000000 - 11644473600ULL;
}
static ULONGLONG update_ParseSeconds(const std::wstring& text) {
    if (text.empty() || text.size() > 12) return 0;
    ULONGLONG value = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return 0;
        value = value * 10 + ch - L'0';
    }
    return value;
}
static void update_ReadBackoff(UpdateRequest* request, UpdateResponse& response) {
    if (request->status != 403 && request->status != 429 && request->status != 503) return;
    ULONGLONG now = update_GetEpoch();
    auto retry = update_ReadHeader(request->handle, L"Retry-After");
    ULONGLONG seconds = update_ParseSeconds(retry);
    SYSTEMTIME date{};
    if (!seconds && !retry.empty() && WinHttpTimeToSystemTime(retry.c_str(), &date)) {
        FILETIME fileTime{};
        if (SystemTimeToFileTime(&date, &fileTime)) {
            ULARGE_INTEGER value;
            value.LowPart = fileTime.dwLowDateTime; value.HighPart = fileTime.dwHighDateTime;
            ULONGLONG epoch = value.QuadPart / 10000000 - 11644473600ULL;
            if (epoch > now) seconds = epoch - now;
        }
    }
    ULONGLONG reset = update_ParseSeconds(update_ReadHeader(request->handle, L"X-RateLimit-Reset"));
    if (reset > now) seconds = (std::max)(seconds, reset - now);
    response.retryAfter = seconds ? now + seconds : 0;
}
static UpdateResponse update_Fetch(UpdateWorker* worker, bool release, const wchar_t* version,
    const std::wstring& etag, ULONGLONG deadline) {
    UpdateResponse response;
    std::unique_ptr<UpdateRequest, decltype(&update_CloseRequest)> owned(
        update_Begin(release, version, etag), update_CloseRequest);
    auto request = owned.get();
    if (!request) return response;
    bool done = false;
    HANDLE events[] = {worker->cancel, request->completed};
    while (!done) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) break;
        DWORD wait = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, static_cast<DWORD>(deadline - now));
        if (wait != WAIT_OBJECT_0 + 1) {
            request->error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
            break;
        }
        LONG event = InterlockedExchange(&request->event, 0);
        if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) break;
        if (event == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
            done = !WinHttpReceiveResponse(request->handle, nullptr);
        else if (event == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
            DWORD length = sizeof(request->status);
            if (!WinHttpQueryHeaders(request->handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &request->status, &length, WINHTTP_NO_HEADER_INDEX)) break;
            response.status = request->status;
            response.etag = update_ReadHeader(request->handle, L"ETag");
            update_ReadBackoff(request, response);
            if (request->status == 304) { response.success = !etag.empty(); break; }
            if (request->status != 200) break;
            done = !WinHttpReadData(request->handle, request->buffer, sizeof(request->buffer), nullptr);
        } else if (event == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            if (!request->received) { response.success = true; break; }
            if (request->body.size() + request->received > request->limit) break;
            try { request->body.append(request->buffer, request->received); }
            catch (...) { break; }
            done = !WinHttpReadData(request->handle, request->buffer, sizeof(request->buffer), nullptr);
        }
    }
    wchar_t diagnostic[160];
    StringCchPrintfW(diagnostic, ARRAYSIZE(diagnostic), L"TClock update: %ls HTTP=%lu error=%lu complete=%d.\n",
        release ? L"release" : L"manifest", response.status, request->error, response.success);
    OutputDebugStringW(diagnostic);
    response.error = request->error;
    response.body.swap(request->body);
    return response;
}
struct UpdateCache {
    std::string manifest, release;
    std::wstring manifestEtag, releaseEtag, version;
    ULONGLONG checked = 0, cooldown = 0;
};
static std::wstring update_GetCachePath() {
    wchar_t executable[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (!length || length >= ARRAYSIZE(executable)) return {};
    // Per-user, per-installation cache; never write user INIs or the program directory.
    CharLowerBuffW(executable, length);
    ULONGLONG hash = 14695981039346656037ULL;
    for (DWORD i = 0; i < length; ++i) { hash ^= executable[i]; hash *= 1099511628211ULL; }
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    std::wstring directory(local);
    CoTaskMemFree(local);
    directory += L"\\TClock-Win11";
    CreateDirectoryW(directory.c_str(), nullptr);
    directory += L"\\UpdateCache";
    CreateDirectoryW(directory.c_str(), nullptr);
    wchar_t suffix[40];
    StringCchPrintfW(suffix, ARRAYSIZE(suffix), L"\\%016llx.json", hash);
    return directory + suffix;
}
static bool update_ReadFile(const std::wstring& path, std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    bool valid = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 1024 * 1024;
    if (valid) {
        data.resize(static_cast<size_t>(length.QuadPart));
        DWORD read = 0;
        valid = ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
    }
    CloseHandle(file);
    return valid;
}
static bool update_IsValidator(const std::wstring& value) {
    if (value.size() > 1000) return false;
    for (wchar_t ch : value) if (ch < 32 || ch == 127) return false;
    return true;
}
static void update_LoadCache(const std::wstring& path, UpdateCache& cache) {
    std::string data;
    if (path.empty() || !update_ReadFile(path, data)) return;
    try {
        auto json = update_ParseObject(data);
        if (json.GetNamedNumber(L"cache_schema") != 1 ||
            json.GetNamedString(L"product") != L"TClock-Win11") return;
        UpdateCache candidate;
        double checked = json.GetNamedNumber(L"checked");
        double cooldown = json.GetNamedNumber(L"cooldown");
        if (checked < 0 || checked > 999999999999.0 || cooldown < 0 || cooldown > 999999999999.0) return;
        candidate.checked = static_cast<ULONGLONG>(checked);
        candidate.cooldown = static_cast<ULONGLONG>(cooldown);
        auto manifest = winrt::to_string(json.GetNamedString(L"manifest"));
        if (!manifest.empty()) {
            std::wstring version;
            UpdateVersion number{};
            if (!update_ParseManifest(manifest, version, number)) return;
            candidate.manifest.swap(manifest);
            candidate.version.swap(version);
            candidate.manifestEtag = json.GetNamedString(L"manifest_etag").c_str();
            candidate.releaseEtag = json.GetNamedString(L"release_etag").c_str();
            if (!update_IsValidator(candidate.manifestEtag) || !update_IsValidator(candidate.releaseEtag)) return;
            auto release = winrt::to_string(json.GetNamedString(L"release"));
            std::wstring metadata;
            try {
                if (!release.empty() && update_ParseRelease(release, candidate.version, metadata)) candidate.release.swap(release);
            } catch (...) { /* Bad details never invalidate a valid announcement. */ }
            if (candidate.release.empty()) candidate.releaseEtag.clear();
        }
        cache = std::move(candidate);
    } catch (...) { /* A cache is optional and never authoritative. */ }
}

static void update_SaveCache(const std::wstring& path, const UpdateCache& cache) {
    if (path.empty() || (cache.manifest.empty() && !cache.cooldown)) return;
    try {
        JsonObject json;
        json.SetNamedValue(L"cache_schema", JsonValue::CreateNumberValue(1));
        json.SetNamedValue(L"product", JsonValue::CreateStringValue(L"TClock-Win11"));
        json.SetNamedValue(L"manifest", JsonValue::CreateStringValue(cache.manifest.empty() ? L"" : update_Decode(cache.manifest)));
        json.SetNamedValue(L"release", JsonValue::CreateStringValue(cache.release.empty() ? L"" : update_Decode(cache.release)));
        json.SetNamedValue(L"manifest_etag", JsonValue::CreateStringValue(cache.manifestEtag));
        json.SetNamedValue(L"release_etag", JsonValue::CreateStringValue(cache.releaseEtag));
        json.SetNamedValue(L"checked", JsonValue::CreateNumberValue(static_cast<double>(cache.checked)));
        json.SetNamedValue(L"cooldown", JsonValue::CreateNumberValue(static_cast<double>(cache.cooldown)));
        std::string data = winrt::to_string(json.Stringify());
        std::string old;
        if (update_ReadFile(path, old) && old == data) return;
        std::wstring temporary = path + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        bool valid = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
            written == data.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!valid || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(temporary.c_str());
    } catch (...) { /* Continue using the in-memory result on a read-only profile. */ }
}
static unsigned __stdcall update_Run(void* context) {
    auto worker = static_cast<UpdateWorker*>(context);
    bool apartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartment = true;
        auto path = update_GetCachePath();
        UpdateCache cache;
        update_LoadCache(path, cache);
        if (!cache.version.empty()) update_Publish(worker, cache.version);
        ULONGLONG next = GetTickCount64() + 10000;
        while (WaitForSingleObject(worker->cancel, 0) == WAIT_TIMEOUT) {
            ULONGLONG now = GetTickCount64(), epoch = update_GetEpoch();
            if (now < next || epoch < cache.cooldown) {
                ULONGLONG delay = now < next ? next - now : 60000;
                if (WaitForSingleObject(worker->cancel, static_cast<DWORD>((std::min)(delay, 60000ULL))) != WAIT_TIMEOUT) break;
                continue;
            }
            next = now + update_day;
            ULONGLONG deadline = now + 15000;
            try {
                auto manifest = update_Fetch(worker, false, nullptr, cache.manifestEtag, deadline);
                cache.cooldown = (std::max)(cache.cooldown, manifest.retryAfter);
                if (manifest.status == 304 && manifest.success) manifest.body = cache.manifest;
                std::wstring version;
                UpdateVersion number{};
                if (manifest.success && update_ParseManifest(manifest.body, version, number)) {
                    if (cache.version != version) { cache.release.clear(); cache.releaseEtag.clear(); }
                    cache.version = version;
                    cache.manifest.swap(manifest.body);
                    if (manifest.status != 304) cache.manifestEtag = manifest.etag;
                    cache.checked = update_GetEpoch();
                    update_Publish(worker, version);
                    if (WaitForSingleObject(worker->cancel, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
                        auto release = update_Fetch(worker, true, version.c_str(), cache.releaseEtag, deadline);
                        cache.cooldown = (std::max)(cache.cooldown, release.retryAfter);
                        std::wstring metadata;
                        if (release.status == 304 && release.success) release.body = cache.release;
                        if (release.success && update_ParseRelease(release.body, version, metadata)) {
                            cache.release.swap(release.body);
                            if (release.status != 304) cache.releaseEtag = release.etag;
                        }
                    }
                }
            } catch (...) {
                OutputDebugStringW(L"TClock update: invalid metadata ignored.\n");
            }
            if (WaitForSingleObject(worker->cancel, 0) == WAIT_TIMEOUT) update_SaveCache(path, cache);
        }
    } catch (...) {
        OutputDebugStringW(L"TClock update: background check unavailable.\n");
    }
    if (apartment) winrt::uninit_apartment();
    update_ReleaseWorker(worker);
    return 0;
}
extern "C" void update_Start(HWND owner, BOOL english) {
    (void)english;
    if (update_state || update_thread) return;
    auto state = new(std::nothrow) UpdateState;
    if (!state) return;
    auto worker = new(std::nothrow) UpdateWorker;
    if (!worker) { delete state; return; }
    worker->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!worker->cancel) { delete worker; delete state; return; }
    state->owner = owner;
    state->current = {{TCLOCK_VER_MAJOR, TCLOCK_VER_MINOR, TCLOCK_VER_PATCH, TCLOCK_VER_BUILD}};
    state->worker = worker;
    InterlockedIncrement(&worker->refs);
    update_thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, update_Run, worker, 0, nullptr));
    if (!update_thread) { update_ReleaseWorker(worker); update_ReleaseWorker(worker); delete state; return; }
    update_state = state;
    SetTimer(owner, UPDATE_NOTICE_TIMER, 1000, nullptr);
}
extern "C" void update_Stop(void) {
    auto state = update_state;
    if (!state) return;
    update_state = nullptr;
    KillTimer(state->owner, UPDATE_NOTICE_TIMER);
    SetEvent(state->worker->cancel);
    update_ReleaseWorker(state->worker);
    delete state;
}
extern "C" void update_Drain(void) {
    update_Stop();
    if (!update_thread) return;
    // Called only after the application message loop, never during UI interaction.
    // Cancellation does not wait for a response. The worker owns all remaining data.
    WaitForSingleObject(update_thread, 2000);
    CloseHandle(update_thread);
    update_thread = nullptr;
}
extern "C" void update_Tick(void) {
    auto state = update_state;
    if (!state) return;
    auto worker = state->worker;
    wchar_t version[24]{};
    if (!TryAcquireSRWLockShared(&worker->lock)) return;
    bool changed = state->generation != worker->generation;
    if (changed) {
        state->generation = worker->generation;
        StringCchCopyW(version, ARRAYSIZE(version), worker->version);
    }
    ReleaseSRWLockShared(&worker->lock);
    if (changed) {
        UpdateVersion number{};
        if (update_ParseVersion(version, number)) {
            state->version = version;
            state->newer = update_IsNewer(number, state->current);
            update_RefreshPage();
        }
    }
}
extern "C" void update_InsertMenu(HMENU menu, BOOL english) {
    if (!update_state || !update_state->newer) return;
    update_MakeLabel(update_menuText, ARRAYSIZE(update_menuText), english != FALSE);
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    item.fType = MFT_OWNERDRAW;
    item.wID = UPDATE_NOTICE_COMMAND;
    item.dwTypeData = update_menuText;
    InsertMenuItemW(menu, 0, TRUE, &item);
}
static HFONT update_CreateFont(HDC dc) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return nullptr;
    // Menu metrics are scaled by the process DPI awareness, matching native items.
    (void)dc;
    return CreateFontIndirectW(&metrics.lfMenuFont);
}
extern "C" BOOL update_MeasureMenu(MEASUREITEMSTRUCT* item) {
    if (!item || item->CtlType != ODT_MENU || item->itemID != UPDATE_NOTICE_COMMAND) return FALSE;
    HDC dc = GetDC(nullptr);
    HFONT font = update_CreateFont(dc);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, update_menuText, lstrlenW(update_menuText), &size);
    item->itemWidth = size.cx + GetSystemMetrics(SM_CXMENUCHECK) * 2;
    item->itemHeight = (std::max)(static_cast<int>(size.cy) + 8, GetSystemMetrics(SM_CYMENU));
    if (previous) SelectObject(dc, previous);
    if (font) DeleteObject(font);
    ReleaseDC(nullptr, dc);
    return TRUE;
}
extern "C" BOOL update_DrawItem(DRAWITEMSTRUCT* item) {
    if (!item || item->itemID != UPDATE_NOTICE_COMMAND ||
        (item->CtlType != ODT_MENU && item->CtlType != ODT_BUTTON)) return FALSE;
    bool menu = item->CtlType == ODT_MENU;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    int saved = SaveDC(item->hDC);
    FillRect(item->hDC, &item->rcItem, GetSysColorBrush(menu ? (selected ? COLOR_HIGHLIGHT : COLOR_MENU) : COLOR_3DFACE));
    SetBkMode(item->hDC, TRANSPARENT);
    COLORREF background = GetSysColor(menu ? (selected ? COLOR_HIGHLIGHT : COLOR_MENU) : COLOR_3DFACE);
    int brightness = GetRValue(background) + GetGValue(background) + GetBValue(background);
    SetTextColor(item->hDC, brightness < 384 ? RGB(255, 150, 150) : RGB(190, 0, 0));
    HFONT font = menu ? update_CreateFont(item->hDC) : nullptr;
    HFONT pageFont = menu ? nullptr : reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    if (pageFont) {
        LOGFONTW description{};
        if (GetObjectW(pageFont, sizeof(description), &description)) {
            description.lfUnderline = TRUE;
            font = CreateFontIndirectW(&description);
        }
    }
    if (font || pageFont) SelectObject(item->hDC, font ? font : pageFont);
    wchar_t text[160];
    if (menu) StringCchCopyW(text, ARRAYSIZE(text), update_menuText);
    else GetWindowTextW(item->hwndItem, text, ARRAYSIZE(text));
    RECT rect = item->rcItem;
    if (menu) rect.left += GetSystemMetrics(SM_CXMENUCHECK);
    DrawTextW(item->hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (menu ? DT_LEFT : (DT_CENTER | DT_END_ELLIPSIS)));
    if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(item->hDC, &rect);
    RestoreDC(item->hDC, saved);
    if (font) DeleteObject(font);
    return TRUE;
}
static LRESULT CALLBACK update_LinkProc(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR subclass, DWORD_PTR) {
    if (message == WM_GETDLGCODE && wparam == VK_RETURN)
        return DefSubclassProc(window, message, wparam, lparam) | DLGC_WANTMESSAGE;
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        if (!(lparam & (1L << 30))) SendMessageW(window, BM_CLICK, 0, 0);
        return 0;
    }
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); // IDC_HAND
        return TRUE;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, update_LinkProc, subclass);
    return DefSubclassProc(window, message, wparam, lparam);
}

extern "C" void update_AttachPage(HWND page, BOOL english) {
    ShowWindow(GetDlgItem(page, UPDATE_NOTICE_COMMAND), SW_HIDE);
    if (!update_state) return;
    SetWindowSubclass(GetDlgItem(page, UPDATE_NOTICE_COMMAND), update_LinkProc, 1, 0);
    update_state->page = page;
    update_state->pageEnglish = english != FALSE;
    update_RefreshPage();
}
extern "C" void update_DetachPage(HWND page) {
    if (update_state && update_state->page == page) update_state->page = nullptr;
}
extern "C" void update_OpenRelease(HWND owner) {
    if (update_state)
        ShellExecuteW(owner, L"open", update_releaseUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

