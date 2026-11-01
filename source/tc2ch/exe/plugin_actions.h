#ifndef TCLOCK_PLUGIN_ACTIONS_H
#define TCLOCK_PLUGIN_ACTIONS_H
#include "plugin_runtime.h"
int pact_count(void);
const char* pact_get_key(int index);
const char* pact_get_label(const char* key, BOOL english);
BOOL pact_execute(const char* key, HWND owner, BOOL english);
#endif
