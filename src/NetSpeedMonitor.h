// NetSpeedMonitor.h - Per-process TCP bandwidth via per-connection byte counters
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

private:
    struct ConnKey {
        DWORD localAddr, remoteAddr;
        USHORT localPort, remotePort;
        bool operator<(const ConnKey& o) const {
            if (localAddr != o.localAddr) return localAddr < o.localAddr;
            if (localPort != o.localPort) return localPort < o.localPort;
            if (remoteAddr != o.remoteAddr) return remoteAddr < o.remoteAddr;
            return remotePort < o.remotePort;
        }
    };

    struct ConnBaseline {
        ULONGLONG outBytes;   // last seen DataBytesOut
        ULONGLONG inBytes;    // last seen DataBytesIn
        bool statsEnabled;    // SetPerTcpConnectionEStats called
    };

    bool m_running;
    std::map<ConnKey, ConnBaseline> m_connMap;  // per-connection baselines
    CRITICAL_SECTION m_cs;
    int m_gcCounter;  // periodic cleanup counter
};
