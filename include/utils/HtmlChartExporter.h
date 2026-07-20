// HtmlChartExporter.h - Generate standalone HTML file with memory trend line charts
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "DataModels.h"

class HtmlChartExporter {
public:
    // Build the HTML content string (without writing to file)
    static std::string BuildHtml(double startTimestamp,
                                 const std::vector<SystemMonitorData>& systemData,
                                 const std::vector<MonitorProcess>& processes,
                                 const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
                                 const wchar_t* netInterface);

    // Generate an HTML file with separate line charts for system + each process.
    // Returns the full path of the generated file, or empty string on failure.
    static std::wstring Export(const wchar_t* outputDir, double startTimestamp,
                               const std::vector<SystemMonitorData>& systemData,
                               const std::vector<MonitorProcess>& processes,
                               const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
                               const wchar_t* netInterface);
};
