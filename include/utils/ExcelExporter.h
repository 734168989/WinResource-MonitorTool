// ExcelExporter.h - XLSX export (multi-sheet, one process per sheet)
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "DataModels.h"

class ExcelExporter {
public:
    ExcelExporter();
    ~ExcelExporter();

    // Export all collected data to a single .xlsx file with multiple sheets
    // Sheet 1 = "系统资源", subsequent sheets = process name (without .exe)
    bool Export(const wchar_t* outputDir, double startTimestamp,
                const std::vector<SystemMonitorData>& systemData,
                const std::vector<MonitorProcess>& processes,
                const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
                const wchar_t* netUnit, const wchar_t* netInterface);

    void SetNetUnit(const wchar_t* unit);
    void SetNetInterface(const wchar_t* iface);
    const wchar_t* GetLastFilePath() const { return m_lastFilePath; }

    // Real-time export: lock file during monitoring, update every cycle
    bool BeginExport(const wchar_t* outputDir, double startTimestamp);
    bool FlushExport(const std::vector<SystemMonitorData>& systemData,
                     const std::vector<MonitorProcess>& processes,
                     const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
                     const wchar_t* netUnit, const wchar_t* netInterface);
    void EndExport();

    bool IsExportActive() const { return m_hFile != INVALID_HANDLE_VALUE; }

private:
    wchar_t m_netUnit[16];
    wchar_t m_netInterface[256];
    wchar_t m_lastFilePath[MAX_PATH];
    HANDLE m_hFile;
};
