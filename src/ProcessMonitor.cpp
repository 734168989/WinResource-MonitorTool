// ProcessMonitor.cpp - Per-process monitoring implementation (multi-PID)
#include "ProcessMonitor.h"
#include "NetSpeedMonitor.h"
#include <cstdio>
#include <ctime>
#include <cmath>

// PROCESS_MEMORY_COUNTERS_EX2 not available in older SDKs (requires Win10 SDK).
// Define it manually to get PrivateWorkingSetSize which matches Task Manager.
typedef struct _PROCESS_MEMORY_COUNTERS_EX2 {
    DWORD     cb;
    DWORD     PageFaultCount;
    SIZE_T    PeakWorkingSetSize;
    SIZE_T    WorkingSetSize;
    SIZE_T    QuotaPeakPagedPoolUsage;
    SIZE_T    QuotaPagedPoolUsage;
    SIZE_T    QuotaPeakNonPagedPoolUsage;
    SIZE_T    QuotaNonPagedPoolUsage;
    SIZE_T    PagefileUsage;
    SIZE_T    PeakPagefileUsage;
    SIZE_T    PrivateUsage;
    SIZE_T    PrivateWorkingSetSize;   // ← matches Task Manager "private working set"
    SIZE_T    SharedCommitUsage;
} PROCESS_MEMORY_COUNTERS_EX2;

ProcessMonitor::ProcessMonitor(const wchar_t* processName) {
    wcscpy_s(m_processName, 260, processName);
    wcscpy_s(m_netUnit, 16, L"Mbps");
    m_lastCollectRunSeconds = -1.0;
    QueryPerformanceFrequency(&m_qpcFrequency);
}

ProcessMonitor::~ProcessMonitor() {}

std::vector<DWORD> ProcessMonitor::FindAllProcessPids() {
    std::vector<DWORD> pids;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, m_processName) == 0) {
                pids.push_back(pe.th32ProcessID);
                // Continue searching — collect ALL matching PIDs
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return pids;
}

bool ProcessMonitor::HasAnyRunning() {
    auto currentPids = FindAllProcessPids();
    return !currentPids.empty();
}

double ProcessMonitor::GetProcessCpuUsage(DWORD pid, PidCpuState& state) {
    if (pid == 0) return 0.0;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return 0.0;
    }

    FILETIME createTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
        CloseHandle(hProcess);
        return 0.0;
    }
    CloseHandle(hProcess);

    ULONGLONG kernel = ((ULONGLONG)kernelTime.dwHighDateTime << 32) | kernelTime.dwLowDateTime;
    ULONGLONG user = ((ULONGLONG)userTime.dwHighDateTime << 32) | userTime.dwLowDateTime;

    ULONGLONG now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);

    if (!state.cpuInitialized) {
        state.lastCpuKernel = kernel;
        state.lastCpuUser = user;
        state.lastCpuTimestamp = now;
        state.cpuInitialized = true;
        return 0.0;
    }

    ULONGLONG kernelDiff = kernel - state.lastCpuKernel;
    ULONGLONG userDiff = user - state.lastCpuUser;
    ULONGLONG timeDiff = now - state.lastCpuTimestamp;

    state.lastCpuKernel = kernel;
    state.lastCpuUser = user;
    state.lastCpuTimestamp = now;

    if (timeDiff == 0) return 0.0;

    // Get CPU count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD cpuCount = sysInfo.dwNumberOfProcessors;
    if (cpuCount == 0) cpuCount = 1;

    double secondsElapsed = (double)timeDiff / (double)m_qpcFrequency.QuadPart;
    double totalCpuTicks = (double)(kernelDiff + userDiff) / 10000000.0; // 100ns units to seconds
    double usage = (totalCpuTicks / secondsElapsed) * 100.0 / (double)cpuCount;

    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;

    return floor(usage * 100.0 + 0.5) / 100.0;
}

void ProcessMonitor::GetProcessMemory(DWORD pid, double& usage, double& usedMB) {
    usage = 0.0;
    usedMB = 0.0;

    if (pid == 0) return;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) {
        return;
    }

    // Use PROCESS_MEMORY_COUNTERS_EX2 on Win10+ for PrivateWorkingSetSize
    // which matches Task Manager's "Memory (private working set)" column.
    // Falls back to PrivateUsage (commit charge) on older Windows.
    PROCESS_MEMORY_COUNTERS_EX2 pmc2 = {};
    pmc2.cb = sizeof(pmc2);
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc2, sizeof(pmc2))) {
        // Prefer PrivateWorkingSetSize (Win10 1809+). If 0, fall back to PrivateUsage.
        SIZE_T memBytes = pmc2.PrivateWorkingSetSize;
        if (memBytes == 0) {
            memBytes = pmc2.PrivateUsage;
        }
        usedMB = (double)memBytes / (1024.0 * 1024.0);
        usedMB = floor(usedMB * 100.0 + 0.5) / 100.0;

        // Calculate percentage of total system memory
        MEMORYSTATUSEX memStat = {};
        memStat.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memStat)) {
            double totalMB = (double)memStat.ullTotalPhys / (1024.0 * 1024.0);
            if (totalMB > 0) {
                usage = (usedMB / totalMB) * 100.0;
                usage = floor(usage * 100.0 + 0.5) / 100.0;
            }
        }
    }

    CloseHandle(hProcess);
}

std::vector<ProcessMonitorData> ProcessMonitor::Collect(double runSeconds, NetSpeedMonitor* netMon) {
    std::vector<ProcessMonitorData> results;

    // Timestamp (shared for all instances)
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    wchar_t timestamp[MAX_TIMESTAMP_LEN];
    swprintf_s(timestamp, MAX_TIMESTAMP_LEN, L"%d/%d/%d %02d:%02d:%02d",
               tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
               tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    // Find all currently running PIDs
    auto currentPids = FindAllProcessPids();

    // Remove stale CPU states for PIDs that no longer exist
    for (auto it = m_pidStates.begin(); it != m_pidStates.end(); ) {
        bool stillAlive = false;
        for (DWORD pid : currentPids) {
            if (pid == it->pid) { stillAlive = true; break; }
        }
        if (!stillAlive) {
            it = m_pidStates.erase(it);
        } else {
            ++it;
        }
    }

    // Collect data for each running PID
    for (DWORD pid : currentPids) {
        ProcessMonitorData data = {};
        data.runSeconds = runSeconds;
        data.pid = pid;
        data.cpuUsage = 0.0;
        data.memoryUsage = 0.0;
        data.memoryUsedMB = 0.0;
        data.netSendSpeed = 0.0;
        data.netRecvSpeed = 0.0;
        wcscpy_s(data.timestamp, MAX_TIMESTAMP_LEN, timestamp);

        // Find or create CPU state for this PID
        PidCpuState* state = nullptr;
        for (auto& s : m_pidStates) {
            if (s.pid == pid) { state = &s; break; }
        }
        if (!state) {
            PidCpuState newState = {};
            newState.pid = pid;
            newState.cpuInitialized = false;
            m_pidStates.push_back(newState);
            state = &m_pidStates.back();
        }

        data.cpuUsage = GetProcessCpuUsage(pid, *state);
        GetProcessMemory(pid, data.memoryUsage, data.memoryUsedMB);

        // Query per-PID network speed from ETW trace
        if (netMon && netMon->IsRunning()) {
            ULONGLONG deltaSent = 0, deltaRecv = 0;
            netMon->QueryDelta(pid, deltaSent, deltaRecv);
            double elapsedSec = GetCollectElapsedSec(runSeconds);
            if (elapsedSec > 0.0) {
                double bytesPerSecSent = (double)deltaSent / elapsedSec;
                double bytesPerSecRecv = (double)deltaRecv / elapsedSec;
                data.netSendSpeed = ConvertBytesToUnit(bytesPerSecSent);
                data.netRecvSpeed = ConvertBytesToUnit(bytesPerSecRecv);
            }
        } else {
            data.netSendSpeed = 0.0;
            data.netRecvSpeed = 0.0;
        }

        results.push_back(data);
    }

    // If no instances found, return a single zero-filled entry for continuity
    if (results.empty()) {
        ProcessMonitorData data = {};
        data.runSeconds = runSeconds;
        data.pid = 0;
        data.cpuUsage = 0.0;
        data.memoryUsage = 0.0;
        data.memoryUsedMB = 0.0;
        data.netSendSpeed = 0.0;
        data.netRecvSpeed = 0.0;
        wcscpy_s(data.timestamp, MAX_TIMESTAMP_LEN, timestamp);
        results.push_back(data);
    }

    return results;
}

double ProcessMonitor::GetCollectElapsedSec(double runSeconds) {
    double elapsed = 0.0;
    if (m_lastCollectRunSeconds >= 0.0 && runSeconds > m_lastCollectRunSeconds) {
        elapsed = runSeconds - m_lastCollectRunSeconds;
    }
    m_lastCollectRunSeconds = runSeconds;
    return elapsed;
}

double ProcessMonitor::ConvertBytesToUnit(double bytesPerSec) const {
    if (wcscmp(m_netUnit, L"Kbps") == 0)
        return floor((bytesPerSec * 8.0 / 1000.0) * 100.0 + 0.5) / 100.0;
    else if (wcscmp(m_netUnit, L"Mbps") == 0)
        return floor((bytesPerSec * 8.0 / 1000000.0) * 100.0 + 0.5) / 100.0;
    else if (wcscmp(m_netUnit, L"Gbps") == 0)
        return floor((bytesPerSec * 8.0 / 1000000000.0) * 100.0 + 0.5) / 100.0;
    return floor((bytesPerSec * 8.0 / 1000.0) * 100.0 + 0.5) / 100.0;
}

void ProcessMonitor::Reset() {
    m_pidStates.clear();
}

void ProcessMonitor::SetNetUnit(const wchar_t* unit) {
    wcscpy_s(m_netUnit, 16, unit);
}
