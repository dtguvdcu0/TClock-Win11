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

bool ParseDateYmd(const std::wstring& s, int& y, int& mo, int& d) {
    y = 0;
    mo = 0;
    d = 0;
    if (swscanf_s(s.c_str(), L"%d-%d-%d", &y, &mo, &d) != 3) return false;
    if (y < 1970 || y > 9999) return false;
    if (mo < 1 || mo > 12) return false;
    if (d < 1 || d > 31) return false;
    return true;
}

long long ComputeWindowStart(long long lastCheckUnix, long long nowUnix, int graceSec) {
    long long windowStart = lastCheckUnix;
    if (graceSec > 0) {
        const long long graceStart = nowUnix - graceSec;
        if (windowStart < graceStart) windowStart = graceStart;
    }
    return windowStart;
}

bool IsDueWindow(long long dueUnix, long long lastCheckUnix, long long nowUnix, int graceSec) {
    const long long windowStart = ComputeWindowStart(lastCheckUnix, nowUnix, graceSec);
    return (dueUnix > windowStart) && (dueUnix <= nowUnix);
}

int TriggerBit(TriggerType t) {
    switch (t) {
    case TriggerType::Interval: return (1 << 0);
    case TriggerType::DateTimeIntervalLimited: return (1 << 1);
    case TriggerType::WeeklyTime: return (1 << 2);
    case TriggerType::Startup: return (1 << 3);
    case TriggerType::HotkeyOnly: return (1 << 4);
    case TriggerType::NonRunning: return (1 << 5);
    default: return 0;
    }
}

bool HasTrigger(const TaskConfig& t, TriggerType tr) {
    if (t.triggerMask != 0) {
        return (t.triggerMask & TriggerBit(tr)) != 0;
    }
    return t.trigger == tr;
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
        if (HasTrigger(t, TriggerType::Startup)) {
            if (!st.startupDone) {
                due = true;
                reason = L"startup";
            }
        }

        if (!due && HasTrigger(t, TriggerType::Interval)) {
            if (t.intervalSec > 0) {
                long long dueUnix = nowUnix;
                if (st.lastFireUnix > 0) {
                    dueUnix = st.lastFireUnix + t.intervalSec;
                    const long long windowStart = ComputeWindowStart(lastCheck, nowUnix, cfg.graceSec);
                    if (dueUnix <= windowStart) {
                        const long long missedSteps = ((windowStart - dueUnix) / t.intervalSec) + 1;
                        dueUnix += missedSteps * t.intervalSec;
                    }
                }
                if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                    due = true;
                    reason = L"interval";
                }
            }
        }

        if (!due && HasTrigger(t, TriggerType::DateTimeIntervalLimited)) {
            if (t.repeatCount > 0 && st.firedCount < t.repeatCount && t.repeatEverySec > 0) {
                long long baseUnix = 0;
                if (ParseLocalDateTime(t.startDateTime, baseUnix)) {
                    int evalCount = st.firedCount;
                    long long dueUnix = baseUnix + static_cast<long long>(evalCount) * t.repeatEverySec;
                    const long long windowStart = ComputeWindowStart(lastCheck, nowUnix, cfg.graceSec);
                    if (dueUnix <= windowStart) {
                        const long long advance = ((windowStart - dueUnix) / t.repeatEverySec) + 1;
                        evalCount += static_cast<int>(advance);
                        if (evalCount > t.repeatCount) evalCount = t.repeatCount;
                        dueUnix = baseUnix + static_cast<long long>(evalCount) * t.repeatEverySec;
                        // Keep scheduler evaluation pure; state is only consumed at launch boundary.
                    }
                    if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                        due = true;
                        reason = L"datetime_interval_limited";
                    }
                }
            }
        }

        if (!due && HasTrigger(t, TriggerType::WeeklyTime)) {
            std::tm nowTm{};
            time_t nowT = static_cast<time_t>(nowUnix);
            localtime_s(&nowTm, &nowT);

            bool dateOk = true;
            if (t.dateEnabled) {
                int y = 0, mo = 0, d = 0;
                if (!ParseDateYmd(t.dateYmd, y, mo, d)) {
                    dateOk = false;
                } else {
                    dateOk = (y == (nowTm.tm_year + 1900) && mo == (nowTm.tm_mon + 1) && d == nowTm.tm_mday);
                }
            }

            bool weekdayOk = true;
            if (t.weekdayEnabled) {
                bool allDays = t.weeklyEveryday;
                int weekdayMask = t.weekdayMask;
                if (!allDays && weekdayMask == 0 && t.weekday >= 0 && t.weekday <= 6) {
                    weekdayMask = (1 << t.weekday);
                }
                weekdayOk = allDays || ((weekdayMask & (1 << nowTm.tm_wday)) != 0);
            }

            bool timeOk = true;
            int dueSec = 0;
            if (t.timeEnabled) {
                if (t.timeOfDaySec < 0) {
                    timeOk = false;
                } else {
                    dueSec = t.timeOfDaySec;
                }
            }

            if (dateOk && weekdayOk && timeOk) {
                std::tm dueTm = nowTm;
                dueTm.tm_hour = dueSec / 3600;
                dueTm.tm_min = (dueSec % 3600) / 60;
                dueTm.tm_sec = dueSec % 60;
                long long dueUnix = static_cast<long long>(mktime(&dueTm));
                if (IsDueWindow(dueUnix, lastCheck, nowUnix, cfg.graceSec)) {
                    due = true;
                    reason = L"weekly_time";
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
