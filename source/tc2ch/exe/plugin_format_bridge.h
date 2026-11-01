#ifndef TCLOCK_PLUGIN_FORMAT_BRIDGE_H
#define TCLOCK_PLUGIN_FORMAT_BRIDGE_H
#include <windows.h>
#include "../common/format_bridge.h"

/* Clock route (main text, tooltip body/title) for a selected tclock.formatter/2 provider.
   All functions run on the runtime UI thread of the main window. */
#define PFB_POLL_TIMER 49012

/* Re-evaluates the selected provider and the attached clock. Unforced calls run at most
   every PFB_REFRESH_MS: a disabled provider already fails at launch, so this only bounds
   how soon a newly selected provider takes over. */
#define PFB_REFRESH_MS 10000u
void pfb_refresh(HWND owner, BOOL force);
BOOL pfb_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam);
void pfb_on_timer(HWND owner);
/* Disables the clock route; a running native callback stays owned until process exit. */
void pfb_shutdown(HWND owner);
#endif
