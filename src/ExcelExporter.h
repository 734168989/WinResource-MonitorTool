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
                const wchar_t* netUnit);

    void SetNetUnit(const wchar_t* unit);

private:
    wchar_t m_netUnit[16];
};
