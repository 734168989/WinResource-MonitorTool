// DataLogger.h - Binary disk logger for monitoring data persistence
#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "DataModels.h"

class DataLogger {
public:
    DataLogger();
    ~DataLogger();

    // Open the first log part file
    bool BeginLog(const wchar_t* outputDir, double startTime);

    // Append one sample cycle.  Automatically rotates to a new _PartN
    // file when the current part exceeds maxRowsPerPart rows.
    void AppendSample(const SystemMonitorData& sys,
                      const std::vector<std::wstring>& procNames,
                      const std::vector<std::vector<ProcessMonitorData>>& procData);

    // Close current part (does NOT delete files)
    void EndLog();

    // Read ALL parts back into vectors for final export.
    bool LoadAll(std::vector<SystemMonitorData>& sysOut,
                 std::vector<std::wstring>& procNamesOut,
                 std::vector<std::vector<ProcessMonitorData>>& procDataOut);

    // Delete all log part files
    void DeleteLog();

    bool IsActive() const { return m_hFile != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_hFile;
    wchar_t m_outputDir[MAX_PATH];
    wchar_t m_filePath[MAX_PATH];
    double  m_startTime;
    DWORD   m_rowCount;       // rows in current part
    int     m_partIndex;      // current _PartN
    std::vector<std::wstring> m_partFiles;  // all part file paths
    CRITICAL_SECTION m_cs;

    static constexpr DWORD MAX_ROWS_PER_PART = 50000;

    void RotatePart();
    bool WriteBytes(const void* data, DWORD size);
    bool WriteU32(DWORD v);
    bool WriteDouble(double v);
    bool WriteWString(const std::wstring& s);
    bool BeginPart();
};
