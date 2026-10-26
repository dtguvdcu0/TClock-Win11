#include <wchar.h>
#include <stdlib.h>
#include "tclock.h"
#include "plugin_runtime.h"
#include "plugin_registry.h"
#include "../common/plugin_api.h"
#include "../common/text_codec.h"
#include <stdio.h>
#include <string.h>
extern BOOL b_EnglishMenu;
#define PRT_MAX_PROVIDERS 32
#define PRT_MAX_ACTIONS PRT_ACTION_CAPACITY
#define PRT_KEY_BYTES 128

typedef struct PRT_PROVIDER {
    HMODULE module;
    const TC_ACTION_PROVIDER* api;
    const TC_PLUGIN_SETTINGS* settings;
    UINT busy, pages;
    BOOL active, started, starting, stopping, pending, wanted, restart;
    const TC_PLUGIN_RUNTIME* runtime;
    TC_PLUGIN_CONTEXT context;
    WCHAR desired_ini[MAX_PATH], legacy[MAX_PATH];
    const TC_PLUGIN_EXTENSION* extension;
    WCHAR path[MAX_PATH], ini[MAX_PATH];
    WCHAR id[TC_PLUGIN_ID_CHARS], registration[64];
} PRT_PROVIDER;
typedef struct PRT_ACTION {
    UINT provider;
    BOOL active;
    TC_REGISTRATION token;
    void* user;
    HRESULT (WINAPI *invoke)(void*, const TC_ACTION_CONTEXT*);
    WCHAR id[TC_PLUGIN_ID_CHARS];
    char key[PRT_KEY_BYTES], japanese[512], english[512];
} PRT_ACTION;
static PRT_PROVIDER prt_providers[PRT_MAX_PROVIDERS];
static PRT_ACTION prt_actions[PRT_MAX_ACTIONS];
static UINT prt_providers_used, prt_actions_used;
static BOOL prt_initialized, prt_refreshing, prt_stopping;

static BOOL prt_is_allowed(void) {
    return !GetMyRegLong("ETC", "MinimalMode", 0) && !GetMyRegLong("Status_DoNotEdit", "SafeMode", 0);
}
static BOOL prt_check_id(const WCHAR* id) {
    UINT i;
    for (i = 0; i < TC_PLUGIN_ID_CHARS; ++i) {
        WCHAR c = id[i];
        if (!c) return i > 0;
        if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_')) return FALSE;
    }
    return FALSE;
}
static void prt_report(DWORD error) {
    char message[128];sprintf_s(message, sizeof(message), "[plugin_runtime] Provider rejected, error %lu", error);WriteNormalLog(message);
}
static BOOL prt_resolve_ini(LPCWSTR path, LPCWSTR configured, WCHAR* ini) {
    WCHAR active[MAX_PATH], absolute[MAX_PATH];
    if (configured && configured[0]) { if (!preg_resolve(configured, ini)) return FALSE; }
    else { wcscpy_s(ini, MAX_PATH, path);if (!PathRenameExtensionW(ini, L".ini")) return FALSE; }
    /* Existing UTF-8 active-INI boundary; plugin context paths stay UTF-16. */
    if (tc_utf8_to_utf16(g_inifile, active, MAX_PATH) > 0 && preg_resolve(active, absolute) && _wcsicmp(absolute, ini) == 0) return FALSE;
    return TRUE;
}
static BOOL prt_is_enabled(const PRT_PROVIDER* provider) {
    PLUGIN_RECORD records[PREG_MAX_RECORDS];int i, count;
    if (prt_stopping || !provider->active || !prt_is_allowed() || provider->busy) return FALSE;
    count = preg_load(records, PREG_MAX_RECORDS);
    for (i = PREG_KNOWN; i < count; ++i) {
        WCHAR path[MAX_PATH], ini[MAX_PATH];PLUGIN_RECORD* r = &records[i];
        if (r->kind == PREG_KIND_DLL && r->enabled && !r->conflict && (!r->automatic || r->discovered) && wcscmp(r->id, provider->registration) == 0 &&
            (!r->provider[0] || wcscmp(r->provider, provider->id) == 0) &&
            preg_resolve(r->path, path) && _wcsicmp(path, provider->path) == 0 &&
            prt_resolve_ini(path, r->ini, ini) && _wcsicmp(ini, provider->ini) == 0) return TRUE;
    }
    return FALSE;
}
static void prt_hide_actions(UINT provider) {
    UINT i;for (i = 0; i < prt_actions_used; ++i) if (prt_actions[i].provider == provider) prt_actions[i].active = FALSE;
}
typedef struct PRT_PAGE {
    UINT provider;
    BOOL active;
    TC_REGISTRATION token;
    WCHAR id[TC_PLUGIN_ID_CHARS], japanese[TC_PLUGIN_LABEL_CHARS], english[TC_PLUGIN_LABEL_CHARS];
    void* user;
    HWND (WINAPI *create)(void*, const TC_EXTENSION_CONTEXT*);
    HRESULT (WINAPI *apply)(void*, HWND);
    HWND window;
} PRT_PAGE;
typedef struct PRT_COLOR {
    UINT provider;
    BOOL active;
    TC_REGISTRATION token;
    WCHAR id[TC_PLUGIN_ID_CHARS];
    INT priority;
    UINT mode;
    void* user;
    HRESULT (WINAPI *query)(void*, const TC_TEXT_CONTEXT*, COLORREF*);
} PRT_COLOR;
static PRT_PAGE prt_pages[PRT_PAGE_CAPACITY];
static PRT_COLOR prt_colors[PRT_MAX_PROVIDERS * 4];
static UINT prt_pages_used, prt_colors_used;
static TC_REGISTRATION prt_serial;
static DWORD prt_thread;
static void prt_settle(PRT_PROVIDER* provider);
static HRESULT prt_start(PRT_PROVIDER* provider);
static HRESULT WINAPI prt_copy_text(HWND owner, LPCWSTR text);
static void WINAPI prt_mark_changed(HWND page);

static PRT_PROVIDER* prt_owner(void* owner) {
    UINT i;
    if (!prt_thread || GetCurrentThreadId() != prt_thread) return NULL;
    for (i = 0; i < prt_providers_used; ++i) if (owner == &prt_providers[i]) return &prt_providers[i];
    return NULL;
}
static BOOL prt_can_register(PRT_PROVIDER* p) {
    return p && !prt_stopping && !p->stopping && (p->starting || p->started);
}
static BOOL prt_check_label(LPCWSTR label) {
    return label && label[0] && wcsnlen_s(label, TC_PLUGIN_LABEL_CHARS) < TC_PLUGIN_LABEL_CHARS;
}
static void prt_leave(PRT_PROVIDER* p) {
    --p->busy;
    if (!p->busy) prt_settle(p);
}
static HRESULT WINAPI prt_register_command(void* owner, const TC_COMMAND_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_ACTION* a;UINT i;char pluginId[TC_PLUGIN_ID_CHARS], actionId[TC_PLUGIN_ID_CHARS];
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || !prt_check_label(entry->japanese) || !prt_check_label(entry->english) || !entry->invoke) return E_INVALIDARG;
    for (i = 0; i < prt_actions_used; ++i) if (prt_actions[i].provider == (UINT)(p - prt_providers) && !wcscmp(prt_actions[i].id, entry->id)) break;
    if (i < prt_actions_used && prt_actions[i].active) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == PRT_MAX_ACTIONS) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    a = &prt_actions[i];
    if (tc_utf16_to_utf8(p->id, pluginId, sizeof(pluginId)) <= 0 || tc_utf16_to_utf8(entry->id, actionId, sizeof(actionId)) <= 0) return E_INVALIDARG;
    if (tc_utf16_to_utf8(entry->japanese, a->japanese, sizeof(a->japanese)) <= 0 || tc_utf16_to_utf8(entry->english, a->english, sizeof(a->english)) <= 0) return E_INVALIDARG;
    if (i == prt_actions_used) ++prt_actions_used;
    a->provider = (UINT)(p - prt_providers);a->active = TRUE;a->token = ++prt_serial;
    wcscpy_s(a->id, TC_PLUGIN_ID_CHARS, entry->id);
    sprintf_s(a->key, sizeof(a->key), "dll:%s/%s", pluginId, actionId);
    a->user = entry->user;a->invoke = entry->invoke;*token = a->token;return S_OK;
}
static HRESULT WINAPI prt_register_page(void* owner, const TC_PAGE_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_PAGE* page;UINT i;
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || !prt_check_label(entry->japanese) || !prt_check_label(entry->english) || !entry->create || !entry->apply) return E_INVALIDARG;
    for (i = 0; i < prt_pages_used; ++i) if (prt_pages[i].provider == (UINT)(p - prt_providers) && !wcscmp(prt_pages[i].id, entry->id)) break;
    if (i < prt_pages_used && (prt_pages[i].active || prt_pages[i].window)) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == PRT_PAGE_CAPACITY) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    if (i == prt_pages_used) ++prt_pages_used;
    page = &prt_pages[i];page->provider = (UINT)(p - prt_providers);page->active = TRUE;page->token = ++prt_serial;
    wcscpy_s(page->id, TC_PLUGIN_ID_CHARS, entry->id);wcscpy_s(page->japanese, TC_PLUGIN_LABEL_CHARS, entry->japanese);wcscpy_s(page->english, TC_PLUGIN_LABEL_CHARS, entry->english);
    page->user = entry->user;page->create = entry->create;page->apply = entry->apply;*token = page->token;return S_OK;
}
static HRESULT WINAPI prt_register_color(void* owner, const TC_COLOR_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_COLOR* color;UINT i;
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || entry->mode > TC_COLOR_REPLACE || !entry->query) return E_INVALIDARG;
    for (i = 0; i < prt_colors_used; ++i) if (prt_colors[i].provider == (UINT)(p - prt_providers) && !wcscmp(prt_colors[i].id, entry->id)) break;
    if (i < prt_colors_used && prt_colors[i].active) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == _countof(prt_colors)) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    if (i == prt_colors_used) ++prt_colors_used;
    color = &prt_colors[i];color->provider = (UINT)(p - prt_providers);color->active = TRUE;color->token = ++prt_serial;
    wcscpy_s(color->id, TC_PLUGIN_ID_CHARS, entry->id);color->priority = entry->priority;color->mode = entry->mode;color->query = entry->query;color->user = entry->user;
    *token = color->token;return S_OK;
}
static HRESULT WINAPI prt_remove(void* owner, TC_REGISTRATION token) {
    PRT_PROVIDER* p = prt_owner(owner);UINT i, index;
    if (!p || !token) return E_INVALIDARG;
    index = (UINT)(p - prt_providers);
    for (i = 0; i < prt_actions_used; ++i) if (prt_actions[i].token == token && prt_actions[i].provider == index) {prt_actions[i].active = FALSE;return S_OK;}
    for (i = 0; i < prt_pages_used; ++i) if (prt_pages[i].token == token && prt_pages[i].provider == index) {prt_pages[i].active = FALSE;return S_OK;}
    for (i = 0; i < prt_colors_used; ++i) if (prt_colors[i].token == token && prt_colors[i].provider == index) {prt_colors[i].active = FALSE;return S_OK;}
    return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
}
static void WINAPI prt_log(LPCWSTR text) {
    char message[1024];
    if (text && tc_utf16_to_utf8(text, message, sizeof(message)) > 0) WriteNormalLog(message);
}
static const TC_HOST_REGISTRY prt_registry = { sizeof(TC_HOST_REGISTRY), prt_register_command, prt_register_page, prt_register_color, prt_remove };
static const TC_HOST_SERVICES prt_services = { sizeof(TC_HOST_SERVICES), prt_copy_text, prt_mark_changed, prt_log };
static HRESULT WINAPI prt_query_host(void* owner, UINT abi, LPCWSTR id, const void** out) {
    if (!out) return E_POINTER;*out = NULL;
    if (!prt_owner(owner)) return E_ACCESSDENIED;
    if (abi != TC_PLUGIN_ABI || !id) return E_NOINTERFACE;
    if (!wcscmp(id, TC_HOST_REGISTRY_ID)) *out = &prt_registry;
    else if (!wcscmp(id, TC_HOST_SERVICES_ID)) *out = &prt_services;
    else return E_NOINTERFACE;
    return S_OK;
}
static void prt_hide_provider(PRT_PROVIDER* p) {
    UINT i,index = (UINT)(p - prt_providers);
    prt_hide_actions(index);
    for (i = 0; i < prt_pages_used; ++i) if (prt_pages[i].provider == index) prt_pages[i].active = FALSE;
    for (i = 0; i < prt_colors_used; ++i) if (prt_colors[i].provider == index) prt_colors[i].active = FALSE;
}
static LRESULT CALLBACK prt_page_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclass, DWORD_PTR data) {
    PRT_PAGE* page = (PRT_PAGE*)data;PRT_PROVIDER* p = &prt_providers[page->provider];
    LRESULT result;
    ++p->busy;
    result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, prt_page_proc, subclass);
        page->window = NULL;--p->pages;
    }
    prt_leave(p);return result;
}
static void prt_settle(PRT_PROVIDER* p) {
    UINT i;BOOL restart;
    if (!p->pending || p->busy || p->stopping) return;
    if (prt_stopping) for (i = 0; i < prt_pages_used; ++i) if (prt_pages[i].provider == (UINT)(p - prt_providers) && prt_pages[i].window) {
        ++p->busy;DestroyWindow(prt_pages[i].window);--p->busy;
    }
    if (p->pages) return;
    restart = p->restart && p->wanted && !prt_stopping;p->restart = FALSE;
    p->stopping = TRUE;prt_hide_provider(p);
    if (p->started && p->runtime) { ++p->busy;p->runtime->stop();--p->busy; }
    p->started = FALSE;p->active = FALSE;p->pending = FALSE;p->stopping = FALSE;
    if (restart) {HRESULT result = prt_start(p);if (FAILED(result)) prt_report((DWORD)result);}
}

static HWND WINAPI prt_legacy_create(void* user, const TC_EXTENSION_CONTEXT* context) {
    return ((PRT_PROVIDER*)user)->extension->create(context);
}
static HRESULT WINAPI prt_legacy_apply(void* user, HWND page) {
    return ((PRT_PROVIDER*)user)->extension->apply(page);
}
static HRESULT WINAPI prt_legacy_color(void* user, const TC_TEXT_CONTEXT* context, COLORREF* color) {
    *color = ((PRT_PROVIDER*)user)->extension->color(context);return S_OK;
}
static HRESULT prt_read_extension(PRT_PROVIDER* p) {
    TC_REGISTRATION token;TC_PAGE_REGISTRATION page = { sizeof(page) };TC_COLOR_REGISTRATION color = { sizeof(color) };HRESULT result;WCHAR ja[TC_PLUGIN_LABEL_CHARS], en[TC_PLUGIN_LABEL_CHARS];LPCWSTR label;
    if (!p->extension) return S_OK;
    label = p->extension->label(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));if (!prt_check_label(label)) return E_INVALIDARG;wcscpy_s(ja, _countof(ja), label);
    label = p->extension->label(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));if (!prt_check_label(label)) return E_INVALIDARG;wcscpy_s(en, _countof(en), label);
    page.id = L"legacy";page.japanese = ja;page.english = en;page.user = p;page.create = prt_legacy_create;page.apply = prt_legacy_apply;
    result = prt_register_page(p, &page, &token);if (FAILED(result)) return result;
    color.id = L"legacy";color.user = p;color.query = prt_legacy_color;
    return prt_register_color(p, &color, &token);
}

static HRESULT prt_read_actions(UINT providerIndex) {
    PRT_PROVIDER* provider = &prt_providers[providerIndex];
    PRT_ACTION* pending;UINT count, i, j, additions = 0;HRESULT result = E_INVALIDARG;
    prt_hide_actions(providerIndex);
    if (!provider->api) return S_OK;
    count = provider->api->count();if (count > PRT_MAX_ACTIONS) return E_INVALIDARG;
    if (!count) return S_OK;
    pending = (PRT_ACTION*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pending) * count);
    if (!pending) return E_OUTOFMEMORY;
    /* Validate the whole snapshot before publishing. Cached indices never rebind. */
    for (i = 0; i < count; ++i) {
        TC_PLUGIN_ACTION en = { sizeof(en) }, ja = { sizeof(ja) };char pluginId[TC_PLUGIN_ID_CHARS], actionId[TC_PLUGIN_ID_CHARS];
        if (provider->api->describe(i, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), &en) != S_OK ||
            provider->api->describe(i, MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT), &ja) != S_OK ||
            en.size < sizeof(en) || ja.size < sizeof(ja) || !prt_check_id(en.id) || !prt_check_id(ja.id) ||
            wcscmp(en.id, ja.id) || !en.label[0] || !ja.label[0] || en.label[TC_PLUGIN_LABEL_CHARS - 1] || ja.label[TC_PLUGIN_LABEL_CHARS - 1]) goto done;
        for (j = 0; j < i; ++j) if (wcscmp(en.id, pending[j].id) == 0) goto done;
        if (tc_utf16_to_utf8(provider->id, pluginId, sizeof(pluginId)) <= 0 || tc_utf16_to_utf8(en.id, actionId, sizeof(actionId)) <= 0 ||
            tc_utf16_to_utf8(en.label, pending[i].english, sizeof(pending[i].english)) <= 0 ||
            tc_utf16_to_utf8(ja.label, pending[i].japanese, sizeof(pending[i].japanese)) <= 0) goto done;
        pending[i].provider = providerIndex;pending[i].active = TRUE;
        wcscpy_s(pending[i].id, _countof(pending[i].id), en.id);
        sprintf_s(pending[i].key, sizeof(pending[i].key), "dll:%s/%s", pluginId, actionId);
        for (j = 0; j < prt_actions_used; ++j) if (strcmp(pending[i].key, prt_actions[j].key) == 0) break;
        if (j == prt_actions_used) ++additions;
    }
    if (additions > PRT_MAX_ACTIONS - prt_actions_used) { result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);goto done; }
    for (i = 0; i < count; ++i) {
        for (j = 0; j < prt_actions_used; ++j) if (strcmp(pending[i].key, prt_actions[j].key) == 0) break;
        if (j == prt_actions_used) ++prt_actions_used;
        prt_actions[j] = pending[i];
    }
    result = S_OK;
done:
    HeapFree(GetProcessHeap(), 0, pending);return result;
}
static HRESULT prt_acquire(LPCWSTR registration, LPCWSTR path, PRT_PROVIDER** out) {
    HMODULE module;TC_GET_INTERFACE query;const TC_PLUGIN_INFO* info = NULL;
    const TC_ACTION_PROVIDER* api = NULL;const TC_PLUGIN_SETTINGS* settings = NULL;const TC_PLUGIN_EXTENSION* extension = NULL;const TC_PLUGIN_RUNTIME* lifecycle = NULL;UINT i;HRESULT result;
    *out = NULL;
    for (i = 0; i < prt_providers_used; ++i) if (wcscmp(registration, prt_providers[i].registration) == 0) {
        if (_wcsicmp(path, prt_providers[i].path)) return HRESULT_FROM_WIN32(ERROR_BUSY);
        { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(declared, prt_providers[i].id)) return HRESULT_FROM_WIN32(ERROR_BUSY); }
        *out = &prt_providers[i];return S_OK;
    }
    if (prt_providers_used == PRT_MAX_PROVIDERS) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return HRESULT_FROM_WIN32(GetLastError());
    query = (TC_GET_INTERFACE)GetProcAddress(module, "TClockGetInterface");if (!query) goto reject;
    if (query(TC_PLUGIN_ABI, TC_PLUGIN_INFO_ID, (const void**)&info) != S_OK || !info || info->size < sizeof(*info) ||
        !prt_check_id(info->id) || !info->name[0] || info->name[TC_PLUGIN_LABEL_CHARS - 1]) goto reject;
    { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(info->id, declared)) goto reject; }
    for (i = 0; i < prt_providers_used; ++i) if (wcscmp(info->id, prt_providers[i].id) == 0) goto reject;
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_RUNTIME_ID, (const void**)&lifecycle);
    if (result == E_NOINTERFACE) lifecycle = NULL;
    else if (result != S_OK || !lifecycle || lifecycle->size < sizeof(*lifecycle) || !lifecycle->start || !lifecycle->stop) goto reject;
    if (!lifecycle) {
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_ACTIONS_ID, (const void**)&api);
    if (result == E_NOINTERFACE) api = NULL;
    else if (result != S_OK || !api || api->size < sizeof(*api) || !api->count || !api->describe || !api->invoke) goto reject;
    }
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_SETTINGS_ID, (const void**)&settings);
    if (result == E_NOINTERFACE) settings = NULL;
    else if (result != S_OK || !settings || settings->size < sizeof(*settings) || !settings->reload || !settings->show) goto reject;
    if (!lifecycle) {
    result = query(TC_PLUGIN_ABI, TC_PLUGIN_EXTENSION_ID, (const void**)&extension);
    if (result == E_NOINTERFACE) extension = NULL;
    else if (result != S_OK || !extension || extension->size < sizeof(*extension) ||
        !extension->reload || !extension->label || !extension->create || !extension->apply || !extension->color) goto reject;
    }
    if (!api && !settings && !extension && !lifecycle) goto reject;
    *out = &prt_providers[prt_providers_used++];
    (*out)->runtime = lifecycle;
    (*out)->module = module;(*out)->api = api;(*out)->settings = settings;(*out)->extension = extension;
    wcscpy_s((*out)->path, MAX_PATH, path);wcscpy_s((*out)->registration, 64, registration);
    wcscpy_s((*out)->id, TC_PLUGIN_ID_CHARS, info->id);return S_OK;
reject:
    FreeLibrary(module);return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
static HRESULT prt_reload(PRT_PROVIDER* provider, LPCWSTR ini, HWND owner, BOOL english) {
    TC_SETTINGS_CONTEXT context = { sizeof(context) };HRESULT result = S_OK;UINT index = (UINT)(provider - prt_providers);
    prt_hide_actions(index);
    context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.ini_path = ini;
    if (provider->settings) result = provider->settings->reload(&context);
    if (result == S_OK && provider->extension) {
        TC_EXTENSION_CONTEXT extra = { sizeof(extra) };WCHAR legacy[MAX_PATH] = { 0 };
        tc_utf8_to_utf16(g_inifile, legacy, MAX_PATH);
        extra.owner = owner;extra.language = context.language;extra.ini_path = ini;extra.legacy_ini = legacy;
        result = provider->extension->reload(&extra);
    }
    provider->active = result == S_OK;
    if (result == S_OK) { wcscpy_s(provider->ini, MAX_PATH, ini);result = prt_read_actions(index); }
    else if (SUCCEEDED(result)) result = E_FAIL;
    return result;
}
static HRESULT prt_start(PRT_PROVIDER* p) {
    HRESULT result;
    if (prt_stopping || p->busy || p->pages || p->started) return HRESULT_FROM_WIN32(ERROR_BUSY);
    wcscpy_s(p->ini, MAX_PATH, p->desired_ini);
    p->legacy[0] = 0;tc_utf8_to_utf16(g_inifile, p->legacy, MAX_PATH);
    ZeroMemory(&p->context, sizeof(p->context));p->context.size = sizeof(p->context);
    p->context.plugin = p;p->context.query_host = prt_query_host;p->context.owner = g_hwndMain;
    p->context.language = MAKELANGID(b_EnglishMenu ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);
    p->context.ini_path = p->ini;p->context.legacy_ini = p->legacy;
    p->starting = TRUE;p->started = TRUE;p->active = FALSE;++p->busy;
    if (p->runtime) result = p->runtime->start(&p->context);
    else {
        result = prt_reload(p, p->ini, NULL, b_EnglishMenu);
        if (result == S_OK) result = prt_read_extension(p);
    }
    p->starting = FALSE;p->active = result == S_OK;
    if (result != S_OK) {p->pending = TRUE;if (SUCCEEDED(result)) result = E_FAIL;}
    prt_leave(p);return result;
}
void prt_refresh(void) {
    PLUGIN_RECORD records[PREG_MAX_RECORDS];int count, i;UINT j;
    if (!prt_thread) prt_thread = GetCurrentThreadId();
    if (GetCurrentThreadId() != prt_thread || prt_refreshing || prt_stopping) return;
    prt_initialized = TRUE;prt_refreshing = TRUE;
    for (j = 0; j < prt_providers_used; ++j) prt_providers[j].wanted = FALSE;
    if (prt_is_allowed()) {
        preg_discover();count = preg_load(records, PREG_MAX_RECORDS);
        for (i = PREG_KNOWN; i < count; ++i) {
            PLUGIN_RECORD* r = &records[i];WCHAR path[MAX_PATH], ini[MAX_PATH];PRT_PROVIDER* p;HRESULT result;
            if (r->kind != PREG_KIND_DLL || !r->enabled || r->conflict || (r->automatic && !r->discovered) || !preg_resolve(r->path, path)) continue;
            if (!prt_resolve_ini(path, r->ini, ini)) { prt_report(ERROR_INVALID_NAME);continue; }
            result = prt_acquire(r->id, path, &p);
            if (FAILED(result)) {prt_report((DWORD)result);continue;}
            p->wanted = TRUE;wcscpy_s(p->desired_ini, MAX_PATH, ini);
            if (p->started && _wcsicmp(p->ini, ini)) {
                p->active = FALSE;p->pending = TRUE;p->restart = TRUE;prt_settle(p);continue;
            }
            if (p->started && p->pending) {p->pending = FALSE;p->restart = FALSE;p->active = TRUE;}
            if (p->busy) continue;
            if (!p->started) result = prt_start(p);
            else if (!p->pages) {
                ++p->busy;
                result = p->runtime ? (p->runtime->reload ? p->runtime->reload(&p->context) : S_OK) : prt_reload(p, p->ini, NULL, b_EnglishMenu);
                if (result != S_OK) {p->active = FALSE;p->pending = TRUE;}
                prt_leave(p);
            } else result = S_OK;
            if (result != S_OK) prt_report((DWORD)result);
        }
    }
    for (j = 0; j < prt_providers_used; ++j) {
        PRT_PROVIDER* p = &prt_providers[j];
        if (!p->wanted) {p->active = FALSE;p->pending = p->started;p->restart = FALSE;prt_settle(p);}
    }
    prt_refreshing = FALSE;
}
void prt_shutdown(void) {
    UINT i;
    if (prt_thread && GetCurrentThreadId() != prt_thread) return;
    prt_stopping = TRUE;
    for (i = 0; i < prt_providers_used; ++i) {
        PRT_PROVIDER* p = &prt_providers[i];p->wanted = FALSE;p->active = FALSE;p->restart = FALSE;p->pending = p->started;
    }
    for (i = 0; i < prt_providers_used; ++i) {
        prt_settle(&prt_providers[i]);
    }
}
static void prt_initialize(void) { if (!prt_initialized) prt_refresh(); }
HRESULT prt_show_settings(LPCWSTR registration, LPCWSTR file, LPCWSTR configured, HWND owner, BOOL english) {
    WCHAR path[MAX_PATH], ini[MAX_PATH];PRT_PROVIDER* p;TC_SETTINGS_CONTEXT context = { sizeof(context) };HRESULT result;
    if (!prt_thread) prt_thread = GetCurrentThreadId();
    if (GetCurrentThreadId() != prt_thread || prt_stopping || !prt_is_allowed()) return E_ACCESSDENIED;
    if (!registration || !registration[0] || wcslen(registration) >= 64 || !preg_resolve(file, path) || !prt_resolve_ini(path, configured, ini)) return E_INVALIDARG;
    result = prt_acquire(registration, path, &p);if (FAILED(result)) return result;
    if (!p->settings) return E_NOINTERFACE;
    if (p->busy || p->pages) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (p->started && _wcsicmp(p->ini, ini)) {p->pending = TRUE;p->restart = FALSE;prt_settle(p);}
    wcscpy_s(p->desired_ini, MAX_PATH, ini);
    if (!p->started) result = prt_start(p);
    else {context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.ini_path = p->ini;++p->busy;result = p->settings->reload(&context);prt_leave(p);}
    if (FAILED(result)) {prt_refresh();return result;}
    context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.ini_path = p->ini;
    ++p->busy;result = p->settings->show(&context);prt_leave(p);
    prt_refresh();return result;
}
int prt_count(void) { prt_initialize();return prt_is_allowed() ? (int)prt_actions_used : 0; }
const char* prt_get_key(int index) {
    prt_initialize();return index >= 0 && (UINT)index < prt_actions_used ? prt_actions[index].key : NULL;
}
const char* prt_get_label(const char* key, BOOL english) {
    UINT i;if (!key || strncmp(key, "dll:", 4)) return NULL;prt_initialize();
    for (i = 0; i < prt_actions_used; ++i)
        if (prt_actions[i].active && prt_providers[prt_actions[i].provider].active && strcmp(key, prt_actions[i].key) == 0 && prt_is_enabled(&prt_providers[prt_actions[i].provider]))
            return english ? prt_actions[i].english : prt_actions[i].japanese;
    return NULL;
}
static HRESULT WINAPI prt_copy_text(HWND owner, LPCWSTR text) {
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
BOOL prt_execute(const char* key, HWND owner, BOOL english) {
    UINT i;if (!prt_get_label(key, english)) return FALSE;
    for (i = 0; i < prt_actions_used; ++i) {
        PRT_ACTION* action = &prt_actions[i];PRT_PROVIDER* provider = &prt_providers[action->provider];
        if (action->active && provider->active && strcmp(key, action->key) == 0 && prt_is_enabled(provider)) {
            TC_ACTION_CONTEXT context = { sizeof(context) };HRESULT result;
            context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.copy_text = prt_copy_text;
            ++provider->busy;result = action->invoke ? action->invoke(action->user, &context) : provider->api->invoke(action->id, &context);prt_leave(provider);
            if (FAILED(result)) prt_report((DWORD)result);return SUCCEEDED(result);
        }
    }
    return FALSE;
}

static void WINAPI prt_mark_changed(HWND page) {
    g_bApplyClock = TRUE;
    SendMessageW(GetParent(page), PSM_CHANGED, (WPARAM)page, 0);
}
static void prt_make_context(PRT_PROVIDER* provider, HWND owner, BOOL english,
    TC_EXTENSION_CONTEXT* context, WCHAR* legacy) {
    ZeroMemory(context, sizeof(*context));context->size = sizeof(*context);
    context->owner = owner;context->language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);
    context->ini_path = provider->ini;context->changed = prt_mark_changed;
    legacy[0] = 0;tc_utf8_to_utf16(g_inifile, legacy, MAX_PATH);context->legacy_ini = legacy;
}
int prt_page_count(void) { prt_initialize();return (int)prt_pages_used; }
LPCWSTR prt_page_label(int index, BOOL english) {
    PRT_PAGE* page;PRT_PROVIDER* p;
    if (index < 0 || (UINT)index >= prt_pages_used) return NULL;
    page = &prt_pages[index];p = &prt_providers[page->provider];
    if (!page->active || !p->active || !prt_is_enabled(p) || GetFileAttributesW(p->path) == INVALID_FILE_ATTRIBUTES) return NULL;
    return english ? page->english : page->japanese;
}
HWND prt_create_page(int index, HWND owner, BOOL english) {
    TC_EXTENSION_CONTEXT context;WCHAR legacy[MAX_PATH];PRT_PAGE* page;PRT_PROVIDER* p;HWND window;
    if (!prt_page_label(index, english)) return NULL;
    page = &prt_pages[index];p = &prt_providers[page->provider];
    if (page->window) return page->window;
    prt_make_context(p, owner, english, &context, legacy);
    ++p->busy;++p->pages;window = page->create(page->user, &context);
    if (window && IsWindow(window) && GetParent(window) == owner && SetWindowSubclass(window, prt_page_proc, (UINT_PTR)page, (DWORD_PTR)page)) page->window = window;
    else {if (window && IsWindow(window)) DestroyWindow(window);window = NULL;--p->pages;}
    prt_leave(p);return page->window == window && IsWindow(window) ? window : NULL;
}
BOOL prt_apply_page(int index, HWND window) {
    HRESULT result;PRT_PAGE* page;PRT_PROVIDER* p;
    if (!window || index < 0 || (UINT)index >= prt_pages_used) return TRUE;
    page = &prt_pages[index];p = &prt_providers[page->provider];
    if (page->window != window || !IsWindow(window) || !p->started) return FALSE;
    ++p->busy;result = page->apply(page->user, window);prt_leave(p);
    if (FAILED(result)) {
        MessageBoxW(GetParent(window), b_EnglishMenu ? L"Could not save plugin settings." : L"\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002", L"TClock", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}
static int prt_compare_colors(const void* first, const void* second) {
    const PRT_COLOR* a = &prt_colors[*(const UINT*)first];const PRT_COLOR* b = &prt_colors[*(const UINT*)second];int result;
    if (a->priority != b->priority) return a->priority > b->priority ? -1 : 1;
    result = wcscmp(prt_providers[a->provider].id, prt_providers[b->provider].id);
    return result ? result : wcscmp(a->id, b->id);
}
BOOL prt_handle_message(UINT message, LPARAM lParam, LRESULT* result) {
    const COPYDATASTRUCT* data;const TC_TEXT_CONTEXT* context;COLORREF color;UINT i, count = 0, order[_countof(prt_colors)];TC_REGISTRATION tokens[_countof(prt_colors)];
    if (message != WM_COPYDATA || !lParam || !result) return FALSE;
    data = (const COPYDATASTRUCT*)lParam;
    if (data->dwData != TC_PLUGIN_COLOR_DATA) return FALSE;
    *result = 0;
    if (data->cbData != sizeof(TC_TEXT_CONTEXT) || !data->lpData) return TRUE;
    context = (const TC_TEXT_CONTEXT*)data->lpData;
    if (context->size != sizeof(*context)) return TRUE;
    color = context->base;
    if (prt_is_allowed() && !prt_stopping) {
        for (i = 0; i < prt_colors_used; ++i) if (prt_colors[i].active && prt_providers[prt_colors[i].provider].active) order[count++] = i;
        qsort(order, count, sizeof(order[0]), prt_compare_colors);
        for (i = 0; i < count; ++i) tokens[i] = prt_colors[order[i]].token;
        for (i = 0; i < count; ++i) {
            PRT_COLOR* hook = &prt_colors[order[i]];PRT_PROVIDER* p = &prt_providers[hook->provider];TC_TEXT_CONTEXT current = *context;COLORREF next = color;HRESULT status;UINT mode = hook->mode;
            if (!hook->active || hook->token != tokens[i] || !p->active || p->busy) continue;
            current.base = color;++p->busy;status = hook->query(hook->user, &current, &next);prt_leave(p);
            if (status == S_OK) {color = next;if (mode == TC_COLOR_REPLACE) break;}
            else if (FAILED(status)) prt_report((DWORD)status);
        }
    }
    *result = (LRESULT)(0x01000000u | (color & 0x00ffffffu));return TRUE;
}
