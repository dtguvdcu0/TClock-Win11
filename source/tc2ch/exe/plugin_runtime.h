#ifndef TCLOCK_PLUGIN_RUNTIME_H
#define TCLOCK_PLUGIN_RUNTIME_H
#include <windows.h>
#include "../common/plugin_api.h"
#define PRT_PAGE_TREE_BASE 200
/* A missing feature returns E_NOINTERFACE; a failed registry snapshot returns E_FAIL.
   invoked is TRUE only after calling the adapter, including when it returns E_NOINTERFACE. */
typedef HRESULT (*PRT_FEATURE_ADAPTER)(const void* table, void* user, void* context);
HRESULT prt_call_feature(LPCWSTR interface_id, PRT_FEATURE_ADAPTER adapter, void* context, BOOL* invoked);
/* Stable host-private selection; no table/user pointer is retained here. */
typedef struct PRT_FEATURE_REF {
    UINT index, provider;
    ULONGLONG registration, activation;
    WCHAR interface_id[TC_FEATURE_INTERFACE_CHARS];
} PRT_FEATURE_REF;
HRESULT prt_capture_feature(LPCWSTR interface_id, PRT_FEATURE_REF* reference);
HRESULT prt_check_feature(const PRT_FEATURE_REF* reference);
HRESULT prt_run_feature(const PRT_FEATURE_REF* reference, PRT_FEATURE_ADAPTER adapter,
    void* context, BOOL* invoked);
/* Only snapshot-safe versioned domains may use this module hold. It serializes
   worker jobs without blocking ordinary UI callbacks or runtime stop/reload.
   Accepted modules stay loaded; domain snapshots must outlive runtime stop.
   Table/user outputs are borrowed only until the next plugin callback. */
typedef struct PRT_FEATURE_HOLD { ULONGLONG token; } PRT_FEATURE_HOLD;
typedef HRESULT (*PRT_HOLD_ADAPTER)(void* context);
BOOL prt_owns_thread(void);
HRESULT prt_hold_feature(const PRT_FEATURE_REF* reference, PRT_FEATURE_HOLD* hold,
    const void** table, void** user);
HRESULT prt_call_hold(const PRT_FEATURE_HOLD* hold, PRT_HOLD_ADAPTER adapter, void* context);
/* S_OK consumes the hold even when *current reports a stale/failed owner.
   Copies of an already consumed hold cannot release a later job. */
HRESULT prt_drop_hold(PRT_FEATURE_HOLD* hold, HRESULT* current);
void prt_refresh(void);
void prt_shutdown(void);
HRESULT prt_show_settings(LPCWSTR registration, LPCWSTR path, LPCWSTR ini, HWND owner, BOOL english);
int prt_count(void);
const char* prt_get_key(int index);
const char* prt_get_label(const char* key, BOOL english);
BOOL prt_execute(const char* key, HWND owner, BOOL english);
/* Host-private owned identity. /1 public plugin tables remain unchanged.
   Busy acceptance is reserved for deferred input; execution always rejects busy.
   Registration/activation exhaustion fails closed until process restart. */
#define PRT_COMMAND_CHARS 68
typedef struct {
    char key[PRT_COMMAND_CHARS];
    UINT provider;
    ULONGLONG registration, activation;
} PRT_COMMAND;
BOOL prt_capture_command(const char* key, BOOL allow_busy, PRT_COMMAND* command);
BOOL prt_run_command(const PRT_COMMAND* command, HWND owner, BOOL english);
void prt_note_setting(const char* section, const char* key);
int prt_page_count(void);
LPCWSTR prt_page_label(int index, BOOL english);
ULONGLONG prt_page_token(int index);
BOOL prt_owns_page(int index, HWND page);
HWND prt_create_page(int index, HWND owner, BOOL english);
BOOL prt_apply_page(int index, HWND page);
/* tclock.page/1: index of the page `provider_id` offers for `target` (-1 if none), and
   whether a page is offered as a substitution (it is then not listed as an extra page). */
int prt_find_substitute(LPCWSTR provider_id, LPCWSTR target);
BOOL prt_is_substitute(int index);
BOOL prt_handle_message(UINT message, LPARAM lParam, LRESULT* result);
#endif
