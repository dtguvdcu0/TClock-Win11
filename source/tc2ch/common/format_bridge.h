#ifndef TCLOCK_FORMAT_BRIDGE_H
#define TCLOCK_FORMAT_BRIDGE_H

#include <windows.h>
#include "format_source.h"
#include "formatter_api.h"

/* Host-private exe <-> clock DLL contract for an exe-side formatter provider.
   Provider modules never load into Explorer:
   1. exe -> clock WM_COPYDATA(TCF_BRIDGE_CONFIG_DATA) names an exe-created section.
   2. clock writes one source body per consumer slot under a sequence counter and posts
      TCF_BRIDGE_READY to the exe; the clock thread never waits on the exe.
   3. exe formats on its /2 worker and returns WM_COPYDATA(TCF_BRIDGE_FRAME_DATA).
   4. clock uses a frame only for the current generation and format while it is fresh;
      otherwise it formats with the built-in engine. */
/* wParam: generation (low bits), lParam: consumer. */
#define TCF_BRIDGE_READY (WM_APP + 0x3B5)
/* Clock-private: posted to the clock window to redraw after a frame/config change;
   wParam 1 (on enable) collects sources first. */
#define TCF_BRIDGE_REDRAW (WM_APP + 0x3B6)
/* Clock-private: posted to the clock window to refresh a shown tooltip after a frame. */
#define TCF_BRIDGE_TOOLTIP (WM_APP + 0x3B7)
#define TCF_BRIDGE_CONFIG_DATA 0x54434643u /* 'TCFC' */
#define TCF_BRIDGE_FRAME_DATA 0x54434646u  /* 'TCFF' */
#define TCF_BRIDGE_NAME_CCH 64
#define TCF_BRIDGE_FRAME_AGE_MS 2000u
/* Consumers 1..TCF_BRIDGE_SLOTS (main, tooltip body, tooltip title) each own one slot. */
#define TCF_BRIDGE_SLOTS 3u
#define TCF_BRIDGE_CONSUMERS ((1u << TCF_BRIDGE_SLOTS) - 1u)
#define TCF_BRIDGE_HEADER_BYTES 64u
#define TCF_BRIDGE_SLOT_BYTES (TCF_BRIDGE_HEADER_BYTES + TCF_SOURCE_MAX_BYTES)
#define TCF_BRIDGE_SECTION_BYTES (TCF_BRIDGE_SLOTS * TCF_BRIDGE_SLOT_BYTES)

#pragma pack(push, 8)
typedef struct TCF_BRIDGE_CONFIG {
    UINT size;
    UINT enabled;
    ULONGLONG generation;
    TC_FORMAT_DEMAND demand;
    WCHAR section[TCF_BRIDGE_NAME_CCH];
} TCF_BRIDGE_CONFIG;

/* Each slot starts with this header at (consumer - 1) * TCF_BRIDGE_SLOT_BYTES.
   Writer: sequence odd -> body/bytes/revision -> sequence even.
   Readers copy only while the same even sequence is observed before and after. */
typedef struct TCF_BRIDGE_HEADER {
    volatile LONG sequence;
    UINT bytes;
    ULONGLONG generation;
    ULONGLONG revision;
    BYTE reserved[TCF_BRIDGE_HEADER_BYTES - 24];
} TCF_BRIDGE_HEADER;

typedef struct TCF_BRIDGE_FRAME {
    ULONGLONG generation;
    UINT consumer;
    UINT reserved;
    TC_FORMAT_FRAME frame;
} TCF_BRIDGE_FRAME;
#pragma pack(pop)

C_ASSERT(sizeof(TCF_BRIDGE_HEADER) == TCF_BRIDGE_HEADER_BYTES);
C_ASSERT(sizeof(TCF_BRIDGE_CONFIG) == 184);
C_ASSERT(TCF_CONSUMER_TOOLTIP_TITLE == TCF_BRIDGE_SLOTS);

#endif
