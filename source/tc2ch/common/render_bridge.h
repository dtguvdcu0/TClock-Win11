#ifndef TCLOCK_RENDER_BRIDGE_H
#define TCLOCK_RENDER_BRIDGE_H
#include <windows.h>
#include "render_api.h"

/* Host-private exe <-> clock DLL contract for tclock.render/1. Provider code never
   loads into Explorer and the clock never waits for the exe:
   1. exe -> clock WM_COPYDATA(TCR_CONFIG_DATA) names an exe-created request section.
   2. clock writes the request under a sequence counter and posts TCR_READY.
   3. exe renders and returns WM_COPYDATA(TCR_FRAME_DATA): TCR_FRAME then pixels.
   4. clock composes a frame only for the current generation, the current image size
      and while it is younger than TCR_FRAME_AGE_MS (counted from the request). */
#define TCR_READY (WM_APP + 0x3B9)
/* Clock-private: redraw after a frame or a configuration change. */
#define TCR_REDRAW (WM_APP + 0x3BA)
#define TCR_CONFIG_DATA 0x54435243u /* 'TCRC' */
#define TCR_FRAME_DATA 0x54435246u  /* 'TCRF' */
#define TCR_NAME_CCH 64
#define TCR_FRAME_AGE_MS 2000u
#define TCR_HEADER_BYTES 32u
#define TCR_SECTION_BYTES (TCR_HEADER_BYTES + sizeof(TC_RENDER_REQUEST))

#pragma pack(push, 8)
typedef struct TCR_CONFIG {
    UINT size;
    UINT enabled;
    ULONGLONG generation;
    WCHAR section[TCR_NAME_CCH];
} TCR_CONFIG;

/* Writer: sequence odd -> request -> sequence even; readers copy between equal even values. */
typedef struct TCR_HEADER {
    volatile LONG sequence;
    UINT reserved;
    ULONGLONG generation;
    ULONGLONG revision;
    ULONGLONG unused;
} TCR_HEADER;

typedef struct TCR_FRAME {
    ULONGLONG generation;
    ULONGLONG revision;
    UINT width;
    UINT height;
    UINT preferredWidth;
    UINT preferredHeight;
    UINT regionCount;
    UINT reserved;
    TC_RENDER_REGION regions[TC_RENDER_MAX_REGIONS];
    /* followed by width * height premultiplied BGRA pixels, top row first */
} TCR_FRAME;
#pragma pack(pop)

C_ASSERT(sizeof(TCR_HEADER) == TCR_HEADER_BYTES);
C_ASSERT(sizeof(TCR_CONFIG) == 16 + 2 * TCR_NAME_CCH);
#endif
