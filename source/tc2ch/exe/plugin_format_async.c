#include "plugin_format_async.h"
#include <string.h>
#include <wchar.h>

enum { PFA_STAGED, PFA_RUNNING, PFA_DONE, PFA_FINISHING, PFA_SETTLED };
struct PFA_TASK {
    volatile LONG phase, cancelled;
    TC_FORMAT_SOURCE* source;
    UINT bytes, capacity;
    ULONGLONG revision;
    PRT_FEATURE_HOLD hold;
    TC_FORMATTER2 api;
    void* snapshot;
    BOOL captured, invoked;
    HRESULT result;
    PTP_WORK work;
    TC_FORMAT_FRAME frame;
};
typedef struct PFA_CAPTURE {PFA_TASK* task;void* user;} PFA_CAPTURE;

static BOOL pfa_check_table(const TC_FORMATTER2* table) {
    return table && table->size==sizeof(*table) && table->capture &&
        table->format && table->release && TcfValidateDemand(&table->demand);
}
static HRESULT pfa_read_demand(const void* table, void* user, void* context) {
    const TC_FORMATTER2* formatter=(const TC_FORMATTER2*)table;
    (void)user;
    if (!pfa_check_table(formatter)) return E_INVALIDARG;
    *(TC_FORMAT_DEMAND*)context=formatter->demand;
    return S_OK;
}
HRESULT pfa_prepare(PFRM_BINDING* binding) {
    PFRM_BINDING staged;BOOL invoked;HRESULT result;
    if (!binding) return E_POINTER;
    ZeroMemory(&staged,sizeof(staged));
    result=prt_capture_feature(TC_PLUGIN_FORMATTER2_ID,&staged.owner);
    if (FAILED(result)) return result;
    result=prt_run_feature(&staged.owner,pfa_read_demand,&staged.demand,&invoked);
    if (result==S_OK) *binding=staged;
    return result;
}
static void CALLBACK pfa_worker(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK work) {
    PFA_TASK* task=(PFA_TASK*)context;
    (void)instance;(void)work;
    if (InterlockedCompareExchange(&task->cancelled,0,0)) {
        task->result=HRESULT_FROM_WIN32(ERROR_CANCELLED);
    } else {
        task->invoked=TRUE;
        task->result=task->api.format(task->snapshot,task->source,task->bytes,task->capacity,&task->frame);
        if (task->result==S_OK && !TcfValidateFrame(&task->frame,sizeof(task->frame),task->revision,task->capacity))
            task->result=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    /* Final context access: UI may settle/dispose after observing DONE. Work
       object close is asynchronous until this native callback has returned. */
    InterlockedExchange(&task->phase,PFA_DONE);
}
HRESULT pfa_stage(const TC_FORMAT_SOURCE* source, UINT bytes, UINT capacity, PFA_TASK** output) {
    PFA_TASK* task;DWORD error;
    if (!output) return E_POINTER;
    if (!capacity || capacity>TCF_FRAME_CCH || !TcfValidateSource(source,bytes)) return E_INVALIDARG;
    task=(PFA_TASK*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*task));
    if (!task) return E_OUTOFMEMORY;
    task->source=(TC_FORMAT_SOURCE*)HeapAlloc(GetProcessHeap(),0,bytes);
    if (!task->source) {HeapFree(GetProcessHeap(),0,task);return E_OUTOFMEMORY;}
    CopyMemory(task->source,source,bytes);
    task->bytes=bytes;task->capacity=capacity;task->revision=source->revision;task->result=E_UNEXPECTED;
    task->work=CreateThreadpoolWork(pfa_worker,task,NULL);
    if (!task->work) {
        error=GetLastError();HeapFree(GetProcessHeap(),0,task->source);HeapFree(GetProcessHeap(),0,task);
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    *output=task;return S_OK;
}
static HRESULT pfa_take_snapshot(void* context) {
    PFA_CAPTURE* capture=(PFA_CAPTURE*)context;
    PFA_TASK* task=capture->task;void* snapshot=NULL;HRESULT result;
    result=task->api.capture(capture->user,&snapshot);
    if (result==S_OK) {task->snapshot=snapshot;task->captured=TRUE;}
    return result;
}
static HRESULT pfa_drop_snapshot(void* context) {
    PFA_TASK* task=(PFA_TASK*)context;void* snapshot=task->snapshot;
    task->captured=FALSE;task->snapshot=NULL;
    task->api.release(snapshot);
    return S_OK;
}
static HRESULT pfa_settle(PFA_TASK* task, HRESULT* current) {
    HRESULT result;
    if (task->captured) {
        result=prt_call_hold(&task->hold,pfa_drop_snapshot,task);
        if (FAILED(result)) return result;
    }
    return prt_drop_hold(&task->hold,current);
}
HRESULT pfa_launch(const PFRM_BINDING* binding, PFA_TASK* task) {
    const void* table;void* user;PFA_CAPTURE capture;HRESULT result,current,settled;
    if (!binding || !task) return E_POINTER;
    if (!prt_owns_thread()) return E_ACCESSDENIED;
    if (InterlockedCompareExchange(&task->phase,0,0)!=PFA_STAGED) return E_UNEXPECTED;
    if (InterlockedCompareExchange(&task->cancelled,0,0)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (!TcfValidateDemand(&binding->demand) ||
        wcsnlen(binding->owner.interface_id,TC_FEATURE_INTERFACE_CHARS)==TC_FEATURE_INTERFACE_CHARS ||
        wcscmp(binding->owner.interface_id,TC_PLUGIN_FORMATTER2_ID)) return E_INVALIDARG;
    if (!(binding->demand.consumers & (1u << (task->source->consumer-1)))) {
        result=prt_check_feature(&binding->owner);return FAILED(result) ? result : S_FALSE;
    }
    result=prt_hold_feature(&binding->owner,&task->hold,&table,&user);
    if (FAILED(result)) return result;
    if (!pfa_check_table((const TC_FORMATTER2*)table)) result=E_INVALIDARG;
    else {
        /* Copy every callback before capture: capture may remove/re-register
           and free the original table/user. Only its strong snapshot survives. */
        task->api=*(const TC_FORMATTER2*)table;
        if (memcmp(&task->api.demand,&binding->demand,sizeof(binding->demand))) result=E_INVALIDARG;
        else {
            capture.task=task;capture.user=user;
            result=prt_call_hold(&task->hold,pfa_take_snapshot,&capture);
            if (result==S_OK) result=prt_check_feature(&binding->owner);
        }
    }
    if (result!=S_OK) {
        settled=pfa_settle(task,&current);
        return FAILED(settled) ? settled : result;
    }
    InterlockedExchange(&task->phase,PFA_RUNNING);
    SubmitThreadpoolWork(task->work);
    return S_OK;
}
HRESULT pfa_poll(const PFA_TASK* task, BOOL* complete) {
    LONG phase;
    if (!task || !complete) return E_POINTER;
    if (!prt_owns_thread()) return E_ACCESSDENIED;
    phase=InterlockedCompareExchange((volatile LONG*)&task->phase,0,0);
    *complete=phase==PFA_DONE || phase==PFA_SETTLED;
    return S_OK;
}
HRESULT pfa_cancel(PFA_TASK* task) {
    LONG phase;
    if (!task) return E_POINTER;
    phase=InterlockedCompareExchange(&task->phase,0,0);
    if (phase!=PFA_STAGED && !prt_owns_thread()) return E_ACCESSDENIED;
    if (phase==PFA_FINISHING || phase==PFA_SETTLED) return E_UNEXPECTED;
    InterlockedExchange(&task->cancelled,1);return S_OK;
}
HRESULT pfa_finish(PFA_TASK* task, TC_FORMAT_FRAME* output, BOOL* invoked) {
    HRESULT result,current,settled;
    if (!invoked) return E_POINTER;
    *invoked=FALSE;
    if (!task) return E_POINTER;
    if (!prt_owns_thread()) return E_ACCESSDENIED;
    { LONG phase=InterlockedCompareExchange(&task->phase,PFA_FINISHING,PFA_DONE);
      if (phase!=PFA_DONE) return phase==PFA_RUNNING ? E_PENDING : E_UNEXPECTED; }
    *invoked=task->invoked;result=task->result;
    if (InterlockedCompareExchange(&task->cancelled,0,0)) result=HRESULT_FROM_WIN32(ERROR_CANCELLED);
    settled=pfa_settle(task,&current);
    if (FAILED(settled)) {InterlockedExchange(&task->phase,PFA_DONE);return settled;}
    if (FAILED(current)) result=current;
    InterlockedExchange(&task->phase,PFA_SETTLED);
    if (result==S_OK && output) *output=task->frame;
    return result;
}
HRESULT pfa_dispose(PFA_TASK* task) {
    LONG phase;
    if (!task) return E_POINTER;
    phase=InterlockedCompareExchange(&task->phase,0,0);
    if ((phase!=PFA_STAGED && phase!=PFA_SETTLED) || task->hold.token || task->captured) return E_PENDING;
    CloseThreadpoolWork(task->work);
    HeapFree(GetProcessHeap(),0,task->source);HeapFree(GetProcessHeap(),0,task);
    return S_OK;
}
