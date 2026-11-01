#include "tcdll.h"
#include "format_collect.h"
#include "minmode.h"
extern BOOL b_SafeMode;
#include "../common/ini_io_utf8.h"
#include "../common/text_codec.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>
static FDM_STATE fcl_state;
static DWORD fcl_thread;
static BOOL fcl_dirty;

static BOOL fcl_check_thread(void) {
    DWORD thread=GetCurrentThreadId();
    return !fcl_thread || fcl_thread==thread;
}
static void fcl_note_change(void) {
    if (!fcl_thread) fcl_thread=GetCurrentThreadId();
    fcl_dirty=TRUE;
}
#define FCL_PATH_CCH 32768u
static WCHAR* fcl_mounts;

static HRESULT fcl_load_mounts(const FDM_STATE* state, WCHAR** paths) {
    FDM_WORK work;WCHAR ini[MAX_PATH];WCHAR* staged;UINT i;
    HRESULT result=S_OK;
    fdm_snapshot(state,&work);*paths=NULL;
    if (!(work.active.spaceDrives >> 26)) return S_OK;
    if (tc_utf8_to_utf16(g_inifile,ini,_countof(ini))<=0) return E_INVALIDARG;
    staged=(WCHAR*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)10*FCL_PATH_CCH*sizeof(WCHAR));
    if (!staged) return E_OUTOFMEMORY;
    if (!tc_begin_legacyW(ini)) {HeapFree(GetProcessHeap(),0,staged);return E_FAIL;}
    for (i=0;i<10;i++) if (work.active.spaceDrives & (1ULL << (26+i))) {
        char key[32];WCHAR* value=staged+(SIZE_T)i*FCL_PATH_CCH;size_t length;
        sprintf_s(key,sizeof(key),"AdditionalMountPath%u",i);
        result=tc_read_valueW(ini,"ETC",key,value,FCL_PATH_CCH);
        if (FAILED(result)) break;
        /* Missing/empty mount slots remain unavailable; quotes are INI syntax. */
        length=wcslen(value);
        if (length>=2 && value[0]==L'"' && value[length-1]==L'"') {
            MoveMemory(value,value+1,(length-2)*sizeof(WCHAR));value[length-2]=0;
        }
    }
    tc_ini_end();
    if (FAILED(result)) {HeapFree(GetProcessHeap(),0,staged);return result;}
    *paths=staged;return S_OK;
}
static void fcl_commit_mounts(WCHAR* paths) {
    if (fcl_mounts) HeapFree(GetProcessHeap(),0,fcl_mounts);
    fcl_mounts=paths;
}
LPCWSTR fcl_mount_path(UINT slot) {
    return fcl_check_thread() && fcl_mounts && slot<10 ?
        fcl_mounts+(SIZE_T)slot*FCL_PATH_CCH : NULL;
}
HRESULT fcl_reload(void) {
    WCHAR* paths;HRESULT result;UINT i;BOOL changed;
    if (!fcl_check_thread()) return E_ACCESSDENIED;
    result=fcl_load_mounts(&fcl_state,&paths);
    if (result!=S_OK) return result;
    changed=(!paths)!=(!fcl_mounts);
    if (paths && fcl_mounts) changed=memcmp(paths,fcl_mounts,(SIZE_T)10*FCL_PATH_CCH*sizeof(WCHAR))!=0;
    if (!changed) {if(paths)HeapFree(GetProcessHeap(),0,paths);return S_FALSE;}
    for (i=0;i<FDM_MAX_OWNERS;i++) if (fcl_state.owners[i].lease &&
        (fcl_state.owners[i].demand.spaceDrives >> 26) &&
        fcl_state.owners[i].revision==~(ULONGLONG)0) {
        if(paths)HeapFree(GetProcessHeap(),0,paths);
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
    for (i=0;i<FDM_MAX_OWNERS;i++) if (fcl_state.owners[i].lease &&
        (fcl_state.owners[i].demand.spaceDrives >> 26)) {
        fcl_state.owners[i].revision++;fcl_state.owners[i].sampled=FALSE;
    }
    fcl_commit_mounts(paths);fcl_note_change();return S_OK;
}

HRESULT fcl_open(UINT consumer, const TC_FORMAT_DEMAND* demand, ULONGLONG* lease) {
    FDM_STATE staged;ULONGLONG selected;WCHAR* paths;HRESULT result;
    if (!lease) return E_POINTER;
    if (!fcl_check_thread()) return E_ACCESSDENIED;
    if (b_MinimalMode || b_SafeMode) return E_ACCESSDENIED;
    staged=fcl_state;
    result=fdm_open(&staged,consumer,demand,&selected);
    if (result!=S_OK) return result;
    result=fcl_load_mounts(&staged,&paths);
    if (result!=S_OK) return result;
    fcl_state=staged;fcl_commit_mounts(paths);*lease=selected;fcl_note_change();
    return S_OK;
}
HRESULT fcl_replace(ULONGLONG lease, const TC_FORMAT_DEMAND* demand) {
    FDM_STATE staged;WCHAR* paths;HRESULT result;
    if (!fcl_check_thread()) return E_ACCESSDENIED;
    if (b_MinimalMode || b_SafeMode) return E_ACCESSDENIED;
    staged=fcl_state;
    result=fdm_replace(&staged,lease,demand);
    if (result!=S_OK) return result;
    result=fcl_load_mounts(&staged,&paths);
    if (result!=S_OK) return result;
    fcl_state=staged;fcl_commit_mounts(paths);fcl_note_change();
    return S_OK;
}
HRESULT fcl_close(ULONGLONG lease) {
    HRESULT result;
    if (!fcl_check_thread()) return E_ACCESSDENIED;
    result=fdm_close(&fcl_state,lease);
    if (result==S_OK) {
        FDM_WORK work;fdm_snapshot(&fcl_state,&work);
        if (!(work.active.spaceDrives >> 26)) fcl_commit_mounts(NULL);
        fcl_note_change();
    }
    return result;
}
void fcl_reset(void) {
    UINT i;
    if (!fcl_check_thread()) return;
    /* Retain serial across endpoint teardown: old leases never name a new owner. */
    for (i=0;i<FDM_MAX_OWNERS;i++) ZeroMemory(&fcl_state.owners[i],sizeof(fcl_state.owners[i]));
    fcl_commit_mounts(NULL);fcl_dirty=TRUE;fcl_thread=0;
}
void fcl_take(ULONGLONG tick, FDM_WORK* work) {
    if (!work) return;
    if (!fcl_check_thread()) {ZeroMemory(work,sizeof(*work));return;}
    fdm_take(&fcl_state,tick,work);fcl_dirty=FALSE;
}
void fcl_snapshot(FDM_WORK* work) {
    if (!work) return;
    if (!fcl_check_thread()) {ZeroMemory(work,sizeof(*work));return;}
    fdm_snapshot(&fcl_state,work);
}
BOOL fcl_pending(void) {return fcl_check_thread() && fcl_dirty;}
ULONGLONG fcl_space_drives(void) {
    FDM_WORK work;fcl_snapshot(&work);return work.active.spaceDrives;
}
UINT fcl_rate_drives(void) {
    FDM_WORK work;fcl_snapshot(&work);return work.active.rateDrives;
}
