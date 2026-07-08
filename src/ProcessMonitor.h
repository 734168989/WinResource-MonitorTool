// ProcessMonitor.h - Per-process resource monitoring
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include "DataModels.h"

#pragma comment(lib, "psapi.lib")

class NetSpeedMonitor;

class ProcessMonitor {
public:
    explicit ProcessMonitor(const wchar_t* processName);
    ~ProcessMonitor();

    // Returns data for ALL running instances of the process (one per PID).
    // netMon may be nullptr — in that case netSendSpeed/netRecvSpeed stay 0.
    std::vector<ProcessMonitorData> Collect(double runSeconds, NetSpeedMonitor* netMon);
    const wchar_t* GetProcessName() const { return m_processName; }
    bool HasAnyRunning();
    void Reset();
    void SetNetUnit(const wchar_t* unit);

private:
    struct PidCpuState {
        DWORD pid;
        ULONGLONG lastCpuKernel;
        ULONGLONG lastCpuUser;
        ULONGLONG lastCpuTimestamp;
        bool cpuInitialized;
    };

    std::vector<DWORD> FindAllProcessPids();
    double GetProcessCpuUsage(DWORD pid, PidCpuState& state);
    void GetProcessMemory(DWORD pid, double& usage, double& usedMB);

    wchar_t m_processName[260];
    wchar_t m_netUnit[16];
    std::vector<PidCpuState> m_pidStates;  // per-PID CPU baseline
    double m_lastCollectRunSeconds;  // for network delta time

    LARGE_INTEGER m_qpcFrequency;

    double ConvertBytesToUnit(double bytesPerSec) const;
    double GetCollectElapsedSec(double runSeconds);
};
