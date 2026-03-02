#include "watchdog.h"
#include "log.h"
#include "runner.h"

namespace tcyc {

std::vector<WatchdogDue> EvaluateWatchdogDue(const RuntimeConfig& cfg, RuntimeState& state, long long nowUnix) {
    std::vector<WatchdogDue> out;
    for (const auto& t : cfg.tasks) {
        if (!t.enabled || !t.watchdogEnabled) continue;
        if (t.actionPath.empty()) continue;

        TaskRuntimeState& st = state.tasks[t.id];
        ProcessMatchMode mode = ProcessMatchMode::None;
        if (IsTaskProcessRunning(t, &mode, cfg.debugForceCmdlineReadFail)) {
            if (mode == ProcessMatchMode::PathOnlyFallbackArgsUnavailable) {
                LogWrite(1, L"Watchdog process match fallback: taskId=%d mode=path_only_fallback_args_unavailable", t.id);
            }
            st.watchdogRetryCount = 0;
            st.watchdogNextRetryUnix = 0;
            continue;
        }

        if (st.watchdogNextRetryUnix == 0) {
            st.watchdogNextRetryUnix = nowUnix + t.watchdogRetrySec;
            continue;
        }
        if (nowUnix < st.watchdogNextRetryUnix) continue;

        if (t.watchdogMaxRetry >= 0 && st.watchdogRetryCount >= t.watchdogMaxRetry) {
            continue;
        }
        out.push_back({t.id, L"watchdog_retry"});
    }
    return out;
}

void MarkWatchdogLaunched(RuntimeState& state, int taskId, long long nextRetryUnix) {
    TaskRuntimeState& st = state.tasks[taskId];
    st.watchdogRetryCount += 1;
    st.watchdogNextRetryUnix = nextRetryUnix;
}

} // namespace tcyc
