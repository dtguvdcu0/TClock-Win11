#include "plugin_actions.h"
#include "plugin_runtime.h"

int pact_count(void) { return prt_count(); }
const char* pact_get_key(int index) { return prt_get_key(index); }
const char* pact_get_label(const char* key, BOOL english) { return prt_get_label(key, english); }
BOOL pact_execute(const char* key, HWND owner, BOOL english) { return prt_execute(key, owner, english); }
