#ifndef TCLOCK_PLUGIN_RENDER_H
#define TCLOCK_PLUGIN_RENDER_H
#include <windows.h>
#include "../common/render_bridge.h"

/* Clock route for a selected tclock.render/1 provider (common/render_bridge.h).
   All functions run on the runtime UI thread of the main window. Unforced refreshes
   run at most every PRN_REFRESH_MS; Plugins Apply and clock attach force one. */
#define PRN_REFRESH_MS 10000u
void prn_refresh(HWND owner, BOOL force);
BOOL prn_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam);
/* While the provider's frame is current its regions own the clock's mouse zones:
   TRUE with *zone 1-3, or 0 when the point is outside every region. FALSE (no current
   frame, or a frame without regions): use the built-in zone split. point and client are clock client coordinates. */
BOOL prn_hit_zone(POINT point, const RECT* client, int* zone);
void prn_shutdown(HWND owner);
#endif
