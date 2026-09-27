#pragma once
#include "wui_api.h"

BOOL wui_render_text(const TC_DISPLAY_BACKEND_RENDER_STATE& state, const RECT& content,
    LONG width, LONG height, BYTE* pixels);
void wui_reset_text(void);
