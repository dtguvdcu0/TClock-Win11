#ifndef TCLOCK_PLUGIN_ACTIONS_H
#define TCLOCK_PLUGIN_ACTIONS_H
#include <windows.h>
/* ASCII action identities only; file paths stay UTF-16 inside the loader. */
/* Fixed editor range; action indices stay append-only across refresh. */
#define PACT_ACTION_CAPACITY 128
void pact_refresh(void);
HRESULT pact_show_settings(LPCWSTR registration, LPCWSTR path, LPCWSTR ini, HWND owner, BOOL english);
int pact_count(void);
const char* pact_get_key(int index);
const char* pact_get_label(const char* key, BOOL english);
BOOL pact_execute(const char* key, HWND owner, BOOL english);
int pact_page_count(void);
LPCWSTR pact_page_label(int index, BOOL english);
HWND pact_create_page(int index, HWND owner, BOOL english);
BOOL pact_apply_page(int index, HWND page);
BOOL pact_handle_message(UINT message, LPARAM lParam, LRESULT* result);
#endif
