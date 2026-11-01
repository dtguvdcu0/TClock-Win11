#include <wchar.h>
#include <stdlib.h>
#include "tclock.h"
#include "plugin_runtime.h"
#include "plugin_registry.h"
#include "../common/plugin_api.h"
#include "../common/page_api.h"
#include "../common/plugin_transport.h"
#include "../common/text_codec.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>
extern BOOL b_EnglishMenu;
#define PRT_KEY_BYTES 128

typedef struct PRT_PROVIDER {
    UINT index;
    HMODULE module;
    const TC_ACTION_PROVIDER* api;
    const TC_PLUGIN_SETTINGS* settings;
    UINT busy, pages, pure;
    BOOL active, started, starting, stopping, pending, wanted, restart;
    BOOL policy_enabled;
    ULONGLONG activation;
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
#define PRT_SEGMENT_SIZE 32
static PRT_PROVIDER** prt_segments;
static UINT prt_segment_count;
static PRT_PROVIDER* prt_get_provider(UINT index) {
    return &prt_segments[index / PRT_SEGMENT_SIZE][index % PRT_SEGMENT_SIZE];
}
static PRT_PROVIDER* prt_alloc_provider(UINT index) {
    UINT block = index / PRT_SEGMENT_SIZE;
    if (block >= prt_segment_count) {
        UINT next = block + 1; PRT_PROVIDER** grown;
        if ((SIZE_T)next > (SIZE_T)-1 / sizeof(*grown)) return NULL;
        if (prt_segments) grown = (PRT_PROVIDER**)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, prt_segments, (SIZE_T)next * sizeof(*grown));
        else grown = (PRT_PROVIDER**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)next * sizeof(*grown));
        if (!grown) return NULL;
        prt_segments = grown; prt_segment_count = next;
    }
    if (!prt_segments[block]) {
        prt_segments[block] = (PRT_PROVIDER*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PRT_SEGMENT_SIZE * sizeof(PRT_PROVIDER));
        if (!prt_segments[block]) return NULL;
    }
    prt_get_provider(index)->index = index;return prt_get_provider(index);
}
/* Action blocks remain stable until process exit, including across pointer-table growth. */
static PRT_ACTION** prt_action_segments;
static UINT prt_action_segment_count;
static PRT_ACTION* prt_get_action(UINT index) {
    return &prt_action_segments[index / PRT_SEGMENT_SIZE][index % PRT_SEGMENT_SIZE];
}
static PRT_ACTION* prt_alloc_action(UINT index) {
    UINT block=index/PRT_SEGMENT_SIZE;
    if(block>=prt_action_segment_count){
        UINT next=block+1;PRT_ACTION** grown;
        if((SIZE_T)next>(SIZE_T)-1/sizeof(*grown))return NULL;
        if(prt_action_segments)grown=(PRT_ACTION**)HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,prt_action_segments,(SIZE_T)next*sizeof(*grown));
        else grown=(PRT_ACTION**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)next*sizeof(*grown));
        if(!grown)return NULL;prt_action_segments=grown;prt_action_segment_count=next;
    }
    if(!prt_action_segments[block]){
        prt_action_segments[block]=(PRT_ACTION*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,PRT_SEGMENT_SIZE*sizeof(PRT_ACTION));
        if(!prt_action_segments[block])return NULL;
    }
    return prt_get_action(index);
}
static UINT prt_providers_used, prt_actions_used;
static BOOL prt_initialized, prt_refreshing, prt_stopping;

/* Policy cache: the last full read stays current while the active INI keeps its path,
   last-write time and size and no setting or provider change was noted in process. */
static BOOL prt_policy_valid, prt_policy_allowed;
static WIN32_FILE_ATTRIBUTE_DATA prt_policy_stamp;
static WCHAR prt_policy_path[MAX_PATH];
static BOOL prt_read_stamp(WCHAR* path, WIN32_FILE_ATTRIBUTE_DATA* stamp) {
    return tc_utf8_to_utf16(g_inifile,path,MAX_PATH) > 0 && GetFileAttributesExW(path,GetFileExInfoStandard,stamp);
}
static BOOL prt_policy_current(void) {
    WCHAR path[MAX_PATH];WIN32_FILE_ATTRIBUTE_DATA stamp;
    return prt_policy_valid && prt_read_stamp(path,&stamp) && !_wcsicmp(path,prt_policy_path) &&
        !memcmp(&stamp.ftLastWriteTime,&prt_policy_stamp.ftLastWriteTime,sizeof(FILETIME)) &&
        stamp.nFileSizeLow == prt_policy_stamp.nFileSizeLow && stamp.nFileSizeHigh == prt_policy_stamp.nFileSizeHigh;
}
static BOOL prt_read_allowed(void) {
    return !GetMyRegLong("ETC", "MinimalMode", 0) && !GetMyRegLong("Status_DoNotEdit", "SafeMode", 0);
}
static BOOL prt_is_allowed(void) {
    return prt_policy_current() ? prt_policy_allowed : prt_read_allowed();
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
static BOOL prt_check_interface(LPCWSTR id) {
    UINT i;BOOL slash = FALSE, digit = FALSE;
    if (!id) return FALSE;
    for (i = 0; i < TC_FEATURE_INTERFACE_CHARS; ++i) {
        WCHAR ch = id[i];
        if (!ch) return slash && digit;
        if (ch == L'/' && !slash && i) { slash = TRUE;continue; }
        if (slash) { if (ch < L'0' || ch > L'9') return FALSE;digit = TRUE; }
        else if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' || ch == L'_')) return FALSE;
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
static BOOL prt_match_policy(const PRT_PROVIDER* provider,PLUGIN_RECORD* records,int count) {
    int i;WCHAR legacy[MAX_PATH];
    if (tc_utf8_to_utf16(g_inifile,legacy,_countof(legacy)) <= 0 || _wcsicmp(legacy,provider->legacy)) return FALSE;
    for (i = PREG_KNOWN; i < count; ++i) {
        WCHAR path[MAX_PATH], ini[MAX_PATH];PLUGIN_RECORD* r = &records[i];
        if (r->kind == PREG_KIND_DLL && r->enabled && !r->conflict && (!r->automatic || r->discovered) && wcscmp(r->id, provider->registration) == 0 &&
            (!r->provider[0] || wcscmp(r->provider, provider->id) == 0) &&
            preg_resolve(r->path, path) && _wcsicmp(path, provider->path) == 0 &&
            prt_resolve_ini(path, r->ini, ini) && _wcsicmp(ini, provider->ini) == 0) return TRUE;
    }
    return FALSE;
}
static BOOL prt_match_enabled(const PRT_PROVIDER* provider,PLUGIN_RECORD* records,int count) {
    if (prt_stopping || !provider->active || !prt_is_allowed() || provider->busy) return FALSE;
    return prt_match_policy(provider,records,count);
}
static BOOL prt_is_enabled(const PRT_PROVIDER* provider) {
    PLUGIN_RECORD* records=NULL;int count;BOOL found;
    if (prt_stopping || !provider->active || !prt_is_allowed() || provider->busy) return FALSE;
    if (!preg_load_all(&records, &count)) return FALSE;
    found=prt_match_enabled(provider,records,count);HeapFree(GetProcessHeap(),0,records);return found;
}
static void prt_hide_actions(UINT provider) {
    UINT i;for (i = 0; i < prt_actions_used; ++i) if (prt_get_action(i)->provider == provider) prt_get_action(i)->active = FALSE;
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
    BOOL creating;
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
typedef struct PRT_FEATURE {
    UINT provider;
    BOOL active;
    TC_REGISTRATION token;
    WCHAR id[TC_PLUGIN_ID_CHARS], interface_id[TC_FEATURE_INTERFACE_CHARS];
    INT priority;
    const void* table;
    void* user;
} PRT_FEATURE;
static PRT_FEATURE** prt_feature_segments;
static UINT prt_feature_segment_count;
static PRT_FEATURE* prt_get_feature(UINT index) {
    return &prt_feature_segments[index / PRT_SEGMENT_SIZE][index % PRT_SEGMENT_SIZE];
}
static PRT_FEATURE* prt_alloc_feature(UINT index) {
    UINT block=index/PRT_SEGMENT_SIZE;
    if(block>=prt_feature_segment_count){
        UINT next=block+1;PRT_FEATURE** grown;
        if((SIZE_T)next>(SIZE_T)-1/sizeof(*grown))return NULL;
        if(prt_feature_segments)grown=(PRT_FEATURE**)HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,prt_feature_segments,(SIZE_T)next*sizeof(*grown));
        else grown=(PRT_FEATURE**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)next*sizeof(*grown));
        if(!grown)return NULL;prt_feature_segments=grown;prt_feature_segment_count=next;
    }
    if(!prt_feature_segments[block]){
        prt_feature_segments[block]=(PRT_FEATURE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,PRT_SEGMENT_SIZE*sizeof(PRT_FEATURE));
        if(!prt_feature_segments[block])return NULL;
    }
    return prt_get_feature(index);
}
static UINT prt_features_used;
/* Page blocks remain stable until process exit, including during callbacks. */
static PRT_PAGE** prt_page_segments;
static UINT prt_page_segment_count;
static PRT_PAGE* prt_get_page(UINT index) {return &prt_page_segments[index/PRT_SEGMENT_SIZE][index%PRT_SEGMENT_SIZE];}
static PRT_PAGE* prt_alloc_page(UINT index) {
    UINT block=index/PRT_SEGMENT_SIZE;
    if(block>=prt_page_segment_count){
        UINT next=block+1;PRT_PAGE** grown;
        if((SIZE_T)next>(SIZE_T)-1/sizeof(*grown))return NULL;
        if(prt_page_segments)grown=(PRT_PAGE**)HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,prt_page_segments,(SIZE_T)next*sizeof(*grown));
        else grown=(PRT_PAGE**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)next*sizeof(*grown));
        if(!grown)return NULL;prt_page_segments=grown;prt_page_segment_count=next;
    }
    if(!prt_page_segments[block]){
        prt_page_segments[block]=(PRT_PAGE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,PRT_SEGMENT_SIZE*sizeof(PRT_PAGE));
        if(!prt_page_segments[block])return NULL;
    }
    return prt_get_page(index);
}
/* Color blocks remain stable until process exit, including during callbacks. */
static PRT_COLOR** prt_color_segments;
static UINT prt_color_segment_count;
static PRT_COLOR* prt_get_color(UINT index) {
    return &prt_color_segments[index / PRT_SEGMENT_SIZE][index % PRT_SEGMENT_SIZE];
}
static PRT_COLOR* prt_alloc_color(UINT index) {
    UINT block=index/PRT_SEGMENT_SIZE;
    if(block>=prt_color_segment_count){
        UINT next=block+1;PRT_COLOR** grown;
        if((SIZE_T)next>(SIZE_T)-1/sizeof(*grown))return NULL;
        if(prt_color_segments)grown=(PRT_COLOR**)HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,prt_color_segments,(SIZE_T)next*sizeof(*grown));
        else grown=(PRT_COLOR**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)next*sizeof(*grown));
        if(!grown)return NULL;prt_color_segments=grown;prt_color_segment_count=next;
    }
    if(!prt_color_segments[block]){
        prt_color_segments[block]=(PRT_COLOR*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,PRT_SEGMENT_SIZE*sizeof(PRT_COLOR));
        if(!prt_color_segments[block])return NULL;
    }
    return prt_get_color(index);
}
static UINT prt_pages_used, prt_colors_used;
static TC_REGISTRATION prt_serial;
static DWORD prt_thread;
static ULONGLONG prt_activation_serial;
static BOOL prt_identity_failed, prt_syncing_policy;

static ULONGLONG prt_next_activation(void) {
    if (prt_identity_failed || prt_activation_serial == ~(ULONGLONG)0) {
        prt_identity_failed = TRUE;return 0;
    }
    return ++prt_activation_serial;
}

static BOOL prt_sync_policy(void) {
    PLUGIN_RECORD* records = NULL;int count = 0;UINT i;BOOL allowed, loaded, stamped;
    if (!prt_thread || GetCurrentThreadId() != prt_thread || prt_syncing_policy || prt_identity_failed) return FALSE;
    if (!prt_providers_used) return TRUE;
    if (!prt_stopping && prt_policy_current()) return TRUE;
    prt_syncing_policy = TRUE;
    /* Stamp before reading: a write during the read changes it and forces the next reload. */
    prt_policy_valid = FALSE;stamped = prt_read_stamp(prt_policy_path,&prt_policy_stamp);
    prt_policy_allowed = prt_read_allowed();
    allowed = !prt_stopping && prt_policy_allowed;
    loaded = !allowed || preg_load_all(&records,&count);
    for (i = 0; i < prt_providers_used; ++i) {
        PRT_PROVIDER* provider = prt_get_provider(i);
        BOOL enabled = allowed && loaded && prt_match_policy(provider,records,count);
        /* Policy is independent of busy/deferred stop. Observed off->on never revives a reference. */
        if (!provider->activation || provider->policy_enabled != enabled) {
            provider->activation = prt_next_activation();provider->policy_enabled = enabled;
        }
    }
    if (records) HeapFree(GetProcessHeap(),0,records);
    prt_policy_valid = stamped && loaded && !prt_stopping && !prt_identity_failed;
    prt_syncing_policy = FALSE;
    return loaded && !prt_identity_failed;
}

void prt_note_setting(const char* section, const char* key) {
    if (!prt_initialized || !prt_thread || GetCurrentThreadId() != prt_thread) return;
    if (!section && key) return;
    if (section && _stricmp(section,"Plugins") &&
        (_stricmp(section,"ETC") || (key && _stricmp(key,"MinimalMode"))) &&
        (_stricmp(section,"Status_DoNotEdit") || (key && _stricmp(key,"SafeMode")))) return;
    prt_policy_valid = FALSE;
    prt_sync_policy();
}
static void prt_settle(PRT_PROVIDER* provider);
static HRESULT prt_start(PRT_PROVIDER* provider);
static HRESULT WINAPI prt_copy_text(HWND owner, LPCWSTR text);
static void WINAPI prt_mark_changed(HWND page);

static PRT_PROVIDER* prt_owner(void* owner) {
    UINT i;
    if (!prt_thread || GetCurrentThreadId() != prt_thread) return NULL;
    for (i = 0; i < prt_providers_used; ++i) if (owner == prt_get_provider(i)) return prt_get_provider(i);
    return NULL;
}
static BOOL prt_can_register(PRT_PROVIDER* p) {
    if (!p) return FALSE;
    if (prt_serial == ~(TC_REGISTRATION)0) prt_identity_failed = TRUE;
    return !prt_identity_failed && !prt_stopping && !p->stopping && (p->starting || p->started);
}
static BOOL prt_check_label(LPCWSTR label) {
    return label && label[0] && wcsnlen_s(label, TC_PLUGIN_LABEL_CHARS) < TC_PLUGIN_LABEL_CHARS;
}
static void prt_leave(PRT_PROVIDER* p) {
    --p->busy;
    if (!p->busy) prt_settle(p);
}
static HRESULT WINAPI prt_register_command(void* owner, const TC_COMMAND_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_ACTION* a;UINT i, free_slot = UINT_MAX;char pluginId[TC_PLUGIN_ID_CHARS], actionId[TC_PLUGIN_ID_CHARS];
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || !prt_check_label(entry->japanese) || !prt_check_label(entry->english) || !entry->invoke) return E_INVALIDARG;
    for (i = 0; i < prt_actions_used; ++i) {
        if (prt_get_action(i)->provider == p->index && !wcscmp(prt_get_action(i)->id, entry->id)) break;
        if (!prt_get_action(i)->active && free_slot == UINT_MAX) free_slot = i;
    }
    if (i < prt_actions_used && prt_get_action(i)->active) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == prt_actions_used && free_slot != UINT_MAX) i = free_slot;
    if (i >= (UINT)INT_MAX) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    a = prt_alloc_action(i);if(!a)return E_OUTOFMEMORY;
    if (tc_utf16_to_utf8(p->id, pluginId, sizeof(pluginId)) <= 0 || tc_utf16_to_utf8(entry->id, actionId, sizeof(actionId)) <= 0) return E_INVALIDARG;
    if (tc_utf16_to_utf8(entry->japanese, a->japanese, sizeof(a->japanese)) <= 0 || tc_utf16_to_utf8(entry->english, a->english, sizeof(a->english)) <= 0) return E_INVALIDARG;
    if (i == prt_actions_used) ++prt_actions_used;
    a->provider = p->index;a->active = TRUE;a->token = ++prt_serial;
    wcscpy_s(a->id, TC_PLUGIN_ID_CHARS, entry->id);
    sprintf_s(a->key, sizeof(a->key), "dll:%s/%s", pluginId, actionId);
    a->user = entry->user;a->invoke = entry->invoke;*token = a->token;return S_OK;
}
static HRESULT WINAPI prt_register_page(void* owner, const TC_PAGE_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_PAGE* page;UINT i, free_slot = UINT_MAX;
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || !prt_check_label(entry->japanese) || !prt_check_label(entry->english) || !entry->create || !entry->apply) return E_INVALIDARG;
    for (i = 0; i < prt_pages_used; ++i) {
        if (prt_get_page(i)->provider == p->index && !wcscmp(prt_get_page(i)->id, entry->id)) break;
        if (!prt_get_page(i)->active && !prt_get_page(i)->window && !prt_get_page(i)->creating && free_slot == UINT_MAX) free_slot = i;
    }
    if (i < prt_pages_used && (prt_get_page(i)->active || prt_get_page(i)->window || prt_get_page(i)->creating)) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == prt_pages_used && free_slot != UINT_MAX) i = free_slot;
    if (i >= (UINT)(INT_MAX - PRT_PAGE_TREE_BASE)) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    page = prt_alloc_page(i);if(!page)return E_OUTOFMEMORY;
    if (i == prt_pages_used) ++prt_pages_used;
    page->provider = p->index;page->active = TRUE;page->token = ++prt_serial;
    wcscpy_s(page->id, TC_PLUGIN_ID_CHARS, entry->id);wcscpy_s(page->japanese, TC_PLUGIN_LABEL_CHARS, entry->japanese);wcscpy_s(page->english, TC_PLUGIN_LABEL_CHARS, entry->english);
    page->user = entry->user;page->create = entry->create;page->apply = entry->apply;*token = page->token;return S_OK;
}
static HRESULT WINAPI prt_register_color(void* owner, const TC_COLOR_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_COLOR* color;UINT i, free_slot = UINT_MAX;
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) || entry->mode > TC_COLOR_REPLACE || !entry->query) return E_INVALIDARG;
    for (i = 0; i < prt_colors_used; ++i) {
        if (prt_get_color(i)->provider == p->index && !wcscmp(prt_get_color(i)->id, entry->id)) break;
        if (!prt_get_color(i)->active && free_slot == UINT_MAX) free_slot = i;
    }
    if (i < prt_colors_used && prt_get_color(i)->active) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == prt_colors_used && free_slot != UINT_MAX) i = free_slot;
    if (i == UINT_MAX) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    color = prt_alloc_color(i);if(!color)return E_OUTOFMEMORY;
    if (i == prt_colors_used) ++prt_colors_used;
    color->provider = p->index;color->active = TRUE;color->token = ++prt_serial;
    wcscpy_s(color->id, TC_PLUGIN_ID_CHARS, entry->id);color->priority = entry->priority;color->mode = entry->mode;color->query = entry->query;color->user = entry->user;
    *token = color->token;return S_OK;
}
static HRESULT WINAPI prt_add_feature(void* owner, const TC_FEATURE_REGISTRATION* entry, TC_REGISTRATION* token) {
    PRT_PROVIDER* p = prt_owner(owner);PRT_FEATURE* feature;UINT i, free_slot = UINT_MAX;
    if (!token) return E_POINTER;*token = 0;
    if (!prt_can_register(p)) return E_ACCESSDENIED;
    if (!entry || entry->size < sizeof(*entry) || !entry->id || !prt_check_id(entry->id) ||
        !prt_check_interface(entry->interface_id) || !entry->table) return E_INVALIDARG;
    for (i = 0; i < prt_features_used; ++i) {
        if (prt_get_feature(i)->provider == p->index && !wcscmp(prt_get_feature(i)->id, entry->id)) break;
        if (!prt_get_feature(i)->active && free_slot == UINT_MAX) free_slot = i;
    }
    if (i < prt_features_used && prt_get_feature(i)->active) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (i == prt_features_used && free_slot != UINT_MAX) i = free_slot;
    if (i == UINT_MAX) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    feature = prt_alloc_feature(i);if(!feature)return E_OUTOFMEMORY;
    if (i == prt_features_used) ++prt_features_used;
    feature->provider = p->index;feature->active = TRUE;feature->token = ++prt_serial;
    wcscpy_s(feature->id, TC_PLUGIN_ID_CHARS, entry->id);
    wcscpy_s(feature->interface_id, TC_FEATURE_INTERFACE_CHARS, entry->interface_id);
    feature->priority = entry->priority;feature->table = entry->table;feature->user = entry->user;
    *token = feature->token;return S_OK;
}
static HRESULT WINAPI prt_remove(void* owner, TC_REGISTRATION token) {
    PRT_PROVIDER* p = prt_owner(owner);UINT i, index;
    if (!p || !token) return E_INVALIDARG;
    index = p->index;
    for (i = 0; i < prt_actions_used; ++i) if (prt_get_action(i)->token == token && prt_get_action(i)->provider == index) {prt_get_action(i)->active = FALSE;return S_OK;}
    for (i = 0; i < prt_pages_used; ++i) if (prt_get_page(i)->token == token && prt_get_page(i)->provider == index) {prt_get_page(i)->active = FALSE;return S_OK;}
    for (i = 0; i < prt_colors_used; ++i) if (prt_get_color(i)->token == token && prt_get_color(i)->provider == index) {prt_get_color(i)->active = FALSE;return S_OK;}
    for (i = 0; i < prt_features_used; ++i) if (prt_get_feature(i)->token == token && prt_get_feature(i)->provider == index) {prt_get_feature(i)->active = FALSE;return S_OK;}
    return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
}
static void WINAPI prt_log(LPCWSTR text) {
    char message[1024];
    if (text && tc_utf16_to_utf8(text, message, sizeof(message)) > 0) WriteNormalLog(message);
}
static const TC_HOST_REGISTRY prt_registry = { sizeof(TC_HOST_REGISTRY), prt_register_command, prt_register_page, prt_register_color, prt_remove };
static const TC_HOST_FEATURES prt_feature_api = { sizeof(TC_HOST_FEATURES), prt_add_feature };
static const TC_HOST_SERVICES prt_services = { sizeof(TC_HOST_SERVICES), prt_copy_text, prt_mark_changed, prt_log };
static HRESULT WINAPI prt_query_host(void* owner, UINT abi, LPCWSTR id, const void** out) {
    if (!out) return E_POINTER;*out = NULL;
    if (!prt_owner(owner)) return E_ACCESSDENIED;
    if (abi != TC_PLUGIN_ABI || !id) return E_NOINTERFACE;
    if (!wcscmp(id, TC_HOST_REGISTRY_ID)) *out = &prt_registry;
    else if (!wcscmp(id, TC_HOST_SERVICES_ID)) *out = &prt_services;
    else if (!wcscmp(id, TC_HOST_FEATURES_ID)) *out = &prt_feature_api;
    else return E_NOINTERFACE;
    return S_OK;
}
static void prt_hide_provider(PRT_PROVIDER* p) {
    UINT i,index = p->index;
    prt_hide_actions(index);
    for (i = 0; i < prt_pages_used; ++i) if (prt_get_page(i)->provider == index) prt_get_page(i)->active = FALSE;
    for (i = 0; i < prt_colors_used; ++i) if (prt_get_color(i)->provider == index) prt_get_color(i)->active = FALSE;
    for (i = 0; i < prt_features_used; ++i) if (prt_get_feature(i)->provider == index) prt_get_feature(i)->active = FALSE;
}

/* Owned references use the same deterministic winner order as legacy dispatch.
   Capture permits busy owners; run rejects recursion without choosing a runner-up. */
HRESULT prt_capture_feature(LPCWSTR interface_id, PRT_FEATURE_REF* reference) {
    UINT i, selected = UINT_MAX;
    if (!reference) return E_POINTER;
    ZeroMemory(reference,sizeof(*reference));
    if (!prt_check_interface(interface_id)) return E_INVALIDARG;
    /* No registration for this interface: answer without reading the INI (periodic route checks). */
    for (i = 0; i < prt_features_used && (!prt_get_feature(i)->active || wcscmp(prt_get_feature(i)->interface_id,interface_id)); ++i) {}
    if (i == prt_features_used) return E_NOINTERFACE;
    if (!prt_thread || GetCurrentThreadId() != prt_thread || prt_stopping || !prt_is_allowed()) return E_ACCESSDENIED;
    if (!prt_sync_policy()) return E_FAIL;
    for (i = 0; i < prt_features_used; ++i) {
        PRT_FEATURE* feature = prt_get_feature(i);
        PRT_PROVIDER* provider = prt_get_provider(feature->provider);
        PRT_FEATURE* previous;int order;
        if (!feature->active || !feature->token || !provider->active || !provider->started ||
            !provider->policy_enabled || wcscmp(feature->interface_id,interface_id)) continue;
        if (selected == UINT_MAX) { selected=i;continue; }
        previous=prt_get_feature(selected);
        if (feature->priority != previous->priority) {
            if (feature->priority > previous->priority) selected=i;
            continue;
        }
        order=wcscmp(provider->id,prt_get_provider(previous->provider)->id);
        if (order < 0 || (!order && wcscmp(feature->id,previous->id) < 0)) selected=i;
    }
    if (selected == UINT_MAX) return E_NOINTERFACE;
    {
        PRT_FEATURE* feature=prt_get_feature(selected);
        reference->index=selected;reference->provider=feature->provider;
        reference->registration=feature->token;
        reference->activation=prt_get_provider(feature->provider)->activation;
        lstrcpyW(reference->interface_id,interface_id);
    }
    return S_OK;
}

HRESULT prt_check_feature(const PRT_FEATURE_REF* reference) {
    PRT_FEATURE_REF current;HRESULT result;
    if (!reference || !reference->registration || !reference->activation ||
        !prt_check_interface(reference->interface_id)) return E_INVALIDARG;
    result=prt_capture_feature(reference->interface_id,&current);
    if (result == E_NOINTERFACE) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (FAILED(result)) return result;
    if (current.index != reference->index || current.provider != reference->provider ||
        current.registration != reference->registration || current.activation != reference->activation)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return S_OK;
}

HRESULT prt_run_feature(const PRT_FEATURE_REF* reference, PRT_FEATURE_ADAPTER adapter,
    void* context, BOOL* invoked) {
    PRT_FEATURE* feature;PRT_PROVIDER* provider;const void* table;void* user;
    HRESULT result, current;
    if (!invoked) return E_POINTER;
    *invoked=FALSE;
    if (!adapter) return E_INVALIDARG;
    result=prt_check_feature(reference);
    if (FAILED(result)) return result;
    feature=prt_get_feature(reference->index);provider=prt_get_provider(feature->provider);
    if (provider->busy) return HRESULT_FROM_WIN32(ERROR_BUSY);
    table=feature->table;user=feature->user;
    ++provider->busy;*invoked=TRUE;
    result=adapter(table,user,context);
    prt_leave(provider);
    current=prt_check_feature(reference);
    return FAILED(current) ? current : result;
}

/* Snapshot-safe work holds are host-private and UI-owned. No /1 callback is
   moved to a worker, and no borrowed provider context crosses that boundary. */
typedef struct PRT_HOLD_NODE {
    struct PRT_HOLD_NODE* next;
    ULONGLONG token;
    PRT_FEATURE_REF owner;
} PRT_HOLD_NODE;
static PRT_HOLD_NODE* prt_holds;
static UINT prt_holds_used;
#define PRT_HOLD_LIMIT 64u

BOOL prt_owns_thread(void) {
    return prt_thread && GetCurrentThreadId()==prt_thread;
}
static PRT_HOLD_NODE* prt_find_hold(const PRT_FEATURE_HOLD* hold) {
    PRT_HOLD_NODE* node;
    if (!hold || !hold->token) return NULL;
    for (node=prt_holds;node;node=node->next) if (node->token==hold->token) return node;
    return NULL;
}
HRESULT prt_hold_feature(const PRT_FEATURE_REF* reference, PRT_FEATURE_HOLD* hold,
    const void** table, void** user) {
    PRT_HOLD_NODE* node;PRT_FEATURE* feature;PRT_PROVIDER* provider;HRESULT result;
    ULONGLONG token;
    if (!hold || !table || !user) return E_POINTER;
    result=prt_check_feature(reference);if (FAILED(result)) return result;
    feature=prt_get_feature(reference->index);provider=prt_get_provider(feature->provider);
    if (provider->busy || provider->pure) return HRESULT_FROM_WIN32(ERROR_BUSY);
    if (prt_holds_used==PRT_HOLD_LIMIT) return E_OUTOFMEMORY;
    node=(PRT_HOLD_NODE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*node));
    if (!node) return E_OUTOFMEMORY;
    token=prt_next_activation();
    if (!token) {HeapFree(GetProcessHeap(),0,node);return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);}
    node->token=token;node->owner=*reference;node->next=prt_holds;prt_holds=node;
    ++prt_holds_used;++provider->pure;
    *table=feature->table;*user=feature->user;hold->token=token;
    return S_OK;
}
HRESULT prt_call_hold(const PRT_FEATURE_HOLD* hold, PRT_HOLD_ADAPTER adapter, void* context) {
    PRT_HOLD_NODE* node;PRT_PROVIDER* provider;HRESULT result;
    if (!adapter) return E_INVALIDARG;
    if (!prt_owns_thread()) return E_ACCESSDENIED;
    node=prt_find_hold(hold);if (!node) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    provider=prt_get_provider(node->owner.provider);
    ++provider->busy;result=adapter(context);prt_leave(provider);
    return result;
}
HRESULT prt_drop_hold(PRT_FEATURE_HOLD* hold, HRESULT* current) {
    PRT_HOLD_NODE** link;PRT_HOLD_NODE* node;PRT_PROVIDER* provider;PRT_FEATURE_REF owner;
    if (!hold || !current) return E_POINTER;
    if (!prt_owns_thread()) return E_ACCESSDENIED;
    for (link=&prt_holds;*link;link=&(*link)->next) if ((*link)->token==hold->token && hold->token) break;
    if (!*link) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    node=*link;owner=node->owner;provider=prt_get_provider(owner.provider);
    if (!provider->pure) return E_UNEXPECTED;
    *link=node->next;--prt_holds_used;--provider->pure;hold->token=0;
    HeapFree(GetProcessHeap(),0,node);
    *current=prt_check_feature(&owner);
    return S_OK;
}

HRESULT prt_call_feature(LPCWSTR interface_id, PRT_FEATURE_ADAPTER adapter, void* context, BOOL* invoked) {
    UINT i;PRT_FEATURE* selected = NULL;PRT_PROVIDER* provider;HRESULT result;
    const void* table;void* user;PLUGIN_RECORD* records=NULL;int count=0;BOOL loaded=FALSE,valid=FALSE;
    if (!invoked) return E_POINTER;*invoked = FALSE;
    if (!prt_check_interface(interface_id) || !adapter) return E_INVALIDARG;
    if (!prt_thread || GetCurrentThreadId() != prt_thread || prt_stopping || !prt_is_allowed()) return E_ACCESSDENIED;
    for (i = 0; i < prt_features_used; ++i) {
        PRT_FEATURE* current = prt_get_feature(i);PRT_PROVIDER* p = prt_get_provider(current->provider);int order;
        if (!current->active || !p->active || wcscmp(current->interface_id, interface_id)) continue;
        if (!p->busy) {
            if (!loaded) {loaded=TRUE;valid=preg_load_all(&records,&count);}
            if (!valid) return E_FAIL;
            if (!prt_match_enabled(p,records,count)) continue;
        }
        if (!selected) { selected = current;continue; }
        if (current->priority != selected->priority) { if (current->priority > selected->priority) selected = current;continue; }
        order = wcscmp(p->id, prt_get_provider(selected->provider)->id);
        if (order < 0 || (!order && wcscmp(current->id, selected->id) < 0)) selected = current;
    }
    if (records) HeapFree(GetProcessHeap(),0,records);
    if (!selected) return E_NOINTERFACE;
    provider = prt_get_provider(selected->provider);
    if (provider->busy) return HRESULT_FROM_WIN32(ERROR_BUSY);
    table = selected->table;user = selected->user;
    ++provider->busy;*invoked = TRUE;result = adapter(table, user, context);prt_leave(provider);
    return result;
}
static LRESULT CALLBACK prt_page_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclass, DWORD_PTR data) {
    PRT_PAGE* page = (PRT_PAGE*)data;PRT_PROVIDER* p = prt_get_provider(page->provider);
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
    if (prt_stopping) for (i = 0; i < prt_pages_used; ++i) if (prt_get_page(i)->provider == p->index && prt_get_page(i)->window) {
        ++p->busy;DestroyWindow(prt_get_page(i)->window);--p->busy;
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
    PRT_PROVIDER* provider=prt_get_provider(providerIndex);PRT_ACTION* pending=NULL;UINT *slots=NULL;BOOL *reserved=NULL;
    UINT count,i,j,next,capacity;HRESULT result=E_INVALIDARG;
    prt_hide_actions(providerIndex);if(!provider->api)return S_OK;
    count=provider->api->count();if(!count)return S_OK;
    if(prt_identity_failed || (ULONGLONG)count > ~(TC_REGISTRATION)0-prt_serial){
        prt_identity_failed=TRUE;return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
    if(count>(UINT)INT_MAX || prt_actions_used>(UINT)INT_MAX-count)return E_INVALIDARG;
    capacity=prt_actions_used+count;
    if((SIZE_T)count>(SIZE_T)-1/sizeof(*pending)||(SIZE_T)count>(SIZE_T)-1/sizeof(*slots)||(SIZE_T)capacity>(SIZE_T)-1/sizeof(*reserved))return E_OUTOFMEMORY;
    pending=(PRT_ACTION*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)count*sizeof(*pending));
    slots=(UINT*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)count*sizeof(*slots));
    reserved=(BOOL*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)capacity*sizeof(*reserved));
    if(!pending||!slots||!reserved){result=E_OUTOFMEMORY;goto done;}
    for(i=0;i<count;++i){
        TC_PLUGIN_ACTION en={sizeof(en)},ja={sizeof(ja)};char pluginId[TC_PLUGIN_ID_CHARS],actionId[TC_PLUGIN_ID_CHARS];
        if(provider->api->describe(i,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),&en)!=S_OK||
           provider->api->describe(i,MAKELANGID(LANG_JAPANESE,SUBLANG_DEFAULT),&ja)!=S_OK||
           en.size<sizeof(en)||ja.size<sizeof(ja)||!prt_check_id(en.id)||!prt_check_id(ja.id)||wcscmp(en.id,ja.id)||
           !en.label[0]||!ja.label[0]||en.label[TC_PLUGIN_LABEL_CHARS-1]||ja.label[TC_PLUGIN_LABEL_CHARS-1])goto done;
        for(j=0;j<i;++j)if(wcscmp(en.id,pending[j].id)==0)goto done;
        if(tc_utf16_to_utf8(provider->id,pluginId,sizeof(pluginId))<=0||tc_utf16_to_utf8(en.id,actionId,sizeof(actionId))<=0||
           tc_utf16_to_utf8(en.label,pending[i].english,sizeof(pending[i].english))<=0||tc_utf16_to_utf8(ja.label,pending[i].japanese,sizeof(pending[i].japanese))<=0)goto done;
        pending[i].provider=providerIndex;pending[i].active=TRUE;wcscpy_s(pending[i].id,_countof(pending[i].id),en.id);
        sprintf_s(pending[i].key,sizeof(pending[i].key),"dll:%s/%s",pluginId,actionId);
    }
    /* Reserve existing identities before reusing inactive slots. */
    for(i=0;i<count;++i){for(j=0;j<prt_actions_used;++j)if(strcmp(pending[i].key,prt_get_action(j)->key)==0)break;slots[i]=j;if(j<prt_actions_used)reserved[j]=TRUE;}
    next=prt_actions_used;
    for(i=0;i<count;++i)if(slots[i]==prt_actions_used){
        for(j=0;j<prt_actions_used;++j)if(!reserved[j]&&!prt_get_action(j)->active)break;
        if(j==prt_actions_used)j=next++;slots[i]=j;reserved[j]=TRUE;
    }
    for(j=prt_actions_used;j<next;++j)if(!prt_alloc_action(j)){result=E_OUTOFMEMORY;goto done;}
    /* Legacy describe callbacks may register other entries; recheck after reentry. */
    if(prt_identity_failed || (ULONGLONG)count > ~(TC_REGISTRATION)0-prt_serial){
        prt_identity_failed=TRUE;result=HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);goto done;
    }
    for(i=0;i<count;++i)pending[i].token=++prt_serial;
    prt_actions_used=next;for(i=0;i<count;++i)*prt_get_action(slots[i])=pending[i];result=S_OK;
done:
    if(pending)HeapFree(GetProcessHeap(),0,pending);if(slots)HeapFree(GetProcessHeap(),0,slots);if(reserved)HeapFree(GetProcessHeap(),0,reserved);return result;
}
static HRESULT prt_acquire(LPCWSTR registration, LPCWSTR path, PRT_PROVIDER** out) {
    HMODULE module;TC_GET_INTERFACE query;const TC_PLUGIN_INFO* info = NULL;
    const TC_ACTION_PROVIDER* api = NULL;const TC_PLUGIN_SETTINGS* settings = NULL;const TC_PLUGIN_EXTENSION* extension = NULL;const TC_PLUGIN_RUNTIME* lifecycle = NULL;UINT i;HRESULT result;
    *out = NULL;
    for (i = 0; i < prt_providers_used; ++i) if (wcscmp(registration, prt_get_provider(i)->registration) == 0) {
        if (_wcsicmp(path, prt_get_provider(i)->path)) return HRESULT_FROM_WIN32(ERROR_BUSY);
        { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(declared, prt_get_provider(i)->id)) return HRESULT_FROM_WIN32(ERROR_BUSY); }
        *out = prt_get_provider(i);return S_OK;
    }
    if (prt_providers_used == UINT_MAX) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return HRESULT_FROM_WIN32(GetLastError());
    query = (TC_GET_INTERFACE)GetProcAddress(module, "TClockGetInterface");if (!query) goto reject;
    if (query(TC_PLUGIN_ABI, TC_PLUGIN_INFO_ID, (const void**)&info) != S_OK || !info || info->size < sizeof(*info) ||
        !prt_check_id(info->id) || !info->name[0] || info->name[TC_PLUGIN_LABEL_CHARS - 1]) goto reject;
    { WCHAR declared[32];if (preg_get_identity(path, declared, 32) && wcscmp(info->id, declared)) goto reject; }
    for (i = 0; i < prt_providers_used; ++i) if (wcscmp(info->id, prt_get_provider(i)->id) == 0) goto reject;
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
    *out = prt_alloc_provider(prt_providers_used);
    if (!*out) {FreeLibrary(module);return E_OUTOFMEMORY;}
    ++prt_providers_used;prt_policy_valid = FALSE;
    (*out)->runtime = lifecycle;
    (*out)->module = module;(*out)->api = api;(*out)->settings = settings;(*out)->extension = extension;
    wcscpy_s((*out)->path, MAX_PATH, path);wcscpy_s((*out)->registration, 64, registration);
    wcscpy_s((*out)->id, TC_PLUGIN_ID_CHARS, info->id);return S_OK;
reject:
    FreeLibrary(module);return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
static HRESULT prt_reload(PRT_PROVIDER* provider, LPCWSTR ini, HWND owner, BOOL english) {
    TC_SETTINGS_CONTEXT context = { sizeof(context) };HRESULT result = S_OK;UINT index = provider->index;
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
    if (result == S_OK) { wcscpy_s(provider->ini, MAX_PATH, ini);prt_policy_valid = FALSE;result = prt_read_actions(index); }
    else if (SUCCEEDED(result)) result = E_FAIL;
    return result;
}
static HRESULT prt_start(PRT_PROVIDER* p) {
    HRESULT result;
    if (prt_stopping || p->busy || p->pages || p->started) return HRESULT_FROM_WIN32(ERROR_BUSY);
    p->activation = prt_next_activation();p->policy_enabled = FALSE;prt_policy_valid = FALSE;
    if (!p->activation) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
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
    PLUGIN_RECORD* records=NULL;int count=0, i;UINT j;BOOL allowed;WCHAR legacy[MAX_PATH];
    if (!prt_thread) prt_thread = GetCurrentThreadId();
    if (GetCurrentThreadId() != prt_thread || prt_refreshing || prt_stopping) return;
    prt_refreshing = TRUE;prt_policy_valid = FALSE;allowed=prt_is_allowed() && tc_utf8_to_utf16(g_inifile,legacy,_countof(legacy)) > 0;
    prt_sync_policy();
    if(allowed){preg_discover();if(!preg_load_all(&records,&count)){prt_report(ERROR_NOT_ENOUGH_MEMORY);prt_refreshing=FALSE;return;}}
    prt_initialized = TRUE;
    for (j = 0; j < prt_providers_used; ++j) prt_get_provider(j)->wanted = FALSE;
    if (allowed) {
        for (i = PREG_KNOWN; i < count; ++i) {
            PLUGIN_RECORD* r = &records[i];WCHAR path[MAX_PATH], ini[MAX_PATH];PRT_PROVIDER* p;HRESULT result;
            if (r->kind != PREG_KIND_DLL || !r->enabled || r->conflict || (r->automatic && !r->discovered) || !preg_resolve(r->path, path)) continue;
            if (!prt_resolve_ini(path, r->ini, ini)) { prt_report(ERROR_INVALID_NAME);continue; }
            result = prt_acquire(r->id, path, &p);
            if (FAILED(result)) {prt_report((DWORD)result);continue;}
            p->wanted = TRUE;wcscpy_s(p->desired_ini, MAX_PATH, ini);
            if (p->started && (_wcsicmp(p->ini, ini) || _wcsicmp(p->legacy, legacy))) {
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
        PRT_PROVIDER* p = prt_get_provider(j);
        if (!p->wanted) {p->active = FALSE;p->pending = p->started;p->restart = FALSE;prt_settle(p);}
    }
    if(records)HeapFree(GetProcessHeap(),0,records);prt_refreshing = FALSE;
    prt_sync_policy();
}
void prt_shutdown(void) {
    UINT i;
    if (prt_thread && GetCurrentThreadId() != prt_thread) return;
    prt_stopping = TRUE;prt_policy_valid = FALSE;
    for (i = 0; i < prt_providers_used; ++i) {
        PRT_PROVIDER* p = prt_get_provider(i);p->wanted = FALSE;p->active = FALSE;p->restart = FALSE;p->pending = p->started;
    }
    for (i = 0; i < prt_providers_used; ++i) {
        prt_settle(prt_get_provider(i));
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
    prt_initialize();return index >= 0 && (UINT)index < prt_actions_used ? prt_get_action(index)->key : NULL;
}
const char* prt_get_label(const char* key, BOOL english) {
    UINT i;if (!key || strncmp(key, "dll:", 4)) return NULL;prt_initialize();
    for (i = 0; i < prt_actions_used; ++i)
        if (prt_get_action(i)->active && prt_get_provider(prt_get_action(i)->provider)->active && strcmp(key, prt_get_action(i)->key) == 0 && prt_is_enabled(prt_get_provider(prt_get_action(i)->provider)))
            return english ? prt_get_action(i)->english : prt_get_action(i)->japanese;
    return NULL;
}
static HRESULT WINAPI prt_copy_text(HWND owner, LPCWSTR text) {
    HGLOBAL memory;
    WCHAR* target;
    size_t count;
    DWORD error;
    int attempt;
    if (!text) return E_INVALIDARG;
    count = wcsnlen_s(text, 65536);
    if (count == 65536) return E_INVALIDARG;
    memory = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(WCHAR));
    if (!memory) return E_OUTOFMEMORY;
    target = (WCHAR*)GlobalLock(memory);
    if (!target) { GlobalFree(memory); return E_OUTOFMEMORY; }
    memcpy(target, text, (count + 1) * sizeof(WCHAR));
    GlobalUnlock(memory);
    /* Another process (clipboard history, RDP rdpclip) can hold the clipboard briefly; retry up to 80 ms. */
    for (attempt = 0; !OpenClipboard(owner); ++attempt) {
        error = GetLastError();
        if (attempt == 4) { GlobalFree(memory); return HRESULT_FROM_WIN32(error ? error : ERROR_ACCESS_DENIED); }
        Sleep(20);
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        error = GetLastError(); CloseClipboard(); GlobalFree(memory);
        return HRESULT_FROM_WIN32(error ? error : ERROR_WRITE_FAULT);
    }
    CloseClipboard();
    return S_OK;
}
BOOL prt_capture_command(const char* key, BOOL allow_busy, PRT_COMMAND* command) {
    UINT i;
    if (!command) return FALSE;
    ZeroMemory(command,sizeof(*command));
    if (!key || strnlen(key,sizeof(command->key)) >= sizeof(command->key) ||
        !prt_thread || GetCurrentThreadId() != prt_thread || !prt_sync_policy()) return FALSE;
    for (i = 0; i < prt_actions_used; ++i) {
        PRT_ACTION* action = prt_get_action(i);PRT_PROVIDER* provider = prt_get_provider(action->provider);
        if (action->active && action->token && provider->active && provider->started && provider->policy_enabled &&
            (allow_busy || !provider->busy) && !strcmp(key,action->key)) {
            strcpy_s(command->key,sizeof(command->key),key);command->provider=action->provider;
            command->registration=action->token;command->activation=provider->activation;return TRUE;
        }
    }
    return FALSE;
}

BOOL prt_run_command(const PRT_COMMAND* command, HWND owner, BOOL english) {
    UINT i;
    if (!command || !command->registration || !command->activation ||
        strnlen(command->key,sizeof(command->key)) >= sizeof(command->key) ||
        !prt_thread || GetCurrentThreadId() != prt_thread || !prt_sync_policy()) return FALSE;
    for (i = 0; i < prt_actions_used; ++i) {
        PRT_ACTION* action = prt_get_action(i);PRT_PROVIDER* provider = prt_get_provider(action->provider);
        if (action->active && action->provider == command->provider && action->token == command->registration &&
            provider->active && provider->started && provider->policy_enabled && !provider->busy &&
            provider->activation == command->activation && !strcmp(command->key,action->key)) {
            TC_ACTION_CONTEXT context = { sizeof(context) };HRESULT result;
            context.owner = owner;context.language = MAKELANGID(english ? LANG_ENGLISH : LANG_JAPANESE, SUBLANG_DEFAULT);context.copy_text = prt_copy_text;
            ++provider->busy;result = action->invoke ? action->invoke(action->user, &context) : provider->api->invoke(action->id, &context);prt_leave(provider);
            if (FAILED(result)) prt_report((DWORD)result);return SUCCEEDED(result);
        }
    }
    return FALSE;
}

BOOL prt_execute(const char* key, HWND owner, BOOL english) {
    PRT_COMMAND command;
    return prt_capture_command(key,FALSE,&command) && prt_run_command(&command,owner,english);
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
    page = prt_get_page(index);p = prt_get_provider(page->provider);
    if (!page->active || !p->active || !prt_is_enabled(p) || GetFileAttributesW(p->path) == INVALID_FILE_ATTRIBUTES) return NULL;
    return english ? page->english : page->japanese;
}
ULONGLONG prt_page_token(int index) {
    return prt_page_label(index, FALSE) ? prt_get_page(index)->token : 0;
}
/* tclock.page/1 substitution tables are immutable while registered, so reading their
   strings needs no callback pin. Only running, enabled providers offer a page. */
static const TC_PAGE_SUBSTITUTE* prt_get_substitute(const PRT_FEATURE* feature) {
    const TC_PAGE_SUBSTITUTE* table = (const TC_PAGE_SUBSTITUTE*)feature->table;
    const PRT_PROVIDER* p = prt_get_provider(feature->provider);
    if (!feature->active || wcscmp(feature->interface_id, TC_PLUGIN_PAGE_ID) || !p->active || !prt_is_enabled(p) ||
        !table || table->size != sizeof(*table) || !table->target || !table->page ||
        wcsnlen(table->target, TC_PLUGIN_ID_CHARS) == TC_PLUGIN_ID_CHARS || wcsnlen(table->page, TC_PLUGIN_ID_CHARS) == TC_PLUGIN_ID_CHARS) return NULL;
    return table;
}
int prt_find_substitute(LPCWSTR provider_id, LPCWSTR target) {
    UINT i, j;
    if (!provider_id || !provider_id[0] || !target) return -1;
    for (i = 0; i < prt_features_used; ++i) {
        PRT_FEATURE* feature = prt_get_feature(i);const TC_PAGE_SUBSTITUTE* table = prt_get_substitute(feature);
        if (!table || wcscmp(table->target, target) || wcscmp(prt_get_provider(feature->provider)->id, provider_id)) continue;
        for (j = 0; j < prt_pages_used; ++j) {
            PRT_PAGE* page = prt_get_page(j);
            if (page->provider == feature->provider && !wcscmp(page->id, table->page) && prt_page_label((int)j, FALSE)) return (int)j;
        }
    }
    return -1;
}
BOOL prt_is_substitute(int index) {
    UINT i;PRT_PAGE* page;
    if (index < 0 || (UINT)index >= prt_pages_used) return FALSE;
    page = prt_get_page(index);
    for (i = 0; i < prt_features_used; ++i) {
        PRT_FEATURE* feature = prt_get_feature(i);const TC_PAGE_SUBSTITUTE* table = prt_get_substitute(feature);
        if (table && feature->provider == page->provider && !wcscmp(table->page, page->id)) return TRUE;
    }
    return FALSE;
}
BOOL prt_owns_page(int index, HWND page) {
    return index >= 0 && (UINT)index < prt_pages_used && page && prt_get_page(index)->window == page && IsWindow(page);
}
HWND prt_create_page(int index, HWND owner, BOOL english) {
    TC_EXTENSION_CONTEXT context;WCHAR legacy[MAX_PATH];PRT_PAGE* page;PRT_PROVIDER* p;HWND window;
    if (!prt_page_label(index, english)) return NULL;
    page = prt_get_page(index);p = prt_get_provider(page->provider);
    if (page->window) return page->window;
    prt_make_context(p, owner, english, &context, legacy);
    ++p->busy;++p->pages;page->creating = TRUE;window = page->create(page->user, &context);
    if (window && IsWindow(window) && GetParent(window) == owner && SetWindowSubclass(window, prt_page_proc, (UINT_PTR)page, (DWORD_PTR)page)) page->window = window;
    else {if (window && IsWindow(window)) DestroyWindow(window);window = NULL;--p->pages;}
    page->creating = FALSE;prt_leave(p);return page->window == window && IsWindow(window) ? window : NULL;
}
BOOL prt_apply_page(int index, HWND window) {
    HRESULT result;PRT_PAGE* page;PRT_PROVIDER* p;
    if (!window || index < 0 || (UINT)index >= prt_pages_used) return TRUE;
    page = prt_get_page(index);p = prt_get_provider(page->provider);
    if (page->window != window || !IsWindow(window) || !p->started) return FALSE;
    ++p->busy;result = page->apply(page->user, window);prt_leave(p);
    if (FAILED(result)) {
        MessageBoxW(GetParent(window), b_EnglishMenu ? L"Could not save plugin settings." : L"\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002", L"TClock", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}
typedef struct PRT_COLOR_SNAPSHOT { UINT index; TC_REGISTRATION token; } PRT_COLOR_SNAPSHOT;
static int prt_compare_colors(const void* first, const void* second) {
    const PRT_COLOR_SNAPSHOT* left=(const PRT_COLOR_SNAPSHOT*)first;const PRT_COLOR_SNAPSHOT* right=(const PRT_COLOR_SNAPSHOT*)second;
    const PRT_COLOR* a=prt_get_color(left->index);const PRT_COLOR* b=prt_get_color(right->index);int result;
    if(a->priority!=b->priority)return a->priority>b->priority?-1:1;
    result=wcscmp(prt_get_provider(a->provider)->id,prt_get_provider(b->provider)->id);
    return result?result:wcscmp(a->id,b->id);
}
BOOL prt_handle_message(UINT message, LPARAM lParam, LRESULT* result) {
    const void* payload;const TC_TEXT_CONTEXT* context;COLORREF color;UINT i,count=0,used=0;
    PRT_COLOR_SNAPSHOT local[128],*snapshot=local;TPI_RECEIVE_RESULT transport;
    if(!result)return FALSE;
    transport=tpi_receive(message,lParam,TC_PLUGIN_COLOR_DATA,sizeof(TC_TEXT_CONTEXT),&payload);
    if(transport==TPI_NOT_MINE)return FALSE;*result=0;if(transport!=TPI_ACCEPTED)return TRUE;
    context=(const TC_TEXT_CONTEXT*)payload;if(context->size!=sizeof(*context))return TRUE;color=context->base;
    if(prt_is_allowed()&&!prt_stopping){
        for(i=0;i<prt_colors_used;++i)if(prt_get_color(i)->active&&prt_get_provider(prt_get_color(i)->provider)->active)++count;
        if(count>_countof(local)){
            /* A missing reply makes the drawing side fall back to its base color. */
            if((SIZE_T)count>(SIZE_T)-1/sizeof(*snapshot))return TRUE;
            snapshot=(PRT_COLOR_SNAPSHOT*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)count*sizeof(*snapshot));if(!snapshot)return TRUE;
        }
        for(i=0;i<prt_colors_used;++i){PRT_COLOR* hook=prt_get_color(i);if(hook->active&&prt_get_provider(hook->provider)->active){snapshot[used].index=i;snapshot[used].token=hook->token;++used;}}
        /* Token snapshots prevent a reused slot from running in this dispatch. */
        qsort(snapshot,used,sizeof(*snapshot),prt_compare_colors);
        for(i=0;i<used;++i){
            PRT_COLOR* hook=prt_get_color(snapshot[i].index);PRT_PROVIDER* p=prt_get_provider(hook->provider);TC_TEXT_CONTEXT current=*context;COLORREF next=color;HRESULT status;UINT mode=hook->mode;
            if(!hook->active||hook->token!=snapshot[i].token||!p->active||p->busy)continue;
            current.base=color;++p->busy;status=hook->query(hook->user,&current,&next);prt_leave(p);
            if(status==S_OK){color=next;if(mode==TC_COLOR_REPLACE)break;}else if(FAILED(status))prt_report((DWORD)status);
        }
    }
    if(snapshot!=local)HeapFree(GetProcessHeap(),0,snapshot);
    *result=(LRESULT)(0x01000000u|(color&0x00ffffffu));return TRUE;
}
