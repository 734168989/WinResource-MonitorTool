// DataBuffer.h - Thread-safe ring buffer for monitoring data
#pragma once
#include <windows.h>
#include <vector>
#include <map>
#include <string>
#include "DataModels.h"

class DataBuffer {
public:
    DataBuffer();
    ~DataBuffer();

    void AddSystemData(const SystemMonitorData& data);
    void AddProcessData(const std::wstring& name, const ProcessMonitorData& data);

    // Get copies for UI display (lightweight, returns pointers to internal storage)
    const std::vector<SystemMonitorData>& GetSystemDataRef();
    const std::vector<ProcessMonitorData>* GetProcessDataRef(const std::wstring& name);

    // Get all process names
    std::vector<std::wstring> GetProcessNames();

    // Get complete copies for export
    std::vector<SystemMonitorData> GetSystemDataCopy();
    std::vector<ProcessMonitorData> GetProcessDataCopy(const std::wstring& name);

    size_t GetSystemCount();
    size_t GetProcessCount(const std::wstring& name);
    size_t GetTotalCount();

    void Clear();
    void ClearProcess(const std::wstring& name);

private:
    CRITICAL_SECTION m_cs;
    std::vector<SystemMonitorData> m_systemData;
    std::map<std::wstring, std::vector<ProcessMonitorData>> m_processData;

    static constexpr size_t MAX_ROWS = 10000;
};
