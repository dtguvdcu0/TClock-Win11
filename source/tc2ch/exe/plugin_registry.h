#ifndef TCLOCK_PLUGIN_REGISTRY_H
#define TCLOCK_PLUGIN_REGISTRY_H
#include <windows.h>
#define PREG_KNOWN 4
#define PREG_MAX_CUSTOM 32
#define PREG_MAX_RECORDS (PREG_KNOWN + PREG_MAX_CUSTOM)
#define PREG_KIND_KNOWN 0
#define PREG_KIND_EXE 1
#define PREG_KIND_DLL 2
#define PPG_COMMIT (WM_APP + 0x3B2)
typedef struct PLUGIN_RECORD {
    int known;
    int slot;
    int kind;
    BOOL enabled;
    BOOL visible;
    BOOL automatic;
    BOOL discovered;
    BOOL conflict;
    WCHAR provider[32];
    WCHAR id[64];
    WCHAR name[128];
    WCHAR path[MAX_PATH];
    WCHAR args[512];
    WCHAR settings[512];
    WCHAR ini[MAX_PATH];
} PLUGIN_RECORD;
BOOL preg_migrate(void);
BOOL preg_discover(void);
BOOL preg_get_identity(LPCWSTR file, WCHAR* id, int capacity);
void preg_get_title(const PLUGIN_RECORD* record, BOOL english, WCHAR* title, int capacity);
BOOL preg_map_key(const char* section, const char* key, char* canonical, int size);
BOOL preg_resolve(LPCWSTR path, WCHAR* absolute);
BOOL preg_is_present(const PLUGIN_RECORD* record);
BOOL preg_can_configure(const PLUGIN_RECORD* record);
int preg_load(PLUGIN_RECORD* records, int capacity);
BOOL preg_load_all(PLUGIN_RECORD** records, int* count);
BOOL preg_discovery_ok(void);
BOOL preg_save(const PLUGIN_RECORD* records, int count);
int preg_stop_tools(void);
BOOL preg_get_version(const PLUGIN_RECORD* record, WCHAR* version, int capacity);
BOOL preg_get_description(const PLUGIN_RECORD* record, BOOL english, WCHAR* description, int capacity);
BOOL preg_get_dll(UINT slot, WCHAR* id, WCHAR* path);
BOOL preg_open(const PLUGIN_RECORD* record, HWND owner, BOOL settings, BOOL english);
int preg_count_actions(void);
const char* preg_get_action(int index);
const char* preg_get_cached_label(int index);
const char* preg_get_label(const char* key);
BOOL preg_run_action(const char* key, HWND owner);
INT_PTR CALLBACK PagePluginsProc(HWND, UINT, WPARAM, LPARAM);
#endif
