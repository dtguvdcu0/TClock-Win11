#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace tcalendar {

struct TaskItem {
    std::wstring id;
    std::wstring date;      // YYYY-MM-DD (UI-facing key)
    std::wstring title;
    bool done = false;
    std::wstring updated_at_utc;
};

class TaskStore {
public:
    bool Initialize(const std::wstring& db_path);
    void Shutdown();

    bool CreateTask(const std::wstring& date, const std::wstring& title, TaskItem& out_task);
    bool ToggleDone(const std::wstring& id, bool done);
    bool UpdateTitle(const std::wstring& id, const std::wstring& title);
    bool DeleteTask(const std::wstring& id);
    std::vector<TaskItem> GetDayTasks(const std::wstring& date) const;
    const std::wstring& GetLastError() const;
    void SetTestForceWriteFailure(bool enabled);

private:
    bool EnsureSchema();
    bool LoadAllTasks();
    bool SaveTaskInsert(const TaskItem& task);

    std::wstring NextId();
    static std::wstring NowUtcIso8601();

    void SetLastError(const std::wstring& message);

    sqlite3* db_ = nullptr;
    std::wstring db_path_;
    std::wstring last_error_;
    std::unordered_map<std::wstring, TaskItem> by_id_;
    unsigned long long seq_ = 0;
    bool test_force_write_failure_ = false;
};

} // namespace tcalendar
