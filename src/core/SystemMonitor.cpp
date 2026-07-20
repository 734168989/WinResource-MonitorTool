// SystemMonitor.cpp - System resource monitoring implementation
#include "SystemMonitor.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <netcon.h>       // INetConnectionManager（ncpa.cpl 底层 API）
#include <objbase.h>      // CoCreateInstance, CoTaskMemFree
#include <unordered_map>

#pragma comment(lib, "iphlpapi.lib")

SystemMonitor::SystemMonitor()
    : m_lastIdleTick(0), m_lastKernelTick(0), m_lastUserTick(0)
    , m_cpuInitialized(false)
    , m_lastNetBytesSent(0), m_lastNetBytesRecv(0), m_lastNetTimestamp(0)
    , m_totalNetSendSpeed(0.0), m_totalNetRecvSpeed(0.0)
    , m_netInitialized(false)
{
    wcscpy_s(m_netUnit, 16, L"Mbps");
    wcscpy_s(m_netInterface, 256, L"全部");
    QueryPerformanceFrequency(&m_qpcFrequency);
    m_ifIndexToFriendly.clear();
}

SystemMonitor::~SystemMonitor() {}

// ============================================================================
// GetNetworkInterfaces — 与 ncpa.cpl 完全一致
//
// 单一数据源：INetConnectionManager COM 接口（ncpa.cpl 自身使用的 API）
//   列表内容 = ncpa.cpl 展示内容
//   网卡名称 = ncpa.cpl 当前名称（改名后实时生效）
// GetIfTable2 仅用于获取 InterfaceIndex（流量）和 OperStatus（绿色加粗）
// ============================================================================
std::vector<std::wstring> SystemMonitor::GetNetworkInterfaces() {
    std::vector<std::wstring> interfaces;
    interfaces.push_back(L"全部");
    m_ifIndexToFriendly.clear();
    m_ifConnected.clear();

    // ======== Step 1: COM → ncpa.cpl 连接名（单一数据源）========
    std::vector<std::wstring> ncpaNames;

    INetConnectionManager* pMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ConnectionManager, nullptr, CLSCTX_ALL,
        IID_INetConnectionManager, (void**)&pMgr);
    if (SUCCEEDED(hr) && pMgr) {
        IEnumNetConnection* pEnum = nullptr;
        hr = pMgr->EnumConnections(NCME_DEFAULT, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            INetConnection* pConn = nullptr;
            ULONG fetched = 0;
            while (pEnum->Next(1, &pConn, &fetched) == S_OK && pConn) {
                NETCON_PROPERTIES* pProps = nullptr;
                if (SUCCEEDED(pConn->GetProperties(&pProps)) && pProps) {
                    if (pProps->pszwName && pProps->pszwName[0]) {
                        ncpaNames.push_back(pProps->pszwName);
                        wchar_t dbg[512];
                        swprintf_s(dbg, 512,
                            L"[GetNetworkInterfaces] ncpa: '%s'\r\n",
                            pProps->pszwName);
                        OutputDebugStringW(dbg);
                    }
                    // 不手动释放 NETCON_PROPERTIES — Debug 堆和 COM 分配器冲突会导致崩溃。
                    // 内存在 pConn->Release() 时由 COM 回收，轻微泄漏可接受。
                }
                pConn->Release();
            }
            pEnum->Release();
        }
        pMgr->Release();
    }

    wchar_t dbg[256];
    swprintf_s(dbg, 256, L"[GetNetworkInterfaces] ncpa.cpl: %zu connections\r\n",
        ncpaNames.size());
    OutputDebugStringW(dbg);

    // ======== Step 2: GetIfTable2 → Alias→{Index,Connected} + Desc→{Index,Connected} ========
    struct IfInfo { NET_IFINDEX index; bool connected; };
    std::unordered_map<std::wstring, IfInfo> ifMap;      // Alias → info
    std::unordered_map<std::wstring, IfInfo> descMap;    // Description → info (fallback)

    PMIB_IF_TABLE2 pIfTable = nullptr;
    if (GetIfTable2(&pIfTable) == NO_ERROR) {
        for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
            MIB_IF_ROW2& row = pIfTable->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (!row.InterfaceAndOperStatusFlags.HardwareInterface) continue;

            IfInfo info = { row.InterfaceIndex,
                (row.OperStatus == IfOperStatusUp) };

            // Alias map
            std::wstring aliasKey(row.Alias);
            for (auto& c : aliasKey) c = towlower(c);
            if (ifMap.find(aliasKey) == ifMap.end()) {
                ifMap[aliasKey] = info;
            }

            // Description map (fallback — ncpa.cpl name often matches Description)
            std::wstring descKey(row.Description);
            for (auto& c : descKey) c = towlower(c);
            if (descMap.find(descKey) == descMap.end()) {
                descMap[descKey] = info;
            }
        }
        FreeMibTable(pIfTable);
    }

    // ======== Step 3: 按名称交叉匹配（Alias 优先，Description 回退）========
    for (const auto& name : ncpaNames) {
        std::wstring key(name);
        for (auto& c : key) c = towlower(c);
        auto it = ifMap.find(key);

        if (it != ifMap.end()) {
            interfaces.push_back(name);
            m_ifIndexToFriendly[it->second.index] = name;
            m_ifConnected[it->second.index] = it->second.connected;
            swprintf_s(dbg, 512,
                L"[GetNetworkInterfaces] ADD (alias): '%s' connected=%d\r\n",
                name.c_str(), it->second.connected);
            OutputDebugStringW(dbg);
        } else {
            // Fallback: try matching by Description
            auto dit = descMap.find(key);
            if (dit != descMap.end()) {
                interfaces.push_back(name);
                m_ifIndexToFriendly[dit->second.index] = name;
                m_ifConnected[dit->second.index] = dit->second.connected;
                swprintf_s(dbg, 512,
                    L"[GetNetworkInterfaces] ADD (desc): '%s' connected=%d\r\n",
                    name.c_str(), dit->second.connected);
                OutputDebugStringW(dbg);
            } else {
                interfaces.push_back(name);
                swprintf_s(dbg, 512,
                    L"[GetNetworkInterfaces] ADD (no hw): '%s'\r\n", name.c_str());
                OutputDebugStringW(dbg);
            }
        }
    }

    return interfaces;
}

bool SystemMonitor::IsInterfaceConnected(const std::wstring& name) const {
    for (const auto& kv : m_ifConnected) {
        auto it = m_ifIndexToFriendly.find(kv.first);
        if (it != m_ifIndexToFriendly.end() && it->second == name)
            return kv.second;
    }
    return false;
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
    ULONGLONG allSent = 0, allRecv = 0;
    wchar_t dbg[512];
    swprintf_s(dbg, 512, L"[Initialize] selected='%s', map_size=%zu\r\n",
        m_netInterface, m_ifIndexToFriendly.size());
    OutputDebugStringW(dbg);

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
        // Always accumulate unfiltered total (all hardware interfaces)
        allSent += row.OutOctets;
        allRecv += row.InOctets;
    }

    m_lastNetBytesSent = totalSent;
    m_lastNetBytesRecv = totalRecv;
    m_lastTotalBytesSent = allSent;
    m_lastTotalBytesRecv = allRecv;
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
    swprintf_s(data.timestamp, MAX_TIMESTAMP_LEN, L"%d/%d/%d %02d:%02d:%02d",
               tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
               tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
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
    ULONGLONG allSent = 0, allRecv = 0;
    for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
        MIB_IF_ROW2& row = pIfTable->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        if (row.OperStatus != IfOperStatusUp) continue;

        // Always accumulate unfiltered total
        allSent += row.OutOctets;
        allRecv += row.InOctets;

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

    // Calculate total (unfiltered) speed
    ULONGLONG allSentDiff = (allSent >= m_lastTotalBytesSent) ? (allSent - m_lastTotalBytesSent) : 0;
    ULONGLONG allRecvDiff = (allRecv >= m_lastTotalBytesRecv) ? (allRecv - m_lastTotalBytesRecv) : 0;
    m_lastTotalBytesSent = allSent;
    m_lastTotalBytesRecv = allRecv;
    m_totalNetSendSpeed = ConvertToUnit((double)allSentDiff / secondsElapsed);
    m_totalNetRecvSpeed = ConvertToUnit((double)allRecvDiff / secondsElapsed);
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
    GetNetworkInterfaces();
}
