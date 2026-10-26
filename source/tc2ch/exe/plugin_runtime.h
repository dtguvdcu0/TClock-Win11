#ifndef TCLOCK_PLUGIN_RUNTIME_H
#define TCLOCK_PLUGIN_RUNTIME_H
#include <windows.h>
#define PRT_ACTION_CAPACITY 128
#define PRT_PAGE_CAPACITY 64
void prt_refresh(void);
void prt_shutdown(void);
HRESULT prt_show_settings(LPCWSTR registration, LPCWSTR path, LPCWSTR ini, HWND owner, BOOL english);
int prt_count(void);
const char* prt_get_key(int index);
const char* prt_get_label(const char* key, BOOL english);
BOOL prt_execute(const char* key, HWND owner, BOOL english);
int prt_page_count(void);
LPCWSTR prt_page_label(int index, BOOL english);
HWND prt_create_page(int index, HWND owner, BOOL english);
BOOL prt_apply_page(int index, HWND page);
BOOL prt_handle_message(UINT message, LPARAM lParam, LRESULT* result);
#endif
