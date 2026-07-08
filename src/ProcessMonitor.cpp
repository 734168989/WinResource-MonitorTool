// ProcessMonitor.cpp - Per-process monitoring implementation
#include "ProcessMonitor.h"
#include <cstdio>
#include <ctime>
#include <cmath>

ProcessMonitor::ProcessMonitor(const wchar_t* processName)
    : m_pid(0), m_cpuInitialized(false)
    , m_lastCpuKernel(0), m_lastCpuUser(0), m_lastCpuTimestamp(0)
{
    wcscpy_s(m_processName, 260, processName);
    wcscpy_s(m_netUnit, 16, L"KB/s");
    QueryPerformanceFrequency(&m_qpcFrequency);
}

ProcessMonitor::~ProcessMonitor() {}

DWORD ProcessMonitor::FindProcessPid() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(PROCESSENTRY32W);

    DWORD foundPid = 0;
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, m_processName) == 0) {
                foundPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return foundPid;
}

DWORD ProcessMonitor::GetPid() const {
    return m_pid;
}

bool ProcessMonitor::IsRunning() {
    if (m_pid == 0) {
        m_pid = FindProcessPid();
        return m_pid != 0;
    }

    // Verify pid still exists and matches our process name
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (hProcess == nullptr) {
        m_pid = FindProcessPid();
        return m_pid != 0;
    }

    // Verify the exe name still matches
    wchar_t exeName[260] = {};
    DWORD size = 260;
    if (QueryFullProcessImageNameW(hProcess, 0, exeName, &size)) {
        // Extract just the filename
        wchar_t* lastSlash = wcsrchr(exeName, L'\\');
        wchar_t* fname = lastSlash ? lastSlash + 1 : exeName;
        if (_wcsicmp(fname, m_processName) != 0) {
            CloseHandle(hProcess);
            m_pid = FindProcessPid();
            return m_pid != 0;
        }
    }

    CloseHandle(hProcess);
    return true;
}

double ProcessMonitor::GetProcessCpuUsage() {
    if (m_pid == 0) return 0.0;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (!hProcess) {
        m_pid = 0;
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

    if (!m_cpuInitialized) {
        m_lastCpuKernel = kernel;
        m_lastCpuUser = user;
        m_lastCpuTimestamp = now;
        m_cpuInitialized = true;
        return 0.0;
    }

    ULONGLONG kernelDiff = kernel - m_lastCpuKernel;
    ULONGLONG userDiff = user - m_lastCpuUser;
    ULONGLONG timeDiff = now - m_lastCpuTimestamp;

    m_lastCpuKernel = kernel;
    m_lastCpuUser = user;
    m_lastCpuTimestamp = now;

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

void ProcessMonitor::GetProcessMemory(double& usage, double& usedMB) {
    usage = 0.0;
    usedMB = 0.0;

    if (m_pid == 0) return;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, m_pid);
    if (!hProcess) {
        m_pid = 0;
        return;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);

    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        usedMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
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

ProcessMonitorData ProcessMonitor::Collect(double runSeconds) {
    ProcessMonitorData data = {};
    data.runSeconds = runSeconds;
    data.pid = 0;
    data.cpuUsage = 0.0;
    data.memoryUsage = 0.0;
    data.memoryUsedMB = 0.0;
    data.netSendSpeed = 0.0;
    data.netRecvSpeed = 0.0;

    // Timestamp
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    wcsftime(data.timestamp, MAX_TIMESTAMP_LEN, L"%Y-%m-%d %H:%M:%S", &tm_now);

    if (!IsRunning()) {
        return data; // Process not running, return zeros
    }

    data.pid = m_pid;
    data.cpuUsage = GetProcessCpuUsage();
    GetProcessMemory(data.memoryUsage, data.memoryUsedMB);

    // Process network: Windows doesn't expose per-process network stats easily
    // without ETW. We approximate with TCP connection count.
    // For now, leave as 0.0 (documented limitation)
    data.netSendSpeed = 0.0;
    data.netRecvSpeed = 0.0;

    return data;
}

void ProcessMonitor::Reset() {
    m_pid = 0;
    m_cpuInitialized = false;
    m_lastCpuKernel = 0;
    m_lastCpuUser = 0;
    m_lastCpuTimestamp = 0;
}

void ProcessMonitor::SetNetUnit(const wchar_t* unit) {
    wcscpy_s(m_netUnit, 16, unit);
}
