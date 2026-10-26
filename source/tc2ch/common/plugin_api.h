#ifndef TCLOCK_PLUGIN_API_H
#define TCLOCK_PLUGIN_API_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TC_PLUGIN_ABI 1u
#define TC_PLUGIN_INFO_ID L"tclock.plugin/1"
#define TC_PLUGIN_ACTIONS_ID L"tclock.actions/1"
#define TC_PLUGIN_SETTINGS_ID L"tclock.settings/1"
#define TC_PLUGIN_ID_CHARS 32
#define TC_PLUGIN_LABEL_CHARS 128
typedef struct TC_PLUGIN_INFO {
    UINT size;
    WCHAR id[TC_PLUGIN_ID_CHARS];
    WCHAR name[TC_PLUGIN_LABEL_CHARS];
} TC_PLUGIN_INFO;
typedef struct TC_PLUGIN_ACTION {
    UINT size;
    WCHAR id[TC_PLUGIN_ID_CHARS];
    WCHAR label[TC_PLUGIN_LABEL_CHARS];
} TC_PLUGIN_ACTION;
typedef struct TC_ACTION_CONTEXT {
    UINT size;
    HWND owner;
    LANGID language;
    HRESULT (WINAPI *copy_text)(HWND owner, LPCWSTR text);
} TC_ACTION_CONTEXT;
typedef struct TC_ACTION_PROVIDER {
    UINT size;
    UINT (WINAPI *count)(void);
    HRESULT (WINAPI *describe)(UINT index, LANGID language, TC_PLUGIN_ACTION* out);
    HRESULT (WINAPI *invoke)(LPCWSTR action, const TC_ACTION_CONTEXT* context);
} TC_ACTION_PROVIDER;
typedef struct TC_SETTINGS_CONTEXT {
    UINT size;
    HWND owner;
    LANGID language;
    LPCWSTR ini_path;
} TC_SETTINGS_CONTEXT;
typedef struct TC_PLUGIN_SETTINGS {
    UINT size;
    HRESULT (WINAPI *reload)(const TC_SETTINGS_CONTEXT* context);
    HRESULT (WINAPI *show)(const TC_SETTINGS_CONTEXT* context);
} TC_PLUGIN_SETTINGS;

#define TC_PLUGIN_EXTENSION_ID L"tclock.extension/1"
#define TC_PLUGIN_COLOR_DATA 0x54435043u
typedef struct TC_EXTENSION_CONTEXT {
    UINT size;
    HWND owner;
    LANGID language;
    LPCWSTR ini_path;
    LPCWSTR legacy_ini;
    void (WINAPI *changed)(HWND page);
} TC_EXTENSION_CONTEXT;
typedef struct TC_TEXT_CONTEXT {
    UINT size;
    UINT kind;
    COLORREF base;
    SYSTEMTIME time;
    BOOL holiday;
    BOOL vpn;
} TC_TEXT_CONTEXT;
typedef struct TC_PLUGIN_EXTENSION {
    UINT size;
    HRESULT (WINAPI *reload)(const TC_EXTENSION_CONTEXT* context);
    LPCWSTR (WINAPI *label)(LANGID language);
    HWND (WINAPI *create)(const TC_EXTENSION_CONTEXT* context);
    HRESULT (WINAPI *apply)(HWND page);
    COLORREF (WINAPI *color)(const TC_TEXT_CONTEXT* context);
} TC_PLUGIN_EXTENSION;

typedef HRESULT (WINAPI *TC_GET_INTERFACE)(UINT abi, LPCWSTR id, const void** out);
#ifdef __cplusplus
}
#endif
#endif
