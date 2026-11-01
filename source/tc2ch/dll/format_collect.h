#ifndef TCLOCK_FORMAT_COLLECT_H
#define TCLOCK_FORMAT_COLLECT_H
#include "../common/format_demand.h"
/* Internal clock-thread owner API. No DLL entry point or IPC capability.
   Context owners keep leases across paint calls and close on actual teardown. */
HRESULT fcl_open(UINT consumer, const TC_FORMAT_DEMAND* demand, ULONGLONG* lease);
HRESULT fcl_replace(ULONGLONG lease, const TC_FORMAT_DEMAND* demand);
HRESULT fcl_close(ULONGLONG lease);
void fcl_reset(void);
HRESULT fcl_reload(void);
LPCWSTR fcl_mount_path(UINT slot);
void fcl_take(ULONGLONG tick, FDM_WORK* work);
void fcl_snapshot(FDM_WORK* work);
BOOL fcl_pending(void);
ULONGLONG fcl_space_drives(void);
UINT fcl_rate_drives(void);
#endif
