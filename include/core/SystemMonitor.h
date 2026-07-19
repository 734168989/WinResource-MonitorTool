// SystemMonitor.h - System-level resource collection via Windows API
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "DataModels.h"

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    // Collect a single snapshot of system resources
    SystemMonitorData Collect(double runSeconds);

    // Network interface enumeration (ncpa.cpl parity)
    std::vector<std::wstring> GetNetworkInterfaces();

    void SetNetUnit(const wchar_t* unit);
    void SetNetInterface(const wchar_t* iface);

    // Initial call to establish baseline (call once before first Collect)
    void Initialize();

private:
    double GetCpuUsage();
    void GetMemoryInfo(double& totalGB, double& availGB, double& usedGB, double& usage);
    void GetNetworkSpeed(double& sendSpeed, double& recvSpeed);
    double ConvertToUnit(double bytesPerSec);

    // CPU baseline
    ULONGLONG m_lastIdleTick;
    ULONGLONG m_lastKernelTick;
    ULONGLONG m_lastUserTick;
    bool m_cpuInitialized;

    // Network baseline
    ULONGLONG m_lastNetBytesSent;
    ULONGLONG m_lastNetBytesRecv;
    ULONGLONG m_lastNetTimestamp;
    LARGE_INTEGER m_qpcFrequency;
    bool m_netInitialized;

    wchar_t m_netUnit[16];
    wchar_t m_netInterface[256];

    // InterfaceIndex → FriendlyName mapping (for reliable name matching)
    std::unordered_map<NET_IFINDEX, std::wstring> m_ifIndexToFriendly;
    std::unordered_map<NET_IFINDEX, bool> m_ifConnected;
public:
    bool IsInterfaceConnected(const std::wstring& name) const;
};
