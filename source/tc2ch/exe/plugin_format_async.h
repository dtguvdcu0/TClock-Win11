#ifndef TCLOCK_PLUGIN_FORMAT_ASYNC_H
#define TCLOCK_PLUGIN_FORMAT_ASYNC_H
#include "plugin_format.h"

typedef struct PFA_TASK PFA_TASK;
/* /2 only. Metadata/capture/release/finish run on the runtime UI thread.
   Stage owns a deep input copy and may run on an I/O thread.
   A launched task remains owned until poll reports completion and finish settles
   its snapshot/hold. Cancel suppresses publication, never preempts native code.
   Caller serializes ownership; no task pointer belongs in an untrusted message. */
HRESULT pfa_prepare(PFRM_BINDING* binding);
HRESULT pfa_stage(const TC_FORMAT_SOURCE* source, UINT bytes, UINT capacity, PFA_TASK** output);
HRESULT pfa_launch(const PFRM_BINDING* binding, PFA_TASK* task);
HRESULT pfa_poll(const PFA_TASK* task, BOOL* complete);
HRESULT pfa_cancel(PFA_TASK* task);
/* A NULL output explicitly discards the result. Other failures preserve output.
   Finish consumes snapshot/hold even on callback failure or stale ownership. */
HRESULT pfa_finish(PFA_TASK* task, TC_FORMAT_FRAME* output, BOOL* invoked);
/* Stage-only or settled tasks may be disposed; running tasks return E_PENDING. */
HRESULT pfa_dispose(PFA_TASK* task);
#endif
