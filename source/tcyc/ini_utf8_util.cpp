#include "ini_utf8_util.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace tcyc {
namespace {

std::wstring Trim(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return s;
}

bool ReadAllBytes(const std::wstring& path, std::vector<unsigned char>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 || li.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    const DWORD size = static_cast<DWORD>(li.QuadPart);
    out.resize(size);
    DWORD rd = 0;
    const BOOL ok = (size == 0) ? TRUE : ReadFile(h, out.data(), size, &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(rd);
    return true;
}

bool BytesToWideBestEffort(const std::vector<unsigned char>& bytes, std::wstring& out) {
    out.clear();
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const size_t chars = (bytes.size() - 2) / 2;
        out.resize(chars);
        for (size_t i = 0; i < chars; ++i) {
            out[i] = static_cast<wchar_t>(bytes[2 + i * 2] | (bytes[3 + i * 2] << 8));
        }
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        const size_t chars = (bytes.size() - 2) / 2;
        out.resize(chars);
        for (size_t i = 0; i < chars; ++i) {
            out[i] = static_cast<wchar_t>((bytes[2 + i * 2] << 8) | bytes[3 + i * 2]);
        }
        return true;
    }

    const int offset = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
    const char* p = reinterpret_cast<const char*>(bytes.data() + offset);
    const int n = static_cast<int>(bytes.size() - offset);
    if (n <= 0) return true;

    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, nullptr, 0);
    if (wlen > 0) {
        out.resize(static_cast<size_t>(wlen));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, out.data(), wlen);
        return true;
    }
    return false;
}

bool WideToUtf8(const std::wstring& s, std::string& out) {
    out.clear();
    if (s.empty()) return true;
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    return WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr) > 0;
}

void SplitLines(const std::wstring& text, std::vector<std::wstring>& lines) {
    lines.clear();
    size_t i = 0;
    while (i < text.size()) {
        size_t j = i;
        while (j < text.size() && text[j] != L'\r' && text[j] != L'\n') ++j;
        lines.push_back(text.substr(i, j - i));
        if (j < text.size() && text[j] == L'\r') ++j;
        if (j < text.size() && text[j] == L'\n') ++j;
        i = j;
    }
}

bool IsSectionLine(const std::wstring& line, std::wstring& outSection) {
    const std::wstring t = Trim(line);
    if (t.size() >= 3 && t.front() == L'[' && t.back() == L']') {
        outSection = Trim(t.substr(1, t.size() - 2));
        return !outSection.empty();
    }
    return false;
}

bool ParseKeyValueLine(const std::wstring& line, std::wstring& outKey, std::wstring& outValue) {
    const std::wstring t = Trim(line);
    if (t.empty() || t[0] == L';' || t[0] == L'#') return false;
    const size_t eq = t.find(L'=');
    if (eq == std::wstring::npos) return false;
    outKey = Trim(t.substr(0, eq));
    outValue = t.substr(eq + 1);
    return !outKey.empty();
}

bool LoadIniLines(const std::wstring& iniPath, std::vector<std::wstring>& lines) {
    std::vector<unsigned char> bytes;
    if (!ReadAllBytes(iniPath, bytes)) {
        lines.clear();
        return true;
    }
    std::wstring text;
    if (!BytesToWideBestEffort(bytes, text)) return false;
    SplitLines(text, lines);
    return true;
}

bool SaveIniLinesUtf8(const std::wstring& iniPath, const std::vector<std::wstring>& lines) {
    std::wstring joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        joined.append(lines[i]);
        joined.append(L"\r\n");
    }
    std::string utf8;
    if (!WideToUtf8(joined, utf8)) return false;

    HANDLE h = CreateFileW(iniPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD wr = 0;
    if (!WriteFile(h, bom, 3, &wr, nullptr) || wr != 3) {
        CloseHandle(h);
        return false;
    }
    if (!utf8.empty()) {
        if (!WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &wr, nullptr) || wr != static_cast<DWORD>(utf8.size())) {
            CloseHandle(h);
            return false;
        }
    }
    CloseHandle(h);
    return true;
}

}

bool ReadIniUtf8Value(const std::wstring& iniPath,
                      const std::wstring& section,
                      const std::wstring& key,
                      const std::wstring& defValue,
                      std::wstring& outValue) {
    std::vector<std::wstring> lines;
    if (!LoadIniLines(iniPath, lines)) {
        outValue = defValue;
        return false;
    }

    const std::wstring sectionL = ToLower(Trim(section));
    const std::wstring keyL = ToLower(Trim(key));
    std::wstring currentSection;
    bool inTarget = false;
    for (const auto& line : lines) {
        std::wstring sec;
        if (IsSectionLine(line, sec)) {
            currentSection = ToLower(sec);
            inTarget = (currentSection == sectionL);
            continue;
        }
        if (!inTarget) continue;
        std::wstring k;
        std::wstring v;
        if (!ParseKeyValueLine(line, k, v)) continue;
        if (ToLower(k) == keyL) {
            outValue = v;
            return true;
        }
    }
    outValue = defValue;
    return true;
}

bool WriteIniUtf8Value(const std::wstring& iniPath,
                       const std::wstring& section,
                       const std::wstring& key,
                       const std::wstring& value) {
    std::vector<std::wstring> lines;
    if (!LoadIniLines(iniPath, lines)) return false;

    const std::wstring sectionN = Trim(section);
    const std::wstring sectionL = ToLower(sectionN);
    const std::wstring keyN = Trim(key);
    const std::wstring keyL = ToLower(keyN);
    const std::wstring newLine = keyN + L"=" + value;

    bool sectionFound = false;
    bool keyWritten = false;
    bool inTarget = false;
    std::vector<std::wstring> out;
    out.reserve(lines.size() + 4);

    for (const auto& line : lines) {
        std::wstring sec;
        if (IsSectionLine(line, sec)) {
            if (inTarget && !keyWritten) {
                out.push_back(newLine);
                keyWritten = true;
            }
            inTarget = (ToLower(sec) == sectionL);
            if (inTarget) sectionFound = true;
            out.push_back(line);
            continue;
        }

        if (inTarget) {
            std::wstring k;
            std::wstring v;
            if (ParseKeyValueLine(line, k, v) && ToLower(k) == keyL) {
                if (!keyWritten) {
                    out.push_back(newLine);
                    keyWritten = true;
                }
                continue;
            }
        }
        out.push_back(line);
    }

    if (!sectionFound) {
        if (!out.empty() && !out.back().empty()) out.push_back(L"");
        out.push_back(L"[" + sectionN + L"]");
        out.push_back(newLine);
    } else if (!keyWritten) {
        out.push_back(newLine);
    }

    return SaveIniLinesUtf8(iniPath, out);
}

}
