// NetSpeedMonitor.cpp - Per-process TCP bandwidth via per-connection byte counters
#include "NetSpeedMonitor.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <cstdio>
#include <set>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

NetSpeedMonitor::NetSpeedMonitor() : m_running(false), m_gcCounter(0) {
    InitializeCriticalSection(&m_cs);
}

NetSpeedMonitor::~NetSpeedMonitor() {
    Stop();
    DeleteCriticalSection(&m_cs);
}

bool NetSpeedMonitor::Start() {
    m_running = true;
    OutputDebugStringW(L"[NetSpeedMonitor] Started\r\n");
    return true;
}

void NetSpeedMonitor::Stop() {
    m_running = false;
    EnterCriticalSection(&m_cs);
    m_connMap.clear();
    m_gcCounter = 0;
    LeaveCriticalSection(&m_cs);
}

void NetSpeedMonitor::QueryDelta(DWORD pid, ULONGLONG& bytesSent,
                                  ULONGLONG& bytesRecv) {
    bytesSent = 0;
    bytesRecv = 0;
    if (!m_running || pid == 0) return;

    EnterCriticalSection(&m_cs);

    // 1. Get TCP connections with owning PIDs
    DWORD bufSize = 0;
    GetExtendedTcpTable(nullptr, &bufSize, FALSE, AF_INET,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
    if (bufSize == 0) { LeaveCriticalSection(&m_cs); return; }

    PMIB_TCPTABLE_OWNER_PID pTable = (PMIB_TCPTABLE_OWNER_PID)malloc(bufSize);
    if (!pTable) { LeaveCriticalSection(&m_cs); return; }

    if (GetExtendedTcpTable(pTable, &bufSize, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != NO_ERROR) {
        free(pTable);
        LeaveCriticalSection(&m_cs);
        return;
    }

    std::set<ConnKey> aliveKeys;

    for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
        MIB_TCPROW_OWNER_PID& row = pTable->table[i];
        if (row.dwOwningPid != pid) continue;
        if (row.dwState != MIB_TCP_STATE_ESTAB) continue;

        ConnKey key;
        key.localAddr  = row.dwLocalAddr;
        key.localPort  = (USHORT)row.dwLocalPort;
        key.remoteAddr = row.dwRemoteAddr;
        key.remotePort = (USHORT)row.dwRemotePort;
        aliveKeys.insert(key);

        // Build MIB_TCPROW (compatible prefix of MIB_TCPROW_OWNER_PID)
        MIB_TCPROW baseRow;
        baseRow.dwState      = row.dwState;
        baseRow.dwLocalAddr  = row.dwLocalAddr;
        baseRow.dwLocalPort  = row.dwLocalPort;
        baseRow.dwRemoteAddr = row.dwRemoteAddr;
        baseRow.dwRemotePort = row.dwRemotePort;

        // Enable extended stats if first time seeing this connection
        ConnBaseline& bl = m_connMap[key];
        if (!bl.statsEnabled) {
            TCP_ESTATS_DATA_RW_v0 rw = {};
            rw.EnableCollection = TRUE;
            SetPerTcpConnectionEStats((PMIB_TCPROW)&baseRow,
                                      TcpConnectionEstatsData,
                                      (PUCHAR)&rw, 0, sizeof(rw), 0);
            bl.statsEnabled = true;
            bl.outBytes = 0;
            bl.inBytes  = 0;
        }

        // Read current byte counters
        TCP_ESTATS_DATA_ROD_v0 rod = {};
        ULONG st = GetPerTcpConnectionEStats(
            (PMIB_TCPROW)&baseRow,
            TcpConnectionEstatsData,
            nullptr, 0, 0,         // Rw (out)
            nullptr, 0, 0,         // Ros
            (PUCHAR)&rod, 0, sizeof(rod));  // Rod

        if (st != NO_ERROR) continue;

        // Compute delta: skip if baseline unset (bl.outBytes==0 means first read)
        if (bl.outBytes > 0) {
            if (rod.DataBytesOut >= bl.outBytes)
                bytesSent += rod.DataBytesOut - bl.outBytes;
            if (rod.DataBytesIn >= bl.inBytes)
                bytesRecv += rod.DataBytesIn - bl.inBytes;
        }

        // Store current as new baseline
        bl.outBytes = rod.DataBytesOut;
        bl.inBytes  = rod.DataBytesIn;
    }

    free(pTable);

    // GC stale connections every 10 calls
    if (++m_gcCounter >= 10) {
        m_gcCounter = 0;
        for (auto it = m_connMap.begin(); it != m_connMap.end(); ) {
            if (aliveKeys.find(it->first) == aliveKeys.end())
                it = m_connMap.erase(it);
            else
                ++it;
        }
    }

    LeaveCriticalSection(&m_cs);
}
