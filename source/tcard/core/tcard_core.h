#pragma once

#include <memory>
#include <string>
#include <vector>
#include <windows.h>

namespace tcard {

struct CardRecord {
    std::wstring id;
    std::wstring title;
    std::wstring source;
    std::wstring color = L"#FFF5A8";
    std::wstring fontFamily = L"Segoe UI";
    double fontSize = 14.0;
    bool live = true;
    bool markdown = false;
    int windowWidthDip = 0;
    int windowHeightDip = 0;
    FILETIME updatedUtc{};
    FILETIME createdUtc{};
};

// A thread-bound update scope. Values are sampled lazily once per scope.
// All renders in the scope use its time; destroy it before the next update.
class RenderBatch {
public:
    explicit RenderBatch(const SYSTEMTIME& localTime);
    ~RenderBatch();
    RenderBatch(const RenderBatch&) = delete;
    RenderBatch& operator=(const RenderBatch&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::wstring Render(const std::wstring& source, const SYSTEMTIME& localTime);
std::wstring RenderMarkdown(const std::wstring& source);
bool ConfigureCustomVariables(const std::wstring& iniPath);
void RefreshCustomVariables();
bool LoadCards(const std::wstring& filePath, std::vector<CardRecord>& cards);
bool SaveCards(const std::wstring& filePath, std::vector<CardRecord>& cards);
bool DeleteCard(const std::wstring& filePath, const std::wstring& id);
std::wstring CardDirectory(const std::wstring& filePath, const std::wstring& id);

}
