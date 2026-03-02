#pragma once

#include "config.h"

#include <string>

namespace tcyc {

enum class ProcessMatchMode {
    None,
    PathOnly,
    PathOnlyFallbackArgsUnavailable,
    PathAndArgs
};

bool IsTaskProcessRunning(const TaskConfig& task, ProcessMatchMode* outMode, bool forceCmdlineReadFailForTest = false);
bool LaunchTask(const TaskConfig& task, std::wstring& outError);

} // namespace tcyc
