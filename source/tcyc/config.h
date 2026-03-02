#pragma once

#include <string>
#include <vector>

namespace tcyc {

enum class TriggerType {
    Interval,
    DateTimeIntervalLimited,
    WeeklyTime,
    Startup,
    HotkeyOnly,
    Unknown
};

struct TaskConfig {
    int id = 0;
    bool enabled = false;
    std::wstring name;
    TriggerType trigger = TriggerType::Unknown;
    std::wstring actionPath;
    std::wstring actionArgs;
    std::wstring actionCwd;
    bool singleInstance = true;
    bool watchdogEnabled = false;
    int watchdogRetrySec = 10;
    int watchdogMaxRetry = -1;
    bool watchdogRequireArgsMatch = true;
    int intervalSec = 0;
    std::wstring startDateTime;
    int repeatEverySec = 0;
    int repeatCount = 0;
    int weekday = -1; // 0=Sun ... 6=Sat
    int timeOfDaySec = -1;
    std::wstring hotkey;
};

struct RuntimeConfig {
    bool enabled = true;
    int pollSec = 1;
    int graceSec = 60;
    int logLevel = 1;
    std::wstring logFile;
    std::wstring stateFile;
    bool debugForceCmdlineReadFail = false; // Test-only: force args-read fallback mode when matching by path.
    bool useTClockIniGate = true;
    std::wstring tclockIniPath;
    std::wstring tclockGateSection = L"TCycle";
    std::wstring tclockGateKey = L"Enabled";
    std::vector<TaskConfig> tasks;
};

std::wstring GetExeDirectory();
std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf);
bool IsAbsolutePath(const std::wstring& path);
std::wstring ResolvePathFromExe(const std::wstring& exeDir, const std::wstring& path);

bool EnsureDefaultIni(const std::wstring& iniPath);
bool LoadRuntimeConfig(const std::wstring& iniPath, const std::wstring& exeDir, RuntimeConfig& outConfig, std::wstring& outError);
bool IsTClockGateDisabled(const RuntimeConfig& config);

} // namespace tcyc
