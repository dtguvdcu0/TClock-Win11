#ifndef TCLOCK_PLUGIN_CHIME_H
#define TCLOCK_PLUGIN_CHIME_H
#include <windows.h>
#include "../common/chime_bridge.h"

/* Clock route for a selected tclock.chime/1 provider (common/chime_bridge.h).
   All functions run on the runtime UI thread of the main window. */

/* A plan covers PCH_WINDOW_SECONDS and is renewed at most every PCH_REFRESH_MS, so the
   clock keeps a current plan while the exe runs and returns to built-in soon after. */
#define PCH_WINDOW_SECONDS 300u
#define PCH_REFRESH_MS 60000u
void pch_refresh(HWND owner, BOOL force);
BOOL pch_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam);
/* Ends the route; accepted events still open are reported before the runtime stops. */
void pch_shutdown(HWND owner);
#endif
