// NetSpeedMonitor.h - ETW-based per-process network speed monitor
// Uses kernel trace to capture TCP/IP events and compute per-PID bandwidth.
// Requires administrator privileges.
#pragma once
#include <windows.h>
#include <evntcons.h>
#include <map>

class NetSpeedMonitor {
public:
    NetSpeedMonitor();
    ~NetSpeedMonitor();

    // Start the ETW kernel trace session. Returns false if admin rights missing.
    bool Start();
    // Stop the trace session and clean up.
    void Stop();

    // Query cumulative bytes sent/received for a given PID.
    // Returns data since the last call (delta).
    void QueryDelta(DWORD pid, ULONGLONG& bytesSent, ULONGLONG& bytesRecv);

    // Check if the monitor is actively running.
    bool IsRunning() const { return m_running; }

private:
    struct PidCounters {
        ULONGLONG sent;
        ULONGLONG recv;
    };

    // Previous snapshot for delta calculation
    std::map<DWORD, PidCounters> m_prevCounters;
    // Live counters updated by ETW callback
    std::map<DWORD, PidCounters> m_liveCounters;
    CRITICAL_SECTION m_cs;

    UINT64 m_sessionHandle;
    UINT64 m_traceHandle;
    HANDLE m_thread;
    volatile bool m_running;
    volatile bool m_stopRequested;

    static DWORD WINAPI TraceThreadProc(LPVOID param);
    static void WINAPI EventRecordCallback(PEVENT_RECORD pEvent);
    void ProcessTcpIpEvent(PEVENT_RECORD pEvent);
};
