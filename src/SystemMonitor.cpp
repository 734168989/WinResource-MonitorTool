// SystemMonitor.cpp - System resource monitoring implementation
#include "SystemMonitor.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <malloc.h>
#include <unordered_set>

#pragma comment(lib, "iphlpapi.lib")

SystemMonitor::SystemMonitor()
    : m_lastIdleTick(0), m_lastKernelTick(0), m_lastUserTick(0)
    , m_cpuInitialized(false)
    , m_lastNetBytesSent(0), m_lastNetBytesRecv(0), m_lastNetTimestamp(0)
    , m_netInitialized(false)
{
    wcscpy_s(m_netUnit, 16, L"Mbps");
    wcscpy_s(m_netInterface, 256, L"全部");
    QueryPerformanceFrequency(&m_qpcFrequency);
    m_ifIndexToFriendly.clear();
}

SystemMonitor::~SystemMonitor() {}

// ============================================================================
// Filters — ncpa.cpl 会过滤掉隐藏适配器和虚拟网卡
// ============================================================================
static bool IsPhysicalNicType(IFTYPE type) {
    switch (type) {
    case IF_TYPE_ETHERNET_CSMACD:  // 6  有线以太网
    case IF_TYPE_IEEE80211:        // 71 Wi-Fi
        return true;
    default:
        return false;
    }
}

// ============================================================================
// GetNetworkInterfaces — ncpa.cpl 完全一致
// GetAdaptersAddresses(flags=0) = 仅 TCP/IP 绑定适配器
// + IsPhysicalNicType()           = 仅物理类型（以太网 / Wi-Fi）
// + HardwareInterface             = 排除隐藏/虚拟适配器（如 Hyper-V "本地连接* N"）
// ============================================================================
std::vector<std::wstring> SystemMonitor::GetNetworkInterfaces() {
    std::vector<std::wstring> interfaces;
    interfaces.push_back(L"全部");
    m_ifIndexToFriendly.clear();

    // Step 1: 通过 GetIfTable2 获取 HardwareInterface 标志
    //         隐藏虚拟适配器（如 Hyper‑V 的 "本地连接* N"）HardwareInterface == FALSE
    std::unordered_set<NET_IFINDEX> hardwareIfSet;
    PMIB_IF_TABLE2 pIfTable = nullptr;
    if (GetIfTable2(&pIfTable) == NO_ERROR) {
        for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
            MIB_IF_ROW2& row = pIfTable->Table[i];
            if (row.InterfaceAndOperStatusFlags.HardwareInterface) {
                hardwareIfSet.insert(row.InterfaceIndex);
            }
        }
        FreeMibTable(pIfTable);
    }

    // Step 2: 通过 GetAdaptersAddresses(flags=0) 获取 TCP/IP 绑定适配器
    ULONG bufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bufLen) != ERROR_BUFFER_OVERFLOW)
        return interfaces;

    PIP_ADAPTER_ADDRESSES pAddr = (PIP_ADAPTER_ADDRESSES)_aligned_malloc(bufLen, sizeof(ULONG_PTR));
    if (!pAddr) return interfaces;

    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, pAddr, &bufLen) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES p = pAddr; p; p = p->Next) {
            if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (!p->FriendlyName || p->FriendlyName[0] == L'\0') continue;
            if (!IsPhysicalNicType(p->IfType)) continue;
            if (hardwareIfSet.find(p->IfIndex) == hardwareIfSet.end()) continue;

            std::wstring name(p->FriendlyName);
            interfaces.push_back(name);
            m_ifIndexToFriendly[p->IfIndex] = name;
        }
    }

    _aligned_free(pAddr);
    return interfaces;
}

// ============================================================================
// Initialize
// ============================================================================
void SystemMonitor::Initialize() {
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        m_lastIdleTick = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
        m_lastKernelTick = ((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        m_lastUserTick = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;
        m_cpuInitialized = true;
    }

    PMIB_IF_TABLE2 pIfTable = nullptr;
    if (GetIfTable2(&pIfTable) != NO_ERROR) return;

    ULONGLONG totalSent = 0, totalRecv = 0;
    for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
        MIB_IF_ROW2& row = pIfTable->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        if (row.OperStatus != IfOperStatusUp) continue;

        auto it = m_ifIndexToFriendly.find(row.InterfaceIndex);
        if (it == m_ifIndexToFriendly.end()) continue;
        bool matchAll = (wcscmp(m_netInterface, L"全部") == 0);
        bool matchOne = !matchAll && (_wcsicmp(it->second.c_str(), m_netInterface) == 0);

        if (matchAll || matchOne) {
            totalSent += row.OutOctets;
            totalRecv += row.InOctets;
        }
    }

    m_lastNetBytesSent = totalSent;
    m_lastNetBytesRecv = totalRecv;
    QueryPerformanceCounter((LARGE_INTEGER*)&m_lastNetTimestamp);
    m_netInitialized = true;
    FreeMibTable(pIfTable);
}

SystemMonitorData SystemMonitor::Collect(double runSeconds) {
    SystemMonitorData data = {};
    data.runSeconds = runSeconds;
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    wcsftime(data.timestamp, MAX_TIMESTAMP_LEN, L"%Y-%m-%d %H:%M:%S", &tm_now);
    data.cpuUsage = GetCpuUsage();
    GetMemoryInfo(data.memoryTotalGB, data.memoryAvailableGB,
                  data.memoryUsedGB, data.memoryUsage);
    GetNetworkSpeed(data.netSendSpeed, data.netRecvSpeed);
    return data;
}

double SystemMonitor::GetCpuUsage() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return 0.0;

    ULONGLONG idleTick = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
    ULONGLONG kernelTick = ((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    ULONGLONG userTick = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;

    if (!m_cpuInitialized) {
        m_lastIdleTick = idleTick;
        m_lastKernelTick = kernelTick;
        m_lastUserTick = userTick;
        m_cpuInitialized = true;
        return 0.0;
    }

    ULONGLONG idleDiff = idleTick - m_lastIdleTick;
    ULONGLONG kernelDiff = kernelTick - m_lastKernelTick;
    ULONGLONG userDiff = userTick - m_lastUserTick;
    ULONGLONG totalDiff = kernelDiff + userDiff;

    m_lastIdleTick = idleTick;
    m_lastKernelTick = kernelTick;
    m_lastUserTick = userTick;

    if (totalDiff == 0) return 0.0;
    double usage = (double)(totalDiff - idleDiff) / (double)totalDiff * 100.0;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return std::floor(usage * 100.0 + 0.5) / 100.0;
}

void SystemMonitor::GetMemoryInfo(double& totalGB, double& availGB,
                                   double& usedGB, double& usage) {
    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        totalGB = (double)memStatus.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        availGB = (double)memStatus.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
        usedGB = totalGB - availGB;
        usage = (double)memStatus.dwMemoryLoad;
        totalGB = std::floor(totalGB * 100.0 + 0.5) / 100.0;
        availGB = std::floor(availGB * 100.0 + 0.5) / 100.0;
        usedGB = std::floor(usedGB * 100.0 + 0.5) / 100.0;
        usage = std::floor(usage * 100.0 + 0.5) / 100.0;
    } else {
        totalGB = availGB = usedGB = usage = 0.0;
    }
}

void SystemMonitor::GetNetworkSpeed(double& sendSpeed, double& recvSpeed) {
    PMIB_IF_TABLE2 pIfTable = nullptr;
    if (GetIfTable2(&pIfTable) != NO_ERROR) {
        sendSpeed = recvSpeed = 0.0;
        return;
    }

    ULONGLONG totalSent = 0, totalRecv = 0;
    for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
        MIB_IF_ROW2& row = pIfTable->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        if (row.OperStatus != IfOperStatusUp) continue;

        auto it = m_ifIndexToFriendly.find(row.InterfaceIndex);
        if (it == m_ifIndexToFriendly.end()) continue;
        bool matchAll = (wcscmp(m_netInterface, L"全部") == 0);
        bool matchOne = !matchAll && (_wcsicmp(it->second.c_str(), m_netInterface) == 0);

        if (matchAll || matchOne) {
            totalSent += row.OutOctets;
            totalRecv += row.InOctets;
        }
    }

    FreeMibTable(pIfTable);

    ULONGLONG nowTick;
    QueryPerformanceCounter((LARGE_INTEGER*)&nowTick);
    if (!m_netInitialized) {
        m_lastNetBytesSent = totalSent;
        m_lastNetBytesRecv = totalRecv;
        m_lastNetTimestamp = nowTick;
        m_netInitialized = true;
        sendSpeed = recvSpeed = 0.0;
        return;
    }

    ULONGLONG tickDiff = nowTick - m_lastNetTimestamp;
    if (tickDiff == 0) { sendSpeed = recvSpeed = 0.0; return; }

    double secondsElapsed = (double)tickDiff / (double)m_qpcFrequency.QuadPart;
    ULONGLONG sentDiff = (totalSent >= m_lastNetBytesSent) ? (totalSent - m_lastNetBytesSent) : 0;
    ULONGLONG recvDiff = (totalRecv >= m_lastNetBytesRecv) ? (totalRecv - m_lastNetBytesRecv) : 0;

    m_lastNetBytesSent = totalSent;
    m_lastNetBytesRecv = totalRecv;
    m_lastNetTimestamp = nowTick;

    sendSpeed = ConvertToUnit((double)sentDiff / secondsElapsed);
    recvSpeed = ConvertToUnit((double)recvDiff / secondsElapsed);
}

double SystemMonitor::ConvertToUnit(double bytesPerSec) {
    if (wcscmp(m_netUnit, L"Kbps") == 0)
        return std::floor((bytesPerSec * 8.0 / 1000.0) * 100.0 + 0.5) / 100.0;
    else if (wcscmp(m_netUnit, L"Mbps") == 0)
        return std::floor((bytesPerSec * 8.0 / 1000000.0) * 100.0 + 0.5) / 100.0;
    else if (wcscmp(m_netUnit, L"Gbps") == 0)
        return std::floor((bytesPerSec * 8.0 / 1000000000.0) * 100.0 + 0.5) / 100.0;
    return std::floor((bytesPerSec * 8.0 / 1000.0) * 100.0 + 0.5) / 100.0;
}

void SystemMonitor::SetNetUnit(const wchar_t* unit) {
    wcscpy_s(m_netUnit, 16, unit);
}

void SystemMonitor::SetNetInterface(const wchar_t* iface) {
    wcscpy_s(m_netInterface, 256, iface);
}
