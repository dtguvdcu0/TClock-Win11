#include "scheduler.h"

#include <windows.h>
#include <time.h>

namespace tcyc {

namespace {

bool ParseLocalDateTime(const std::wstring& s, long long& outUnix) {
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;
    if (swscanf_s(s.c_str(), L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) < 5) return false;
    std::tm tmv{};
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = hh;
    tmv.tm_min = mm;
    tmv.tm_sec = ss;
    tmv.tm_isdst = -1;
    time_t t = mktime(&tmv);
    if (t == static_cast<time_t>(-1)) return false;
    outUnix = static_cast<long long>(t);
    return true;
}

bool IsDueWindow(long long dueUnix, long long lastCheckUnix, long long nowUnix, int graceSec) {
    return (dueUnix > lastCheckUnix) && (dueUnix <= (nowUnix + graceSec));
}

} // namespace

std::vector<DueTask> EvaluateDueTasks(const RuntimeConfig& cfg, RuntimeState& state, long long nowUnix) {
    std::vector<DueTask> out;
    for (const auto& t : cfg.tasks) {
        if (!t.enabled) continue;
        TaskRuntimeState& st = state.tasks[t.id];
        long long lastCheck = st.lastCheckUnix;
        if (lastCheck <= 0) lastCheck = nowUnix - cfg.pollSec;

        bool due = false;
        std::wstring reason;
        if (t.trigger == TriggerType::Startup) {
            if (!st.startupDone) {
                due = true;
                reason = L"startup";
            }
        } else if (t.trigger == TriggerType::Interval) {
            if (t.intervalSec > 0) {
                long long dueUnix = (st.lastFireUnix > 0) ? (st.lastFireUnix + t.intervalSec) : nowUnix;
                if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                    due = true;
                    reason = L"interval";
                }
            }
        } else if (t.trigger == TriggerType::DateTimeIntervalLimited) {
            if (t.repeatCount > 0 && st.firedCount < t.repeatCount && t.repeatEverySec > 0) {
                long long baseUnix = 0;
                if (ParseLocalDateTime(t.startDateTime, baseUnix)) {
                    long long dueUnix = baseUnix + static_cast<long long>(st.firedCount) * t.repeatEverySec;
                    if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                        due = true;
                        reason = L"datetime_interval_limited";
                    }
                }
            }
        } else if (t.trigger == TriggerType::WeeklyTime) {
            if (t.weekday >= 0 && t.weekday <= 6 && t.timeOfDaySec >= 0) {
                std::tm nowTm{};
                time_t nowT = static_cast<time_t>(nowUnix);
                localtime_s(&nowTm, &nowT);
                if (nowTm.tm_wday == t.weekday) {
                    std::tm dueTm = nowTm;
                    dueTm.tm_hour = t.timeOfDaySec / 3600;
                    dueTm.tm_min = (t.timeOfDaySec % 3600) / 60;
                    dueTm.tm_sec = t.timeOfDaySec % 60;
                    long long dueUnix = static_cast<long long>(mktime(&dueTm));
                    if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                        due = true;
                        reason = L"weekly_time";
                    }
                }
            }
        }

        st.lastCheckUnix = nowUnix;
        if (due) {
            out.push_back({t.id, reason});
        }
    }
    return out;
}

void MarkTaskFired(RuntimeState& state, int taskId, long long fireUnix) {
    TaskRuntimeState& st = state.tasks[taskId];
    st.lastFireUnix = fireUnix;
    st.firedCount += 1;
    st.startupDone = true;
}

void MarkTaskObservedRunning(RuntimeState& state, int taskId, long long nowUnix, bool consumeCount) {
    TaskRuntimeState& st = state.tasks[taskId];
    st.lastFireUnix = nowUnix;
    st.startupDone = true;
    if (consumeCount) st.firedCount += 1;
}

} // namespace tcyc
