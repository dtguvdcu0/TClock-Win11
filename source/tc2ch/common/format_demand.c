#include "format_demand.h"
#include <string.h>

static BOOL fdm_check_demand(UINT consumer, const TC_FORMAT_DEMAND* demand) {
    return consumer >= TCF_CONSUMER_MAIN && consumer <= TCF_CONSUMER_MENU &&
        TcfValidateDemand(demand) && (demand->consumers & (1u << (consumer - 1)));
}
static FDM_OWNER* fdm_find_owner(FDM_STATE* state, ULONGLONG lease) {
    UINT i;
    if (!state || !lease) return NULL;
    for (i=0;i<FDM_MAX_OWNERS;i++) if (state->owners[i].lease==lease) return &state->owners[i];
    return NULL;
}
static void fdm_merge_demand(TC_FORMAT_DEMAND* total, const FDM_OWNER* owner) {
    const TC_FORMAT_DEMAND* demand=&owner->demand;
    total->size=sizeof(*total);total->categories|=demand->categories;
    total->spaceDrives|=demand->spaceDrives;total->rateDrives|=demand->rateDrives;
    total->customVariables|=demand->customVariables;total->consumers|=1u << (owner->consumer-1);
    if (!total->intervalMs || demand->intervalMs<total->intervalMs) total->intervalMs=demand->intervalMs;
}
HRESULT fdm_open(FDM_STATE* state, UINT consumer, const TC_FORMAT_DEMAND* demand, ULONGLONG* lease) {
    UINT i;FDM_OWNER staged;
    if (!state || !lease) return E_POINTER;
    if (!fdm_check_demand(consumer,demand)) return E_INVALIDARG;
    if (state->serial==~(ULONGLONG)0) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    for (i=0;i<FDM_MAX_OWNERS;i++) if (!state->owners[i].lease) break;
    if (i==FDM_MAX_OWNERS) return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
    ZeroMemory(&staged,sizeof(staged));
    staged.lease=state->serial+1;staged.revision=1;staged.consumer=consumer;staged.demand=*demand;
    state->owners[i]=staged;state->serial=staged.lease;*lease=staged.lease;
    return S_OK;
}
HRESULT fdm_replace(FDM_STATE* state, ULONGLONG lease, const TC_FORMAT_DEMAND* demand) {
    FDM_OWNER* owner=fdm_find_owner(state,lease);
    if (!owner) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (!fdm_check_demand(owner->consumer,demand)) return E_INVALIDARG;
    if (!memcmp(&owner->demand,demand,sizeof(*demand))) return S_FALSE;
    if (owner->revision==~(ULONGLONG)0) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    owner->demand=*demand;owner->revision++;owner->sampled=FALSE;
    return S_OK;
}
HRESULT fdm_close(FDM_STATE* state, ULONGLONG lease) {
    FDM_OWNER* owner=fdm_find_owner(state,lease);
    if (!owner) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    ZeroMemory(owner,sizeof(*owner));
    return S_OK;
}
void fdm_snapshot(const FDM_STATE* state, FDM_WORK* work) {
    UINT i;
    if (!work) return;
    ZeroMemory(work,sizeof(*work));work->active.size=sizeof(work->active);work->due.size=sizeof(work->due);
    if (!state) return;
    for (i=0;i<FDM_MAX_OWNERS;i++) if (state->owners[i].lease) {
        work->activeCount++;fdm_merge_demand(&work->active,&state->owners[i]);
    }
}
void fdm_take(FDM_STATE* state, ULONGLONG tick, FDM_WORK* work) {
    UINT i;
    fdm_snapshot(state,work);
    if (!state || !work) return;
    for (i=0;i<FDM_MAX_OWNERS;i++) {
        FDM_OWNER* owner=&state->owners[i];
        if (!owner->lease || (owner->sampled && tick>=owner->lastTick &&
            tick-owner->lastTick<owner->demand.intervalMs)) continue;
        owner->lastTick=tick;owner->sampled=TRUE;
        work->dueOwners[work->dueCount].lease=owner->lease;
        work->dueOwners[work->dueCount].revision=owner->revision;
        work->dueOwners[work->dueCount].consumer=owner->consumer;
        work->dueCount++;fdm_merge_demand(&work->due,owner);
    }
}
