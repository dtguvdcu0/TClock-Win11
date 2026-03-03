#include "gui_main.h"

#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cwctype>

namespace {
namespace fs = std::filesystem;

constexpr wchar_t kWindowClassName[] = L"TCycleSettingsWindow";
constexpr wchar_t kRenameClassName[] = L"TCycleRenameDialog";
constexpr wchar_t kReloadEventName[] = L"Local\\TCycle_Reload_Config_Event";

constexpr int kCtrlPollSec = 1002;
constexpr int kCtrlGraceSec = 1003;
constexpr int kCtrlTaskList = 1005;
constexpr int kCtrlTaskEnabled = 1006;
constexpr int kCtrlTriggerInterval = 1008;
constexpr int kCtrlIntervalSec = 1009;
constexpr int kCtrlRepeatCount = 1012;
constexpr int kCtrlTimeOfDay = 1013;
constexpr int kCtrlHotkeyMod = 1016;
constexpr int kCtrlHotkeyKey = 1017;
constexpr int kCtrlStatus = 1018;
constexpr int kCtrlTaskAdd = 1019;
constexpr int kCtrlTaskDelete = 1020;
constexpr int kCtrlTaskRename = 1021;
constexpr int kCtrlActionMode = 1022;
constexpr int kCtrlActionPath = 1023;
constexpr int kCtrlActionArgs = 1024;
constexpr int kCtrlActionCwd = 1025;
constexpr int kCtrlWeekdaySun = 1030;
constexpr int kCtrlWeekdayMon = 1031;
constexpr int kCtrlWeekdayTue = 1032;
constexpr int kCtrlWeekdayWed = 1033;
constexpr int kCtrlWeekdayThu = 1034;
constexpr int kCtrlWeekdayFri = 1035;
constexpr int kCtrlWeekdaySat = 1036;
constexpr int kCtrlWeekdayEveryday = 1049;
constexpr int kCtrlDateEnabled = 1050;
constexpr int kCtrlDateValue = 1051;
constexpr int kCtrlWeekdayEnabled = 1052;
constexpr int kCtrlTimeEnabled = 1053;
constexpr int kCtrlTriggerNonRunning = 1054;
constexpr int kCtrlWatchdogRetrySec = 1055;
constexpr int kCtrlSpinWatchdogRetrySec = 1056;
constexpr int kCtrlTriggerWeeklyTime = 1038;
constexpr int kCtrlTriggerStartup = 1039;
constexpr int kCtrlTriggerHotkeyOnly = 1040;
constexpr int kCtrlSpinPollSec = 1041;
constexpr int kCtrlSpinGraceSec = 1042;
constexpr int kCtrlSpinIntervalSec = 1043;
constexpr int kCtrlSpinRepeatCount = 1045;

struct HotkeyModOption {
    const wchar_t* label;
    bool ctrl;
    bool shift;
    bool alt;
    bool win;
};

struct HotkeyKeyOption {
    const wchar_t* label;
    int vk;
};

struct RenameDialogState {
    std::wstring title;
    std::wstring initialText;
    std::wstring resultText;
    HWND edit = nullptr;
    bool accepted = false;
};

struct WindowState {
    tcyc::RuntimeConfig config;
    std::wstring iniPath;
    std::wstring exeDir;
    std::string languageCode = "en";
    std::unordered_map<std::wstring, std::wstring> translations;

    HWND pollSec = nullptr;
    HWND graceSec = nullptr;

    HWND taskList = nullptr;
    HWND taskAdd = nullptr;
    HWND taskDelete = nullptr;
    HWND taskRename = nullptr;

    HWND taskEnabled = nullptr;
    HWND actionPrimaryLabel = nullptr;
    HWND actionMode = nullptr;
    HWND actionPath = nullptr;
    HWND actionArgs = nullptr;
    HWND actionCwd = nullptr;
    HWND triggerChecks[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    HWND intervalSec = nullptr;
    HWND watchdogRetrySec = nullptr;
    HWND repeatCount = nullptr;
    HWND dateEnabled = nullptr;
    HWND dateValue = nullptr;
    HWND weekdayEnabled = nullptr;
    HWND timeEnabled = nullptr;
    HWND weekdayEveryday = nullptr;
    HWND weekdayChecks[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    HWND timeOfDay = nullptr;
    HWND hotkeyMod = nullptr;
    HWND hotkeyKey = nullptr;
    HWND grpInterval = nullptr;
    HWND grpDateTime = nullptr;
    HWND grpWeekly = nullptr;
    HWND grpNonRunning = nullptr;
    HWND grpStartup = nullptr;
    HWND grpHotkey = nullptr;

    HWND status = nullptr;

    int selectedTask = -1;
    bool suppressEvents = false;
    bool actionPathDirty = false;
    bool actionCwdDirty = false;
};

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string TrimAscii(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::wstring UnescapeText(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            if (s[i + 1] == L'n') {
                out.push_back(L'\n');
                ++i;
                continue;
            }
            if (s[i + 1] == L'\\') {
                out.push_back(L'\\');
                ++i;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::wstring StripQuotes(const std::wstring& s) {
    if (s.size() >= 2) {
        if ((s.front() == L'"' && s.back() == L'"') || (s.front() == L'\'' && s.back() == L'\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

std::wstring TrimWide(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return s.substr(b, e - b);
}

fs::path GetLanguageRoot(const std::wstring& exeDir) {
    fs::path base(exeDir);
    fs::path modern = base / "tcyc" / "lang";
    if (fs::exists(modern) && fs::is_directory(modern)) return modern;
    return base / "lang";
}

bool LoadLanguage(WindowState* st, const std::string& code) {
    if (!st) return false;
    std::string lang = ToLowerAscii(code.empty() ? std::string("en") : code);
    if (lang != "ja" && lang != "en") lang = "en";
    st->translations.clear();
    st->languageCode = lang;

    auto loadFile = [&](const std::string& langCode) -> bool {
        fs::path path = GetLanguageRoot(st->exeDir) / langCode / "strings.ini";
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = TrimAscii(line.substr(0, eq));
            std::string val = line.substr(eq + 1);
            std::wstring wkey = Utf8ToWide(key);
            std::wstring wval = StripQuotes(UnescapeText(Utf8ToWide(val)));
            if (!wkey.empty()) st->translations[wkey] = wval;
        }
        return true;
    };

    bool loaded = loadFile(lang);
    if (!loaded && lang != "en") {
        loadFile("en");
        st->languageCode = "en";
    }
    return loaded;
}

std::wstring Tr(const WindowState* st, const wchar_t* id, const wchar_t* fallback) {
    if (!st || !id) return fallback ? std::wstring(fallback) : L"";
    auto it = st->translations.find(id);
    if (it != st->translations.end()) return it->second;
    return fallback ? std::wstring(fallback) : L"";
}

std::string ReadLanguageFromIni(const std::wstring& iniPath) {
    wchar_t buf[16] = {0};
    GetPrivateProfileStringW(L"TCycle", L"Language", L"", buf, static_cast<DWORD>(std::size(buf)), iniPath.c_str());
    std::wstring ws = TrimWide(buf);
    if (ws == L"ja" || ws == L"JA" || ws == L"Ja" || ws == L"jA") return "ja";
    if (ws == L"en" || ws == L"EN" || ws == L"En" || ws == L"eN") return "en";
    return "en";
}

std::wstring TriggerToString(tcyc::TriggerType trigger) {
    switch (trigger) {
    case tcyc::TriggerType::Interval: return L"interval";
    case tcyc::TriggerType::DateTimeIntervalLimited: return L"datetime_interval_limited";
    case tcyc::TriggerType::WeeklyTime: return L"weekly_time";
    case tcyc::TriggerType::Startup: return L"startup";
    case tcyc::TriggerType::HotkeyOnly: return L"hotkey_only";
    case tcyc::TriggerType::NonRunning: return L"non_running";
    default: return L"unknown";
    }
}

int TriggerTypeToIndex(tcyc::TriggerType trigger) {
    switch (trigger) {
    case tcyc::TriggerType::Interval: return 0;
    case tcyc::TriggerType::DateTimeIntervalLimited: return 1;
    case tcyc::TriggerType::WeeklyTime: return 2;
    case tcyc::TriggerType::Startup: return 3;
    case tcyc::TriggerType::HotkeyOnly: return 4;
    case tcyc::TriggerType::NonRunning: return 5;
    default: return 0;
    }
}

tcyc::TriggerType TriggerBitToType(int bit) {
    switch (bit) {
    case 0: return tcyc::TriggerType::Interval;
    case 1: return tcyc::TriggerType::DateTimeIntervalLimited;
    case 2: return tcyc::TriggerType::WeeklyTime;
    case 3: return tcyc::TriggerType::Startup;
    case 4: return tcyc::TriggerType::HotkeyOnly;
    case 5: return tcyc::TriggerType::NonRunning;
    default: return tcyc::TriggerType::Unknown;
    }
}

int TriggerTypeToBit(tcyc::TriggerType trigger) {
    const int idx = TriggerTypeToIndex(trigger);
    if (idx < 0 || idx > 5) return 0;
    return (1 << idx);
}

std::wstring TriggerMaskToString(int mask) {
    std::wstring out;
    for (int i = 0; i < 6; ++i) {
        if ((mask & (1 << i)) == 0) continue;
        if (!out.empty()) out += L",";
        out += TriggerToString(TriggerBitToType(i));
    }
    return out;
}

int FirstTriggerFromMask(int mask) {
    for (int i = 0; i < 6; ++i) {
        if ((mask & (1 << i)) != 0) return i;
    }
    return 0;
}

tcyc::TriggerType TriggerIndexToType(int idx) {
    switch (idx) {
    case 0: return tcyc::TriggerType::Interval;
    case 1: return tcyc::TriggerType::DateTimeIntervalLimited;
    case 2: return tcyc::TriggerType::WeeklyTime;
    case 3: return tcyc::TriggerType::Startup;
    case 4: return tcyc::TriggerType::HotkeyOnly;
    case 5: return tcyc::TriggerType::NonRunning;
    default: return tcyc::TriggerType::Unknown;
    }
}

int ActionModeToIndex(const std::wstring& modeRaw) {
    std::wstring mode = modeRaw;
    for (auto& ch : mode) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    if (mode == L"command") return 1;
    if (mode == L"shell") return 2;
    return 0;
}

std::wstring ActionModeFromIndex(int idx) {
    if (idx == 1) return L"command";
    if (idx == 2) return L"shell";
    return L"program";
}

void ApplyActionModeUi(WindowState* st) {
    if (!st) return;
    const int idx = static_cast<int>(SendMessageW(st->actionMode, CB_GETCURSEL, 0, 0));
    const bool isProgram = (idx == 0);
    if (st->actionPrimaryLabel) {
        const std::wstring label = isProgram
            ? Tr(st, L"label_action_path_only", L"Path")
            : Tr(st, L"label_action_args", L"Params");
        SetWindowTextW(st->actionPrimaryLabel, label.c_str());
    }
}

std::wstring WeekdayToString(int d) {
    static const wchar_t* kNames[] = {L"sun", L"mon", L"tue", L"wed", L"thu", L"fri", L"sat"};
    if (d < 0 || d > 6) return L"";
    return kNames[d];
}

std::wstring WeekdayMaskToString(int mask) {
    std::wstring out;
    for (int i = 0; i < 7; ++i) {
        if ((mask & (1 << i)) == 0) continue;
        if (!out.empty()) out += L",";
        out += WeekdayToString(i);
    }
    return out;
}

int FirstWeekdayFromMask(int mask) {
    for (int i = 0; i < 7; ++i) {
        if ((mask & (1 << i)) != 0) return i;
    }
    return -1;
}

std::wstring TimeOfDayToString(int sec) {
    if (sec < 0) sec = 0;
    int hh = sec / 3600;
    int mm = (sec % 3600) / 60;
    wchar_t buf[16] = {0};
    swprintf_s(buf, L"%02d:%02d", hh, mm);
    return std::wstring(buf);
}

std::wstring CurrentDateYmd() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buf[16] = {0};
    swprintf_s(buf, L"%04d-%02d-%02d", static_cast<int>(now.wYear), static_cast<int>(now.wMonth), static_cast<int>(now.wDay));
    return buf;
}

bool ParseTimeOfDay(const std::wstring& s, int& outSec) {
    int hh = 0, mm = 0, ss = 0;
    int n = swscanf_s(s.c_str(), L"%d:%d:%d", &hh, &mm, &ss);
    if (n < 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
    outSec = hh * 3600 + mm * 60 + ss;
    return true;
}

int ParseIntOrDefault(const std::wstring& s, int def, int lo, int hi) {
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 10);
    if (!end || *end != L'\0') return def;
    if (v < lo || v > hi) return def;
    return static_cast<int>(v);
}

std::wstring GetEditText(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return L"";
    std::wstring out;
    out.resize(static_cast<size_t>(len + 1), L'\0');
    int copied = GetWindowTextW(h, out.data(), len + 1);
    if (copied <= 0) return L"";
    out.resize(static_cast<size_t>(copied));
    return out;
}

void SetEditText(HWND h, const std::wstring& s) {
    SetWindowTextW(h, s.c_str());
}

void SetEditInt(HWND h, int v) {
    wchar_t buf[64] = {0};
    swprintf_s(buf, L"%d", v);
    SetWindowTextW(h, buf);
}

int ReadWeekdayMaskFromChecks(const WindowState* st) {
    if (!st) return 0;
    int mask = 0;
    for (int i = 0; i < 7; ++i) {
        if (st->weekdayChecks[i] && SendMessageW(st->weekdayChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
            mask |= (1 << i);
        }
    }
    return mask;
}

bool IsEverydayChecked(const WindowState* st) {
    return st && st->weekdayEveryday && (SendMessageW(st->weekdayEveryday, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

bool IsChecked(HWND h) {
    return h && (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
}

void SetChecked(HWND h, bool on) {
    if (!h) return;
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SetWeekdayChecksFromMask(WindowState* st, int mask) {
    if (!st) return;
    for (int i = 0; i < 7; ++i) {
        if (!st->weekdayChecks[i]) continue;
        SendMessageW(st->weekdayChecks[i], BM_SETCHECK, (mask & (1 << i)) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SetWeekdayEveryday(WindowState* st, bool on) {
    if (!st || !st->weekdayEveryday) return;
    SendMessageW(st->weekdayEveryday, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ApplyWeekdayUiState(WindowState* st, bool isWeeklyEnabled, bool weekdayConditionEnabled) {
    if (!st) return;
    const bool enableWeekdays = isWeeklyEnabled && weekdayConditionEnabled && !IsEverydayChecked(st);
    for (int i = 0; i < 7; ++i) {
        if (st->weekdayChecks[i]) EnableWindow(st->weekdayChecks[i], enableWeekdays ? TRUE : FALSE);
    }
}

int ReadTriggerMaskFromChecks(const WindowState* st) {
    if (!st) return 0;
    int mask = 0;
    for (int i = 0; i < 6; ++i) {
        if (st->triggerChecks[i] && SendMessageW(st->triggerChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
            mask |= (1 << i);
        }
    }
    return mask;
}

void SetTriggerChecksFromMask(WindowState* st, int mask) {
    if (!st) return;
    mask &= ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5));
    if (mask == 0) mask = (1 << 3);
    for (int i = 0; i < 6; ++i) {
        if (!st->triggerChecks[i]) continue;
        SendMessageW(st->triggerChecks[i], BM_SETCHECK, ((mask & (1 << i)) != 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void EnsureAtLeastOneTriggerChecked(WindowState* st, int fallbackIndex) {
    if (!st) return;
    if (ReadTriggerMaskFromChecks(st) != 0) return;
    if (fallbackIndex < 0 || fallbackIndex > 5) fallbackIndex = 0;
    if (!st->triggerChecks[fallbackIndex]) fallbackIndex = 0;
    SendMessageW(st->triggerChecks[fallbackIndex], BM_SETCHECK, BST_CHECKED, 0);
}

void EnsureAtLeastOneDateConditionChecked(WindowState* st, int fallbackCtrlId) {
    if (!st) return;
    const bool any = IsChecked(st->dateEnabled) || IsChecked(st->weekdayEnabled) || IsChecked(st->timeEnabled);
    if (any) return;
    HWND fallback = st->timeEnabled;
    if (fallbackCtrlId == kCtrlDateEnabled) fallback = st->dateEnabled;
    else if (fallbackCtrlId == kCtrlWeekdayEnabled) fallback = st->weekdayEnabled;
    if (!fallback) fallback = st->timeEnabled;
    SetChecked(fallback, true);
}

bool HasTriggerEnabled(const WindowState* st, tcyc::TriggerType trigger) {
    const int mask = ReadTriggerMaskFromChecks(st);
    return (mask & TriggerTypeToBit(trigger)) != 0;
}

void SetEnabled(HWND h, bool enabled) {
    if (h) EnableWindow(h, enabled ? TRUE : FALSE);
}

void ApplyTriggerUiState(WindowState* st) {
    if (!st) return;
    const bool isInterval = HasTriggerEnabled(st, tcyc::TriggerType::Interval);
    const bool isWeekly = HasTriggerEnabled(st, tcyc::TriggerType::WeeklyTime);
    const bool isHotkeyOnly = HasTriggerEnabled(st, tcyc::TriggerType::HotkeyOnly);
    const bool isNonRunning = HasTriggerEnabled(st, tcyc::TriggerType::NonRunning);
    if (isWeekly) {
        if (IsChecked(st->dateEnabled) && IsChecked(st->weekdayEnabled)) {
            SetChecked(st->weekdayEnabled, false);
        }
        if (!IsChecked(st->timeEnabled)) {
            SetChecked(st->timeEnabled, true);
        }
    }
    const bool isDateCondition = IsChecked(st->dateEnabled);
    const bool isWeekdayCondition = IsChecked(st->weekdayEnabled);
    const bool isTimeCondition = IsChecked(st->timeEnabled);

    SetEnabled(st->grpInterval, true);
    SetEnabled(st->intervalSec, isInterval);

    SetEnabled(st->grpWeekly, true);
    SetEnabled(st->dateEnabled, isWeekly);
    SetEnabled(st->dateValue, isWeekly && isDateCondition);
    SetEnabled(st->weekdayEnabled, isWeekly);
    SetEnabled(st->weekdayEveryday, isWeekly && isWeekdayCondition);
    for (int i = 0; i < 7; ++i) SetEnabled(st->weekdayChecks[i], isWeekly && isWeekdayCondition && !IsEverydayChecked(st));
    SetEnabled(st->timeEnabled, isWeekly);
    SetEnabled(st->timeOfDay, isWeekly && isTimeCondition);

    SetEnabled(st->grpHotkey, true);
    SetEnabled(st->hotkeyMod, isHotkeyOnly);
    SetEnabled(st->hotkeyKey, isHotkeyOnly);

    SetEnabled(st->grpNonRunning, true);
    SetEnabled(st->watchdogRetrySec, isNonRunning);
    SetEnabled(st->repeatCount, isNonRunning);

    SetEnabled(st->grpStartup, true);
    ApplyWeekdayUiState(st, isWeekly, isWeekdayCondition);
}

void SetStatus(WindowState* st, const std::wstring& msg) {
    if (st && st->status) SetWindowTextW(st->status, msg.c_str());
}

const std::vector<HotkeyModOption>& HotkeyModOptions() {
    static const std::vector<HotkeyModOption> k = {
        {L"(none)", false, false, false, false},
        {L"Ctrl", true, false, false, false},
        {L"Shift", false, true, false, false},
        {L"Alt", false, false, true, false},
        {L"Win", false, false, false, true},
        {L"Ctrl+Shift", true, true, false, false},
        {L"Ctrl+Alt", true, false, true, false},
        {L"Ctrl+Win", true, false, false, true},
        {L"Shift+Alt", false, true, true, false},
        {L"Shift+Win", false, true, false, true},
        {L"Alt+Win", false, false, true, true},
        {L"Ctrl+Shift+Alt", true, true, true, false},
        {L"Ctrl+Shift+Win", true, true, false, true},
        {L"Ctrl+Alt+Win", true, false, true, true},
        {L"Shift+Alt+Win", false, true, true, true},
        {L"Ctrl+Shift+Alt+Win", true, true, true, true},
    };
    return k;
}

const std::vector<HotkeyKeyOption>& HotkeyKeyOptions() {
    static std::vector<HotkeyKeyOption> k;
    if (!k.empty()) return k;
    k.push_back({L"(none)", 0});
    for (wchar_t c = L'A'; c <= L'Z'; ++c) {
        wchar_t label[2] = {c, 0};
        k.push_back({label, c});
    }
    for (wchar_t c = L'0'; c <= L'9'; ++c) {
        wchar_t label[2] = {c, 0};
        k.push_back({label, c});
    }
    for (int i = 1; i <= 24; ++i) {
        wchar_t buf[8] = {0};
        swprintf_s(buf, L"F%d", i);
        k.push_back({buf, VK_F1 + (i - 1)});
    }
    k.push_back({L"PrintScreen", VK_SNAPSHOT});
    k.push_back({L"Insert", VK_INSERT});
    k.push_back({L"Delete", VK_DELETE});
    k.push_back({L"Home", VK_HOME});
    k.push_back({L"End", VK_END});
    k.push_back({L"PgUp", VK_PRIOR});
    k.push_back({L"PgDn", VK_NEXT});
    k.push_back({L"Space", VK_SPACE});
    k.push_back({L"Enter", VK_RETURN});
    return k;
}

bool ParseHotkeyString(const std::wstring& hotkey, bool& ctrl, bool& shift, bool& alt, bool& win, int& keyVk) {
    ctrl = false;
    shift = false;
    alt = false;
    win = false;
    keyVk = 0;
    if (hotkey.empty()) return true;

    std::wstring token;
    auto flush = [&](const std::wstring& t) {
        std::wstring x = TrimWide(t);
        if (x.empty()) return;
        std::wstring lower = x;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (lower == L"ctrl" || lower == L"control") ctrl = true;
        else if (lower == L"shift") shift = true;
        else if (lower == L"alt") alt = true;
        else if (lower == L"win" || lower == L"windows") win = true;
        else {
            const auto& keys = HotkeyKeyOptions();
            for (const auto& k : keys) {
                std::wstring l = k.label;
                std::transform(l.begin(), l.end(), l.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                if (l == lower) {
                    keyVk = k.vk;
                    return;
                }
            }
            if (lower.size() == 1) {
                wchar_t c = static_cast<wchar_t>(towupper(lower[0]));
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) keyVk = c;
            }
        }
    };

    for (wchar_t c : hotkey) {
        if (c == L'+') {
            flush(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    flush(token);
    return true;
}

std::wstring ComposeHotkeyString(WindowState* st) {
    int modSel = static_cast<int>(SendMessageW(st->hotkeyMod, CB_GETCURSEL, 0, 0));
    int keySel = static_cast<int>(SendMessageW(st->hotkeyKey, CB_GETCURSEL, 0, 0));
    if (modSel < 0 || modSel >= static_cast<int>(HotkeyModOptions().size())) modSel = 0;
    if (keySel < 0 || keySel >= static_cast<int>(HotkeyKeyOptions().size())) keySel = 0;

    const auto& m = HotkeyModOptions()[static_cast<size_t>(modSel)];
    const auto& k = HotkeyKeyOptions()[static_cast<size_t>(keySel)];
    if (k.vk == 0) return L"";

    std::wstring out;
    if (m.ctrl) out += L"Ctrl+";
    if (m.shift) out += L"Shift+";
    if (m.alt) out += L"Alt+";
    if (m.win) out += L"Win+";
    out += k.label;
    return out;
}

void SetHotkeyCombosFromString(WindowState* st, const std::wstring& hotkey) {
    bool ctrl = false, shift = false, alt = false, win = false;
    int keyVk = 0;
    ParseHotkeyString(hotkey, ctrl, shift, alt, win, keyVk);

    int modIndex = 0;
    for (int i = 0; i < static_cast<int>(HotkeyModOptions().size()); ++i) {
        const auto& m = HotkeyModOptions()[static_cast<size_t>(i)];
        if (m.ctrl == ctrl && m.shift == shift && m.alt == alt && m.win == win) {
            modIndex = i;
            break;
        }
    }
    int keyIndex = 0;
    for (int i = 0; i < static_cast<int>(HotkeyKeyOptions().size()); ++i) {
        if (HotkeyKeyOptions()[static_cast<size_t>(i)].vk == keyVk) {
            keyIndex = i;
            break;
        }
    }
    SendMessageW(st->hotkeyMod, CB_SETCURSEL, modIndex, 0);
    SendMessageW(st->hotkeyKey, CB_SETCURSEL, keyIndex, 0);
}

bool WriteIniInt(const std::wstring& iniPath, const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t buf[64] = {0};
    swprintf_s(buf, L"%d", v);
    return !!WritePrivateProfileStringW(sec, key, buf, iniPath.c_str());
}

void PopulateTaskList(WindowState* st) {
    SendMessageW(st->taskList, LB_RESETCONTENT, 0, 0);
    for (const auto& t : st->config.tasks) {
        wchar_t line[256] = {0};
        swprintf_s(line, L"Task.%d [%s] %s", t.id, t.enabled ? L"on" : L"off", t.name.c_str());
        SendMessageW(st->taskList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (st->config.tasks.empty()) {
        st->selectedTask = -1;
        return;
    }
    if (st->selectedTask < 0 || st->selectedTask >= static_cast<int>(st->config.tasks.size())) st->selectedTask = 0;
    SendMessageW(st->taskList, LB_SETCURSEL, st->selectedTask, 0);
}

void LoadGlobalControls(WindowState* st) {
    SetEditInt(st->pollSec, st->config.pollSec);
    SetEditInt(st->graceSec, st->config.graceSec);
}

void LoadTaskControls(WindowState* st, int idx) {
    st->selectedTask = idx;
    if (idx < 0 || idx >= static_cast<int>(st->config.tasks.size())) {
        SendMessageW(st->taskEnabled, BM_SETCHECK, BST_UNCHECKED, 0);
        SetTriggerChecksFromMask(st, TriggerTypeToBit(tcyc::TriggerType::Startup));
        SetEditText(st->intervalSec, L"600");
        SendMessageW(st->actionMode, CB_SETCURSEL, 0, 0);
        SetEditText(st->actionPath, L"");
        SetEditText(st->actionCwd, L"");
        SetEditText(st->watchdogRetrySec, L"10");
        SetEditText(st->repeatCount, L"5");
        SetChecked(st->dateEnabled, false);
        SetEditText(st->dateValue, CurrentDateYmd());
        SetChecked(st->weekdayEnabled, false);
        SetWeekdayEveryday(st, false);
        SetWeekdayChecksFromMask(st, 0);
        SetChecked(st->timeEnabled, true);
        SetEditText(st->timeOfDay, L"00:00");
        SetHotkeyCombosFromString(st, L"");
        ApplyTriggerUiState(st);
        st->actionPathDirty = false;
        st->actionCwdDirty = false;
        return;
    }
    const auto& t = st->config.tasks[static_cast<size_t>(idx)];
    SendMessageW(st->taskEnabled, BM_SETCHECK, t.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    int triggerMask = t.triggerMask;
    if (triggerMask == 0) triggerMask = TriggerTypeToBit(t.trigger);
    if (t.watchdogEnabled) triggerMask |= TriggerTypeToBit(tcyc::TriggerType::NonRunning);
    SetTriggerChecksFromMask(st, triggerMask);
    SetEditInt(st->intervalSec, (t.intervalSec > 0) ? t.intervalSec : 600);
    SendMessageW(st->actionMode, CB_SETCURSEL, ActionModeToIndex(t.actionMode), 0);
    SetEditText(st->actionPath, t.actionPath);
    SetEditText(st->actionCwd, t.actionCwd);
    SetEditInt(st->watchdogRetrySec, t.watchdogRetrySec);
    SetEditInt(st->repeatCount, (t.watchdogMaxRetry >= 0) ? t.watchdogMaxRetry : 5);
    SetChecked(st->dateEnabled, t.dateEnabled);
    SetEditText(st->dateValue, t.dateYmd.empty() ? CurrentDateYmd() : t.dateYmd);
    SetChecked(st->weekdayEnabled, t.weekdayEnabled && !t.dateEnabled);
    int weekdayMask = t.weekdayMask;
    if (weekdayMask == 0 && t.weekday >= 0 && t.weekday <= 6) {
        weekdayMask = (1 << t.weekday);
    }
    SetWeekdayEveryday(st, t.weeklyEveryday);
    SetWeekdayChecksFromMask(st, weekdayMask);
    SetChecked(st->timeEnabled, t.timeEnabled);
    SetEditText(st->timeOfDay, TimeOfDayToString((t.timeOfDaySec >= 0) ? t.timeOfDaySec : 0));
    SetHotkeyCombosFromString(st, t.hotkey);
    ApplyActionModeUi(st);
    ApplyTriggerUiState(st);
    st->actionPathDirty = false;
    st->actionCwdDirty = false;
}

bool SignalReloadEvent() {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, kReloadEventName);
    if (!h) return false;
    BOOL ok = SetEvent(h);
    CloseHandle(h);
    return ok == TRUE;
}

bool SaveAllToIni(WindowState* st, std::wstring& err) {
    if (!st) return false;
    const std::wstring iniPath = st->iniPath;

    st->config.pollSec = ParseIntOrDefault(GetEditText(st->pollSec), st->config.pollSec, 1, 60);
    st->config.graceSec = ParseIntOrDefault(GetEditText(st->graceSec), st->config.graceSec, 0, 300);

    if (st->selectedTask >= 0 && st->selectedTask < static_cast<int>(st->config.tasks.size())) {
        auto& t = st->config.tasks[static_cast<size_t>(st->selectedTask)];
        t.enabled = (SendMessageW(st->taskEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
        if (t.name.empty()) {
            wchar_t buf[64] = {0};
            swprintf_s(buf, L"Task.%d", t.id);
            t.name = buf;
        }
        t.triggerMask = ReadTriggerMaskFromChecks(st);
        if (t.triggerMask == 0) {
            t.triggerMask = TriggerTypeToBit(tcyc::TriggerType::Startup);
            SetTriggerChecksFromMask(st, t.triggerMask);
        }
        t.trigger = TriggerIndexToType(FirstTriggerFromMask(t.triggerMask));
        t.intervalSec = ParseIntOrDefault(GetEditText(st->intervalSec), (t.intervalSec > 0 ? t.intervalSec : 600), 0, 86400);
        t.actionMode = ActionModeFromIndex(static_cast<int>(SendMessageW(st->actionMode, CB_GETCURSEL, 0, 0)));
        if (st->actionPathDirty) {
            t.actionPath = TrimWide(GetEditText(st->actionPath));
        }
        t.actionArgs = L"";
        if (st->actionCwdDirty) {
            t.actionCwd = TrimWide(GetEditText(st->actionCwd));
        }
        t.watchdogEnabled = ((t.triggerMask & TriggerTypeToBit(tcyc::TriggerType::NonRunning)) != 0);
        t.watchdogRetrySec = ParseIntOrDefault(GetEditText(st->watchdogRetrySec), t.watchdogRetrySec, 10, 3600);
        t.watchdogMaxRetry = ParseIntOrDefault(GetEditText(st->repeatCount), (t.watchdogMaxRetry >= 0 ? t.watchdogMaxRetry : 5), 1, 1000000);
        t.dateEnabled = IsChecked(st->dateEnabled);
        t.dateYmd = TrimWide(GetEditText(st->dateValue));
        if (t.dateEnabled && t.dateYmd.empty()) t.dateYmd = CurrentDateYmd();
        t.weekdayEnabled = IsChecked(st->weekdayEnabled);
        t.weeklyEveryday = t.weekdayEnabled && IsEverydayChecked(st);
        if (!t.weekdayEnabled) {
            t.weekday = -1;
            t.weekdayMask = 0;
        } else if (t.weeklyEveryday) {
            t.weekday = -1;
            t.weekdayMask = 0;
        } else {
            t.weekdayMask = ReadWeekdayMaskFromChecks(st);
            t.weekday = FirstWeekdayFromMask(t.weekdayMask);
        }

        t.timeEnabled = IsChecked(st->timeEnabled);
        if (!t.timeEnabled) {
            t.timeOfDaySec = 0;
        } else {
            const std::wstring tod = TrimWide(GetEditText(st->timeOfDay));
            if (tod.empty()) {
                t.timeOfDaySec = 0;
            } else {
                int sec = -1;
                if (ParseTimeOfDay(tod, sec)) t.timeOfDaySec = sec;
                else t.timeOfDaySec = 0;
            }
        }
        t.hotkey = ComposeHotkeyString(st);
    }

    if (!WriteIniInt(iniPath, L"TCycle", L"PollSec", st->config.pollSec) ||
        !WriteIniInt(iniPath, L"TCycle", L"GraceSec", st->config.graceSec) ||
        !WritePrivateProfileStringW(L"TCycle", L"Language", Utf8ToWide(st->languageCode).c_str(), iniPath.c_str())) {
        err = Tr(st, L"err_save_global", L"Failed to save global settings to ini.");
        return false;
    }

    for (const auto& t : st->config.tasks) {
        wchar_t sec[32] = {0};
        swprintf_s(sec, L"Task.%d", t.id);
        if (!WriteIniInt(iniPath, sec, L"Enabled", t.enabled ? 1 : 0) ||
            !WritePrivateProfileStringW(sec, L"Name", t.name.c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"TriggerTypes", TriggerMaskToString(t.triggerMask).c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"TriggerType", TriggerToString(t.trigger).c_str(), iniPath.c_str()) ||
            !WriteIniInt(iniPath, sec, L"IntervalSec", t.intervalSec) ||
            !WritePrivateProfileStringW(sec, L"ActionMode", t.actionMode.c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"ActionPath", t.actionPath.c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"ActionArgs", t.actionArgs.c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"ActionCwd", t.actionCwd.c_str(), iniPath.c_str()) ||
            !WriteIniInt(iniPath, sec, L"WatchdogEnabled", t.watchdogEnabled ? 1 : 0) ||
            !WriteIniInt(iniPath, sec, L"WatchdogRetrySec", t.watchdogRetrySec) ||
            !WriteIniInt(iniPath, sec, L"WatchdogMaxRetry", t.watchdogMaxRetry) ||
            !WritePrivateProfileStringW(sec, L"StartDateTime", t.startDateTime.c_str(), iniPath.c_str()) ||
            !WriteIniInt(iniPath, sec, L"RepeatEverySec", t.repeatEverySec) ||
            !WriteIniInt(iniPath, sec, L"DateEnabled", t.dateEnabled ? 1 : 0) ||
            !WritePrivateProfileStringW(sec, L"Date", t.dateYmd.c_str(), iniPath.c_str()) ||
            !WriteIniInt(iniPath, sec, L"WeekdayEnabled", t.weekdayEnabled ? 1 : 0) ||
            !WriteIniInt(iniPath, sec, L"EveryDay", t.weeklyEveryday ? 1 : 0) ||
            !WritePrivateProfileStringW(sec, L"Weekdays", (t.weeklyEveryday ? L"everyday" : WeekdayMaskToString(t.weekdayMask)).c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"Weekday", WeekdayToString(t.weekday).c_str(), iniPath.c_str()) ||
            !WriteIniInt(iniPath, sec, L"TimeEnabled", t.timeEnabled ? 1 : 0) ||
            !WritePrivateProfileStringW(sec, L"TimeOfDay", TimeOfDayToString(t.timeOfDaySec).c_str(), iniPath.c_str()) ||
            !WritePrivateProfileStringW(sec, L"Hotkey", t.hotkey.c_str(), iniPath.c_str())) {
            err = Tr(st, L"err_save_task", L"Failed to save task settings to ini.");
            return false;
        }
    }
    if (!WritePrivateProfileStringW(nullptr, nullptr, nullptr, iniPath.c_str())) {
        err = Tr(st, L"err_save_task", L"Failed to save task settings to ini.");
        return false;
    }
    st->actionPathDirty = false;
    st->actionCwdDirty = false;
    return true;
}

void PersistRealtime(WindowState* st, bool showErrorDialog) {
    if (!st || st->suppressEvents) return;
    std::wstring err;
    if (!SaveAllToIni(st, err)) {
        SetStatus(st, err);
        if (showErrorDialog) MessageBoxW(nullptr, err.c_str(), Tr(st, L"app_name", L"TCycle").c_str(), MB_ICONERROR);
        return;
    }
    (void)SignalReloadEvent();
    SetStatus(st, Tr(st, L"status_realtime_saved", L"Realtime saved.").c_str());

    int prevSel = st->selectedTask;
    st->suppressEvents = true;
    PopulateTaskList(st);
    if (prevSel >= 0 && prevSel < static_cast<int>(st->config.tasks.size())) {
        LoadTaskControls(st, prevSel);
    }
    st->suppressEvents = false;
}

LRESULT CALLBACK RenameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<RenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* init = reinterpret_cast<RenameDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(init));
        return TRUE;
    }
    case WM_CREATE: {
        if (!st) return -1;
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        st->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->initialText.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 12, 12, 320, 24, hwnd, nullptr, nullptr, nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            172, 46, 76, 26, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
            256, 46, 76, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(st->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(st->edit, EM_SETSEL, 0, -1);
        SetFocus(st->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            st->resultText = TrimWide(GetEditText(st->edit));
            st->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptRename(HWND owner, const std::wstring& title, const std::wstring& current, std::wstring& outName) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RenameWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kRenameClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    RenameDialogState state{};
    state.title = title;
    state.initialText = current;

    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kRenameClassName, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 350, 115,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!wnd) return false;

    EnableWindow(owner, FALSE);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);

    MSG msg{};
    while (IsWindow(wnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (!state.accepted || state.resultText.empty()) return false;
    outName = state.resultText;
    return true;
}

void AddTask(WindowState* st) {
    int maxId = 0;
    for (const auto& t : st->config.tasks) maxId = (t.id > maxId) ? t.id : maxId;
    tcyc::TaskConfig t{};
    t.id = maxId + 1;
    wchar_t nameBuf[64] = {0};
    swprintf_s(nameBuf, L"Task.%d", t.id);
    t.name = nameBuf;
    t.enabled = true;
    t.trigger = tcyc::TriggerType::Startup;
    t.triggerMask = TriggerTypeToBit(tcyc::TriggerType::Startup);
    t.intervalSec = 600;
    t.actionMode = L"program";
    t.watchdogEnabled = false;
    t.watchdogRetrySec = 10;
    t.watchdogMaxRetry = 5;
    t.startDateTime = L"";
    t.repeatEverySec = 600;
    t.repeatCount = 5;
    t.dateEnabled = false;
    t.dateYmd = L"";
    t.weekdayEnabled = true;
    t.weeklyEveryday = false;
    t.weekday = 6;
    t.weekdayMask = (1 << 6);
    t.timeEnabled = true;
    t.timeOfDaySec = 0;
    t.hotkey = L"";
    st->config.tasks.push_back(t);
    st->selectedTask = static_cast<int>(st->config.tasks.size()) - 1;
}

void DeleteSelectedTask(WindowState* st) {
    if (st->selectedTask < 0 || st->selectedTask >= static_cast<int>(st->config.tasks.size())) return;
    if (st->config.tasks.size() <= 1) return;
    st->config.tasks.erase(st->config.tasks.begin() + st->selectedTask);
    if (st->selectedTask >= static_cast<int>(st->config.tasks.size())) st->selectedTask = static_cast<int>(st->config.tasks.size()) - 1;
}

void InitializeCombos(WindowState* st) {
    SendMessageW(st->actionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(st, L"action_mode_program", L"Program").c_str()));
    SendMessageW(st->actionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(st, L"action_mode_command", L"Command").c_str()));
    SendMessageW(st->actionMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(st, L"action_mode_shell", L"Shell").c_str()));

    for (const auto& m : HotkeyModOptions()) {
        SendMessageW(st->hotkeyMod, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m.label));
    }
    for (const auto& k : HotkeyKeyOptions()) {
        SendMessageW(st->hotkeyKey, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(k.label));
    }
}

void RefreshAll(WindowState* st) {
    st->suppressEvents = true;
    LoadGlobalControls(st);
    PopulateTaskList(st);
    LoadTaskControls(st, st->selectedTask);
    st->suppressEvents = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* createState = reinterpret_cast<WindowState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createState));
        return TRUE;
    }
    case WM_CREATE: {
        if (!st) return -1;
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto createStatic = [&](const wchar_t* text, int x, int y, int w, int h) -> HWND {
            HWND hWnd = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, x, y, w, h, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return hWnd;
        };
        auto createEdit = [&](int id, int x, int y, int w, int h, DWORD style) -> HWND {
            HWND hWnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return hWnd;
        };
        auto createNumericEdit = [&](int id, int x, int y, int w, int h, int maxChars) -> HWND {
            HWND hWnd = createEdit(id, x, y, w, h, ES_RIGHT | ES_AUTOHSCROLL | ES_NUMBER);
            SendMessageW(hWnd, EM_LIMITTEXT, static_cast<WPARAM>(maxChars), 0);
            return hWnd;
        };
        auto createBtn = [&](int id, const wchar_t* text, int x, int y, int w, int h, DWORD style) -> HWND {
            HWND hWnd = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return hWnd;
        };
        auto createSpin = [&](int id, int x, int y, int w, int h, int minV, int maxV) -> HWND {
            HWND hWnd = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_AUTOBUDDY | UDS_ARROWKEYS,
                x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(hWnd, UDM_SETRANGE32, static_cast<WPARAM>(minV), static_cast<LPARAM>(maxV));
            return hWnd;
        };

        const int m = 10;
        const int globalX = m, globalY = m, globalW = 700, globalH = 56;
        const int listX = m, listY = globalY + globalH + 8, listW = 176, listH = 500;
        const int detailX = listX + listW + 8, detailY = listY, detailW = 516, detailH = 500;
        const int colLabelX = detailX + 18;
        const int colInputX = detailX + 90;
        const int editH = 22;

        createBtn(0, Tr(st, L"group_global", L"Global").c_str(), globalX, globalY, globalW, globalH, BS_GROUPBOX);
        createStatic(Tr(st, L"label_pollsec", L"PollSec").c_str(), globalX + 12, globalY + 24, 70, 20);
        st->pollSec = createNumericEdit(kCtrlPollSec, globalX + 84, globalY + 20, 50, editH, 2);
        createSpin(kCtrlSpinPollSec, globalX + 124, globalY + 20, 10, editH, 1, 60);
        createStatic(Tr(st, L"label_gracesec", L"GraceSec").c_str(), globalX + 152, globalY + 24, 174, 20);
        st->graceSec = createNumericEdit(kCtrlGraceSec, globalX + 330, globalY + 20, 50, editH, 3);
        createSpin(kCtrlSpinGraceSec, globalX + 370, globalY + 20, 10, editH, 0, 300);

        createBtn(0, Tr(st, L"group_tasks", L"Tasks").c_str(), listX, listY, listW, listH, BS_GROUPBOX);
        st->taskList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            listX + 10, listY + 22, listW - 20, listH - 64, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlTaskList)), nullptr, nullptr);
        SendMessageW(st->taskList, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        st->taskAdd = createBtn(kCtrlTaskAdd, L"+", listX + 10, listY + listH - 34, 32, 24, BS_PUSHBUTTON);
        st->taskDelete = createBtn(kCtrlTaskDelete, L"Del", listX + 46, listY + listH - 34, 40, 24, BS_PUSHBUTTON);
        st->taskRename = createBtn(kCtrlTaskRename, Tr(st, L"button_rename", L"Rename").c_str(), listX + 90, listY + listH - 34, 76, 24, BS_PUSHBUTTON);

        createBtn(0, Tr(st, L"group_schedule", L"Schedule").c_str(), detailX, detailY, detailW, detailH, BS_GROUPBOX);
        st->taskEnabled = createBtn(kCtrlTaskEnabled, Tr(st, L"label_task_enabled", L"Task Enabled").c_str(), detailX + 12, detailY + 22, 118, 22, BS_AUTOCHECKBOX);
        createStatic(Tr(st, L"label_trigger", L"Trigger").c_str(), colLabelX, detailY + 52, 64, 20);
        st->triggerChecks[3] = createBtn(kCtrlTriggerStartup, Tr(st, L"trigger_startup", L"startup").c_str(), colInputX, detailY + 48, 94, 22, BS_AUTOCHECKBOX);
        st->triggerChecks[0] = createBtn(kCtrlTriggerInterval, Tr(st, L"trigger_interval", L"interval").c_str(), colInputX + 100, detailY + 48, 78, 22, BS_AUTOCHECKBOX);
        st->triggerChecks[1] = nullptr;
        st->triggerChecks[2] = createBtn(kCtrlTriggerWeeklyTime, Tr(st, L"trigger_weekly_time", L"weekly").c_str(), colInputX + 184, detailY + 48, 78, 22, BS_AUTOCHECKBOX);
        st->triggerChecks[4] = createBtn(kCtrlTriggerHotkeyOnly, Tr(st, L"trigger_hotkey_only", L"hotkey").c_str(), colInputX + 268, detailY + 48, 70, 22, BS_AUTOCHECKBOX);
        st->triggerChecks[5] = createBtn(kCtrlTriggerNonRunning, Tr(st, L"trigger_non_running", L"non_running").c_str(), colInputX + 344, detailY + 48, 70, 22, BS_AUTOCHECKBOX);

        createBtn(0, Tr(st, L"group_execution", L"Execution").c_str(), detailX + 8, detailY + 78, detailW - 16, 78, BS_GROUPBOX);
        st->actionMode = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            colLabelX, detailY + 98, 90, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlActionMode)), nullptr, nullptr);
        SendMessageW(st->actionMode, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        st->actionPrimaryLabel = createStatic(Tr(st, L"label_action_args", L"Params").c_str(), detailX + 114, detailY + 102, 58, 20);
        st->actionPath = createEdit(kCtrlActionPath, detailX + 174, detailY + 98, 324, editH, ES_AUTOHSCROLL);
        createStatic(Tr(st, L"label_action_cwd", L"Cwd").c_str(), colLabelX, detailY + 130, 64, 20);
        st->actionCwd = createEdit(kCtrlActionCwd, colInputX, detailY + 126, 408, editH, ES_AUTOHSCROLL);
        st->actionArgs = nullptr;

        st->grpInterval = createBtn(0, Tr(st, L"trigger_interval", L"interval").c_str(), detailX + 8, detailY + 162, 150, 50, BS_GROUPBOX);
        createStatic(Tr(st, L"label_intervalsec", L"IntervalSec").c_str(), colLabelX, detailY + 182, 72, 20);
        st->intervalSec = createNumericEdit(kCtrlIntervalSec, colInputX, detailY + 178, 52, editH, 5);
        createSpin(kCtrlSpinIntervalSec, colInputX + 42, detailY + 178, 10, editH, 0, 86400);

        st->grpDateTime = nullptr;

        st->grpWeekly = createBtn(0, Tr(st, L"trigger_weekly_time", L"weekly_time").c_str(), detailX + 8, detailY + 220, detailW - 16, 90, BS_GROUPBOX);
        st->dateEnabled = createBtn(kCtrlDateEnabled, Tr(st, L"label_date", L"Date").c_str(), colLabelX, detailY + 238, 64, 22, BS_AUTOCHECKBOX);
        st->dateValue = createEdit(kCtrlDateValue, colInputX, detailY + 238, 98, editH, ES_AUTOHSCROLL);
        SendMessageW(st->dateValue, EM_LIMITTEXT, 10, 0);

        st->timeEnabled = createBtn(kCtrlTimeEnabled, Tr(st, L"label_timeofday", L"Time").c_str(), detailX + 206, detailY + 238, 64, 22, BS_AUTOCHECKBOX);
        st->timeOfDay = createEdit(kCtrlTimeOfDay, detailX + 278, detailY + 238, 72, editH, ES_AUTOHSCROLL);
        SendMessageW(st->timeOfDay, EM_LIMITTEXT, 5, 0);

        st->weekdayEnabled = createBtn(kCtrlWeekdayEnabled, Tr(st, L"label_weekday", L"Weekday").c_str(), colLabelX, detailY + 266, 64, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[0] = createBtn(kCtrlWeekdaySun, Tr(st, L"weekday_sun", L"Sun").c_str(), colInputX, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[1] = createBtn(kCtrlWeekdayMon, Tr(st, L"weekday_mon", L"Mon").c_str(), colInputX + 38, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[2] = createBtn(kCtrlWeekdayTue, Tr(st, L"weekday_tue", L"Tue").c_str(), colInputX + 76, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[3] = createBtn(kCtrlWeekdayWed, Tr(st, L"weekday_wed", L"Wed").c_str(), colInputX + 114, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[4] = createBtn(kCtrlWeekdayThu, Tr(st, L"weekday_thu", L"Thu").c_str(), colInputX + 152, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[5] = createBtn(kCtrlWeekdayFri, Tr(st, L"weekday_fri", L"Fri").c_str(), colInputX + 190, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayChecks[6] = createBtn(kCtrlWeekdaySat, Tr(st, L"weekday_sat", L"Sat").c_str(), colInputX + 228, detailY + 264, 38, 22, BS_AUTOCHECKBOX);
        st->weekdayEveryday = createBtn(kCtrlWeekdayEveryday, Tr(st, L"weekday_everyday", L"Everyday").c_str(), detailX + 360, detailY + 264, 74, 22, BS_AUTOCHECKBOX);

        st->grpStartup = nullptr;

        st->grpHotkey = createBtn(0, Tr(st, L"trigger_hotkey_only", L"hotkey_only").c_str(), detailX + 164, detailY + 162, 344, 50, BS_GROUPBOX);
        createStatic(Tr(st, L"label_hotkey", L"Hotkey").c_str(), detailX + 174, detailY + 182, 50, 20);
        st->hotkeyMod = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            detailX + 228, detailY + 178, 110, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlHotkeyMod)), nullptr, nullptr);
        st->hotkeyKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            detailX + 348, detailY + 178, 110, 220, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlHotkeyKey)), nullptr, nullptr);
        SendMessageW(st->hotkeyMod, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(st->hotkeyKey, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        st->grpNonRunning = createBtn(0, Tr(st, L"trigger_non_running", L"non_running").c_str(), detailX + 8, detailY + 316, detailW - 16, 70, BS_GROUPBOX);
        createStatic(Tr(st, L"label_retrysec", L"RetrySec").c_str(), colLabelX, detailY + 340, 72, 20);
        st->watchdogRetrySec = createNumericEdit(kCtrlWatchdogRetrySec, colInputX, detailY + 336, 52, editH, 4);
        createSpin(kCtrlSpinWatchdogRetrySec, colInputX + 42, detailY + 336, 10, editH, 10, 3600);
        createStatic(Tr(st, L"label_retrycount", L"RetryCount").c_str(), detailX + 174, detailY + 340, 72, 20);
        st->repeatCount = createNumericEdit(kCtrlRepeatCount, detailX + 248, detailY + 336, 64, editH, 7);
        createSpin(kCtrlSpinRepeatCount, detailX + 302, detailY + 336, 10, editH, 1, 1000000);

        st->status = createStatic(Tr(st, L"status_ready", L"Ready.").c_str(), m, 596, globalW, 20);

        InitializeCombos(st);
        ApplyActionModeUi(st);
        RefreshAll(st);
        return 0;
    }
    case WM_COMMAND: {
        if (!st) return 0;
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == kCtrlTaskList && code == LBN_SELCHANGE) {
            int idx = static_cast<int>(SendMessageW(st->taskList, LB_GETCURSEL, 0, 0));
            st->suppressEvents = true;
            LoadTaskControls(st, idx);
            st->suppressEvents = false;
            SetStatus(st, Tr(st, L"status_task_selected", L"Task selected."));
            return 0;
        }
        if (id == kCtrlTaskAdd && code == BN_CLICKED) {
            AddTask(st);
            PersistRealtime(st, true);
            RefreshAll(st);
            return 0;
        }
        if (id == kCtrlTaskDelete && code == BN_CLICKED) {
            if (st->config.tasks.size() <= 1) {
                SetStatus(st, Tr(st, L"status_keep_one_task", L"At least one task must remain."));
                return 0;
            }
            DeleteSelectedTask(st);
            PersistRealtime(st, true);
            RefreshAll(st);
            return 0;
        }
        if (id == kCtrlTaskRename && code == BN_CLICKED) {
            if (st->selectedTask < 0 || st->selectedTask >= static_cast<int>(st->config.tasks.size())) return 0;
            std::wstring newName;
            if (PromptRename(hwnd, Tr(st, L"rename_title", L"Rename Task"), st->config.tasks[static_cast<size_t>(st->selectedTask)].name, newName)) {
                st->config.tasks[static_cast<size_t>(st->selectedTask)].name = newName;
                PersistRealtime(st, true);
                RefreshAll(st);
            }
            return 0;
        }
        if (id == kCtrlActionMode && code == CBN_SELCHANGE) {
            ApplyActionModeUi(st);
            PersistRealtime(st, false);
            return 0;
        }
        if (id == kCtrlActionPath && code == EN_CHANGE) {
            st->actionPathDirty = true;
            return 0;
        }
        if (id == kCtrlActionCwd && code == EN_CHANGE) {
            st->actionCwdDirty = true;
            return 0;
        }

        const bool triggerClick =
            (id == kCtrlTriggerInterval) ||
            (id == kCtrlTriggerWeeklyTime) ||
            (id == kCtrlTriggerStartup) ||
            (id == kCtrlTriggerHotkeyOnly) ||
            (id == kCtrlTriggerNonRunning);
        if (triggerClick && code == BN_CLICKED) {
            int clickedIndex = 0;
            if (id == kCtrlTriggerInterval) clickedIndex = 0;
            else if (id == kCtrlTriggerWeeklyTime) clickedIndex = 2;
            else if (id == kCtrlTriggerStartup) clickedIndex = 3;
            else if (id == kCtrlTriggerHotkeyOnly) clickedIndex = 4;
            else if (id == kCtrlTriggerNonRunning) clickedIndex = 5;
            EnsureAtLeastOneTriggerChecked(st, clickedIndex);
            if (id == kCtrlTriggerWeeklyTime && SendMessageW(st->triggerChecks[2], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                EnsureAtLeastOneDateConditionChecked(st, kCtrlTimeEnabled);
            }
            ApplyTriggerUiState(st);
            PersistRealtime(st, false);
            return 0;
        }

        const bool conditionClick =
            (id == kCtrlDateEnabled) ||
            (id == kCtrlWeekdayEnabled) ||
            (id == kCtrlTimeEnabled) ||
            (id == kCtrlWeekdayEveryday);
        if (conditionClick && code == BN_CLICKED) {
            if (HasTriggerEnabled(st, tcyc::TriggerType::WeeklyTime)) {
                if (id == kCtrlDateEnabled && IsChecked(st->dateEnabled)) {
                    SetChecked(st->weekdayEnabled, false);
                } else if (id == kCtrlWeekdayEnabled && IsChecked(st->weekdayEnabled)) {
                    SetChecked(st->dateEnabled, false);
                }
                if (!IsChecked(st->timeEnabled)) {
                    SetChecked(st->timeEnabled, true);
                }
                if (id == kCtrlDateEnabled || id == kCtrlWeekdayEnabled || id == kCtrlTimeEnabled) {
                    EnsureAtLeastOneDateConditionChecked(st, id);
                }
            }
            ApplyTriggerUiState(st);
            PersistRealtime(st, false);
            return 0;
        }

        const bool realtimeChange =
            (id == kCtrlPollSec && code == EN_CHANGE) ||
            (id == kCtrlGraceSec && code == EN_CHANGE) ||
            (id == kCtrlTaskEnabled && code == BN_CLICKED) ||
            (id == kCtrlActionPath && code == EN_KILLFOCUS) ||
            (id == kCtrlActionCwd && code == EN_KILLFOCUS) ||
            (id == kCtrlIntervalSec && code == EN_CHANGE) ||
            (id == kCtrlWatchdogRetrySec && code == EN_CHANGE) ||
            (id == kCtrlRepeatCount && code == EN_CHANGE) ||
            ((id >= kCtrlWeekdaySun && id <= kCtrlWeekdaySat) && code == BN_CLICKED) ||
            (id == kCtrlDateValue && code == EN_KILLFOCUS) ||
            (id == kCtrlTimeOfDay && code == EN_KILLFOCUS) ||
            (id == kCtrlHotkeyMod && code == CBN_SELCHANGE) ||
            (id == kCtrlHotkeyKey && code == CBN_SELCHANGE);

        if (realtimeChange) {
            PersistRealtime(st, false);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        if (st && !st->suppressEvents) {
            if (st->actionPathDirty || st->actionCwdDirty) {
                PersistRealtime(st, false);
            }
            st->suppressEvents = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

int tcyc::RunReadOnlySettingsWindow(const RuntimeConfig& cfg, const std::wstring& iniPath, const std::wstring& exeDir, const std::string& preferredLanguage) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icex);

    WindowState state{};
    state.config = cfg;
    state.iniPath = iniPath;
    state.exeDir = exeDir;
    state.languageCode = preferredLanguage.empty() ? ReadLanguageFromIni(iniPath) : ToLowerAscii(preferredLanguage);
    LoadLanguage(&state, state.languageCode);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClassName,
        Tr(&state, L"window_title", L"TCycle Settings").c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 742, 648,
        nullptr,
        nullptr,
        inst,
        &state);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
