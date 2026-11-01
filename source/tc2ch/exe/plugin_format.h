#ifndef TCLOCK_PLUGIN_FORMAT_H
#define TCLOCK_PLUGIN_FORMAT_H
#include "plugin_runtime.h"
#include "../common/formatter_api.h"

/* Host-private owned selection and immutable registration demand. */
typedef struct PFRM_BINDING {
    PRT_FEATURE_REF owner;
    TC_FORMAT_DEMAND demand;
} PFRM_BINDING;

/* Failure leaves the caller's binding/frame unchanged.
   invoked reports the DLL format callback, not host validation adapters. */
HRESULT pfrm_prepare(PFRM_BINDING* binding);
HRESULT pfrm_query(const PFRM_BINDING* binding, const TC_FORMAT_SOURCE* source,
    UINT sourceBytes, UINT capacity, TC_FORMAT_FRAME* output, BOOL* invoked);
#endif
