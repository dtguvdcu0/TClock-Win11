#include <wchar.h>
#include "tclock.h"
#include "plugin_actions.h"
#include "plugin_registry.h"
#include "../common/plugin_api.h"
#include "../common/text_codec.h"
#include <stdio.h>
#include <string.h>
extern BOOL b_EnglishMenu;
#define PACT_MAX_PROVIDERS 32
#define PACT_MAX_ACTIONS PACT_ACTION_CAPACITY
#define PACT_KEY_BYTES 128

typedef struct PACT_PROVIDER {
    HMODULE module;
    const TC_ACTION_PROVIDER* api;
    const TC_PLUGIN_SETTINGS* settings;
    BOOL busy, active;
    const TC_PLUGIN_EXTENSION* extension;
    WCHAR path[MAX_PATH], ini[MAX_PATH];
    WCHAR id[TC_PLUGIN_ID_CHARS], registration[64];
} PACT_PROVIDER;
typedef struct PACT_ACTION {
    UINT provider;
    BOOL active;
    WCHAR id[TC_PLUGIN_ID_CHARS];
    char key[PACT_KEY_BYTES], japanese[512], english[512];
} PACT_ACTION;
static PACT_PROVIDER pact_providers[PACT_MAX_PROVIDERS];
static PACT_ACTION pact_actions[PACT_MAX_ACTIONS];
static UINT pact_providers_used, pact_actions_used;
static BOOL pact_initialized, pact_refreshing;

static BOOL pact_is_allowed(void) {
    return !GetMyRegLong("ETC", "MinimalMode", 0) && !GetMyRegLong("Status_DoNotEdit", "SafeMode", 0);
}
static BOOL pact_check_id(const WCHAR* id) {
    UINT i;
    for (i = 0; i < TC_PLUGIN_ID_CHARS; ++i) {
        WCHAR c = id[i];
        if (!c) return i > 0;
        if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_')) return FALSE;
    }
    return FALSE;
}
static void pact_report(DWORD error) {
    char message[128];sprintf_s(message, sizeof(message), "[plugin_actions] Provider rejected, error %lu", error);WriteNormalLog(message);
}
static BOOL pact_resolve_ini(LPCWSTR path, LPCWSTR configured, WCHAR* ini) {
    WCHAR active[MAX_PATH], absolute[MAX_PATH];
    if (configured && configured[0]) { if (!preg_resolve(configured, ini)) return FALSE; }
    else { wcscpy_s(ini, MAX_PATH, path);if (!PathRenameExtensionW(ini, L".ini")) return FALSE; }
    /* Existing UTF-8 active-INI boundary; plugin context paths stay UTF-16. */
    if (tc_utf8_to_utf16(g_inifile, active, MAX_PATH) > 0 && preg_resolve(active, absolute) && _wcsicmp(absolute, ini) == 0) return FALSE;
    return TRUE;
}
static BOOL pact_is_enabled(const PACT_PROVIDER* provider) {
    PLUGIN_RECORD records[PREG_MAX_RECORDS];int i, count;
    if (!pact_is_allowed() || provider->busy) return FALSE;
    count = preg_load(records, PREG_MAX_RECORDS);
    for (i = PREG_KNOWN; i < count; ++i) {
        WCHAR path[MAX_PATH], ini[MAX_PATH];PLUGIN_RECORD* r = &records[i];
        if (r->kind == PREG_KIND_DLL && r->enabled && !r->conflict && (!r->automatic || r->discovered) && wcscmp(r->id, provider->registration) == 0 &&
            (!r->provider[0] || wcscmp(r->provider, provider->id) == 0) &&
            preg_resolve(r->path, path) && _wcsicmp(path, provider->path) == 0 &&
            pact_resolve_ini(path, r->ini, ini) && _wcsicmp(ini, provider->ini) == 0) return TRUE;
    }
    return FALSE;
}
static void pact_hide_actions(UINT provider) {
    UINT i;for (i = 0; i < pact_actions_used; ++i) if (pact_actions[i].provider == provider) pact_actions[i].active = FALSE;
}
static HRESULT pact_read_actions(UINT providerIndex) {
    PACT_PROVIDER* provider = &pact_providers[providerIndex];
    PACT_ACTION* pending;UINT count, i, j, additions = 0;HRESULT result = E_INVALIDARG;
    pact_hide_actions(providerIndex);
    if (!provider->api) return S_OK;
    count = provider->api->count();if (count > PACT_MAX_ACTIONS) return E_INVALIDARG;
    if (!count) return S_OK;
    pending = (PACT_ACTION*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pending) * count);
    if (!pending) return E_OUTOFMEMORY;
    /* Validate the whole snapshot before publishing. Cached indices never rebind. */
    for (i = 0; i < count; ++i) {
        TC_PLUGIN_ACTION en = { sizeof(en) }, ja = { sizeof(ja) };char pluginId[TC_PLUGIN_ID_CHARS], actionId[TC_PLUGIN_ID_CHARS];
        if (provider->api->describe(i, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), &en) != S_OK ||
            provider->api->describe(i, MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), &ja) != S_OK ||
            en.size < sizeof(en) || ja.size < sizeof(ja) || !pact_check_id(en.id) || !pact_check_id(ja.id) ||
            wcscmp(en.id, ja.id) || !en.label[0] || !ja.label[0] || en.label[TC_PLUGIN_LABEL_CHARS - 1] || ja.label[TC_PLUGIN_LABEL_CHARS - 1]) goto done;
        for (j = 0; j < i; ++j) if (wcscmp(en.id, pending[j].id) == 0) goto done;
        if (tc_utf16_to_utf8(provider->id, pluginId, sizeof(pluginId)) <= 0 || tc_utf16_to_utf8(en.id, actionId, sizeof(actionId)) <= 0 ||
            tc_utf16_to_utf8(en.label, pending[i].english, sizeof(pending[i].english)) <= 0 ||
            tc_utf16_to_utf8(ja.label, pending[i].japanese, sizeof(pending[i].japanese)) <= 0) goto done;
        pending[i].provider = providerIndex;pending[i].active = TRUE;
        wcscpy_s(pending[i].id, _countof(pending[i].id), en.id);
        sprintf_s(pending[i].key, sizeof(pending[i].key), "dll:%s/%s", pluginId, actionId);
        for (j = 0; j < pact_actions_used; ++j) if (strcmp(pending[i].key, pact_actions[j].key) == 0) break;
        if (j == pact_actions_used) ++additions;
    }
    if (additions > PACT_MAX_ACTIONS - pact_actions_used) { result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);goto done; }
    for (i = 0; i < count; ++i) {
        for (j = 0; j < pact_actions_used; ++j) if (strcmp(pending[i].key, pact_actions[j].key) == 0) break;
        if (j == pact_actions_used) ++pact_actions_used;
        pact_actions[j] = pending[i];
    }
    result = S_OK;
done:
    HeapFree(GetProcessHeap(), 0, pending);return result;
}
static HRESULT pact_acquire(LPCWSTR registration, LPCWSTR path, PACT_PROVIDER** out) {
    HMODULE module;TC_GET_INTERFACE query;const TC_PLUGIN_INFO* info = NULL;
    const TC_ACTION_PROVIDER* api = NULL;const TC_PLUGIN_SETTINGS* settings = NULL;const TC_PLUGIN_EXTENSION* extension = NULL;UINT i;HRESULT result;
    *out = NULL;
    for (i = 0; i < pact_providers_used; ++i) if (wcscmp(registration, pact_providers[i].registration) == 0) {
        if (_wcsicmp(path, pact_providers[i].path)) return HRESULT_FROM_WIN32(ERROR_BUSY);
        { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(declared, pact_providers[i].id)) return HRESULT_FROM_WIN32(ERROR_BUSY); }
        *out = &pact_providers[i];return S_OK;
    }
    if (pact_providers_used == PACT_MAX_PROVIDERS) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return HRESULT_FROM_WIN32(GetLastError());
    query = (TC_GET_INTERFACE)GetProcAddress(module, "TClockGetInterface");if (!query) goto reject;
    if (query(TC_PLUGIN_ABI, TC_PLUGIN_INFO_ID, (const void**)&info) != S_OK || !info || info->size < sizeof(*info) ||
        !pact_check_id(info->id) || !info->name[0] || info->name[TC_PLUGIN_LABEL_CHARS - 1]) goto reject;
    { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(info->id, declared)) goto reject; }
    for (i = 0; i < pact_providers_used; ++i) if (wcscmp(info->id, pact_providers[i].id) == 0) goto reject;
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_ACTIONS_ID, (const void**)&api);
    if (result == E_NOINTERFACE) api = NULL;
    else if (result != S_OK || !api || api->size < sizeof(*api) || !api->count || !api->describe || !api->invoke) goto reject;
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_SETTINGS_ID, (const void**)&settings);
    if (result == E_NOINTERFACE) settings = NULL;
    else if (result != S_OK || !settings || settings->size < sizeof(*settings) || !settings->reload || !settings->show) goto reject;
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_EXTENSION_ID, (const void**)&extension);
    if (result == E_NOINTERFACE) extension = NULL;
    else if (result != S_OK || !extension || extension->size < sizeof(*extension) ||
        !extension->reload || !extension->label || !extension->create || !extension->apply || !extension->color) goto reject;
    if (!api && !settings && !extension) goto reject;
    *out = &pact_providers[pact_providers_used++];
    (*out)->module = module;(*out)->api = api;(*out)->settings = settings;(*out)->extension = extension;
    wcscpy_s((*out)->path, MAX_PATH, path);wcscpy_s((*out)->registration, 64, registration);
    wcscpy_s((*out)->id, TC_PLUGIN_ID_CHARS, info->id);return S_OK;
reject:
    FreeLibrary(module);return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
static HRESULT pact_reload(PACT_PROVIDER* provider, LPCWSTR ini, HWND owner, BOOL english) {
    TC_SETTINGS_CONTEXT context = { sizeof(context) };HRESULT result = S_OK;UINT index = (UINT)(provider - pact_providers);
    if (provider->busy) return HRESULT_FROM_WIN32(ERROR_BUSY);
    pact_hide_actions(index);provider->busy = TRUE;
    context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.ini_path = ini;
    if (provider->settings) result = provider->settings->reload(&context);
    if (result == S_OK && provider->extension) {
        TC_EXTENSION_CONTEXT extra = { sizeof(extra) };WCHAR legacy[MAX_PATH] = { 0 };
        tc_utf8_to_utf16(g_inifile, legacy, MAX_PATH);
        extra.owner = owner;extra.language = context.language;extra.ini_path = ini;extra.legacy_ini = legacy;
        result = provider->extension->reload(&extra);
    }
    provider->active = result == S_OK;
    if (result == S_OK) { wcscpy_s(provider->ini, MAX_PATH, ini);result = pact_read_actions(index); }
    else if (SUCCEEDED(result)) result = E_FAIL;
    provider->busy = FALSE;return result;
}
void pact_refresh(void) {
    PLUGIN_RECORD records[PREG_MAX_RECORDS];int count, i;UINT j;
    if (pact_refreshing) return;
    for (j = 0; j < pact_providers_used; ++j) pact_providers[j].active = FALSE;
    if (!pact_is_allowed()) return;
    pact_initialized = TRUE;pact_refreshing = TRUE;
    for (j = 0; j < pact_providers_used; ++j) if (!pact_providers[j].busy) pact_hide_actions(j);
    preg_discover();count = preg_load(records, PREG_MAX_RECORDS);
    for (i = PREG_KNOWN; i < count; ++i) {
        PLUGIN_RECORD* r = &records[i];WCHAR path[MAX_PATH], ini[MAX_PATH];PACT_PROVIDER* provider;HRESULT result;
        if (r->kind != PREG_KIND_DLL || !r->enabled || r->conflict || (r->automatic && !r->discovered) || !preg_resolve(r->path, path)) continue;
        if (!pact_resolve_ini(path, r->ini, ini)) { pact_report(ERROR_INVALID_NAME);continue; }
        result = pact_acquire(r->id, path, &provider);
        if (SUCCEEDED(result)) result = pact_reload(provider, ini, NULL, FALSE);
        if (FAILED(result)) pact_report((DWORD)result);
    }
    pact_refreshing = FALSE;
}
static void pact_initialize(void) { if (!pact_initialized) pact_refresh(); }
HRESULT pact_show_settings(LPCWSTR registration, LPCWSTR file, LPCWSTR configured, HWND owner, BOOL english) {
    WCHAR path[MAX_PATH], ini[MAX_PATH];PACT_PROVIDER* provider;TC_SETTINGS_CONTEXT context = { sizeof(context) };HRESULT result;
    if (!pact_is_allowed()) return E_ACCESSDENIED;
    if (!registration || !registration[0] || wcslen(registration) >= 64 || !preg_resolve(file, path) || !pact_resolve_ini(path, configured, ini)) return E_INVALIDARG;
    result = pact_acquire(registration, path, &provider);if (FAILED(result)) return result;
    if (!provider->settings) return E_NOINTERFACE;
    result = pact_reload(provider, ini, owner, english);if (FAILED(result)) { pact_refresh();return result; }
    context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.ini_path = ini;
    provider->busy = TRUE;result = provider->settings->show(&context);provider->busy = FALSE;
    /* Plugin saves are independent of pending host edits; restore registered scope. */
    pact_refresh();return result;
}
int pact_count(void) { pact_initialize();return pact_is_allowed() ? (int)pact_actions_used : 0; }
const char* pact_get_key(int index) {
    pact_initialize();return index >= 0 && (UINT)index < pact_actions_used ? pact_actions[index].key : NULL;
}
const char* pact_get_label(const char* key, BOOL english) {
    UINT i;if (!key || strncmp(key, "dll:", 4)) return NULL;pact_initialize();
    for (i = 0; i < pact_actions_used; ++i)
        if (pact_actions[i].active && strcmp(key, pact_actions[i].key) == 0 && pact_is_enabled(&pact_providers[pact_actions[i].provider]))
            return english ? pact_actions[i].english : pact_actions[i].japanese;
    return NULL;
}
static HRESULT WINAPI pact_copy_text(HWND owner, LPCWSTR text) {
    HGLOBAL memory;
    WCHAR* target;
    size_t count;
    DWORD error;
    if (!text) return E_INVALIDARG;
    count = wcsnlen_s(text, 65536);
    if (count == 65536) return E_INVALIDARG;
    memory = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(WCHAR));
    if (!memory) return E_OUTOFMEMORY;
    target = (WCHAR*)GlobalLock(memory);
    if (!target) { GlobalFree(memory); return E_OUTOFMEMORY; }
    memcpy(target, text, (count + 1) * sizeof(WCHAR));
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) { error = GetLastError(); GlobalFree(memory); return HRESULT_FROM_WIN32(error ? error : ERROR_ACCESS_DENIED); }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        error = GetLastError(); CloseClipboard(); GlobalFree(memory);
        return HRESULT_FROM_WIN32(error ? error : ERROR_WRITE_FAULT);
    }
    CloseClipboard();
    return S_OK;
}
BOOL pact_execute(const char* key, HWND owner, BOOL english) {
    UINT i;if (!pact_get_label(key, english)) return FALSE;
    for (i = 0; i < pact_actions_used; ++i) {
        PACT_ACTION* action = &pact_actions[i];PACT_PROVIDER* provider = &pact_providers[action->provider];
        if (action->active && strcmp(key, action->key) == 0 && pact_is_enabled(provider)) {
            TC_ACTION_CONTEXT context = { sizeof(context) };HRESULT result;
            context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.copy_text = pact_copy_text;
            provider->busy = TRUE;result = provider->api->invoke(action->id, &context);provider->busy = FALSE;
            if (FAILED(result)) pact_report((DWORD)result);return SUCCEEDED(result);
        }
    }
    return FALSE;
}

static void WINAPI pact_mark_changed(HWND page) {
    g_bApplyClock = TRUE;
    SendMessageW(GetParent(page), PSM_CHANGED, (WPARAM)page, 0);
}
static void pact_make_context(PACT_PROVIDER* provider, HWND owner, BOOL english,
    TC_EXTENSION_CONTEXT* context, WCHAR* legacy) {
    ZeroMemory(context, sizeof(*context));context->size = sizeof(*context);
    context->owner = owner;context->language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);
    context->ini_path = provider->ini;context->changed = pact_mark_changed;
    legacy[0] = 0;tc_utf8_to_utf16(g_inifile, legacy, MAX_PATH);context->legacy_ini = legacy;
}
int pact_page_count(void) { pact_initialize();return (int)pact_providers_used; }
LPCWSTR pact_page_label(int index, BOOL english) {
    PACT_PROVIDER* p;
    if (index < 0 || (UINT)index >= pact_providers_used) return NULL;
    p = &pact_providers[index];
    if (!p->active || !p->extension || !pact_is_enabled(p) || GetFileAttributesW(p->path) == INVALID_FILE_ATTRIBUTES) return NULL;
    return p->extension->label(MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT));
}
HWND pact_create_page(int index, HWND owner, BOOL english) {
    TC_EXTENSION_CONTEXT context;WCHAR legacy[MAX_PATH];PACT_PROVIDER* p;
    if (!pact_page_label(index, english)) return NULL;
    p = &pact_providers[index];pact_make_context(p, owner, english, &context, legacy);
    return p->extension->create(&context);
}
BOOL pact_apply_page(int index, HWND page) {
    HRESULT result;PACT_PROVIDER* p;
    if (!page || index < 0 || (UINT)index >= pact_providers_used) return TRUE;
    p = &pact_providers[index];if (!p->extension) return TRUE;
    p->busy = TRUE;result = p->extension->apply(page);p->busy = FALSE;
    if (FAILED(result)) {
        MessageBoxW(GetParent(page), b_EnglishMenu ? L"Could not save plugin settings." : L"\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002",
            L"TClock", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}
BOOL pact_handle_message(UINT message, LPARAM lParam, LRESULT* result) {
    const COPYDATASTRUCT* data;const TC_TEXT_CONTEXT* context;COLORREF color;UINT i;
    if (message != WM_COPYDATA || !lParam) return FALSE;
    data = (const COPYDATASTRUCT*)lParam;
    if (data->dwData != TC_PLUGIN_COLOR_DATA) return FALSE;
    *result = 0;
    if (data->cbData != sizeof(TC_TEXT_CONTEXT) || !data->lpData) return TRUE;
    context = (const TC_TEXT_CONTEXT*)data->lpData;
    if (context->size != sizeof(*context)) return TRUE;
    pact_initialize();color = context->base;
    if (!pact_is_allowed()) { *result = 0x01000000u | (color & 0x00ffffffu);return TRUE; }
    for (i = 0; i < pact_providers_used; ++i) {
        PACT_PROVIDER* p = &pact_providers[i];
        if (p->active && p->extension && !p->busy) {
            TC_TEXT_CONTEXT current = *context;current.base = color;
            p->busy = TRUE;color = p->extension->color(&current);p->busy = FALSE;
        }
    }
    *result = (LRESULT)(0x01000000u | (color & 0x00ffffffu));return TRUE;
}
