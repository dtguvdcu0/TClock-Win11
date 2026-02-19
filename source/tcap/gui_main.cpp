#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#ifndef TCM_SETBKCOLOR
#define TCM_SETBKCOLOR (TCM_FIRST + 29)
#endif
#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR (WM_USER + 67)
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <cwctype>

#include "settings.h"
#include "resource.h"

namespace fs = std::filesystem;

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_APP_CAPTURE_REQUEST = WM_APP + 2;
constexpr UINT WM_APP_SHOW_SETTINGS = WM_APP + 3;
constexpr UINT ID_TRAY_SETTINGS = 2001;
constexpr UINT ID_TRAY_OPEN_OUTPUT = 2003;
constexpr UINT ID_TRAY_EXIT = 2004;
constexpr UINT ID_TRAY_CAPTURE_BASE = 2100; // per profile
constexpr UINT HOTKEY_ID_BASE = 3000;      // per profile
constexpr UINT TIMER_AUTO_BASE = 4000;     // per profile
constexpr UINT ID_TRAY_LANG_BASE = 5100;
static const wchar_t* kAgentMainWindowClass = L"TCaptureAgentWindow";
static const wchar_t* kSettingsWindowClass = L"TCaptureSettings";
static const wchar_t* kSingletonMutexName = L"Local\\TCaptureMutex";
struct HotkeySpec {
    UINT modifiers = 0;
    UINT key = 0;
};

struct AppState {
    HINSTANCE hInstance = nullptr;
    HWND mainWindow = nullptr;
    HWND settingsWindow = nullptr;
    NOTIFYICONDATA nid{};
    fs::path iniPath;
    fs::path exeDir;
    std::string language = "en";
    std::unordered_map<std::wstring, std::wstring> translations;
    std::vector<std::string> availableLanguages;
    std::vector<ProfileSettings> profiles;
    std::vector<UINT> hotkeyIds; // 0 if not registered
    std::vector<HotkeySpec> hotkeySpecs;
    struct PrintScreenHotkey {
        int index = -1;
        UINT modifiers = 0;
        std::string profileName;
    };
    std::vector<PrintScreenHotkey> printScreenHotkeys;
    size_t printScreenHotkeyCount = 0;
    std::vector<UINT_PTR> autoTimerIds; // 0 if not registered
    bool enableTray = false;
    bool startWithSettings = false;
};

// Forward declarations
std::wstring translateId(const AppState& app, const std::wstring& id, const std::wstring& fallback);
bool loadLanguage(AppState& app, const std::string& code);
void setLanguage(AppState& app, const std::string& langCode);
void applyLanguageToProfiles(AppState& app);
HWND showSettingsWindow(AppState& app);
std::vector<std::string> listAvailableLanguages(const fs::path& root);
std::wstring languageLabel(const std::string& code);
void updateStatusBrush(struct SettingsDialog* dlg);
LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);

int RunCaptureMain(int argc, char* argv[]);

static HHOOK g_keyboardHook = nullptr;
static AppState* g_hookApp = nullptr;

void writeDefaultSettingsIni(const fs::path& path) {
    static const char* kDefaultIni =
        "[Default]\r\n"
        "output_dir=\r\n"
        "format=jpg\r\n"
        "compression_png=9\r\n"
        "compression_jpg=96\r\n"
        "compression=96\r\n"
        "burst_fps=0\r\n"
        "burst_seconds=1\r\n"
        "auto_capture=false\r\n"
        "auto_seconds=60\r\n"
        "displays=0\r\n"
        "hotkey_capture=Ctrl+Alt+PrintScreen\r\n"
        "\r\n"
        "[Active Window]\r\n"
        "output_dir=\r\n"
        "format=png\r\n"
        "compression_png=6\r\n"
        "compression_jpg=96\r\n"
        "compression=6\r\n"
        "burst_fps=0\r\n"
        "burst_seconds=1\r\n"
        "auto_capture=false\r\n"
        "auto_seconds=60\r\n"
        "displays=active_window\r\n"
        "hotkey_capture=Alt+PrintScreen\r\n"
        "\r\n"
        "[Active Display]\r\n"
        "output_dir=\r\n"
        "format=png\r\n"
        "compression_png=6\r\n"
        "compression_jpg=96\r\n"
        "burst_fps=0\r\n"
        "burst_seconds=1\r\n"
        "auto_capture=false\r\n"
        "auto_seconds=60\r\n"
        "displays=active_display\r\n"
        "hotkey_capture=PrintScreen\r\n";
    fs::path parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return;
    out.write(kDefaultIni, static_cast<std::streamsize>(std::strlen(kDefaultIni)));
}


struct SettingsDialog {
    AppState* app = nullptr;
    HWND hwnd = nullptr;
    HWND tab = nullptr;
    HWND addBtn = nullptr;
    HWND deleteBtn = nullptr;
    HWND outputEdit = nullptr;
    HWND formatCombo = nullptr;
    HWND compressionLabelPng = nullptr;
    HWND compressionLabelJpg = nullptr;
    HWND compressionEditPng = nullptr;
    HWND compressionEditJpg = nullptr;
    HWND burstFpsCombo = nullptr;
    HWND burstSecondsEdit = nullptr;
    HWND autoCaptureCheck = nullptr;
    HWND autoIntervalEdit = nullptr;
    HWND displaysEdit = nullptr;
    HWND displaysHelpBtn = nullptr;
    HWND hotkeyModCombo = nullptr;
    HWND hotkeyKeyCombo = nullptr;
    HWND autoStatusLabel = nullptr;
    bool autoStatusActive = false;
    HWND tooltip = nullptr;
    std::list<std::wstring> tooltipTexts;
    HBRUSH statusBrushActive = nullptr;
    HBRUSH statusBrushInactive = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH editBackgroundBrush = nullptr;
    COLORREF backgroundColor = 0;
    COLORREF editBackgroundColor = 0;
    int layoutClientWidth = 0;
    int layoutClientHeight = 0;
    int activeProfile = 0;
    bool suppressSave = false;
};

using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
static SetWindowThemeFn g_setWindowTheme = nullptr;

void ensureThemeProc() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    HMODULE theme = LoadLibraryW(L"uxtheme.dll");
    if (!theme) return;
    g_setWindowTheme = reinterpret_cast<SetWindowThemeFn>(GetProcAddress(theme, "SetWindowTheme"));
}

void applyControlTheme(SettingsDialog* dlg, HWND hwnd, bool setEditBackground) {
    if (!dlg || !hwnd) return;
    ensureThemeProc();
    if (g_setWindowTheme) {
        g_setWindowTheme(hwnd, L"", L"");
    }
    if (setEditBackground) {
        SendMessageW(hwnd, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(dlg->editBackgroundColor));
    }
}

void adjustSettingsWindowSize(SettingsDialog* dlg) {
    if (!dlg || !dlg->hwnd || dlg->layoutClientHeight <= 0 || dlg->layoutClientWidth <= 0) return;
    RECT client{};
    GetClientRect(dlg->hwnd, &client);
    int currentClientWidth = client.right - client.left;
    int currentClientHeight = client.bottom - client.top;
    int targetClientWidth = dlg->layoutClientWidth;
    int targetClientHeight = dlg->layoutClientHeight;
    if (currentClientWidth == targetClientWidth && currentClientHeight == targetClientHeight) return;

    DWORD style = static_cast<DWORD>(GetWindowLongW(dlg->hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(dlg->hwnd, GWL_EXSTYLE));
    RECT adjusted{0, 0, targetClientWidth, targetClientHeight};
    AdjustWindowRectEx(&adjusted, style, FALSE, exStyle);

    RECT window{};
    GetWindowRect(dlg->hwnd, &window);
    int width = adjusted.right - adjusted.left;
    int height = adjusted.bottom - adjusted.top;
    SetWindowPos(dlg->hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
}

bool namesEqual(const std::string& a, const std::string& b);

std::string displaysToString(const AppSettings& settings) {
    if (!settings.displaysRaw.empty()) return settings.displaysRaw;
    if (settings.displayIndices.empty()) return "";
    std::string s;
    for (size_t i = 0; i < settings.displayIndices.size(); ++i) {
        if (i > 0) s += ",";
        s += std::to_string(settings.displayIndices[i] + 1);
    }
    return s;
}

std::string trimCopy(const std::string& text) {
    return trim(text);
}

std::wstring trimCopy(const std::wstring& text) {
    auto isSpace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\u3000';
    };
    size_t start = 0;
    size_t end = text.size();
    while (start < end && isSpace(text[start])) ++start;
    while (end > start && isSpace(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::wstring replaceEscapes(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) {
            if (text[i + 1] == L'n') {
                result.push_back(L'\n');
                ++i;
                continue;
            }
            if (text[i + 1] == L'\\') {
                // Allow both "\n" and "\\n" from config files.
                if (i + 2 < text.size() && text[i + 2] == L'n') {
                    result.push_back(L'\n');
                    i += 2;
                    continue;
                }
                result.push_back(L'\\');
                ++i;
                continue;
            }
        }
        result.push_back(text[i]);
    }
    return result;
}

std::wstring stripQuotes(const std::wstring& text) {
    std::wstring t = trimCopy(text);
    if (t.size() >= 2) {
        wchar_t first = t.front();
        wchar_t last = t.back();
        if ((first == L'"' && last == L'"') || (first == L'\'' && last == L'\'')) {
            return t.substr(1, t.size() - 2);
        }
    }
    return t;
}

bool parseHotkey(const std::string& text, HotkeySpec& spec) {
    std::string lower = toLower(trimCopy(text));
    if (lower.empty()) return false;

    UINT modifiers = 0;
    UINT vk = 0;
    std::string token;
    auto flushToken = [&](const std::string& raw) {
        std::string t = trimCopy(raw);
        if (t.empty()) return;
        if (t == "ctrl" || t == "control") {
            modifiers |= MOD_CONTROL;
        } else if (t == "shift") {
            modifiers |= MOD_SHIFT;
        } else if (t == "alt") {
            modifiers |= MOD_ALT;
        } else if (t == "win" || t == "windows") {
            modifiers |= MOD_WIN;
        } else {
            if (vk != 0) return;
            if (t.size() == 1) {
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
                if (c >= 'A' && c <= 'Z') vk = static_cast<UINT>(c);
                else if (c >= '0' && c <= '9') vk = static_cast<UINT>(c);
            } else if (t[0] == 'f' && t.size() <= 3) {
                int fn = 0;
                try { fn = std::stoi(t.substr(1)); } catch (...) { fn = 0; }
                if (fn >= 1 && fn <= 24) vk = VK_F1 + static_cast<UINT>(fn - 1);
            } else if (t == "printscreen" || t == "prtsc" || t == "prtscr") {
                vk = VK_SNAPSHOT;
            } else if (t == "insert") {
                vk = VK_INSERT;
            } else if (t == "delete") {
                vk = VK_DELETE;
            } else if (t == "home") {
                vk = VK_HOME;
            } else if (t == "end") {
                vk = VK_END;
            } else if (t == "pgup" || t == "pageup") {
                vk = VK_PRIOR;
            } else if (t == "pgdn" || t == "pagedown") {
                vk = VK_NEXT;
            } else if (t == "space" || t == "spacebar") {
                vk = VK_SPACE;
            } else if (t == "enter" || t == "return") {
                vk = VK_RETURN;
            }
        }
    };

    for (char c : lower) {
        if (c == '+') {
            flushToken(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    flushToken(token);

    if (vk == 0) return false;
    spec.modifiers = modifiers | MOD_NOREPEAT;
    spec.key = vk;
    return true;
}

void showNotification(AppState& app, const std::wstring& title, const std::wstring& message, DWORD infoFlag = NIIF_INFO) {
    NOTIFYICONDATA data = app.nid;
    data.uFlags = NIF_INFO;
    wcsncpy_s(data.szInfoTitle, std::size(data.szInfoTitle), title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, std::size(data.szInfo), message.c_str(), _TRUNCATE);
    data.dwInfoFlags = infoFlag;
    Shell_NotifyIcon(NIM_MODIFY, &data);
}

void buildTrayIcon(AppState& app) {
    app.nid = {};
    app.nid.cbSize = sizeof(NOTIFYICONDATA);
    app.nid.hWnd = app.mainWindow;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    app.nid.uCallbackMessage = WM_TRAYICON;
    app.nid.hIcon = static_cast<HICON>(LoadImageW(app.hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                  0, 0, LR_DEFAULTSIZE));
    if (!app.nid.hIcon) {
        app.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcsncpy_s(app.nid.szTip, std::size(app.nid.szTip), L"TCapture", _TRUNCATE);
    Shell_NotifyIcon(NIM_ADD, &app.nid);
}

std::wstring buildCommandLine(const fs::path& exePath, const std::string& profileName) {
    std::wstring cmd = L"\"";
    cmd += exePath.wstring();
    cmd += L"\" --profile \"";
    cmd += utf8ToWide(profileName);
    cmd += L"\"";
    return cmd;
}

bool triggerCapture(AppState& app, int profileIndex) {
    if (profileIndex < 0 || profileIndex >= static_cast<int>(app.profiles.size())) return false;
    const auto profileName = app.profiles[profileIndex].name;

    std::thread([profileName]() {
        std::vector<std::string> args;
        args.push_back("TCapture.exe");
        args.push_back("--capture");
        args.push_back("--profile");
        args.push_back(profileName);

        std::vector<char*> argv;
        argv.reserve(args.size());
        for (auto& s : args) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        (void)RunCaptureMain(static_cast<int>(argv.size()), argv.data());
    }).detach();

    return true;
}

bool loadProfilesWithFallback(AppState& app, const std::string& preferredLanguage = "") {
    fs::path primary = app.exeDir / "TCapture.ini";
    fs::path cwdIni = fs::current_path() / "TCapture.ini";
    if (!fs::exists(primary) && !fs::exists(cwdIni)) {
        writeDefaultSettingsIni(primary);
    }
    auto profiles = loadSettingsProfiles(primary.string());
    std::string globalLang = readGlobalLanguage(primary.string());
    app.iniPath = primary;
    if (profiles.empty()) {
        profiles = loadSettingsProfiles(cwdIni.string());
        globalLang = readGlobalLanguage(cwdIni.string());
        if (!profiles.empty()) {
            app.iniPath = cwdIni;
        }
    }
    if (profiles.empty()) {
        ProfileSettings p;
        p.name = "default";
        normalizeSettings(p.settings);
        if (p.settings.captureHotkey.empty()) p.settings.captureHotkey = "Ctrl+Alt+S";
        profiles.push_back(p);
    }
    for (auto& p : profiles) {
        normalizeSettings(p.settings);
    }
    app.profiles = profiles;
    if (!globalLang.empty()) {
        app.language = toLower(globalLang);
    } else {
        std::string pref = toLower(preferredLanguage);
        if (pref == "ja" || pref == "en") {
            app.language = pref;
        } else {
            app.language = "en";
        }
    }
    app.availableLanguages = listAvailableLanguages(app.exeDir / "lang");
    loadLanguage(app, app.language);
    applyLanguageToProfiles(app);
    app.hotkeyIds.assign(app.profiles.size(), 0);
    app.hotkeySpecs.assign(app.profiles.size(), HotkeySpec{});
    app.printScreenHotkeys.clear();
    app.printScreenHotkeyCount = 0;
    app.autoTimerIds.assign(app.profiles.size(), 0);
    return true;
}

bool saveProfiles(AppState& app) {
    if (app.profiles.empty()) return false;
    applyLanguageToProfiles(app);
    return saveSettingsProfiles(app.profiles, app.iniPath, app.language);
}

void unregisterAllHotkeys(AppState& app) {
    for (UINT id : app.hotkeyIds) {
        if (id != 0 && app.mainWindow) {
            UnregisterHotKey(app.mainWindow, id);
        }
    }
    std::fill(app.hotkeyIds.begin(), app.hotkeyIds.end(), 0);
    app.printScreenHotkeyCount = 0;
    for (auto& spec : app.hotkeySpecs) {
        spec = HotkeySpec{};
    }
    app.printScreenHotkeys.clear();
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        g_hookApp = nullptr;
    }
}

void applyLanguageToProfiles(AppState& app) {
    for (auto& p : app.profiles) {
        p.settings.language = app.language;
    }
}

std::wstring translateId(const AppState& app, const std::wstring& id, const std::wstring& fallback) {
    auto it = app.translations.find(id);
    if (it != app.translations.end()) {
        return it->second;
    }
    return fallback;
}

bool loadLanguage(AppState& app, const std::string& code) {
    std::string lang = code.empty() ? "en" : toLower(code);
    if (lang != "en" && lang != "ja") lang = "en";
    app.translations.clear();
    app.language = lang;
    app.availableLanguages = listAvailableLanguages(app.exeDir / "lang");

    auto loadFile = [&](const std::string& langCode) -> bool {
        fs::path path = app.exeDir / "lang" / langCode / "strings.ini";
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trimCopy(line.substr(0, eq));
            std::string value = line.substr(eq + 1);
            std::wstring wkey = replaceEscapes(utf8ToWide(key));
            std::wstring wval = stripQuotes(replaceEscapes(utf8ToWide(value)));
            if (!wkey.empty()) {
                app.translations[wkey] = wval;
            }
        }
        return true;
    };

    bool loaded = loadFile(lang);
    if (!loaded && lang != "en") {
        loadFile("en");
        app.language = "en";
    }
    return loaded;
}

void setLanguage(AppState& app, const std::string& langCode) {
    std::string lang = toLower(langCode);
    if (lang.empty()) lang = "en";
    app.language = lang;
    applyLanguageToProfiles(app);
    saveProfiles(app);
    loadLanguage(app, app.language);
}

std::vector<std::string> listAvailableLanguages(const fs::path& root) {
    std::vector<std::string> codes;
    if (!fs::exists(root) || !fs::is_directory(root)) return codes;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        fs::path ini = entry.path() / "strings.ini";
        if (fs::exists(ini)) {
            codes.push_back(toLower(entry.path().filename().string()));
        }
    }
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    if (codes.empty()) {
        codes.push_back("en");
    }
    return codes;
}

std::wstring languageLabel(const std::string& code) {
    std::string lc = toLower(code);
    // Try loading display name from the language file.
    fs::path exeDir = getExecutableDir();
    fs::path ini = exeDir / "lang" / lc / "strings.ini";
    if (fs::exists(ini)) {
        std::ifstream in(ini);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trimCopy(line.substr(0, eq));
            std::string value = line.substr(eq + 1);
            if (toLower(key) == "language_name") {
                std::wstring w = replaceEscapes(utf8ToWide(value));
                if (!w.empty()) return w;
            }
        }
    }
    if (lc == "ja") return L"Japanese";
    if (lc == "en") return L"English";
    std::wstring w = utf8ToWide(code);
    return w.empty() ? L"en" : w;
}

void stopAutoCaptureTimers(AppState& app) {
    if (!app.mainWindow) return;
    for (UINT_PTR id : app.autoTimerIds) {
        if (id != 0) {
            KillTimer(app.mainWindow, id);
        }
    }
    std::fill(app.autoTimerIds.begin(), app.autoTimerIds.end(), 0);
}

void updateAutoCaptureTimers(AppState& app) {
    if (!app.mainWindow) return;
    stopAutoCaptureTimers(app);
    if (app.autoTimerIds.size() != app.profiles.size()) {
        app.autoTimerIds.assign(app.profiles.size(), 0);
    }
    for (size_t i = 0; i < app.profiles.size(); ++i) {
        const auto& s = app.profiles[i].settings;
        if (!s.autoCapture || s.autoSeconds <= 0) continue;
        UINT_PTR timerId = TIMER_AUTO_BASE + static_cast<UINT_PTR>(i);
        if (SetTimer(app.mainWindow, timerId, static_cast<UINT>(s.autoSeconds * 1000), nullptr)) {
            app.autoTimerIds[i] = timerId;
        }
    }
}

bool isKeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

UINT currentModifierState() {
    UINT mods = 0;
    if (isKeyDown(VK_LCONTROL) || isKeyDown(VK_RCONTROL)) mods |= MOD_CONTROL;
    if (isKeyDown(VK_LSHIFT) || isKeyDown(VK_RSHIFT)) mods |= MOD_SHIFT;
    if (isKeyDown(VK_LMENU) || isKeyDown(VK_RMENU) || isKeyDown(VK_MENU)) mods |= MOD_ALT;
    if (isKeyDown(VK_LWIN) || isKeyDown(VK_RWIN)) mods |= MOD_WIN;
    return mods;
}

UINT currentModifierStateForHook(const KBDLLHOOKSTRUCT& info) {
    UINT mods = currentModifierState();
    if (info.flags & LLKHF_ALTDOWN) {
        mods |= MOD_ALT;
    }
    return mods;
}

void installPrintScreenHook(AppState& app) {
    if (g_keyboardHook || app.printScreenHotkeyCount == 0) return;
    g_hookApp = &app;
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (!g_keyboardHook) {
        g_hookApp = nullptr;
        showNotification(app, translateId(app, L"app_name", L"TCapture"),
                         L"Failed to install PrintScreen hook.", NIIF_WARNING);
    }
}

void registerAllHotkeys(AppState& app) {
    unregisterAllHotkeys(app);
    if (!app.mainWindow) return;
    if (app.hotkeySpecs.size() != app.profiles.size()) {
        app.hotkeySpecs.assign(app.profiles.size(), HotkeySpec{});
    }
    for (size_t i = 0; i < app.profiles.size(); ++i) {
        const auto& profile = app.profiles[i];
        if (profile.settings.captureHotkey.empty()) continue;
        HotkeySpec spec;
        if (!parseHotkey(profile.settings.captureHotkey, spec)) {
            continue;
        }
        app.hotkeySpecs[i] = spec;
        if (spec.key == VK_SNAPSHOT) {
            app.printScreenHotkeys.push_back({ static_cast<int>(i), spec.modifiers, profile.name });
            app.printScreenHotkeyCount = app.printScreenHotkeys.size();
            continue;
        }
        UINT id = HOTKEY_ID_BASE + static_cast<UINT>(i);
        if (RegisterHotKey(app.mainWindow, id, spec.modifiers, spec.key)) {
            app.hotkeyIds[i] = id;
        }
    }
    installPrintScreenHook(app);
    // Notify if nothing registered.
    bool any = app.printScreenHotkeyCount > 0;
    for (UINT id : app.hotkeyIds) {
        if (id != 0) {
            any = true;
            break;
        }
    }
    if (!any) {
                showNotification(app, translateId(app, L"app_name", L"TCapture"),
                         translateId(app, L"no_hotkeys", L"No hotkeys registered. Set hotkey_capture in settings."), NIIF_WARNING);
    }
}

void openOutputFolder(const AppState& app, int profileIndex) {
    if (profileIndex < 0 || profileIndex >= static_cast<int>(app.profiles.size())) return;
    const auto& s = app.profiles[profileIndex].settings;
    fs::path dir = s.outputDir.empty() ? fs::path(".") : fs::path(s.outputDir);
    fs::path base = app.iniPath.empty() ? app.exeDir : app.iniPath.parent_path();
    if (!dir.is_absolute()) {
        dir = base / dir;
    }
    ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int findPrintScreenMatch(const AppState& app, UINT modifiers) {
    modifiers &= ~MOD_NOREPEAT;
    for (const auto& entry : app.printScreenHotkeys) {
        if (entry.index < 0) continue;
        if ((entry.modifiers & ~MOD_NOREPEAT) == modifiers) {
            return entry.index;
        }
    }
    return -1;
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    static bool printScreenTriggered = false;
    static UINT modifierState = 0;
    if (code == HC_ACTION && g_hookApp && g_hookApp->printScreenHotkeyCount > 0) {
        const KBDLLHOOKSTRUCT* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (info) {
            const bool isKeyDownEvent = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isKeyUpEvent = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            const bool isDown = isKeyDownEvent && !(info->flags & LLKHF_UP);
            const bool isUp = isKeyUpEvent || (info->flags & LLKHF_UP);
            switch (info->vkCode) {
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_CONTROL:
                if (isDown) modifierState |= MOD_CONTROL;
                if (isUp) modifierState &= ~MOD_CONTROL;
                break;
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_SHIFT:
                if (isDown) modifierState |= MOD_SHIFT;
                if (isUp) modifierState &= ~MOD_SHIFT;
                break;
            case VK_LMENU:
            case VK_RMENU:
            case VK_MENU:
                if (isDown) modifierState |= MOD_ALT;
                if (isUp) modifierState &= ~MOD_ALT;
                break;
            case VK_LWIN:
            case VK_RWIN:
                if (isDown) modifierState |= MOD_WIN;
                if (isUp) modifierState &= ~MOD_WIN;
                break;
            default:
                break;
            }
        }
        if (info && info->vkCode == VK_SNAPSHOT) {
            const bool isKeyDownEvent = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isKeyUpEvent = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            if (isKeyDownEvent && !printScreenTriggered) {
                UINT mods = modifierState;
                if (info->flags & LLKHF_ALTDOWN) mods |= MOD_ALT;
                int match = findPrintScreenMatch(*g_hookApp, mods);
                if (match >= 0 && g_hookApp->mainWindow) {
                    PostMessageW(g_hookApp->mainWindow, WM_APP_CAPTURE_REQUEST,
                                 static_cast<WPARAM>(match), 0);
                    printScreenTriggered = true;
                }
            } else if (isKeyUpEvent) {
                if (!printScreenTriggered) {
                    UINT mods = modifierState;
                    if (info->flags & LLKHF_ALTDOWN) mods |= MOD_ALT;
                    int match = findPrintScreenMatch(*g_hookApp, mods);
                    if (match >= 0 && g_hookApp->mainWindow) {
                        PostMessageW(g_hookApp->mainWindow, WM_APP_CAPTURE_REQUEST,
                                     static_cast<WPARAM>(match), 0);
                    }
                }
                printScreenTriggered = false;
            }
            return 1; // Always swallow PrintScreen when configured to avoid OS capture UI.
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

int CALLBACK browseFolderCallback(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
    }
    return 0;
}

std::wstring browseForFolder(HWND owner, const std::wstring& initialPath, const std::wstring& title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title.empty() ? nullptr : title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    bi.lpfn = browseFolderCallback;
    bi.lParam = initialPath.empty() ? 0 : reinterpret_cast<LPARAM>(initialPath.c_str());
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = {};
    BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok || path[0] == L'\0') return L"";
    return std::wstring(path);
}

void setControlText(HWND hwnd, const std::string& text) {
    std::wstring wide = utf8ToWide(text);
    SetWindowTextW(hwnd, wide.c_str());
}

std::string getControlText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return "";
    std::wstring buffer(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hwnd, buffer.data(), len + 1);
    return wideToUtf8(buffer);
}

void ensureTooltip(SettingsDialog* dlg) {
    if (dlg->tooltip) return;
    dlg->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
                                   WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   dlg->hwnd, nullptr, dlg->app->hInstance, nullptr);
    SetWindowPos(dlg->tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(dlg->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
}

void addTooltip(SettingsDialog* dlg, HWND target, const std::wstring& text) {
    ensureTooltip(dlg);
    if (!dlg->tooltip || !target) return;
    const std::wstring& stored = dlg->tooltipTexts.emplace_back(text);
    TOOLINFOW ti{};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.hwnd = target;
    ti.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    ti.uId = 0;
    ti.lpszText = const_cast<wchar_t*>(stored.c_str());
    GetClientRect(target, &ti.rect);
    SendMessageW(dlg->tooltip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
}

struct HotkeyModOption {
    std::wstring label;
    std::string text;
    UINT modifiers;
};

struct HotkeyKeyOption {
    std::wstring label;
    std::string text;
    UINT key;
};

const std::vector<HotkeyModOption>& modOptions() {
    static const std::vector<HotkeyModOption> opts = {
        {L"None", "", 0},
        {L"Ctrl", "Ctrl", MOD_CONTROL},
        {L"Alt", "Alt", MOD_ALT},
        {L"Shift", "Shift", MOD_SHIFT},
        {L"Ctrl+Alt", "Ctrl+Alt", MOD_CONTROL | MOD_ALT},
        {L"Ctrl+Shift", "Ctrl+Shift", MOD_CONTROL | MOD_SHIFT},
        {L"Alt+Shift", "Alt+Shift", MOD_ALT | MOD_SHIFT},
        {L"Ctrl+Alt+Shift", "Ctrl+Alt+Shift", MOD_CONTROL | MOD_ALT | MOD_SHIFT},
        {L"Win", "Win", MOD_WIN},
        {L"Ctrl+Win", "Ctrl+Win", MOD_CONTROL | MOD_WIN},
        {L"Alt+Win", "Alt+Win", MOD_ALT | MOD_WIN},
        {L"Ctrl+Alt+Win", "Ctrl+Alt+Win", MOD_CONTROL | MOD_ALT | MOD_WIN},
    };
    return opts;
}

const std::vector<HotkeyKeyOption>& keyOptions() {
    static std::vector<HotkeyKeyOption> opts;
    if (!opts.empty()) return opts;
    opts.push_back({L"(None)", "", 0});
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::wstring label(1, static_cast<wchar_t>(c));
        std::string text(1, c);
        opts.push_back({label, text, static_cast<UINT>(c)});
    }
    for (char d = '0'; d <= '9'; ++d) {
        std::wstring label(1, static_cast<wchar_t>(d));
        std::string text(1, d);
        opts.push_back({label, text, static_cast<UINT>(d)});
    }
    for (int f = 1; f <= 24; ++f) {
        std::wstring label = L"F" + std::to_wstring(f);
        std::string text = "F" + std::to_string(f);
        opts.push_back({label, text, VK_F1 + static_cast<UINT>(f - 1)});
    }
    opts.push_back({L"PrintScreen", "PrintScreen", VK_SNAPSHOT});
    opts.push_back({L"Insert", "Insert", VK_INSERT});
    opts.push_back({L"Delete", "Delete", VK_DELETE});
    opts.push_back({L"Home", "Home", VK_HOME});
    opts.push_back({L"End", "End", VK_END});
    opts.push_back({L"PageUp", "PgUp", VK_PRIOR});
    opts.push_back({L"PageDown", "PgDn", VK_NEXT});
    opts.push_back({L"Space", "Space", VK_SPACE});
    opts.push_back({L"Enter", "Enter", VK_RETURN});
    return opts;
}

int clampBurstFps(int fps) {
    if (fps < 0) return 0;
    if (fps > 60) return 60;
    return fps;
}

void populateBurstFpsCombo(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"0 (off)"));
    for (int i = 1; i <= 60; ++i) {
        std::wstring label = std::to_wstring(i) + L" fps";
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
}

int findModIndex(UINT modifiers) {
    UINT target = modifiers & ~MOD_NOREPEAT;
    const auto& opts = modOptions();
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts[i].modifiers == target) return static_cast<int>(i);
    }
    return 0;
}

int findKeyIndex(UINT key) {
    const auto& opts = keyOptions();
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts[i].key == key) return static_cast<int>(i);
    }
    return 0;
}

int burstFpsToIndex(int fps) {
    return clampBurstFps(fps);
}

int burstFpsFromCombo(HWND combo) {
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel < 0) sel = 0;
    return clampBurstFps(sel);
}

void setBurstFpsSelection(HWND combo, int fps) {
    int idx = burstFpsToIndex(fps);
    SendMessageW(combo, CB_SETCURSEL, idx, 0);
}

void setComboToValue(HWND combo, int value) {
    if (!combo) return;
    std::wstring text = std::to_wstring(value);
    LRESULT idx = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    if (idx >= 0) {
        SendMessageW(combo, CB_SETCURSEL, idx, 0);
    } else {
        SetWindowTextW(combo, text.c_str());
    }
}

void populateCompressionCombos(SettingsDialog* dlg) {
    if (!dlg) return;
    (void)dlg;
}

void updateCompressionFields(SettingsDialog* dlg) {
    if (!dlg || !dlg->formatCombo) return;
    int sel = static_cast<int>(SendMessageW(dlg->formatCombo, CB_GETCURSEL, 0, 0));
    bool usePng = (sel == 0);
    if (dlg->compressionEditPng) EnableWindow(dlg->compressionEditPng, usePng);
    if (dlg->compressionEditJpg) EnableWindow(dlg->compressionEditJpg, !usePng);
    if (dlg->compressionLabelPng) ShowWindow(dlg->compressionLabelPng, usePng ? SW_SHOW : SW_HIDE);
    if (dlg->compressionEditPng) ShowWindow(dlg->compressionEditPng, usePng ? SW_SHOW : SW_HIDE);
    if (dlg->compressionLabelJpg) ShowWindow(dlg->compressionLabelJpg, usePng ? SW_HIDE : SW_SHOW);
    if (dlg->compressionEditJpg) ShowWindow(dlg->compressionEditJpg, usePng ? SW_HIDE : SW_SHOW);
}

void updateAutoStatusLabel(SettingsDialog* dlg) {
    if (!dlg || !dlg->app || !dlg->autoStatusLabel) return;
    updateStatusBrush(dlg);
    bool active = false;
    int idx = dlg->activeProfile;
    if (idx >= 0 && idx < static_cast<int>(dlg->app->autoTimerIds.size())) {
        active = dlg->app->autoTimerIds[idx] != 0;
    }
    dlg->autoStatusActive = active;
    std::wstring text = active ? translateId(*dlg->app, L"auto_running", L"Auto capture: running")
                               : translateId(*dlg->app, L"auto_stopped", L"Auto capture: stopped");
    SetWindowTextW(dlg->autoStatusLabel, text.c_str());
    InvalidateRect(dlg->autoStatusLabel, nullptr, TRUE);
}

void updateStatusBrush(SettingsDialog* dlg) {
    if (!dlg) return;
    if (!dlg->statusBrushActive) {
        dlg->statusBrushActive = CreateSolidBrush(RGB(255, 230, 230));
    }
    if (!dlg->statusBrushInactive) {
        dlg->statusBrushInactive = CreateSolidBrush(dlg->backgroundColor);
    }
}

void updateSettingsWindowTitle(SettingsDialog* dlg) {
    if (!dlg || !dlg->app || !dlg->hwnd) return;
    std::wstring appTitle = translateId(*dlg->app, L"app_name", L"TCapture");
    std::wstring settingsTitle = translateId(*dlg->app, L"settings_title", L"TCapture settings");
    std::wstring profileTitle;
    if (dlg->activeProfile >= 0 && dlg->activeProfile < static_cast<int>(dlg->app->profiles.size())) {
        profileTitle = utf8ToWide(dlg->app->profiles[dlg->activeProfile].name);
    }
    std::wstring title = appTitle;
    if (!profileTitle.empty()) {
        title += L" - " + profileTitle;
    }
    title += L" - " + settingsTitle;
    SetWindowTextW(dlg->hwnd, title.c_str());
}

void setHotkeyCombosFromSettings(SettingsDialog* dlg, const std::string& hotkey) {
    HotkeySpec spec;
    int modIndex = 0;
    int keyIndex = 0;
    if (parseHotkey(hotkey, spec)) {
        modIndex = findModIndex(spec.modifiers);
        keyIndex = findKeyIndex(spec.key);
    }
    SendMessageW(dlg->hotkeyModCombo, CB_SETCURSEL, modIndex, 0);
    SendMessageW(dlg->hotkeyKeyCombo, CB_SETCURSEL, keyIndex, 0);
}

std::string getHotkeyFromControls(SettingsDialog* dlg) {
    int modSel = static_cast<int>(SendMessageW(dlg->hotkeyModCombo, CB_GETCURSEL, 0, 0));
    int keySel = static_cast<int>(SendMessageW(dlg->hotkeyKeyCombo, CB_GETCURSEL, 0, 0));
    const auto& mods = modOptions();
    const auto& keys = keyOptions();
    if (modSel < 0 || modSel >= static_cast<int>(mods.size())) modSel = 0;
    if (keySel < 0 || keySel >= static_cast<int>(keys.size())) keySel = 0;
    const auto& mod = mods[modSel];
    const auto& key = keys[keySel];
    if (key.text.empty()) return "";
    if (mod.text.empty()) return key.text;
    return mod.text + "+" + key.text;
}

void loadProfileToControls(SettingsDialog* dlg, int index) {
    if (index < 0 || index >= static_cast<int>(dlg->app->profiles.size())) return;
    dlg->suppressSave = true;
    dlg->activeProfile = index;
    const auto& s = dlg->app->profiles[index].settings;
    setControlText(dlg->outputEdit, s.outputDir);
    setBurstFpsSelection(dlg->burstFpsCombo, s.burstFps);
    setControlText(dlg->burstSecondsEdit, std::to_string(s.burstSeconds));
    SendMessageW(dlg->autoCaptureCheck, BM_SETCHECK, s.autoCapture ? BST_CHECKED : BST_UNCHECKED, 0);
    setControlText(dlg->autoIntervalEdit, std::to_string(s.autoSeconds));
    setControlText(dlg->displaysEdit, displaysToString(s));
    setHotkeyCombosFromSettings(dlg, s.captureHotkey);
    int fmtIndex = toLower(s.format) == "jpg" ? 1 : 0;
    SendMessageW(dlg->formatCombo, CB_SETCURSEL, fmtIndex, 0);
    populateCompressionCombos(dlg);
    setControlText(dlg->compressionEditPng, std::to_string(s.pngCompression));
    setControlText(dlg->compressionEditJpg, std::to_string(s.jpgQuality));
    dlg->suppressSave = false;
    updateCompressionFields(dlg);
    updateAutoStatusLabel(dlg);
    updateSettingsWindowTitle(dlg);
}

bool saveControlsToProfile(SettingsDialog* dlg, int index, bool showErrors) {
    if (index < 0 || index >= static_cast<int>(dlg->app->profiles.size())) return false;
    ProfileSettings& profile = dlg->app->profiles[index];
    profile.settings.outputDir = trimCopy(getControlText(dlg->outputEdit));
    profile.settings.format = trimCopy(getControlText(dlg->formatCombo));
    profile.settings.displaysRaw = trimCopy(getControlText(dlg->displaysEdit));
    profile.settings.captureHotkey = trimCopy(getHotkeyFromControls(dlg));
    profile.settings.autoCapture = (SendMessageW(dlg->autoCaptureCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    auto parseCompression = [&](HWND ctrl, int& dest, const wchar_t* errorText) -> bool {
        std::string txt = trimCopy(getControlText(ctrl));
        if (txt.empty()) return true; // keep existing
        try {
            dest = std::stoi(txt);
            return true;
        } catch (...) {
            if (showErrors && IsWindowEnabled(ctrl)) {
                MessageBoxW(dlg->hwnd, errorText, L"TCapture", MB_ICONWARNING);
            }
            return IsWindowEnabled(ctrl) ? false : true;
        }
    };
    if (!parseCompression(dlg->compressionEditPng, profile.settings.pngCompression, L"PNG compression must be a number.")) return false;
    if (!parseCompression(dlg->compressionEditJpg, profile.settings.jpgQuality, L"JPG quality must be a number.")) return false;
    profile.settings.burstFps = burstFpsFromCombo(dlg->burstFpsCombo);
    try {
        profile.settings.burstSeconds = std::stoi(trimCopy(getControlText(dlg->burstSecondsEdit)));
    } catch (...) {
        if (showErrors) {
            MessageBoxW(dlg->hwnd, L"Burst seconds must be a number.", L"TCapture", MB_ICONWARNING);
        }
        return false;
    }
    try {
        profile.settings.autoSeconds = std::stoi(trimCopy(getControlText(dlg->autoIntervalEdit)));
    } catch (...) {
        if (showErrors) {
            MessageBoxW(dlg->hwnd, L"Auto-capture seconds must be a number.", L"TCapture", MB_ICONWARNING);
        }
        return false;
    }
    bool parsedAll = false;
    bool activeDisplay = false;
    bool activeWindow = false;
    profile.settings.displayIndices = parseDisplayList(profile.settings.displaysRaw, parsedAll, activeDisplay, activeWindow);
    if (parsedAll) profile.settings.displayIndices.clear();
    profile.settings.captureActiveDisplay = activeDisplay;
    profile.settings.captureActiveWindow = activeWindow;
    normalizeSettings(profile.settings);

    if (!profile.settings.captureHotkey.empty()) {
        HotkeySpec spec;
        if (!parseHotkey(profile.settings.captureHotkey, spec)) {
            if (showErrors) {
                MessageBoxW(dlg->hwnd, L"Hotkey is invalid. Use Ctrl+Alt+S or Shift+F12, etc.", L"TCapture", MB_ICONWARNING);
            }
            return false;
        }
    }
    return true;
}

void populateTabs(SettingsDialog* dlg) {
    SendMessageW(dlg->tab, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < dlg->app->profiles.size(); ++i) {
        std::wstring name = utf8ToWide(dlg->app->profiles[i].name);
        SendMessageW(dlg->tab, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(dlg->tab, LB_SETCURSEL, static_cast<WPARAM>(dlg->activeProfile), 0);
    EnableWindow(dlg->deleteBtn, dlg->app->profiles.size() > 1);
}

bool promptRenameProfile(SettingsDialog* dlg, int index) {
    if (!dlg || !dlg->app) return false;
    if (index < 0 || index >= static_cast<int>(dlg->app->profiles.size())) return false;
    saveControlsToProfile(dlg, dlg->activeProfile, false);

    std::wstring appTitle = translateId(*dlg->app, L"app_name", L"TCapture");
    std::wstring baseTitle = translateId(*dlg->app, L"rename_profile_title", L"Rename profile");
    std::wstring title = appTitle + L" - " + baseTitle;
    std::wstring initial = utf8ToWide(dlg->app->profiles[index].name);

    const wchar_t CLASS_NAME[] = L"RenamePrompt";
    struct PromptState {
        HWND hwnd;
        HWND edit;
        std::wstring result;
        bool accepted = false;
    } state{};

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* st = reinterpret_cast<PromptState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            switch (m) {
            case WM_NCCREATE: {
                auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                st = reinterpret_cast<PromptState*>(cs->lpCreateParams);
                SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
                return TRUE;
            }
            case WM_CREATE: {
                HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                st->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                           10, 10, 220, 24, h, nullptr, nullptr, nullptr);
                SendMessageW(st->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                60, 44, 60, 24, h, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                                130, 44, 60, 24, h, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
                SetWindowTextW(st->edit, st->result.c_str());
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK) {
                    int len = GetWindowTextLengthW(st->edit);
                    std::wstring buf(static_cast<size_t>(len), L'\0');
                    GetWindowTextW(st->edit, buf.data(), len + 1);
                    st->result = buf;
                    st->accepted = true;
                    DestroyWindow(h);
                    return 0;
                } else if (LOWORD(wp) == IDCANCEL) {
                    DestroyWindow(h);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(h);
                return 0;
            }
            return DefWindowProcW(h, m, wp, lp);
        };
        wc.hInstance = dlg->app->hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    state.result = initial;
    HWND promptWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLASS_NAME, title.c_str(),
                                     WS_CAPTION | WS_SYSMENU | WS_POPUPWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 260, 120,
                                     dlg->hwnd, nullptr, dlg->app->hInstance, &state);
    if (!promptWnd) return false;

    SetWindowPos(promptWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
    SetForegroundWindow(promptWnd);
    MSG msg;
    while (IsWindow(promptWnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!state.accepted) return false;

    std::string newName = trimCopy(wideToUtf8(state.result));
    if (newName.empty()) return false;

    auto isDup = [&](const std::string& name) {
        for (size_t i = 0; i < dlg->app->profiles.size(); ++i) {
            if (i == static_cast<size_t>(index)) continue;
            if (namesEqual(dlg->app->profiles[i].name, name)) return true;
        }
        return false;
    };
    if (isDup(newName)) {
        MessageBoxW(dlg->hwnd, L"Name already exists.", L"TCapture", MB_ICONWARNING);
        return false;
    }

    dlg->app->profiles[index].name = newName;
    populateTabs(dlg);
    if (index == dlg->activeProfile) {
        SendMessageW(dlg->tab, LB_SETCURSEL, static_cast<WPARAM>(dlg->activeProfile), 0);
    }
    saveProfiles(*dlg->app);
    updateSettingsWindowTitle(dlg);
    return true;
}

std::wstring buildDisplayListText() {
    struct DisplayInfo {
        std::wstring name;
        RECT rect{};
    };

    std::vector<DisplayInfo> displays;
    auto enumProc = [](HMONITOR hMon, HDC, LPRECT, LPARAM lpData) -> BOOL {
        auto* list = reinterpret_cast<std::vector<DisplayInfo>*>(lpData);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(hMon, &info)) {
            return TRUE;
        }
        DisplayInfo d{};
        d.name = info.szDevice;
        d.rect = info.rcMonitor;
        list->push_back(d);
        return TRUE;
    };

    if (!EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&displays))) {
        return L"Failed to enumerate displays.";
    }
    if (displays.empty()) {
        return L"No displays detected.";
    }

    std::wostringstream oss;
    oss << L"Detected displays: " << displays.size() << L"\n";
    for (size_t i = 0; i < displays.size(); ++i) {
        const auto& d = displays[i];
        int width = d.rect.right - d.rect.left;
        int height = d.rect.bottom - d.rect.top;
        oss << L"\n[" << (i + 1) << L"] " << d.name
            << L"\n  Resolution: " << width << L"x" << height
            << L"\n  Position: (" << d.rect.left << L", " << d.rect.top << L")";
    }
    oss << L"\n\nNote: Numbers may not match Windows display settings.";
    return oss.str();
}

bool namesEqual(const std::string& a, const std::string& b) {
    return toLower(a) == toLower(b);
}

std::vector<ProfileSettings> mergeProfilesForSave(SettingsDialog* dlg, const ProfileSettings& activeProfile) {
    std::vector<ProfileSettings> merged;
    auto diskProfiles = loadSettingsProfiles(dlg->app->iniPath.string());
    if (!diskProfiles.empty()) {
        for (const auto& p : diskProfiles) {
            if (namesEqual(p.name, activeProfile.name)) {
                merged.push_back(activeProfile);
            } else {
                merged.push_back(p);
            }
        }
        for (const auto& p : dlg->app->profiles) {
            bool exists = false;
            for (const auto& m : merged) {
                if (namesEqual(m.name, p.name)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                merged.push_back(namesEqual(p.name, activeProfile.name) ? activeProfile : p);
            }
        }
    } else {
        merged = dlg->app->profiles;
        bool replaced = false;
        for (auto& p : merged) {
            if (namesEqual(p.name, activeProfile.name)) {
                p = activeProfile;
                replaced = true;
                break;
            }
        }
        if (!replaced) merged.push_back(activeProfile);
    }
    return merged;
}

void buildSettingsLayout(SettingsDialog* dlg) {
    static HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    RECT rc{};
    GetClientRect(dlg->hwnd, &rc);
    int margin = 8;
    int labelWidth = 88;
    int rowHeight = 24;
    int sidebarWidth = 116;
    int sidebarGap = 8;
    int buttonWidth = 34;
    int controlSpacing = 6;
    int sectionSpacing = 12;
    int groupPadding = 8;
    int groupHeader = 16;

    int clientWidth = static_cast<int>(rc.right - rc.left);
    int desiredFieldWidth = 230;
    int desiredClientWidth = margin * 2 + sidebarWidth + sidebarGap + groupPadding * 2 + labelWidth + desiredFieldWidth;
    int layoutWidth = desiredClientWidth;
    dlg->layoutClientWidth = layoutWidth;

    int listHeight = rowHeight * 6 + 6;
    dlg->tab = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                               margin, margin, sidebarWidth, listHeight, dlg->hwnd,
                               reinterpret_cast<HMENU>(120), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->tab, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->tab, false);

    int sidebarButtonWidth = (sidebarWidth - 6) / 2;
    int btnX = margin;
    int btnY = margin + listHeight + 6;
    dlg->addBtn = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE,
                                  btnX, btnY, sidebarButtonWidth, rowHeight, dlg->hwnd,
                                  reinterpret_cast<HMENU>(130), dlg->app->hInstance, nullptr);
    dlg->deleteBtn = CreateWindowExW(0, L"BUTTON", L"Del", WS_CHILD | WS_VISIBLE,
                                     btnX + sidebarButtonWidth + 6, btnY, sidebarButtonWidth, rowHeight, dlg->hwnd,
                                     reinterpret_cast<HMENU>(132), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->addBtn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(dlg->deleteBtn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->addBtn, false);
    applyControlTheme(dlg, dlg->deleteBtn, false);

    int contentX = margin + sidebarWidth + sidebarGap;
    int groupWidth = layoutWidth - contentX - margin;

    auto createLabel = [&](int yPos, const wchar_t* text, int xOffset = 0) -> HWND {
        HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOTIFY, contentX + xOffset, yPos + 5, labelWidth, rowHeight, dlg->hwnd, nullptr, dlg->app->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyControlTheme(dlg, h, false);
        return h;
    };
    auto createEdit = [&](int yPos, int width, int controlId, int xOffset = 0) -> HWND {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 contentX + labelWidth + xOffset, yPos, width, rowHeight, dlg->hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), dlg->app->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyControlTheme(dlg, h, true);
        return h;
    };
    auto createLabelAt = [&](int xPos, int yPos, int width, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOTIFY, xPos, yPos + 5, width, rowHeight, dlg->hwnd, nullptr, dlg->app->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyControlTheme(dlg, h, false);
        return h;
    };
    auto createEditAt = [&](int xPos, int yPos, int width, int controlId) -> HWND {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                 xPos, yPos, width, rowHeight, dlg->hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), dlg->app->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyControlTheme(dlg, h, true);
        return h;
    };
    auto createGroup = [&](int yPos, int height, const std::wstring& title) -> HWND {
        HWND h = CreateWindowExW(0, L"BUTTON", title.c_str(), WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                 contentX, yPos, groupWidth, height, dlg->hwnd,
                                 nullptr, dlg->app->hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        applyControlTheme(dlg, h, false);
        return h;
    };
    auto addFieldTip = [&](HWND labelHwnd, HWND fieldHwnd, const std::wstring& tip) {
        if (!tip.empty()) {
            addTooltip(dlg, labelHwnd, tip);
            if (fieldHwnd) addTooltip(dlg, fieldHwnd, tip);
        }
    };

    int y = margin;
    HWND hint = CreateWindowExW(0, L"STATIC", translateId(*dlg->app, L"hint_tabs", L"Right-click a tab to rename. Changes save instantly.").c_str(), WS_CHILD | WS_VISIBLE,
                                contentX, y, groupWidth, rowHeight, dlg->hwnd, nullptr, dlg->app->hInstance, nullptr);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, hint, false);
    y += rowHeight + 4;

    // Language is controlled via tray menu; no selector in the dialog.

    // Save settings section
    int sectionHeight = groupHeader + groupPadding * 2 + rowHeight * 2 + controlSpacing;
    createGroup(y, sectionHeight, translateId(*dlg->app, L"save_group", L"Save settings").c_str());
    int sectionY = y + groupHeader + groupPadding;
    int fieldX = contentX + groupPadding + labelWidth;
    int fieldWidth = groupWidth - groupPadding * 2 - labelWidth;
    int innerWidth = groupWidth - groupPadding * 2;

    HWND lblOutput = createLabel(sectionY, translateId(*dlg->app, L"output_dir", L"Folder").c_str(), groupPadding);
    int browseBtnWidth = 32;
    int outputEditWidth = std::max(0, fieldWidth - browseBtnWidth - 6);
    dlg->outputEdit = createEdit(sectionY, outputEditWidth, 101, groupPadding);
    HWND browseBtn = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE,
                                     fieldX + outputEditWidth + 4, sectionY,
                                     browseBtnWidth, rowHeight, dlg->hwnd,
                                     reinterpret_cast<HMENU>(140), dlg->app->hInstance, nullptr);
    SendMessageW(browseBtn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, browseBtn, false);
    addFieldTip(lblOutput, dlg->outputEdit, translateId(*dlg->app, L"example_output", L"Example: C:\\captures or .\\shots (relative)").c_str());
    sectionY += rowHeight + controlSpacing;

    // Format + compression/quality on a single row (toggle by format)
    int formatLabelWidth = (layoutWidth < 520) ? 64 : 70;
    int compLabelWidth = (layoutWidth < 520) ? 70 : 90;
    int gapSmall = 4;
    int gapMid = (layoutWidth < 520) ? 6 : 10;
    int remainingFmt = std::max(0, innerWidth - (formatLabelWidth + compLabelWidth) - gapSmall * 2 - gapMid);
    int fmtComboWidth = remainingFmt / 2;
    int compComboWidth = remainingFmt - fmtComboWidth;
    int startX = contentX + groupPadding;
    int x = startX;

    HWND lblFmtInline = createLabelAt(x, sectionY, formatLabelWidth, translateId(*dlg->app, L"format", L"Format").c_str());
    x += formatLabelWidth + gapSmall;
    dlg->formatCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL,
                                       x, sectionY, fmtComboWidth, rowHeight * 6, dlg->hwnd,
                                       reinterpret_cast<HMENU>(102), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->formatCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->formatCombo, false);
    SendMessageW(dlg->formatCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"png"));
    SendMessageW(dlg->formatCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"jpg"));
    addFieldTip(lblFmtInline, dlg->formatCombo, L"");

    x += fmtComboWidth + gapMid;
    int compX = x;
    dlg->compressionLabelPng = createLabelAt(compX, sectionY, compLabelWidth, translateId(*dlg->app, L"compression_png", L"PNG compression").c_str());
    dlg->compressionLabelJpg = createLabelAt(compX, sectionY, compLabelWidth, translateId(*dlg->app, L"compression_jpg", L"JPG quality").c_str());
    x += compLabelWidth + gapSmall;
    dlg->compressionEditPng = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                              x, sectionY, compComboWidth, rowHeight, dlg->hwnd,
                                              reinterpret_cast<HMENU>(103), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->compressionEditPng, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->compressionEditPng, true);
    dlg->compressionEditJpg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                              x, sectionY, compComboWidth, rowHeight, dlg->hwnd,
                                              reinterpret_cast<HMENU>(113), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->compressionEditJpg, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->compressionEditJpg, true);
    addFieldTip(dlg->compressionLabelPng, dlg->compressionEditPng, L"");
    addFieldTip(dlg->compressionLabelJpg, dlg->compressionEditJpg, L"");

    sectionY += rowHeight + controlSpacing;
    y += sectionHeight + sectionSpacing;

    // Burst capture section
    sectionHeight = groupHeader + groupPadding * 2 + rowHeight;
    createGroup(y, sectionHeight, translateId(*dlg->app, L"burst_group", L"Burst capture").c_str());
    sectionY = y + groupHeader + groupPadding;

    int fpsLabelWidth = (layoutWidth < 520) ? 56 : 68;
    int burstLabelWidth = (layoutWidth < 520) ? 100 : 120;
    int spacing1 = 4;
    int spacing2 = (layoutWidth < 520) ? 6 : 10;
    int gaps = spacing1 * 2 + spacing2;
    int remainingBurst = std::max(0, innerWidth - fpsLabelWidth - burstLabelWidth - gaps);
    int fpsComboWidth = remainingBurst / 2;
    int burstSecondsWidth = remainingBurst - fpsComboWidth;
    int startBurstX = contentX + groupPadding;

    HWND lblBurstFps = createLabelAt(startBurstX, sectionY, fpsLabelWidth,
                                     translateId(*dlg->app, L"burst_fps_label", L"FPS").c_str());
    int burstX = startBurstX + fpsLabelWidth + spacing1;
    dlg->burstFpsCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                         burstX, sectionY, fpsComboWidth, rowHeight * 7, dlg->hwnd,
                                         reinterpret_cast<HMENU>(107), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->burstFpsCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->burstFpsCombo, false);
    populateBurstFpsCombo(dlg->burstFpsCombo);
    SendMessageW(dlg->burstFpsCombo, CB_SETCURSEL, 0, 0);
    SendMessageW(dlg->burstFpsCombo, CB_SETMINVISIBLE, 7, 0);
    addFieldTip(lblBurstFps, dlg->burstFpsCombo, translateId(*dlg->app, L"burst_note", L"Set FPS=0 to disable burst").c_str());

    burstX += fpsComboWidth + spacing2;
    HWND lblBurstSeconds = createLabelAt(burstX, sectionY, burstLabelWidth,
                                         translateId(*dlg->app, L"burst_seconds_label", L"Duration (sec)").c_str());
    dlg->burstSecondsEdit = createEditAt(burstX + burstLabelWidth, sectionY, burstSecondsWidth, 108);
    addFieldTip(lblBurstSeconds, dlg->burstSecondsEdit, translateId(*dlg->app, L"burst_note", L"Set FPS=0 to disable burst").c_str());
    y += sectionHeight + sectionSpacing;

    // Auto capture section
    sectionHeight = groupHeader + groupPadding * 2 + rowHeight;
    createGroup(y, sectionHeight, translateId(*dlg->app, L"auto_group", L"Auto capture").c_str());
    sectionY = y + groupHeader + groupPadding;
    int labelAutoWidth = 92;
    int buttonWidthAuto = 88;
    int spacingAuto = 6;
    int autoIntervalWidth = std::max(0, innerWidth - labelAutoWidth - buttonWidthAuto - spacingAuto);
    HWND lblAuto = createLabelAt(contentX + groupPadding, sectionY, labelAutoWidth,
                                 translateId(*dlg->app, L"auto_interval_label", L"Interval (sec)").c_str());
    dlg->autoIntervalEdit = createEditAt(contentX + groupPadding + labelAutoWidth, sectionY, autoIntervalWidth, 110);
    dlg->autoCaptureCheck = CreateWindowExW(0, L"BUTTON", translateId(*dlg->app, L"enable", L"Enable").c_str(),
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
                                            contentX + groupPadding + labelAutoWidth + autoIntervalWidth + spacingAuto, sectionY,
                                            buttonWidthAuto, rowHeight, dlg->hwnd,
                                            reinterpret_cast<HMENU>(109), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->autoCaptureCheck, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->autoCaptureCheck, false);
    addFieldTip(lblAuto, dlg->autoIntervalEdit, translateId(*dlg->app, L"auto_note", L"Check to start, uncheck to stop. Per-profile setting.").c_str());
    addFieldTip(lblAuto, dlg->autoCaptureCheck, translateId(*dlg->app, L"auto_note", L"Check to start, uncheck to stop. Per-profile setting.").c_str());
    y += sectionHeight + sectionSpacing;

    // Target and hotkey section
    sectionHeight = groupHeader + groupPadding * 2 + rowHeight * 2 + controlSpacing;
    createGroup(y, sectionHeight, translateId(*dlg->app, L"targets_group", L"Capture target").c_str());
    sectionY = y + groupHeader + groupPadding;

    HWND lblDisplays = createLabel(sectionY, translateId(*dlg->app, L"displays", L"Display").c_str(), groupPadding);
    int displayBtnWidth = 44;
    int displayEditWidth = std::max(0, fieldWidth - displayBtnWidth - 6);
    dlg->displaysEdit = createEdit(sectionY, displayEditWidth, 104, groupPadding);
    dlg->displaysHelpBtn = CreateWindowExW(0, L"BUTTON", L"List", WS_CHILD | WS_VISIBLE,
                                           fieldX + displayEditWidth + 4, sectionY,
                                           displayBtnWidth, rowHeight, dlg->hwnd,
                                           reinterpret_cast<HMENU>(141), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->displaysHelpBtn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->displaysHelpBtn, false);
    addFieldTip(lblDisplays, dlg->displaysEdit, translateId(*dlg->app, L"displays_examples", L"Examples: all / 1,3 / active_display / active_window").c_str());
    sectionY += rowHeight + controlSpacing;

    HWND lblHotkey = createLabel(sectionY, translateId(*dlg->app, L"hotkey", L"Hotkey").c_str(), groupPadding);
    int modWidth = std::max(0, fieldWidth / 2 - 4);
    int keyWidth = std::max(0, fieldWidth - modWidth - 8);
    dlg->hotkeyModCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                          fieldX, sectionY, modWidth, rowHeight * 8, dlg->hwnd,
                                          reinterpret_cast<HMENU>(105), dlg->app->hInstance, nullptr);
    dlg->hotkeyKeyCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                          fieldX + modWidth + 8, sectionY, keyWidth, rowHeight * 14, dlg->hwnd,
                                          reinterpret_cast<HMENU>(106), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->hotkeyModCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(dlg->hotkeyKeyCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->hotkeyModCombo, false);
    applyControlTheme(dlg, dlg->hotkeyKeyCombo, false);
    SendMessageW(dlg->hotkeyModCombo, CB_SETEXTENDEDUI, TRUE, 0);
    SendMessageW(dlg->hotkeyKeyCombo, CB_SETEXTENDEDUI, TRUE, 0);
    for (const auto& opt : modOptions()) {
        SendMessageW(dlg->hotkeyModCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt.label.c_str()));
    }
    for (const auto& opt : keyOptions()) {
        SendMessageW(dlg->hotkeyKeyCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(opt.label.c_str()));
    }
    SendMessageW(dlg->hotkeyModCombo, CB_SETMINVISIBLE, 8, 0);
    SendMessageW(dlg->hotkeyKeyCombo, CB_SETMINVISIBLE, 14, 0);
    addFieldTip(lblHotkey, dlg->hotkeyModCombo, L"");
    addFieldTip(lblHotkey, dlg->hotkeyKeyCombo, L"");
    y += sectionHeight + controlSpacing;

    // Status bar
    updateStatusBrush(dlg);
    int statusWidth = groupWidth;
    dlg->autoStatusLabel = CreateWindowExW(0, L"STATIC", translateId(*dlg->app, L"auto_stopped", L"Auto capture: stopped").c_str(), WS_CHILD | WS_VISIBLE | SS_CENTER,
                                           contentX, y, statusWidth, rowHeight + 6,
                                           dlg->hwnd, reinterpret_cast<HMENU>(111), dlg->app->hInstance, nullptr);
    SendMessageW(dlg->autoStatusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    applyControlTheme(dlg, dlg->autoStatusLabel, false);
    dlg->layoutClientHeight = y + rowHeight + 6 + margin;
}

bool persistActiveProfile(SettingsDialog* dlg, bool showErrors) {
    if (dlg->suppressSave) return true;
    if (!saveControlsToProfile(dlg, dlg->activeProfile, showErrors)) return false;
    normalizeSettings(dlg->app->profiles[dlg->activeProfile].settings);
    if (dlg->activeProfile < 0 || dlg->activeProfile >= static_cast<int>(dlg->app->profiles.size())) {
        dlg->activeProfile = static_cast<int>(std::max<size_t>(dlg->app->profiles.size(), 1)) - 1;
    }
    if (dlg->app->hotkeyIds.size() != dlg->app->profiles.size()) {
        dlg->app->hotkeyIds.assign(dlg->app->profiles.size(), 0);
    }
    int tabCount = static_cast<int>(SendMessageW(dlg->tab, LB_GETCOUNT, 0, 0));
    if (tabCount != static_cast<int>(dlg->app->profiles.size())) {
        populateTabs(dlg);
    }
    SendMessageW(dlg->tab, LB_SETCURSEL, static_cast<WPARAM>(dlg->activeProfile), 0);
    if (!saveProfiles(*dlg->app)) {
        if (showErrors) {
            MessageBoxW(dlg->hwnd, translateId(*dlg->app, L"write_failed", L"Failed to write TCapture.ini.").c_str(),
                        translateId(*dlg->app, L"app_name", L"TCapture").c_str(), MB_ICONERROR);
        }
        return false;
    }
    return true;
}

bool persistActiveProfileNoUI(SettingsDialog* dlg, bool showErrors) {
    if (dlg->suppressSave) return true;
    if (!saveControlsToProfile(dlg, dlg->activeProfile, showErrors)) return false;
    normalizeSettings(dlg->app->profiles[dlg->activeProfile].settings);
    if (dlg->activeProfile < 0 || dlg->activeProfile >= static_cast<int>(dlg->app->profiles.size())) {
        dlg->activeProfile = static_cast<int>(std::max<size_t>(dlg->app->profiles.size(), 1)) - 1;
    }
    if (dlg->app->hotkeyIds.size() != dlg->app->profiles.size()) {
        dlg->app->hotkeyIds.assign(dlg->app->profiles.size(), 0);
    }
    if (!saveProfiles(*dlg->app)) {
        if (showErrors) {
            MessageBoxW(dlg->hwnd, translateId(*dlg->app, L"write_failed", L"Failed to write TCapture.ini.").c_str(),
                        translateId(*dlg->app, L"app_name", L"TCapture").c_str(), MB_ICONERROR);
        }
        return false;
    }
    loadLanguage(*dlg->app, dlg->app->language);
    registerAllHotkeys(*dlg->app);
    updateAutoCaptureTimers(*dlg->app);
    updateAutoStatusLabel(dlg);
    return true;
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* dlg = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        dlg = reinterpret_cast<SettingsDialog*>(cs->lpCreateParams);
        dlg->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
        return TRUE;
    }
    case WM_CREATE:
        if (dlg && !dlg->backgroundBrush) {
            dlg->backgroundColor = GetSysColor(COLOR_BTNFACE);
            dlg->backgroundBrush = CreateSolidBrush(dlg->backgroundColor);
            dlg->editBackgroundColor = GetSysColor(COLOR_WINDOW);
            dlg->editBackgroundBrush = CreateSolidBrush(dlg->editBackgroundColor);
        }
        buildSettingsLayout(dlg);
        adjustSettingsWindowSize(dlg);
        populateTabs(dlg);
        loadProfileToControls(dlg, dlg->activeProfile);
        return 0;
    case WM_ERASEBKGND: {
        if (dlg && dlg->backgroundBrush) {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, dlg->backgroundBrush);
            return 1;
        }
        break;
    }
    case WM_NOTIFY:
        break;
    case WM_CONTEXTMENU: {
        if (!dlg) break;
        HWND target = reinterpret_cast<HWND>(wParam);
        if (target == dlg->tab) {
            int hit = -1;
            if (lParam == -1) {
                hit = static_cast<int>(SendMessageW(dlg->tab, LB_GETCURSEL, 0, 0));
            } else {
                POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                POINT listPt = screenPt;
                ScreenToClient(dlg->tab, &listPt);
                DWORD res = static_cast<DWORD>(SendMessageW(dlg->tab, LB_ITEMFROMPOINT, 0, MAKELPARAM(listPt.x, listPt.y)));
                hit = LOWORD(res);
                if (HIWORD(res) != 0) hit = -1;
            }
            if (hit >= 0) {
                promptRenameProfile(dlg, hit);
                return 0;
            }
        }
        break;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_CTLCOLORDLG: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (dlg && dlg->backgroundBrush) {
            SetBkColor(hdc, dlg->backgroundColor);
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(dlg->backgroundBrush);
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND target = reinterpret_cast<HWND>(lParam);
        if (dlg && target == dlg->autoStatusLabel) {
            if (dlg->autoStatusActive) {
                SetTextColor(hdc, RGB(160, 0, 0));
            }
            if (dlg->backgroundBrush) {
                SetBkColor(hdc, dlg->backgroundColor);
                SetBkMode(hdc, OPAQUE);
                return reinterpret_cast<INT_PTR>(dlg->backgroundBrush);
            }
        }
        if (dlg && dlg->backgroundBrush) {
            SetBkColor(hdc, dlg->backgroundColor);
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(dlg->backgroundBrush);
        }
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (dlg && dlg->editBackgroundBrush) {
            SetBkColor(hdc, dlg->editBackgroundColor);
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(dlg->editBackgroundBrush);
        }
        break;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (dlg && dlg->backgroundBrush) {
            SetBkColor(hdc, dlg->backgroundColor);
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(dlg->backgroundBrush);
        }
        break;
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (dlg && dlg->backgroundBrush) {
            SetBkColor(hdc, dlg->backgroundColor);
            SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<INT_PTR>(dlg->backgroundBrush);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 120: // profile list
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(dlg->tab, LB_GETCURSEL, 0, 0));
                if (!persistActiveProfileNoUI(dlg, true)) {
                    SendMessageW(dlg->tab, LB_SETCURSEL, static_cast<WPARAM>(dlg->activeProfile), 0);
                    return 0;
                }
                if (sel >= 0) {
                    dlg->activeProfile = sel;
                    loadProfileToControls(dlg, sel);
                }
            } else if (HIWORD(wParam) == LBN_DBLCLK) {
                int sel = static_cast<int>(SendMessageW(dlg->tab, LB_GETCURSEL, 0, 0));
                if (sel < 0) sel = dlg->activeProfile;
                promptRenameProfile(dlg, sel);
            }
            return 0;
        case 130: { // Add
            saveControlsToProfile(dlg, dlg->activeProfile, false);
            ProfileSettings base = dlg->app->profiles.empty() ? ProfileSettings{} : dlg->app->profiles[dlg->activeProfile];
            base.settings.captureHotkey.clear(); // start new profile with no hotkey to avoid conflicts
            std::string newNameBase = "profile";
            int suffix = static_cast<int>(dlg->app->profiles.size()) + 1;
            auto isDuplicate = [&](const std::string& name) {
                for (const auto& p : dlg->app->profiles) {
                    if (toLower(p.name) == toLower(name)) return true;
                }
                return false;
            };
            while (isDuplicate(newNameBase + std::to_string(suffix))) {
                ++suffix;
            }
            base.name = newNameBase + std::to_string(suffix);
            dlg->app->profiles.push_back(base);
            dlg->app->hotkeyIds.resize(dlg->app->profiles.size(), 0);
            dlg->app->autoTimerIds.resize(dlg->app->profiles.size(), 0);
            dlg->activeProfile = static_cast<int>(dlg->app->profiles.size() - 1);
            populateTabs(dlg);
            loadProfileToControls(dlg, dlg->activeProfile);
            persistActiveProfile(dlg, false);
            return 0;
        }
        case 132: { // Delete
            if (dlg->app->profiles.size() <= 1) {
                MessageBoxW(dlg->hwnd, translateId(*dlg->app, L"profile_required", L"At least one profile must remain.").c_str(),
                            translateId(*dlg->app, L"app_name", L"TCapture").c_str(), MB_ICONINFORMATION);
                return 0;
            }
            int idx = dlg->activeProfile;
            if (idx < 0 || idx >= static_cast<int>(dlg->app->profiles.size())) return 0;
            std::wstring prompt = L"Delete profile \"" + utf8ToWide(dlg->app->profiles[idx].name) + L"\"?";
            int confirm = MessageBoxW(dlg->hwnd, prompt.c_str(), L"Confirm delete", MB_ICONQUESTION | MB_YESNO);
            if (confirm != IDYES) return 0;
            dlg->app->profiles.erase(dlg->app->profiles.begin() + idx);
            dlg->app->hotkeyIds.resize(dlg->app->profiles.size(), 0);
            dlg->app->autoTimerIds.resize(dlg->app->profiles.size(), 0);
            if (idx >= static_cast<int>(dlg->app->profiles.size())) {
                idx = static_cast<int>(dlg->app->profiles.size()) - 1;
            }
            dlg->activeProfile = idx;
            populateTabs(dlg);
            loadProfileToControls(dlg, dlg->activeProfile);
            persistActiveProfile(dlg, false);
            return 0;
        }
        case 101: // output dir
        case 104: // displays
        case 108: // burst seconds
            if (HIWORD(wParam) == EN_CHANGE) {
                if (dlg->suppressSave) return 0;
                persistActiveProfile(dlg, false);
            }
            return 0;
        case 110: // auto interval
            if (HIWORD(wParam) == EN_CHANGE) {
                if (dlg->suppressSave) return 0;
                persistActiveProfileNoUI(dlg, false);
            }
            return 0;
        case 102: // format
        case 105: // hotkey modifiers
        case 106: // hotkey key
        case 107: // burst fps
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                if (LOWORD(wParam) == 102) {
                    updateCompressionFields(dlg);
                }
                if (dlg->suppressSave) return 0;
                persistActiveProfile(dlg, false);
            }
            return 0;
        case 103: // png compression edit
            if (HIWORD(wParam) == EN_CHANGE) {
                if (dlg->suppressSave) return 0;
                persistActiveProfile(dlg, false);
            }
            return 0;
        case 113: // jpg quality edit
            if (HIWORD(wParam) == EN_CHANGE) {
                if (dlg->suppressSave) return 0;
                persistActiveProfile(dlg, false);
            }
            return 0;
        case 140: { // browse output dir
            std::wstring initial = utf8ToWide(trimCopy(getControlText(dlg->outputEdit)));
            std::wstring title = translateId(*dlg->app, L"output_dir", L"Output dir");
            std::wstring selected = browseForFolder(dlg->hwnd, initial, title);
            if (!selected.empty()) {
                dlg->suppressSave = true;
                setControlText(dlg->outputEdit, wideToUtf8(selected));
                dlg->suppressSave = false;
                persistActiveProfile(dlg, false);
            }
            return 0;
        }
        case 141: { // list displays
            std::wstring info = buildDisplayListText();
            MessageBoxW(dlg->hwnd, info.c_str(), translateId(*dlg->app, L"displays", L"Display").c_str(), MB_ICONINFORMATION);
            return 0;
        }
        case 109: // auto capture
            if (HIWORD(wParam) == BN_CLICKED) {
                if (dlg->suppressSave) return 0;
                bool wasEnabled = false;
                if (dlg->activeProfile >= 0 && dlg->activeProfile < static_cast<int>(dlg->app->profiles.size())) {
                    wasEnabled = dlg->app->profiles[dlg->activeProfile].settings.autoCapture;
                }
                bool isEnabled = (SendMessageW(dlg->autoCaptureCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (persistActiveProfileNoUI(dlg, false) && !wasEnabled && isEnabled) {
                    triggerCapture(*dlg->app, dlg->activeProfile);
                }
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (dlg) {
            if (dlg->tooltip) DestroyWindow(dlg->tooltip);
            if (dlg->statusBrushActive) DeleteObject(dlg->statusBrushActive);
            if (dlg->statusBrushInactive) DeleteObject(dlg->statusBrushInactive);
            if (dlg->backgroundBrush) DeleteObject(dlg->backgroundBrush);
            if (dlg->editBackgroundBrush) DeleteObject(dlg->editBackgroundBrush);
            dlg->app->settingsWindow = nullptr;
            delete dlg;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND showSettingsWindow(AppState& app) {
    if (app.settingsWindow) {
        ShowWindow(app.settingsWindow, SW_RESTORE);
        ShowWindow(app.settingsWindow, SW_SHOW);
        SetForegroundWindow(app.settingsWindow);
        return app.settingsWindow;
    }

    const wchar_t* CLASS_NAME = kSettingsWindowClass;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = app.hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hIcon = app.nid.hIcon;
        RegisterClassW(&wc);
        registered = true;
    }

    SettingsDialog* dlg = new SettingsDialog();
    dlg->app = &app;
    dlg->activeProfile = 0;
    std::wstring appTitle = translateId(app, L"app_name", L"TCapture");
    std::wstring settingsTitle = translateId(app, L"settings_title", L"TCapture settings");
    std::wstring windowTitle = appTitle + L" - " + settingsTitle;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, CLASS_NAME, windowTitle.c_str(),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 720, 660,
                                nullptr, nullptr, app.hInstance, dlg);
    if (hwnd) {
        app.settingsWindow = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    } else {
        delete dlg;
    }
    return hwnd;
}

void showTrayMenu(AppState& app, POINT pt) {
    HMENU menu = CreatePopupMenu();
    HMENU captureMenu = CreatePopupMenu();
    for (size_t i = 0; i < app.profiles.size(); ++i) {
        std::wstring label = translateId(app, L"capture_prefix", L"Capture: ") + utf8ToWide(app.profiles[i].name);
        AppendMenuW(captureMenu, MF_STRING, ID_TRAY_CAPTURE_BASE + static_cast<UINT>(i), label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(captureMenu), translateId(app, L"capture", L"Capture").c_str());
    HMENU langMenu = CreatePopupMenu();
    UINT_PTR langBase = ID_TRAY_LANG_BASE;
    for (size_t i = 0; i < app.availableLanguages.size(); ++i) {
        const auto& code = app.availableLanguages[i];
        UINT flags = (toLower(code) == toLower(app.language)) ? MF_CHECKED : MF_UNCHECKED;
        AppendMenuW(langMenu, MF_STRING | flags, langBase + static_cast<UINT_PTR>(i), languageLabel(code).c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu), translateId(app, L"language_label", L"Language").c_str());
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, translateId(app, L"settings_menu", L"Settings...").c_str());
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN_OUTPUT, translateId(app, L"open_output_menu", L"Open output folder").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, translateId(app, L"exit_menu", L"Exit").c_str());

    SetForegroundWindow(app.mainWindow);
    TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, app.mainWindow, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<AppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return TRUE;
    }
    case WM_CREATE:
        app->mainWindow = hwnd;
        if (app->enableTray) {
            buildTrayIcon(*app);
        }
        registerAllHotkeys(*app);
        updateAutoCaptureTimers(*app);
        if (app->startWithSettings) {
            PostMessageW(hwnd, WM_APP_SHOW_SETTINGS, 0, 0);
        }
        return 0;
    case WM_COMMAND: {
        UINT id = LOWORD(wParam);
        if (id >= ID_TRAY_CAPTURE_BASE && id < ID_TRAY_CAPTURE_BASE + app->profiles.size()) {
            triggerCapture(*app, static_cast<int>(id - ID_TRAY_CAPTURE_BASE));
        } else {
            switch (id) {
            case ID_TRAY_SETTINGS:
                showSettingsWindow(*app);
                break;
            case ID_TRAY_OPEN_OUTPUT:
                openOutputFolder(*app, 0);
                break;
            case ID_TRAY_EXIT:
                DestroyWindow(hwnd);
                break;
            default:
                break;
            }
            if (id >= ID_TRAY_LANG_BASE && id < ID_TRAY_LANG_BASE + app->availableLanguages.size()) {
                size_t idx = id - ID_TRAY_LANG_BASE;
                if (idx < app->availableLanguages.size()) {
                    std::string lang = app->availableLanguages[idx];
                    setLanguage(*app, lang);
                    if (app->settingsWindow) {
                        DestroyWindow(app->settingsWindow);
                        app->settingsWindow = nullptr;
                        showSettingsWindow(*app);
                    }
                    showNotification(*app, translateId(*app, L"app_name", L"TCapture"),
                                    translateId(*app, L"language_changed", L"Language changed."));
                }
            }
        }
        return 0;
    }
    case WM_APP_CAPTURE_REQUEST: {
        int index = static_cast<int>(wParam);
        if (index >= 0 && index < static_cast<int>(app->profiles.size())) {
            triggerCapture(*app, index);
        }
        return 0;
    }
    case WM_APP_SHOW_SETTINGS:
        showSettingsWindow(*app);
        return 0;
    case WM_HOTKEY: {
        UINT id = static_cast<UINT>(wParam);
        if (id >= HOTKEY_ID_BASE) {
            size_t index = id - HOTKEY_ID_BASE;
            if (index < app->profiles.size()) {
                triggerCapture(*app, static_cast<int>(index));
            }
        }
        return 0;
    }
    case WM_TIMER: {
        UINT_PTR id = static_cast<UINT_PTR>(wParam);
        if (id >= TIMER_AUTO_BASE) {
            size_t index = id - TIMER_AUTO_BASE;
            if (index < app->profiles.size()) {
                triggerCapture(*app, static_cast<int>(index));
            }
        }
        return 0;
    }
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            showTrayMenu(*app, pt);
        } else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            showSettingsWindow(*app);
        }
        return 0;
    case WM_DESTROY:
        if (app->enableTray) {
            Shell_NotifyIcon(NIM_DELETE, &app->nid);
        }
        unregisterAllHotkeys(*app);
        stopAutoCaptureTimers(*app);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

enum class LaunchMode {
    Capture,
    Agent,
    Settings,
};

static LaunchMode detectLaunchMode(const std::vector<std::string>& args)
{
    for (size_t i = 1; i < args.size(); ++i) {
        std::string a = toLower(args[i]);
        if (a == "--settings") return LaunchMode::Settings;
        if (a == "--agent") return LaunchMode::Agent;
    }
    return LaunchMode::Capture;
}

static std::string detectPreferredLanguage(const std::vector<std::string>& args)
{
    for (size_t i = 1; i < args.size(); ++i) {
        std::string a = toLower(args[i]);
        if (a.rfind("--lang=", 0) == 0) {
            std::string v = a.substr(7);
            if (v == "ja" || v == "en") return v;
        }
        if (a == "--lang" && i + 1 < args.size()) {
            std::string v = toLower(args[i + 1]);
            if (v == "ja" || v == "en") return v;
        }
    }
    return std::string();
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    int argcW = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
    std::vector<std::string> args;
    if (argvW && argcW > 0) {
        args.reserve(static_cast<size_t>(argcW));
        for (int i = 0; i < argcW; ++i) {
            args.push_back(wideToUtf8(argvW[i]));
        }
    } else {
        args.push_back("TCapture.exe");
    }

    LaunchMode mode = detectLaunchMode(args);
    std::string preferredLanguage = detectPreferredLanguage(args);
    if (mode == LaunchMode::Capture) {
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        if (argvW) LocalFree(argvW);
        return RunCaptureMain(static_cast<int>(argv.size()), argv.data());
    }

    INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    HANDLE singleMutex = CreateMutexW(nullptr, FALSE, kSingletonMutexName);
    if (!singleMutex) {
        if (argvW) LocalFree(argvW);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mode == LaunchMode::Settings) {
            HWND existing = FindWindowW(kAgentMainWindowClass, nullptr);
            if (existing) {
                PostMessageW(existing, WM_APP_SHOW_SETTINGS, 0, 0);
                SetForegroundWindow(existing);
            }
        }
        CloseHandle(singleMutex);
        if (argvW) LocalFree(argvW);
        return 0;
    }

    AppState app{};
    app.hInstance = hInstance;
    app.exeDir = getExecutableDir();
    app.enableTray = false;
    app.startWithSettings = (mode == LaunchMode::Settings);
    loadProfilesWithFallback(app, preferredLanguage);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kAgentMainWindowClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                             0, 0, LR_DEFAULTSIZE));
    if (!wc.hIcon) {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kAgentMainWindowClass, L"TCapture-Agent", WS_OVERLAPPED,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                nullptr, nullptr, hInstance, &app);
    if (!hwnd) {
        CloseHandle(singleMutex);
        if (argvW) LocalFree(argvW);
        return 1;
    }

    ShowWindow(hwnd, SW_HIDE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (app.settingsWindow && IsDialogMessageW(app.settingsWindow, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (singleMutex) {
        CloseHandle(singleMutex);
    }
    if (argvW) LocalFree(argvW);
    return 0;
}
