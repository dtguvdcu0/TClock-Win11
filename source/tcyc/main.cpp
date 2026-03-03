#include "config.h"
#include "gui_main.h"
#include "hotkey.h"
#include "log.h"
#include "runner.h"
#include "scheduler.h"
#include "state.h"
#include "watchdog.h"

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <unordered_map>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\TCycle_Singleton_Mutex";
constexpr wchar_t kReloadEventName[] = L"Local\\TCycle_Reload_Config_Event";

struct Args {
    bool validateOnly = false;
    bool settingsOnly = false;
    std::string preferredLanguage;
};

Args ParseArgs() {
    Args a{};
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return a;
    for (int i = 1; i < argc; ++i) {
        if (lstrcmpiW(argv[i], L"--validate-config") == 0) {
            a.validateOnly = true;
        } else if (lstrcmpiW(argv[i], L"--settings") == 0) {
            a.settingsOnly = true;
        } else if (wcsncmp(argv[i], L"--lang=", 7) == 0) {
            if (lstrcmpiW(argv[i] + 7, L"ja") == 0) a.preferredLanguage = "ja";
            else if (lstrcmpiW(argv[i] + 7, L"en") == 0) a.preferredLanguage = "en";
        } else if (lstrcmpiW(argv[i], L"--lang") == 0 && i + 1 < argc) {
            ++i;
            if (lstrcmpiW(argv[i], L"ja") == 0) a.preferredLanguage = "ja";
            else if (lstrcmpiW(argv[i], L"en") == 0) a.preferredLanguage = "en";
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

struct LaunchGuardSnapshot {
    bool gateOff = false;
    std::unordered_map<int, bool> taskEnabledCache;
};

LaunchGuardSnapshot BuildLaunchGuardSnapshot(const tcyc::RuntimeConfig& cfg, bool gateOff) {
    LaunchGuardSnapshot snap{};
    snap.gateOff = gateOff;
    for (const auto& t : cfg.tasks) {
        snap.taskEnabledCache.emplace(t.id, t.enabled);
    }
    return snap;
}

bool IsRuntimeLaunchAllowed(LaunchGuardSnapshot& snap, int taskId, const wchar_t* reason) {
    auto it = snap.taskEnabledCache.find(taskId);
    const bool taskEnabledNow = (it != snap.taskEnabledCache.end()) ? it->second : false;

    if (!taskEnabledNow) {
        tcyc::LogWrite(1, L"Launch skipped: taskId=%d reason=%s disabled by config", taskId, reason);
        return false;
    }
    if (snap.gateOff) {
        tcyc::LogWrite(1, L"Launch skipped: taskId=%d reason=%s gate disabled at launch", taskId, reason);
        return false;
    }
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const Args args = ParseArgs();

    const std::wstring exeDir = tcyc::GetExeDirectory();
    const std::wstring iniPath = tcyc::JoinPath(exeDir, L"TCycle.ini");
    tcyc::RuntimeConfig cfg{};
    std::wstring err;
    if (!tcyc::LoadRuntimeConfig(iniPath, exeDir, cfg, err)) {
        MessageBoxW(nullptr, err.c_str(), L"TCycle", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (args.settingsOnly) {
        return tcyc::RunReadOnlySettingsWindow(cfg, iniPath, exeDir, args.preferredLanguage);
    }

    if (!AcquireSingleton()) {
        return 0;
    }
    HANDLE hReloadEvent = CreateEventW(nullptr, FALSE, FALSE, kReloadEventName);

    tcyc::LogInit(cfg.logFile);
    tcyc::LogSetLevel(cfg.logLevel);
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
        tcyc::LogWrite(1, L"Validate-only: gateOff=%d pollSec=%d",
            gateOff ? 1 : 0, cfg.pollSec);
        return 0;
    }

    bool prevEffectiveEnabled = false;
    bool hasPrev = false;
    std::wstring hotkeySig;

    auto applyReloadedConfig = [&]() -> bool {
        if (!tcyc::LoadRuntimeConfig(iniPath, exeDir, cfg, err)) {
            tcyc::LogWrite(0, L"Config reload failed: %s", err.c_str());
            return false;
        }
        tcyc::LogSetLevel(cfg.logLevel);

        const std::wstring sigNow = tcyc::BuildHotkeyConfigSignature(cfg);
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
        return true;
    };

    {
        hotkeySig = tcyc::BuildHotkeyConfigSignature(cfg);
        std::wstring hkErr;
        tcyc::ReloadHotkeys(cfg, hkErr);
        if (!hkErr.empty()) {
            tcyc::LogWrite(0, L"Hotkey reload warning: %s", hkErr.c_str());
        } else if (!hotkeySig.empty()) {
            tcyc::LogWrite(1, L"Hotkey set reloaded");
        }
    }

    for (;;) {
        const bool gateOff = tcyc::IsTClockGateDisabled(cfg);
        const bool effectiveEnabled = !gateOff;

        if (!hasPrev || prevEffectiveEnabled != effectiveEnabled) {
            tcyc::LogWrite(1, L"EffectiveEnabled changed: gateOff=%d effective=%d",
                gateOff ? 1 : 0, effectiveEnabled ? 1 : 0);
            prevEffectiveEnabled = effectiveEnabled;
            hasPrev = true;
        }

        auto findTask = [&](int taskId) -> const tcyc::TaskConfig* {
            for (const auto& t : cfg.tasks) {
                if (t.id == taskId) return &t;
            }
            return nullptr;
        };

        LaunchGuardSnapshot launchGuard = BuildLaunchGuardSnapshot(cfg, gateOff);

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
                if (wcscmp(reason, L"hotkey") == 0) consumeCount = false;
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
                if (!IsRuntimeLaunchAllowed(launchGuard, task->id, d.reason.c_str())) continue;
                tryLaunch(*task, d.reason.c_str(), false);
            }

            std::vector<tcyc::WatchdogDue> wd = tcyc::EvaluateWatchdogDue(cfg, state, tcyc::UnixNow());
            for (const auto& w : wd) {
                const tcyc::TaskConfig* task = findTask(w.taskId);
                if (!task) {
                    tcyc::LogWrite(0, L"Watchdog due missing config: taskId=%d", w.taskId);
                    continue;
                }
                if (!IsRuntimeLaunchAllowed(launchGuard, task->id, w.reason.c_str())) continue;
                tryLaunch(*task, w.reason.c_str(), true);
            }

            std::vector<int> hkTasks = tcyc::DrainHotkeyTaskIds();
            for (int tid : hkTasks) {
                const tcyc::TaskConfig* task = findTask(tid);
                if (!task) {
                    tcyc::LogWrite(0, L"Hotkey task missing config: taskId=%d", tid);
                    continue;
                }
                if (!IsRuntimeLaunchAllowed(launchGuard, task->id, L"hotkey")) continue;
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
        if (hReloadEvent) {
            DWORD wr = WaitForSingleObject(hReloadEvent, waitMs);
            if (wr == WAIT_OBJECT_0) {
                tcyc::LogWrite(1, L"Reload event received: config reload scheduled immediately");
                (void)applyReloadedConfig();
            }
        } else {
            Sleep(waitMs);
        }
    }
}
