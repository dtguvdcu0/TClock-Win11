#pragma once

#include <string>

namespace tcyc {

bool ReadIniUtf8Value(const std::wstring& iniPath,
                      const std::wstring& section,
                      const std::wstring& key,
                      const std::wstring& defValue,
                      std::wstring& outValue);

bool WriteIniUtf8Value(const std::wstring& iniPath,
                       const std::wstring& section,
                       const std::wstring& key,
                       const std::wstring& value);

}

