#ifndef TCLOCK_FORMAT_DEMAND_H
#define TCLOCK_FORMAT_DEMAND_H
#include "formatter_api.h"

/* Host-private process-local scheduler, never a wire structure.
   The owner thread serializes calls. A lease spans a consumer's lifetime,
   not one render/snapshot call. Exhaustion falls back without evicting owners. */
#ifdef __cplusplus
extern "C" {
#endif

#define FDM_MAX_OWNERS 64
typedef struct FDM_OWNER {
    ULONGLONG lease, revision, lastTick;
    UINT consumer;
    BOOL sampled;
    TC_FORMAT_DEMAND demand;
} FDM_OWNER;
typedef struct FDM_STATE {
    ULONGLONG serial;
    FDM_OWNER owners[FDM_MAX_OWNERS];
} FDM_STATE;
typedef struct FDM_TICKET {
    ULONGLONG lease, revision;
    UINT consumer;
} FDM_TICKET;
typedef struct FDM_WORK {
    UINT activeCount, dueCount;
    TC_FORMAT_DEMAND active, due;
    FDM_TICKET dueOwners[FDM_MAX_OWNERS];
} FDM_WORK;

/* All rejected changes preserve state and caller output. Handles never wrap. */
HRESULT fdm_open(FDM_STATE* state, UINT consumer, const TC_FORMAT_DEMAND* demand, ULONGLONG* lease);
HRESULT fdm_replace(FDM_STATE* state, ULONGLONG lease, const TC_FORMAT_DEMAND* demand);
HRESULT fdm_close(FDM_STATE* state, ULONGLONG lease);
void fdm_snapshot(const FDM_STATE* state, FDM_WORK* work);
void fdm_take(FDM_STATE* state, ULONGLONG tick, FDM_WORK* work);
/* Empty aggregates have size set and zero consumers/interval, not a registration.
   A clock rollback makes active owners due once; elapsed subtraction cannot wrap. */
#ifdef __cplusplus
}
#endif
#endif
