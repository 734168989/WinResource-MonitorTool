// DataLogger.cpp - Binary disk logger with automatic file rotation
#include "DataLogger.h"
#include <cstdio>

DataLogger::DataLogger() : m_hFile(INVALID_HANDLE_VALUE),
    m_startTime(0), m_rowCount(0), m_partIndex(0) {
    m_outputDir[0] = L'\0';
    m_filePath[0] = L'\0';
    InitializeCriticalSection(&m_cs);
}

DataLogger::~DataLogger() {
    EndLog();
    DeleteCriticalSection(&m_cs);
}

bool DataLogger::BeginLog(const wchar_t* outputDir, double startTime) {
    EndLog();
    DeleteLog();

    wcscpy_s(m_outputDir, MAX_PATH, outputDir);
    m_startTime = startTime;
    m_rowCount = 0;
    m_partIndex = 1;
    m_partFiles.clear();

    return BeginPart();
}

bool DataLogger::BeginPart() {
    if (m_hFile != INVALID_HANDLE_VALUE)
        CloseHandle(m_hFile);

    swprintf_s(m_filePath, MAX_PATH, L"%s\\_monitor_data_Part%d.bin",
               m_outputDir, m_partIndex);
    m_partFiles.push_back(m_filePath);

    m_hFile = CreateFileW(m_filePath, GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH | FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (m_hFile == INVALID_HANDLE_VALUE) return false;

    m_rowCount = 0;
    // Header: magic + version + startTime
    WriteU32(0x4D4F4E54); // "MONT"
    WriteU32(1);          // version
    WriteDouble(m_startTime);
    return true;
}

void DataLogger::RotatePart() {
    m_partIndex++;
    BeginPart();
}

void DataLogger::AppendSample(const SystemMonitorData& sys,
                               const std::vector<std::wstring>& procNames,
                               const std::vector<std::vector<ProcessMonitorData>>& procData) {
    EnterCriticalSection(&m_cs);
    if (m_hFile == INVALID_HANDLE_VALUE) { LeaveCriticalSection(&m_cs); return; }

    // Rotate if current part is full
    if (m_rowCount >= MAX_ROWS_PER_PART) {
        RotatePart();
        if (m_hFile == INVALID_HANDLE_VALUE) { LeaveCriticalSection(&m_cs); return; }
    }

    // Marker: sample start
    WriteU32(0x53414D50); // "SAMP"
    WriteBytes(&sys, sizeof(SystemMonitorData));

    DWORD n = (DWORD)procNames.size();
    WriteU32(n);
    for (DWORD i = 0; i < n; i++) {
        WriteWString(procNames[i]);
        DWORD rows = (DWORD)procData[i].size();
        WriteU32(rows);
        if (rows > 0)
            WriteBytes(procData[i].data(), rows * sizeof(ProcessMonitorData));
    }

    m_rowCount++;
    LeaveCriticalSection(&m_cs);
}

void DataLogger::EndLog() {
    EnterCriticalSection(&m_cs);
    if (m_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&m_cs);
}

void DataLogger::DeleteLog() {
    for (auto& path : m_partFiles)
        DeleteFileW(path.c_str());
    m_partFiles.clear();
}

bool DataLogger::LoadAll(std::vector<SystemMonitorData>& sysOut,
                          std::vector<std::wstring>& procNamesOut,
                          std::vector<std::vector<ProcessMonitorData>>& procDataOut) {
    if (m_partFiles.empty()) return false;

    for (auto& path : m_partFiles) {
        HANDLE hRead = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hRead == INVALID_HANDLE_VALUE) continue;

        DWORD size = GetFileSize(hRead, nullptr);
        if (size == INVALID_FILE_SIZE || size < 20) { CloseHandle(hRead); continue; }

        BYTE* buf = (BYTE*)malloc(size);
        if (!buf) { CloseHandle(hRead); continue; }

        DWORD read = 0;
        BOOL ok = ReadFile(hRead, buf, size, &read, nullptr);
        CloseHandle(hRead);
        if (!ok || read != size) { free(buf); continue; }

        BYTE* p = buf;
        BYTE* end = buf + size;

        auto readU32 = [&]() -> DWORD {
            if (p + 4 > end) return 0;
            DWORD v = *(DWORD*)p; p += 4; return v;
        };

        // Header
        if (readU32() != 0x4D4F4E54) { free(buf); continue; }
        if (readU32() != 1) { free(buf); continue; }
        p += 8; // skip startTime double

        // Read samples
        while (p + 4 <= end) {
            DWORD marker = readU32();
            if (marker != 0x53414D50) break;

            if (p + sizeof(SystemMonitorData) > end) break;
            SystemMonitorData sys = *(SystemMonitorData*)p;
            p += sizeof(SystemMonitorData);
            sysOut.push_back(sys);

            DWORD nProc = readU32();
            for (DWORD i = 0; i < nProc; i++) {
                if (p + 4 > end) break;
                DWORD nameLen = *(DWORD*)p; p += 4;
                if (p + nameLen * 2 > end) break;
                std::wstring name((wchar_t*)p, nameLen);
                p += nameLen * 2;

                DWORD nRows = readU32();
                std::vector<ProcessMonitorData> rows;
                if (nRows > 0) {
                    if (p + nRows * sizeof(ProcessMonitorData) > end) break;
                    rows.assign((ProcessMonitorData*)p, (ProcessMonitorData*)p + nRows);
                    p += nRows * sizeof(ProcessMonitorData);
                }

                int idx = -1;
                for (size_t j = 0; j < procNamesOut.size(); j++) {
                    if (procNamesOut[j] == name) { idx = (int)j; break; }
                }
                if (idx < 0) {
                    idx = (int)procNamesOut.size();
                    procNamesOut.push_back(name);
                    procDataOut.push_back({});
                }
                procDataOut[idx].insert(procDataOut[idx].end(), rows.begin(), rows.end());
            }
        }
        free(buf);
    }
    return !sysOut.empty();
}

bool DataLogger::WriteBytes(const void* data, DWORD size) {
    DWORD written = 0;
    return WriteFile(m_hFile, data, size, &written, nullptr) && written == size;
}
bool DataLogger::WriteU32(DWORD v) { return WriteBytes(&v, 4); }
bool DataLogger::WriteDouble(double v) { return WriteBytes(&v, 8); }
bool DataLogger::WriteWString(const std::wstring& s) {
    DWORD len = (DWORD)s.size();
    return WriteU32(len) && (len == 0 || WriteBytes(s.c_str(), len * 2));
}
