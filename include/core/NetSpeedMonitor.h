// NetSpeedMonitor.h - Per-process network bandwidth
// TCP: GetPerTcpConnectionEStats (per-connection byte counters, IPv4+IPv6)
// UDP: GetExtendedUdpTable (endpoint presence) + system-wide UDP counters
#pragma once
#include <windows.h>
#include <map>

class NetSpeedMonitor {
public:
    NetSpeedMonitor();
    ~NetSpeedMonitor();

    bool Start();
    void Stop();
    void QueryDelta(DWORD pid, ULONGLONG& bytesSent, ULONGLONG& bytesRecv);
    bool IsRunning() const { return m_running; }

    // Internal types
    struct ConnKey {
        DWORD  pid;
        BYTE   localAddr[16], remoteAddr[16];
        USHORT localPort, remotePort;
        bool operator<(const ConnKey& o) const {
            if (pid != o.pid) return pid < o.pid;
            int c = memcmp(localAddr, o.localAddr, 16);
            if (c) return c < 0;
            c = memcmp(remoteAddr, o.remoteAddr, 16);
            if (c) return c < 0;
            if (localPort != o.localPort) return localPort < o.localPort;
            return remotePort < o.remotePort;
        }
    };
    struct ConnBaseline {
        ULONGLONG outBytes, inBytes;
        bool statsEnabled;
    };

private:
    void QueryTcp(DWORD pid, ULONGLONG& sent, ULONGLONG& recv);
    void QueryUdp(DWORD pid, ULONGLONG& sent, ULONGLONG& recv);

    bool m_running;
    std::map<ConnKey, ConnBaseline> m_connMap;
    CRITICAL_SECTION m_cs;
    int m_gcCounter;
    ULONGLONG m_lastSysUdpSent, m_lastSysUdpRecv;
    ULONGLONG m_sysUdpSendDelta, m_sysUdpRecvDelta;
    DWORD m_lastUdpTick;
    DWORD m_udpTotalEndpoints;
};
