// NetSpeedMonitor.cpp - ETW kernel trace for per-process TCP/IP network speed
#include "NetSpeedMonitor.h"
#include <evntrace.h>
#include <tdh.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "advapi32.lib")

// EventTraceGuid — the actual provider GUID for kernel trace events.
// {68fdd900-4a3e-11d1-84f4-0000f80464e3}
static const GUID EVENT_TRACE_GUID =
    { 0x68fdd900, 0x4a3e, 0x11d1, { 0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3 } };

// SystemTraceControlGuid — enables kernel trace providers
// {9e814aad-3204-11d2-9a82-006008a86939}
static const GUID SYSTEM_TRACE_CONTROL_GUID =
    { 0x9e814aad, 0x3204, 0x11d2, { 0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39 } };

// Known TcpIp kernel event IDs (varies by Windows version)
static bool IsTcpIpSendEvent(USHORT eventId) {
    return eventId == 10 || eventId == 26 || eventId == 1026 || eventId == 1028
        || eventId == 1030 || eventId == 1032 || eventId == 12 || eventId == 28;
}

static bool IsTcpIpRecvEvent(USHORT eventId) {
    return eventId == 11 || eventId == 27 || eventId == 1027 || eventId == 1029
        || eventId == 1031 || eventId == 1033 || eventId == 13 || eventId == 29
        || eventId == 15;
}

static bool IsTcpIpEvent(USHORT eventId) {
    return IsTcpIpSendEvent(eventId) || IsTcpIpRecvEvent(eventId);
}

// Single global instance pointer for the callback
static NetSpeedMonitor* g_netMonitor = nullptr;

NetSpeedMonitor::NetSpeedMonitor()
    : m_sessionHandle(0), m_traceHandle(0), m_thread(nullptr)
    , m_running(false), m_stopRequested(false)
{
    InitializeCriticalSection(&m_cs);
}

NetSpeedMonitor::~NetSpeedMonitor() {
    Stop();
    DeleteCriticalSection(&m_cs);
}

void WINAPI NetSpeedMonitor::EventRecordCallback(PEVENT_RECORD pEvent) {
    if (!g_netMonitor) return;
    g_netMonitor->ProcessTcpIpEvent(pEvent);
}

void NetSpeedMonitor::ProcessTcpIpEvent(PEVENT_RECORD pEvent) {
    // Only process kernel trace events (EventTraceGuid, not SystemTraceControlGuid)
    if (!IsEqualGUID(pEvent->EventHeader.ProviderId, EVENT_TRACE_GUID))
        return;

    USHORT eventId = pEvent->EventHeader.EventDescriptor.Id;
    if (!IsTcpIpEvent(eventId))
        return;

    bool isSend = IsTcpIpSendEvent(eventId);

    // Use TdhGetProperty to extract PID and size from the event payload
    DWORD pid = 0;
    ULONG sizeVal = 0;

    PROPERTY_DATA_DESCRIPTOR pidDesc = {};
    pidDesc.PropertyName = (ULONGLONG)L"PID";
    pidDesc.ArrayIndex = ULONG_MAX;
    ULONG pidSize = sizeof(pid);
    if (TdhGetProperty(pEvent, 0, nullptr, 1, &pidDesc, pidSize, (PBYTE)&pid) != ERROR_SUCCESS) {
        // Try alternative property name "ProcessId"
        PROPERTY_DATA_DESCRIPTOR altDesc = {};
        altDesc.PropertyName = (ULONGLONG)L"ProcessId";
        altDesc.ArrayIndex = ULONG_MAX;
        if (TdhGetProperty(pEvent, 0, nullptr, 1, &altDesc, pidSize, (PBYTE)&pid) != ERROR_SUCCESS) {
            pid = 0;
        }
    }

    PROPERTY_DATA_DESCRIPTOR sizeDesc = {};
    sizeDesc.PropertyName = (ULONGLONG)L"size";
    sizeDesc.ArrayIndex = ULONG_MAX;
    ULONG sizeSize = sizeof(sizeVal);
    if (TdhGetProperty(pEvent, 0, nullptr, 1, &sizeDesc, sizeSize, (PBYTE)&sizeVal) != ERROR_SUCCESS) {
        sizeVal = 0;
    }

    if (pid > 0 && sizeVal > 0 && sizeVal < 100 * 1024 * 1024) { // sanity: < 100MB per event
        EnterCriticalSection(&m_cs);
        if (isSend) {
            m_liveCounters[pid].sent += sizeVal;
        } else {
            m_liveCounters[pid].recv += sizeVal;
        }
        LeaveCriticalSection(&m_cs);
    }
}

DWORD WINAPI NetSpeedMonitor::TraceThreadProc(LPVOID param) {
    NetSpeedMonitor* self = (NetSpeedMonitor*)param;
    TRACEHANDLE h = (TRACEHANDLE)self->m_traceHandle;
    ProcessTrace(&h, 1, nullptr, nullptr);
    return 0;
}

bool NetSpeedMonitor::Start() {
    if (m_running) return true;

    g_netMonitor = this;

    // Try starting a kernel trace session for TCP/IP events
    const wchar_t* sessionName = L"MonitorToolNetTrace";
    size_t nameBytes = (wcslen(sessionName) + 1) * sizeof(wchar_t);
    size_t propsSize = sizeof(EVENT_TRACE_PROPERTIES) + nameBytes + 64;
    EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)calloc(1, propsSize);
    if (!props) return false;

    props->Wnode.BufferSize = (ULONG)propsSize;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->Wnode.Guid = SYSTEM_TRACE_CONTROL_GUID;
    props->LogFileMode = EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    props->EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP;
    wcscpy_s((wchar_t*)((BYTE*)props + props->LoggerNameOffset),
             nameBytes / sizeof(wchar_t), sessionName);

    ULONG status = StartTraceW((TRACEHANDLE*)&m_sessionHandle, sessionName, props);

    if (status != ERROR_SUCCESS && status != ERROR_ALREADY_EXISTS) {
        // Try alternate session name
        const wchar_t* altName = L"MonitorToolNet2";
        free(props);
        nameBytes = (wcslen(altName) + 1) * sizeof(wchar_t);
        propsSize = sizeof(EVENT_TRACE_PROPERTIES) + nameBytes + 64;
        props = (EVENT_TRACE_PROPERTIES*)calloc(1, propsSize);
        if (!props) return false;
        props->Wnode.BufferSize = (ULONG)propsSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1;
        props->Wnode.Guid = SYSTEM_TRACE_CONTROL_GUID;
        props->LogFileMode = EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP;
        wcscpy_s((wchar_t*)((BYTE*)props + props->LoggerNameOffset),
                 nameBytes / sizeof(wchar_t), altName);
        status = StartTraceW((TRACEHANDLE*)&m_sessionHandle, altName, props);
    }
    free(props);

    if (status != ERROR_SUCCESS && status != ERROR_ALREADY_EXISTS) {
        OutputDebugStringW(L"[NetSpeedMonitor] StartTrace failed (admin rights required)\r\n");
        m_sessionHandle = 0;
        return false;
    }

    // Open the trace for real-time consumption
    EVENT_TRACE_LOGFILEW logFile = {};
    logFile.LoggerName = (LPWSTR)sessionName;
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.EventRecordCallback = EventRecordCallback;

    TRACEHANDLE hTrace = OpenTraceW(&logFile);
    if (hTrace == INVALID_PROCESSTRACE_HANDLE) {
        logFile.LoggerName = (LPWSTR)L"MonitorToolNet2";
        hTrace = OpenTraceW(&logFile);
        if (hTrace == INVALID_PROCESSTRACE_HANDLE) {
            OutputDebugStringW(L"[NetSpeedMonitor] OpenTrace failed\r\n");
            ControlTraceW((TRACEHANDLE)m_sessionHandle, nullptr,
                          (PEVENT_TRACE_PROPERTIES)calloc(1, sizeof(EVENT_TRACE_PROPERTIES) + 1024),
                          EVENT_TRACE_CONTROL_STOP);
            m_sessionHandle = 0;
            return false;
        }
    }
    m_traceHandle = (UINT64)hTrace;

    m_stopRequested = false;
    m_running = true;

    m_thread = CreateThread(nullptr, 0, TraceThreadProc, this, 0, nullptr);
    if (!m_thread) {
        CloseTrace(hTrace);
        m_traceHandle = 0;
        m_running = false;
        return false;
    }

    OutputDebugStringW(L"[NetSpeedMonitor] Started successfully\r\n");
    return true;
}

void NetSpeedMonitor::Stop() {
    if (!m_running) return;

    m_stopRequested = true;
    m_running = false;

    if (m_traceHandle) {
        CloseTrace((TRACEHANDLE)m_traceHandle);
        m_traceHandle = 0;
    }

    if (m_thread) {
        WaitForSingleObject(m_thread, 5000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }

    if (m_sessionHandle) {
        size_t propsSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
        EVENT_TRACE_PROPERTIES* sprops = (EVENT_TRACE_PROPERTIES*)calloc(1, propsSize);
        if (sprops) {
            sprops->Wnode.BufferSize = (ULONG)propsSize;
            sprops->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            sprops->Wnode.Guid = SYSTEM_TRACE_CONTROL_GUID;
            ControlTraceW((TRACEHANDLE)m_sessionHandle, nullptr, sprops, EVENT_TRACE_CONTROL_STOP);
            free(sprops);
        }
        m_sessionHandle = 0;
    }

    g_netMonitor = nullptr;
    OutputDebugStringW(L"[NetSpeedMonitor] Stopped\r\n");
}

void NetSpeedMonitor::QueryDelta(DWORD pid, ULONGLONG& bytesSent, ULONGLONG& bytesRecv) {
    bytesSent = 0;
    bytesRecv = 0;
    if (!m_running) return;

    EnterCriticalSection(&m_cs);
    auto liveIt = m_liveCounters.find(pid);
    if (liveIt != m_liveCounters.end()) {
        ULONGLONG curSent = liveIt->second.sent;
        ULONGLONG curRecv = liveIt->second.recv;
        auto prevIt = m_prevCounters.find(pid);
        if (prevIt != m_prevCounters.end()) {
            if (curSent >= prevIt->second.sent)
                bytesSent = curSent - prevIt->second.sent;
            if (curRecv >= prevIt->second.recv)
                bytesRecv = curRecv - prevIt->second.recv;
        }
        m_prevCounters[pid].sent = curSent;
        m_prevCounters[pid].recv = curRecv;
    }
    LeaveCriticalSection(&m_cs);
}
