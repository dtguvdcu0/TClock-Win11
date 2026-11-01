#ifndef TCLOCK_CHIME_BRIDGE_H
#define TCLOCK_CHIME_BRIDGE_H
#include <windows.h>
#include "chime_api.h"

/* Host-private exe <-> clock DLL contract for tclock.chime/1. The provider runs in the
   exe; the clock only executes a validated plan, so it never waits for the exe.
   1. exe -> clock WM_COPYDATA(TCC_PLAN_DATA): the whole current plan of one generation.
      enabled = 0 ends the route; the clock then returns to the built-in decision.
   2. The clock is the only authority for events it accepted: it posts TCC_OUTCOME
      (wParam: generation low bits, lParam: MAKELPARAM(tag, TCC_STARTED or a
      TC_CHIME_* outcome)) and posts every outcome before WM_COPYDATA returns
      when a plan replaces or ends events.
   3. The route owns the decision only until `until`; afterwards built-in resumes. */
#define TCC_PLAN_DATA 0x54434350u /* 'TCCP' */
#define TCC_OUTCOME (WM_APP + 0x3B8)
#define TCC_STARTED 0x10u
/* An event starts only within this many seconds after its time; later it is skipped. */
#define TCC_START_GRACE 2u

#pragma pack(push, 8)
typedef struct TCC_EVENT {
    UINT tag;
    UINT effects;
    UINT strikes;
    UINT reserved;
    SYSTEMTIME time;
} TCC_EVENT;

typedef struct TCC_PLAN {
    UINT size;
    UINT enabled;
    ULONGLONG generation;
    SYSTEMTIME until;
    UINT count;
    UINT reserved;
    TCC_EVENT events[TC_CHIME_MAX_EVENTS];
} TCC_PLAN;
#pragma pack(pop)

C_ASSERT(sizeof(TCC_EVENT) == 32);
C_ASSERT(sizeof(TCC_PLAN) == 40 + 32 * TC_CHIME_MAX_EVENTS);
#endif
