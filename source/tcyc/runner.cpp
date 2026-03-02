#include "runner.h"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <vector>

namespace tcyc {

namespace {

std::wstring NormalizePath(std::wstring s) {
    for (auto& ch : s) {
        if (ch == L'/') ch = L'\\';
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return s;
}

bool QueryProcessPath(DWORD pid, std::wstring& outPath) {
    outPath.clear();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t buf[MAX_PATH * 4] = {0};
    DWORD size = static_cast<DWORD>(_countof(buf));
    BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &size);
    CloseHandle(h);
    if (!ok || size == 0) return false;
    outPath.assign(buf, size);
    return true;
}

bool TryParseArgsFromCmdline(const std::wstring& cmdLine, std::vector<std::wstring>& outArgs) {
    outArgs.clear();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
    if (!argv || argc <= 0) {
        if (argv) LocalFree(argv);
        return false;
    }
    for (int i = 1; i < argc; ++i) outArgs.emplace_back(argv[i]);
    LocalFree(argv);
    return true;
}

std::wstring ToLowerString(std::wstring s) {
    for (auto& ch : s) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return s;
}

bool EqualArgTokensCaseInsensitive(const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ToLowerString(a[i]) != ToLowerString(b[i])) return false;
    }
    return true;
}

bool QueryProcessCommandLine(DWORD pid, std::wstring& outCmdLine) {
    outCmdLine.clear();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return false;

    auto cleanup = [&]() {
        CloseHandle(h);
    };

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        cleanup();
        return false;
    }

    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    auto ntQueryInformationProcess =
        reinterpret_cast<NtQueryInformationProcessFn>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!ntQueryInformationProcess) {
        cleanup();
        return false;
    }

    PROCESS_BASIC_INFORMATION pbi{};
    NTSTATUS st = ntQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr);
    if (st < 0 || !pbi.PebBaseAddress) {
        cleanup();
        return false;
    }

    PEB peb{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(h, pbi.PebBaseAddress, &peb, sizeof(peb), &bytesRead) || bytesRead < sizeof(peb)) {
        cleanup();
        return false;
    }
    if (!peb.ProcessParameters) {
        cleanup();
        return false;
    }

    RTL_USER_PROCESS_PARAMETERS procParams{};
    if (!ReadProcessMemory(h, peb.ProcessParameters, &procParams, sizeof(procParams), &bytesRead) ||
        bytesRead < sizeof(procParams)) {
        cleanup();
        return false;
    }
    if (!procParams.CommandLine.Buffer || procParams.CommandLine.Length == 0) {
        cleanup();
        return false;
    }

    const size_t wcharCount = static_cast<size_t>(procParams.CommandLine.Length / sizeof(wchar_t));
    std::wstring cmdLine(wcharCount, L'\0');
    if (!ReadProcessMemory(
            h,
            procParams.CommandLine.Buffer,
            cmdLine.data(),
            procParams.CommandLine.Length,
            &bytesRead) ||
        bytesRead < procParams.CommandLine.Length) {
        cleanup();
        return false;
    }

    cleanup();
    outCmdLine.swap(cmdLine);
    return true;
}

std::vector<std::wstring> BuildExpectedArgs(const TaskConfig& task) {
    if (task.actionArgs.empty()) return {};
    std::vector<std::wstring> expected;
    std::wstring synthetic = L"dummy.exe ";
    synthetic.append(task.actionArgs);
    if (!TryParseArgsFromCmdline(synthetic, expected)) expected.clear();
    return expected;
}

} // namespace

bool IsTaskProcessRunning(const TaskConfig& task, ProcessMatchMode* outMode, bool forceCmdlineReadFailForTest) {
    if (outMode) *outMode = ProcessMatchMode::None;
    if (task.actionPath.empty()) return false;

    const std::wstring target = NormalizePath(task.actionPath);
    const bool requireArgsMatch = task.watchdogRequireArgsMatch && !task.actionArgs.empty();
    const std::vector<std::wstring> expectedArgs = BuildExpectedArgs(task);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == GetCurrentProcessId()) continue;
            std::wstring p;
            if (!QueryProcessPath(pe.th32ProcessID, p)) continue;
            if (NormalizePath(p) != target) continue;

            if (!requireArgsMatch) {
                found = true;
                if (outMode) *outMode = ProcessMatchMode::PathOnly;
                break;
            }

            std::wstring cmdLine;
            if (forceCmdlineReadFailForTest || !QueryProcessCommandLine(pe.th32ProcessID, cmdLine)) {
                found = true;
                if (outMode) *outMode = ProcessMatchMode::PathOnlyFallbackArgsUnavailable;
                break;
            }

            std::vector<std::wstring> actualArgs;
            if (!TryParseArgsFromCmdline(cmdLine, actualArgs)) {
                found = true;
                if (outMode) *outMode = ProcessMatchMode::PathOnlyFallbackArgsUnavailable;
                break;
            }

            if (EqualArgTokensCaseInsensitive(expectedArgs, actualArgs)) {
                found = true;
                if (outMode) *outMode = ProcessMatchMode::PathAndArgs;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool LaunchTask(const TaskConfig& task, std::wstring& outError) {
    outError.clear();
    if (task.actionPath.empty()) {
        outError = L"ActionPath is empty";
        return false;
    }

    std::wstring cmdLine = L"\"";
    cmdLine.append(task.actionPath);
    cmdLine.append(L"\"");
    if (!task.actionArgs.empty()) {
        cmdLine.push_back(L' ');
        cmdLine.append(task.actionArgs);
    }

    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    LPCWSTR cwd = task.actionCwd.empty() ? nullptr : task.actionCwd.c_str();
    BOOL ok = CreateProcessW(
        task.actionPath.c_str(),
        mutableCmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        cwd,
        &si,
        &pi);
    if (!ok) {
        wchar_t msg[128] = {0};
        swprintf_s(msg, L"CreateProcessW failed: %lu", GetLastError());
        outError = msg;
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace tcyc
