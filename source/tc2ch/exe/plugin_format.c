#include "plugin_format.h"
#include <string.h>
#include <wchar.h>

typedef struct PFRM_QUERY {
    const PFRM_BINDING* binding;
    const TC_FORMAT_SOURCE* source;
    UINT sourceBytes, capacity;
    ULONGLONG revision;
    TC_FORMAT_FRAME* output;
    BOOL invoked;
} PFRM_QUERY;

static BOOL pfrm_check_table(const TC_FORMATTER* formatter) {
    return formatter && formatter->size == sizeof(*formatter) &&
        formatter->format && TcfValidateDemand(&formatter->demand);
}

static HRESULT pfrm_read_demand(const void* table, void* user, void* context) {
    const TC_FORMATTER* formatter=(const TC_FORMATTER*)table;
    (void)user;
    if (!pfrm_check_table(formatter)) return E_INVALIDARG;
    *(TC_FORMAT_DEMAND*)context=formatter->demand;
    return S_OK;
}

HRESULT pfrm_prepare(PFRM_BINDING* binding) {
    PFRM_BINDING staged;BOOL invoked;HRESULT result;
    if (!binding) return E_POINTER;
    ZeroMemory(&staged,sizeof(staged));
    result=prt_capture_feature(TC_PLUGIN_FORMATTER_ID,&staged.owner);
    if (FAILED(result)) return result;
    result=prt_run_feature(&staged.owner,pfrm_read_demand,&staged.demand,&invoked);
    if (result == S_OK) *binding=staged;
    return result;
}

static HRESULT pfrm_format_source(const void* table, void* user, void* context) {
    const TC_FORMATTER* formatter=(const TC_FORMATTER*)table;
    PFRM_QUERY* query=(PFRM_QUERY*)context;HRESULT result;
    if (!pfrm_check_table(formatter) ||
        memcmp(&formatter->demand,&query->binding->demand,sizeof(formatter->demand)))
        return E_INVALIDARG;
    query->invoked=TRUE;
    result=formatter->format(user,query->source,query->sourceBytes,query->capacity,query->output);
    if (result == S_OK && !TcfValidateFrame(query->output,sizeof(*query->output),
        query->revision,query->capacity)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return result;
}

HRESULT pfrm_query(const PFRM_BINDING* binding, const TC_FORMAT_SOURCE* source,
    UINT sourceBytes, UINT capacity, TC_FORMAT_FRAME* output, BOOL* invoked) {
    PFRM_QUERY query;HRESULT result;BOOL adapted;
    if (!invoked) return E_POINTER;
    *invoked=FALSE;
    if (!binding || !output) return E_POINTER;
    if (!TcfValidateDemand(&binding->demand) ||
        wcsnlen(binding->owner.interface_id,TC_FEATURE_INTERFACE_CHARS) == TC_FEATURE_INTERFACE_CHARS ||
        wcscmp(binding->owner.interface_id,TC_PLUGIN_FORMATTER_ID) ||
        !capacity || capacity > TCF_FRAME_CCH || !TcfValidateSource(source,sourceBytes)) return E_INVALIDARG;
    if (!(binding->demand.consumers & (1u << (source->consumer - 1)))) {
        result=prt_check_feature(&binding->owner);
        return FAILED(result) ? result : S_FALSE;
    }
    ZeroMemory(&query,sizeof(query));
    query.binding=binding;query.source=source;query.sourceBytes=sourceBytes;
    query.capacity=capacity;query.revision=source->revision;
    query.output=(TC_FORMAT_FRAME*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*query.output));
    if (!query.output) return E_OUTOFMEMORY;
    result=prt_run_feature(&binding->owner,pfrm_format_source,&query,&adapted);
    *invoked=query.invoked;
    if (result == S_OK) *output=*query.output;
    HeapFree(GetProcessHeap(),0,query.output);
    return result;
}
