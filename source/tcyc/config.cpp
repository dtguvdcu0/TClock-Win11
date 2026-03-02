#include "config.h"

#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace tcyc {

namespace {

int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (auto& ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return out;
}

std::wstring ReadIniString(const std::wstring& iniPath, const wchar_t* section, const wchar_t* key, const wchar_t* defval) {
    wchar_t buf[2048] = {0};
    GetPrivateProfileStringW(section, key, defval ? defval : L"", buf, static_cast<DWORD>(_countof(buf)), iniPath.c_str());
    return std::wstring(buf);
}

int ReadIniInt(const std::wstring& iniPath, const wchar_t* section, const wchar_t* key, int defval) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, defval, iniPath.c_str()));
}

bool TryParseTimeOfDay(const std::wstring& s, int& outSec) {
    int hh = 0, mm = 0, ss = 0;
    if (swscanf_s(s.c_str(), L"%d:%d:%d", &hh, &mm, &ss) < 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
    outSec = hh * 3600 + mm * 60 + ss;
    return true;
}

int ParseWeekday(const std::wstring& raw) {
    const std::wstring s = ToLower(raw);
    if (s == L"sun" || s == L"sunday") return 0;
    if (s == L"mon" || s == L"monday") return 1;
    if (s == L"tue" || s == L"tuesday") return 2;
    if (s == L"wed" || s == L"wednesday") return 3;
    if (s == L"thu" || s == L"thursday") return 4;
    if (s == L"fri" || s == L"friday") return 5;
    if (s == L"sat" || s == L"saturday") return 6;
    return -1;
}

TriggerType ParseTriggerType(const std::wstring& raw) {
    const std::wstring s = ToLower(raw);
    if (s == L"interval") return TriggerType::Interval;
    if (s == L"datetime_interval_limited") return TriggerType::DateTimeIntervalLimited;
    if (s == L"weekly_time") return TriggerType::WeeklyTime;
    if (s == L"startup") return TriggerType::Startup;
    if (s == L"hotkey_only") return TriggerType::HotkeyOnly;
    return TriggerType::Unknown;
}

void LoadTasks(const std::wstring& iniPath, const std::wstring& exeDir, std::vector<TaskConfig>& outTasks) {
    outTasks.clear();
    for (int i = 1; i <= 128; ++i) {
        wchar_t section[32] = {0};
        swprintf_s(section, L"Task.%d", i);

        TaskConfig t{};
        t.id = i;
        t.enabled = ReadIniInt(iniPath, section, L"Enabled", 0) != 0;
        t.name = ReadIniString(iniPath, section, L"Name", L"");
        t.actionPath = ReadIniString(iniPath, section, L"ActionPath", L"");
        t.actionArgs = ReadIniString(iniPath, section, L"ActionArgs", L"");
        t.actionCwd = ReadIniString(iniPath, section, L"ActionCwd", L"");
        t.singleInstance = ReadIniInt(iniPath, section, L"SingleInstance", 1) != 0;
        t.watchdogEnabled = ReadIniInt(iniPath, section, L"WatchdogEnabled", 0) != 0;
        t.watchdogRetrySec = ClampInt(ReadIniInt(iniPath, section, L"WatchdogRetrySec", 10), 1, 3600);
        t.watchdogMaxRetry = ReadIniInt(iniPath, section, L"WatchdogMaxRetry", -1);
        t.watchdogRequireArgsMatch = ReadIniInt(iniPath, section, L"WatchdogRequireArgsMatch", 1) != 0;
        t.intervalSec = ClampInt(ReadIniInt(iniPath, section, L"IntervalSec", 0), 0, 86400);
        t.startDateTime = ReadIniString(iniPath, section, L"StartDateTime", L"");
        t.repeatEverySec = ClampInt(ReadIniInt(iniPath, section, L"RepeatEverySec", 0), 0, 86400);
        t.repeatCount = ClampInt(ReadIniInt(iniPath, section, L"RepeatCount", 0), 0, 1000000);
        t.hotkey = ReadIniString(iniPath, section, L"Hotkey", L"");
        t.trigger = ParseTriggerType(ReadIniString(iniPath, section, L"TriggerType", L""));
        t.weekday = ParseWeekday(ReadIniString(iniPath, section, L"Weekday", L""));
        t.timeOfDaySec = -1;
        {
            int tod = -1;
            if (TryParseTimeOfDay(ReadIniString(iniPath, section, L"TimeOfDay", L""), tod)) t.timeOfDaySec = tod;
        }

        if (t.actionPath.empty() && t.name.empty() && t.trigger == TriggerType::Unknown && !t.enabled) {
            continue;
        }
        t.actionPath = ResolvePathFromExe(exeDir, t.actionPath);
        if (!t.actionCwd.empty()) t.actionCwd = ResolvePathFromExe(exeDir, t.actionCwd);
        outTasks.push_back(t);
    }
}

} // namespace

std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(_countof(path)));
    if (n == 0 || n >= _countof(path)) {
        return L".";
    }
    PathRemoveFileSpecW(path);
    return std::wstring(path);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& leaf) {
    if (dir.empty()) return leaf;
    std::wstring out = dir;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out.append(leaf);
    return out;
}

bool IsAbsolutePath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    return false;
}

std::wstring ResolvePathFromExe(const std::wstring& exeDir, const std::wstring& path) {
    if (path.empty()) return path;
    if (IsAbsolutePath(path)) return path;
    return JoinPath(exeDir, path);
}

bool EnsureDefaultIni(const std::wstring& iniPath) {
    DWORD attrs = GetFileAttributesW(iniPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    if (!WritePrivateProfileStringW(L"TCycle", L"Enabled", L"1", iniPath.c_str())) return false;
    WritePrivateProfileStringW(L"TCycle", L"PollSec", L"1", iniPath.c_str());
    WritePrivateProfileStringW(L"TCycle", L"GraceSec", L"60", iniPath.c_str());
    WritePrivateProfileStringW(L"TCycle", L"LogLevel", L"1", iniPath.c_str());
    WritePrivateProfileStringW(L"TCycle", L"LogFile", L"tcycle.log", iniPath.c_str());
    WritePrivateProfileStringW(L"TCycle", L"StateFile", L"tcycle.state.ini", iniPath.c_str());
    WritePrivateProfileStringW(L"Debug", L"ForceCmdlineReadFail", L"0", iniPath.c_str());

    WritePrivateProfileStringW(L"Integration", L"UseTClockIniGate", L"1", iniPath.c_str());
    WritePrivateProfileStringW(L"Integration", L"TClockIniPath", L"tclock-win11.ini", iniPath.c_str());
    WritePrivateProfileStringW(L"Integration", L"TClockGateSection", L"TCycle", iniPath.c_str());
    WritePrivateProfileStringW(L"Integration", L"TClockGateKey", L"Enabled", iniPath.c_str());
    return true;
}

bool LoadRuntimeConfig(const std::wstring& iniPath, const std::wstring& exeDir, RuntimeConfig& outConfig, std::wstring& outError) {
    outError.clear();
    if (!EnsureDefaultIni(iniPath)) {
        outError = L"Failed to create default ini: " + iniPath;
        return false;
    }

    RuntimeConfig cfg{};
    cfg.enabled = ReadIniInt(iniPath, L"TCycle", L"Enabled", 1) != 0;
    cfg.pollSec = ClampInt(ReadIniInt(iniPath, L"TCycle", L"PollSec", 1), 1, 60);
    cfg.graceSec = ClampInt(ReadIniInt(iniPath, L"TCycle", L"GraceSec", 60), 0, 300);
    cfg.logLevel = ClampInt(ReadIniInt(iniPath, L"TCycle", L"LogLevel", 1), 0, 3);
    cfg.logFile = ReadIniString(iniPath, L"TCycle", L"LogFile", L"tcycle.log");
    if (cfg.logFile.empty()) cfg.logFile = L"tcycle.log";
    cfg.logFile = ResolvePathFromExe(exeDir, cfg.logFile);
    cfg.stateFile = ReadIniString(iniPath, L"TCycle", L"StateFile", L"tcycle.state.ini");
    if (cfg.stateFile.empty()) cfg.stateFile = L"tcycle.state.ini";
    cfg.stateFile = ResolvePathFromExe(exeDir, cfg.stateFile);
    cfg.debugForceCmdlineReadFail = ReadIniInt(iniPath, L"Debug", L"ForceCmdlineReadFail", 0) != 0;

    cfg.useTClockIniGate = ReadIniInt(iniPath, L"Integration", L"UseTClockIniGate", 1) != 0;
    cfg.tclockIniPath = ReadIniString(iniPath, L"Integration", L"TClockIniPath", L"tclock-win11.ini");
    if (cfg.tclockIniPath.empty()) cfg.tclockIniPath = L"tclock-win11.ini";
    cfg.tclockIniPath = ResolvePathFromExe(exeDir, cfg.tclockIniPath);
    cfg.tclockGateSection = ReadIniString(iniPath, L"Integration", L"TClockGateSection", L"TCycle");
    cfg.tclockGateKey = ReadIniString(iniPath, L"Integration", L"TClockGateKey", L"Enabled");
    if (cfg.tclockGateSection.empty()) cfg.tclockGateSection = L"TCycle";
    if (cfg.tclockGateKey.empty()) cfg.tclockGateKey = L"Enabled";
    LoadTasks(iniPath, exeDir, cfg.tasks);

    outConfig = cfg;
    return true;
}

bool IsTClockGateDisabled(const RuntimeConfig& config) {
    if (!config.useTClockIniGate) return false;
    const DWORD attrs = GetFileAttributesW(config.tclockIniPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    const int gate = static_cast<int>(GetPrivateProfileIntW(
        config.tclockGateSection.c_str(),
        config.tclockGateKey.c_str(),
        1,
        config.tclockIniPath.c_str()));
    return gate == 0;
}

} // namespace tcyc
