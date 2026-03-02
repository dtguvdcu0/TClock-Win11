#include "config.h"
#include "hotkey.h"
#include "log.h"
#include "runner.h"
#include "scheduler.h"
#include "state.h"
#include "watchdog.h"

#include <windows.h>
#include <shellapi.h>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\TCycle_Singleton_Mutex";

struct Args {
    bool validateOnly = false;
};

Args ParseArgs() {
    Args a{};
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return a;
    for (int i = 1; i < argc; ++i) {
        if (lstrcmpiW(argv[i], L"--validate-config") == 0) {
            a.validateOnly = true;
        }
    }
    LocalFree(argv);
    return a;
}

bool AcquireSingleton() {
    HANDLE h = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!h) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return false;
    }
    return true;
}

const wchar_t* MatchModeToString(tcyc::ProcessMatchMode mode) {
    switch (mode) {
    case tcyc::ProcessMatchMode::PathOnly:
        return L"path_only";
    case tcyc::ProcessMatchMode::PathOnlyFallbackArgsUnavailable:
        return L"path_only_fallback_args_unavailable";
    case tcyc::ProcessMatchMode::PathAndArgs:
        return L"path_and_args";
    default:
        return L"none";
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const Args args = ParseArgs();
    if (!AcquireSingleton()) {
        return 0;
    }

    const std::wstring exeDir = tcyc::GetExeDirectory();
    const std::wstring iniPath = tcyc::JoinPath(exeDir, L"TCycle.ini");
    tcyc::RuntimeConfig cfg{};
    std::wstring err;
    if (!tcyc::LoadRuntimeConfig(iniPath, exeDir, cfg, err)) {
        MessageBoxW(nullptr, err.c_str(), L"TCycle", MB_OK | MB_ICONERROR);
        return 1;
    }

    tcyc::LogInit(cfg.logFile);
    tcyc::LogWrite(1, L"TCycle boot: ini=%s", iniPath.c_str());

    tcyc::RuntimeState state{};
    if (!tcyc::LoadRuntimeState(cfg.stateFile, state)) {
        tcyc::LogWrite(0, L"State load failed: %s", cfg.stateFile.c_str());
    }
    state.bootCount += 1;
    state.lastEvalUnix = tcyc::UnixNow();
    state.lastSaveUnix = state.lastEvalUnix;
    if (!tcyc::SaveRuntimeState(cfg.stateFile, state)) {
        tcyc::LogWrite(0, L"State save failed at boot: %s", cfg.stateFile.c_str());
    } else {
        tcyc::LogWrite(1, L"State loaded: bootCount=%d", state.bootCount);
    }
    WritePrivateProfileStringW(L"Runtime", L"ResolvedLogFile", cfg.logFile.c_str(), cfg.stateFile.c_str());

    if (args.validateOnly) {
        const bool gateOff = tcyc::IsTClockGateDisabled(cfg);
        tcyc::LogWrite(1, L"Validate-only: enabled=%d gateOff=%d pollSec=%d",
            cfg.enabled ? 1 : 0, gateOff ? 1 : 0, cfg.pollSec);
        return 0;
    }

    bool prevEffectiveEnabled = false;
    bool hasPrev = false;
    std::wstring hotkeySig;

    for (;;) {
        if (!tcyc::LoadRuntimeConfig(iniPath, exeDir, cfg, err)) {
            tcyc::LogWrite(0, L"Config reload failed: %s", err.c_str());
            Sleep(3000);
            continue;
        }

        const bool gateOff = tcyc::IsTClockGateDisabled(cfg);
        const bool effectiveEnabled = cfg.enabled && !gateOff;

        if (!hasPrev || prevEffectiveEnabled != effectiveEnabled) {
            tcyc::LogWrite(1, L"EffectiveEnabled changed: enabled=%d gateOff=%d effective=%d",
                cfg.enabled ? 1 : 0, gateOff ? 1 : 0, effectiveEnabled ? 1 : 0);
            prevEffectiveEnabled = effectiveEnabled;
            hasPrev = true;
        }

        {
            std::wstring sigNow = tcyc::BuildHotkeyConfigSignature(cfg);
            if (sigNow != hotkeySig) {
                std::wstring hkErr;
                tcyc::ReloadHotkeys(cfg, hkErr);
                hotkeySig = sigNow;
                if (!hkErr.empty()) {
                    tcyc::LogWrite(0, L"Hotkey reload warning: %s", hkErr.c_str());
                } else {
                    tcyc::LogWrite(1, L"Hotkey set reloaded");
                }
            }
        }

        auto findTask = [&](int taskId) -> const tcyc::TaskConfig* {
            for (const auto& t : cfg.tasks) {
                if (t.id == taskId) return &t;
            }
            return nullptr;
        };

        auto tryLaunch = [&](const tcyc::TaskConfig& task, const wchar_t* reason, bool fromWatchdog) {
            if (task.actionPath.empty()) {
                tcyc::LogWrite(0, L"Launch skipped: taskId=%d reason=%s actionPath empty", task.id, reason);
                return;
            }
            tcyc::ProcessMatchMode mode = tcyc::ProcessMatchMode::None;
            const bool alreadyRunning = tcyc::IsTaskProcessRunning(task, &mode, cfg.debugForceCmdlineReadFail);
            if (task.singleInstance && alreadyRunning) {
                const long long now = tcyc::UnixNow();
                bool consumeCount = true;
                if (task.trigger == tcyc::TriggerType::HotkeyOnly) consumeCount = false;
                tcyc::MarkTaskObservedRunning(state, task.id, now, consumeCount);
                const tcyc::TaskRuntimeState& st = state.tasks[task.id];
                tcyc::LogWrite(1, L"Launch skipped: taskId=%d reason=%s already running (mode=%s)",
                    task.id, reason, MatchModeToString(mode));
                tcyc::LogWrite(2, L"State after running-observed: taskId=%d startupDone=%d firedCount=%d lastFire=%I64d",
                    task.id, st.startupDone ? 1 : 0, st.firedCount, st.lastFireUnix);
                return;
            }
            std::wstring launchErr;
            if (!tcyc::LaunchTask(task, launchErr)) {
                if (fromWatchdog) {
                    const long long now = tcyc::UnixNow();
                    tcyc::MarkWatchdogLaunched(state, task.id, now + task.watchdogRetrySec);
                }
                tcyc::LogWrite(0, L"Launch failed: taskId=%d reason=%s err=%s", task.id, reason, launchErr.c_str());
                return;
            }
            const long long now = tcyc::UnixNow();
            tcyc::MarkTaskFired(state, task.id, now);
            if (fromWatchdog) {
                tcyc::MarkWatchdogLaunched(state, task.id, now + task.watchdogRetrySec);
            }
            tcyc::LogWrite(1, L"Launch success: taskId=%d reason=%s", task.id, reason);
        };

        if (effectiveEnabled) {
            tcyc::LogWrite(2, L"Heartbeat: scheduler lane active");
            std::vector<tcyc::DueTask> due = tcyc::EvaluateDueTasks(cfg, state, tcyc::UnixNow());
            for (const auto& d : due) {
                const tcyc::TaskConfig* task = findTask(d.taskId);
                if (!task) {
                    tcyc::LogWrite(0, L"Due task missing config: taskId=%d", d.taskId);
                    continue;
                }
                tryLaunch(*task, d.reason.c_str(), false);
            }

            std::vector<tcyc::WatchdogDue> wd = tcyc::EvaluateWatchdogDue(cfg, state, tcyc::UnixNow());
            for (const auto& w : wd) {
                const tcyc::TaskConfig* task = findTask(w.taskId);
                if (!task) {
                    tcyc::LogWrite(0, L"Watchdog due missing config: taskId=%d", w.taskId);
                    continue;
                }
                tryLaunch(*task, w.reason.c_str(), true);
            }

            std::vector<int> hkTasks = tcyc::DrainHotkeyTaskIds();
            for (int tid : hkTasks) {
                const tcyc::TaskConfig* task = findTask(tid);
                if (!task) {
                    tcyc::LogWrite(0, L"Hotkey task missing config: taskId=%d", tid);
                    continue;
                }
                tryLaunch(*task, L"hotkey", false);
            }
        } else {
            tcyc::LogWrite(2, L"Heartbeat: all action lanes disabled by config/gate");
            (void)tcyc::DrainHotkeyTaskIds();
        }

        state.lastEvalUnix = tcyc::UnixNow();
        if (state.lastEvalUnix - state.lastSaveUnix >= 10) {
            state.lastSaveUnix = state.lastEvalUnix;
            if (!tcyc::SaveRuntimeState(cfg.stateFile, state)) {
                tcyc::LogWrite(0, L"State periodic save failed: %s", cfg.stateFile.c_str());
            }
        }

        const DWORD waitMs = static_cast<DWORD>(cfg.pollSec <= 0 ? 1000 : cfg.pollSec * 1000);
        Sleep(waitMs);
    }
}
