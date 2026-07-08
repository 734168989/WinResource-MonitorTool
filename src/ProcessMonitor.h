// ProcessMonitor.h - Per-process resource monitoring
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include "DataModels.h"

#pragma comment(lib, "psapi.lib")

class ProcessMonitor {
public:
    explicit ProcessMonitor(const wchar_t* processName);
    ~ProcessMonitor();

    ProcessMonitorData Collect(double runSeconds);
    const wchar_t* GetProcessName() const { return m_processName; }
    DWORD GetPid() const;
    bool IsRunning();
    void Reset();
    void SetNetUnit(const wchar_t* unit);

private:
    DWORD FindProcessPid();
    double GetProcessCpuUsage();
    void GetProcessMemory(double& usage, double& usedMB);

    wchar_t m_processName[260];
    DWORD m_pid;
    wchar_t m_netUnit[16];

    // CPU baseline
    ULONGLONG m_lastCpuKernel;
    ULONGLONG m_lastCpuUser;
    ULONGLONG m_lastCpuTimestamp;
    bool m_cpuInitialized;

    LARGE_INTEGER m_qpcFrequency;
};
