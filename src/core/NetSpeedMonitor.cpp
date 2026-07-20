// NetSpeedMonitor.cpp - Per-process network bandwidth
// TCP: GetPerTcpConnectionEStats (IPv4+IPv6) — 精确到字节
// UDP: GetIpStatisticsEx 系统级统计 + GetExtendedUdpTable 按端点比例分配
#include "NetSpeedMonitor.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <cstdio>
#include <set>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

static void FillV4Mapped(BYTE out[16], DWORD ipv4) {
    memset(out, 0, 10);
    out[10] = 0xFF; out[11] = 0xFF;
    out[12] = (BYTE)(ipv4 >> 24); out[13] = (BYTE)(ipv4 >> 16);
    out[14] = (BYTE)(ipv4 >> 8);  out[15] = (BYTE)(ipv4);
}

NetSpeedMonitor::NetSpeedMonitor()
    : m_running(false), m_gcCounter(0)
    , m_lastSysUdpSent(0), m_lastSysUdpRecv(0)
    , m_sysUdpSendDelta(0), m_sysUdpRecvDelta(0)
    , m_lastUdpTick(0), m_udpTotalEndpoints(0)
    { InitializeCriticalSection(&m_cs); }

NetSpeedMonitor::~NetSpeedMonitor() { Stop(); DeleteCriticalSection(&m_cs); }

// ============================================================================
// TCP — GetPerTcpConnectionEStats (IPv4 + IPv6)
// 关键修复：ConnKey 包含 PID，每个 PID 的 baselines 独立追踪
// ============================================================================
static void TcpQueryV4(DWORD pid, ULONGLONG& sent, ULONGLONG& recv,
                       std::map<NetSpeedMonitor::ConnKey, NetSpeedMonitor::ConnBaseline>& connMap) {
    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (!sz) return;
    PMIB_TCPTABLE_OWNER_PID pT = (PMIB_TCPTABLE_OWNER_PID)malloc(sz);
    if (!pT) return;
    if (GetExtendedTcpTable(pT, &sz, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR) {
        free(pT); return;
    }
    for (DWORD i = 0; i < pT->dwNumEntries; i++) {
        MIB_TCPROW_OWNER_PID& r = pT->table[i];
        if (r.dwOwningPid != pid) continue;
        if (r.dwState != MIB_TCP_STATE_ESTAB) continue;

        NetSpeedMonitor::ConnKey k = {};
        k.pid = pid;                        // ← 关键修复：PID 作为 key 的一部分
        FillV4Mapped(k.localAddr,  r.dwLocalAddr);
        FillV4Mapped(k.remoteAddr, r.dwRemoteAddr);
        k.localPort  = (USHORT)r.dwLocalPort;
        k.remotePort = (USHORT)r.dwRemotePort;

        MIB_TCPROW br;
        br.dwState = r.dwState; br.dwLocalAddr = r.dwLocalAddr;
        br.dwLocalPort = r.dwLocalPort; br.dwRemoteAddr = r.dwRemoteAddr;
        br.dwRemotePort = r.dwRemotePort;

        auto& bl = connMap[k];
        if (!bl.statsEnabled) {
            TCP_ESTATS_DATA_RW_v0 rw = {}; rw.EnableCollection = TRUE;
            SetPerTcpConnectionEStats((PMIB_TCPROW)&br, TcpConnectionEstatsData,
                                      (PUCHAR)&rw, 0, sizeof(rw), 0);
            bl.statsEnabled = true; bl.outBytes = bl.inBytes = 0;
        }
        TCP_ESTATS_DATA_ROD_v0 rod = {};
        if (GetPerTcpConnectionEStats((PMIB_TCPROW)&br, TcpConnectionEstatsData,
            nullptr,0,0, nullptr,0,0, (PUCHAR)&rod,0,sizeof(rod)) == NO_ERROR) {
            // ← 关键修复：sent/recv 独立追踪，不再用 outBytes 同时门控两者
            if (bl.outBytes > 0 && rod.DataBytesOut >= bl.outBytes)
                sent += rod.DataBytesOut - bl.outBytes;
            if (bl.inBytes  > 0 && rod.DataBytesIn  >= bl.inBytes)
                recv += rod.DataBytesIn  - bl.inBytes;
            bl.outBytes = rod.DataBytesOut; bl.inBytes = rod.DataBytesIn;
        } else {
            // ESTATS 失败回退：按连接存在估算最低流量（~500 bytes/s per connection）
            sent += 500; recv += 500;
        }
    }
    free(pT);
}

static void TcpQueryV6(DWORD pid, ULONGLONG& sent, ULONGLONG& recv,
                       std::map<NetSpeedMonitor::ConnKey, NetSpeedMonitor::ConnBaseline>& connMap) {
    DWORD sz = 0;
    GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET6,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (!sz) return;
    PMIB_TCP6TABLE_OWNER_PID pT = (PMIB_TCP6TABLE_OWNER_PID)malloc(sz);
    if (!pT) return;
    if (GetExtendedTcpTable(pT, &sz, FALSE, AF_INET6,
                            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR) {
        free(pT); return;
    }
    for (DWORD i = 0; i < pT->dwNumEntries; i++) {
        MIB_TCP6ROW_OWNER_PID& r = pT->table[i];
        if (r.dwOwningPid != pid) continue;
        if (r.dwState != MIB_TCP_STATE_ESTAB) continue;

        NetSpeedMonitor::ConnKey k = {};
        k.pid = pid;                        // ← 关键修复：PID 作为 key 的一部分
        memcpy(k.localAddr,  r.ucLocalAddr,  16);
        memcpy(k.remoteAddr, r.ucRemoteAddr, 16);
        k.localPort  = (USHORT)r.dwLocalPort;
        k.remotePort = (USHORT)r.dwRemotePort;

        MIB_TCP6ROW br;
        memcpy(&br.LocalAddr,  r.ucLocalAddr,  16);
        memcpy(&br.RemoteAddr, r.ucRemoteAddr, 16);
        br.dwLocalScopeId = r.dwLocalScopeId; br.dwRemoteScopeId = r.dwRemoteScopeId;
        br.dwLocalPort = r.dwLocalPort; br.dwRemotePort = r.dwRemotePort;
        br.State = (MIB_TCP_STATE)r.dwState;

        auto& bl = connMap[k];
        if (!bl.statsEnabled) {
            TCP_ESTATS_DATA_RW_v0 rw = {}; rw.EnableCollection = TRUE;
            SetPerTcp6ConnectionEStats(&br, TcpConnectionEstatsData,
                                       (PUCHAR)&rw, 0, sizeof(rw), 0);
            bl.statsEnabled = true; bl.outBytes = bl.inBytes = 0;
        }
        TCP_ESTATS_DATA_ROD_v0 rod = {};
        if (GetPerTcp6ConnectionEStats(&br, TcpConnectionEstatsData,
            nullptr,0,0, nullptr,0,0, (PUCHAR)&rod,0,sizeof(rod)) == NO_ERROR) {
            // ← 关键修复：sent/recv 独立追踪
            if (bl.outBytes > 0 && rod.DataBytesOut >= bl.outBytes)
                sent += rod.DataBytesOut - bl.outBytes;
            if (bl.inBytes  > 0 && rod.DataBytesIn  >= bl.inBytes)
                recv += rod.DataBytesIn  - bl.inBytes;
            bl.outBytes = rod.DataBytesOut; bl.inBytes = rod.DataBytesIn;
        } else {
            sent += 500; recv += 500;
        }
    }
    free(pT);
}

void NetSpeedMonitor::QueryTcp(DWORD pid, ULONGLONG& sent, ULONGLONG& recv) {
    TcpQueryV4(pid, sent, recv, m_connMap);
    TcpQueryV6(pid, sent, recv, m_connMap);
}

// ============================================================================
// UDP — GetIpStatisticsEx 系统级 + 按 UDP 端点比例分配
// ============================================================================
void NetSpeedMonitor::QueryUdp(DWORD pid, ULONGLONG& sent, ULONGLONG& recv) {
    // 每 800ms 更新一次系统级 UDP 基线（保证同一采样周期内所有 PID 共享相同增量）
    DWORD tick = GetTickCount();
    if (m_lastUdpTick == 0 || (tick - m_lastUdpTick) >= 800) {
        MIB_UDPSTATS udp4 = {}, udp6 = {};
        GetUdpStatisticsEx(&udp4, AF_INET);
        GetUdpStatisticsEx(&udp6, AF_INET6);
        ULONGLONG totalSend = (ULONGLONG)udp4.dwOutDatagrams +
                              (ULONGLONG)udp6.dwOutDatagrams;
        ULONGLONG totalRecv = (ULONGLONG)udp4.dwInDatagrams +
                              (ULONGLONG)udp6.dwInDatagrams;

        if (m_lastSysUdpSent > 0 || m_lastSysUdpRecv > 0) {
            m_sysUdpSendDelta = (totalSend >= m_lastSysUdpSent)
                ? totalSend - m_lastSysUdpSent : 0;
            m_sysUdpRecvDelta = (totalRecv >= m_lastSysUdpRecv)
                ? totalRecv - m_lastSysUdpRecv : 0;
        }
        m_lastSysUdpSent = totalSend;
        m_lastSysUdpRecv = totalRecv;
        m_lastUdpTick = tick;

        // 缓存系统级 UDP 端点总数
        m_udpTotalEndpoints = 0;
        for (int af = 0; af < 2; af++) {
            DWORD sz = 0;
            GetExtendedUdpTable(nullptr, &sz, FALSE,
                af == 0 ? AF_INET : AF_INET6, UDP_TABLE_OWNER_PID, 0);
            if (!sz) continue;
            PMIB_UDPTABLE_OWNER_PID pT = (PMIB_UDPTABLE_OWNER_PID)malloc(sz);
            if (!pT) continue;
            if (GetExtendedUdpTable(pT, &sz, FALSE,
                af == 0 ? AF_INET : AF_INET6, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                for (DWORD i = 0; i < pT->dwNumEntries; i++)
                    if (pT->table[i].dwOwningPid > 0) m_udpTotalEndpoints++;
            }
            free(pT);
        }
    }

    if (m_sysUdpSendDelta == 0 && m_sysUdpRecvDelta == 0) return;

    // 统计该 PID 的 UDP 端点数量
    DWORD pidEndpoints = 0;
    for (int af = 0; af < 2; af++) {
        DWORD sz = 0;
        GetExtendedUdpTable(nullptr, &sz, FALSE,
            af == 0 ? AF_INET : AF_INET6, UDP_TABLE_OWNER_PID, 0);
        if (!sz) continue;
        PMIB_UDPTABLE_OWNER_PID pT = (PMIB_UDPTABLE_OWNER_PID)malloc(sz);
        if (!pT) continue;
        if (GetExtendedUdpTable(pT, &sz, FALSE,
            af == 0 ? AF_INET : AF_INET6, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            for (DWORD i = 0; i < pT->dwNumEntries; i++)
                if (pT->table[i].dwOwningPid == pid) pidEndpoints++;
        }
        free(pT);
    }

    // 按端点比例分配 UDP 包数 → 字节（每包约 1400 字节）
    if (m_udpTotalEndpoints > 0 && pidEndpoints > 0) {
        double ratio = (double)pidEndpoints / (double)m_udpTotalEndpoints;
        sent += (ULONGLONG)((double)m_sysUdpSendDelta * ratio * 1400.0);
        recv += (ULONGLONG)((double)m_sysUdpRecvDelta * ratio * 1400.0);
    }
}

// ============================================================================
// Public API
// ============================================================================
bool NetSpeedMonitor::Start() {
    if (m_running) return true;
    m_running = true;
    m_lastSysUdpSent = m_lastSysUdpRecv = 0;
    m_sysUdpSendDelta = m_sysUdpRecvDelta = 0;
    m_lastUdpTick = 0;
    m_udpTotalEndpoints = 0;
    OutputDebugStringW(L"[NetSpeedMonitor] Started (TCP+UDP, per-PID baselines)\r\n");
    return true;
}

void NetSpeedMonitor::Stop() {
    m_running = false;
    EnterCriticalSection(&m_cs);
    m_connMap.clear();
    m_gcCounter = 0;
    m_lastSysUdpSent = m_lastSysUdpRecv = 0;
    LeaveCriticalSection(&m_cs);
}

void NetSpeedMonitor::QueryDelta(DWORD pid, ULONGLONG& bytesSent,
                                 ULONGLONG& bytesRecv) {
    bytesSent = bytesRecv = 0;
    if (!m_running || pid == 0) return;

    EnterCriticalSection(&m_cs);
    QueryTcp(pid, bytesSent, bytesRecv);
    QueryUdp(pid, bytesSent, bytesRecv);

    // GC every 10 calls — clean stale entries for THIS pid,
    // and every 100 calls do a full sweep of ALL entries.
    if (++m_gcCounter >= 10) {
        m_gcCounter = 0;
        static int fullGcCounter = 0;
        bool doFullGc = (++fullGcCounter >= 10);  // full sweep every 100 queries

        for (auto it = m_connMap.begin(); it != m_connMap.end(); ) {
            // Normal sweep: only current PID.  Full sweep: all PIDs.
            if (!doFullGc && it->first.pid != pid) { ++it; continue; }

            DWORD checkPid = doFullGc ? it->first.pid : pid;

            // Check if this connection is still alive
            bool alive = false;
            for (int af = 0; af < 2 && !alive; af++) {
                DWORD sz = 0;
                int family = (af == 0) ? AF_INET : AF_INET6;
                GetExtendedTcpTable(nullptr, &sz, FALSE, family,
                                    TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
                if (!sz) continue;
                if (af == 0) {
                    PMIB_TCPTABLE_OWNER_PID pT = (PMIB_TCPTABLE_OWNER_PID)malloc(sz);
                    if (pT && GetExtendedTcpTable(pT, &sz, FALSE, family,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                        for (DWORD i = 0; i < pT->dwNumEntries; i++) {
                            auto& r = pT->table[i];
                            if (r.dwOwningPid != checkPid) continue;
                            ConnKey ck = {};
                            ck.pid = checkPid;
                            FillV4Mapped(ck.localAddr, r.dwLocalAddr);
                            FillV4Mapped(ck.remoteAddr, r.dwRemoteAddr);
                            ck.localPort = (USHORT)r.dwLocalPort;
                            ck.remotePort = (USHORT)r.dwRemotePort;
                            if (!(ck < it->first) && !(it->first < ck)) { alive = true; break; }
                        }
                    }
                    if (pT) free(pT);
                } else {
                    PMIB_TCP6TABLE_OWNER_PID pT = (PMIB_TCP6TABLE_OWNER_PID)malloc(sz);
                    if (pT && GetExtendedTcpTable(pT, &sz, FALSE, family,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                        for (DWORD i = 0; i < pT->dwNumEntries; i++) {
                            auto& r = pT->table[i];
                            if (r.dwOwningPid != checkPid) continue;
                            ConnKey ck = {};
                            ck.pid = checkPid;
                            memcpy(ck.localAddr, r.ucLocalAddr, 16);
                            memcpy(ck.remoteAddr, r.ucRemoteAddr, 16);
                            ck.localPort = (USHORT)r.dwLocalPort;
                            ck.remotePort = (USHORT)r.dwRemotePort;
                            if (!(ck < it->first) && !(it->first < ck)) { alive = true; break; }
                        }
                    }
                    if (pT) free(pT);
                }
            }
            if (!alive)
                it = m_connMap.erase(it);
            else
                ++it;
        }
    }
    LeaveCriticalSection(&m_cs);
}
