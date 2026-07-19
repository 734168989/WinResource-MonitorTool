// DataModels.h - Core data structures for system resource monitoring
#pragma once
#include <windows.h>
#include <cstdlib>

#define MAX_PROCESS_NAME 260
#define MAX_TIMESTAMP_LEN 20
#define MAX_BUFFER_ROWS 2000000

struct SystemMonitorData {
    wchar_t timestamp[MAX_TIMESTAMP_LEN];
    double  runSeconds;
    double  cpuUsage;
    double  memoryTotalGB;
    double  memoryAvailableGB;
    double  memoryUsedGB;
    double  memoryUsage;
    double  netSendSpeed;
    double  netRecvSpeed;
};

struct ProcessMonitorData {
    wchar_t timestamp[MAX_TIMESTAMP_LEN];
    double  runSeconds;
    DWORD   pid;
    double  cpuUsage;
    double  memoryUsage;
    double  memoryUsedMB;
    double  netSendSpeed;
    double  netRecvSpeed;
};

struct MonitorProcess {
    wchar_t name[MAX_PROCESS_NAME];
    bool    enabled;
};

struct MonitorConfig {
    MonitorProcess* processes;
    int             processCount;
    int             processCapacity;
    bool            monitorCpu;
    bool            monitorMemory;
    bool            monitorNetwork;
    bool            generateReport;     // 是否自动生成 HTML 监测报告
    int             samplePeriod;       // 1-60 seconds
    wchar_t         netUnit[16];        // "Kbps", "Mbps", "Gbps"
    wchar_t         netInterface[256];  // interface name or "全部"
    wchar_t         outputDir[MAX_PATH];
};

// Sentinel array to guarantee cfg->processes is never NULL after init
static MonitorProcess g_emptyProcess;

// Initialize config with clean empty state (no default processes)
inline void InitEmptyConfig(MonitorConfig* cfg) {
    cfg->processes = (MonitorProcess*)malloc(16 * sizeof(MonitorProcess));
    if (!cfg->processes) {
        cfg->processes = &g_emptyProcess;
        cfg->processCount = 0;
        cfg->processCapacity = 1;
        return;
    }
    cfg->processCount = 0;
    cfg->processCapacity = 16;
    cfg->monitorCpu = true;
    cfg->monitorMemory = true;
    cfg->monitorNetwork = true;
    cfg->generateReport = true;
    cfg->samplePeriod = 5;
    wcscpy_s(cfg->netUnit, 16, L"Mbps");
    wcscpy_s(cfg->netInterface, 256, L"全部");
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    wcscpy_s(cfg->outputDir, MAX_PATH, exePath);
}

// Initialize config with defaults
inline void InitDefaultConfig(MonitorConfig* cfg) {
    cfg->processes = (MonitorProcess*)malloc(16 * sizeof(MonitorProcess));
    if (!cfg->processes) {
        cfg->processes = &g_emptyProcess;
        cfg->processCount = 0;
        cfg->processCapacity = 1;
        return;
    }
    cfg->processCount = 1;
    cfg->processCapacity = 16;
    wcscpy_s(cfg->processes[0].name, MAX_PROCESS_NAME, L"MPrintExp.exe");
    cfg->processes[0].enabled = true;
    cfg->monitorCpu = true;
    cfg->monitorMemory = true;
    cfg->monitorNetwork = true;
    cfg->generateReport = true;
    cfg->samplePeriod = 5;
    wcscpy_s(cfg->netUnit, 16, L"Mbps");
    wcscpy_s(cfg->netInterface, 256, L"全部");
    // 默认输出目录为 exe 所在目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    wcscpy_s(cfg->outputDir, MAX_PATH, exePath);
}

inline void FreeConfig(MonitorConfig* cfg) {
    if (cfg->processes && cfg->processes != &g_emptyProcess) {
        free(cfg->processes);
    }
    cfg->processes = &g_emptyProcess;
    cfg->processCount = 0;
    cfg->processCapacity = 1;
}

// Add process to config, returns true if added
inline bool ConfigAddProcess(MonitorConfig* cfg, const wchar_t* name) {
    // Check for duplicate
    for (int i = 0; i < cfg->processCount; i++) {
        if (_wcsicmp(cfg->processes[i].name, name) == 0)
            return false;
    }
    // Expand if needed
    if (cfg->processCount >= cfg->processCapacity) {
        int newCap = cfg->processCapacity * 2;
        MonitorProcess* newProcs;
        if (cfg->processes == &g_emptyProcess) {
            // Sentinel — can't realloc, use malloc + copy
            newProcs = (MonitorProcess*)malloc(newCap * sizeof(MonitorProcess));
            if (!newProcs) return false;
        } else {
            newProcs = (MonitorProcess*)realloc(cfg->processes, newCap * sizeof(MonitorProcess));
            if (!newProcs) return false;
        }
        cfg->processes = newProcs;
        cfg->processCapacity = newCap;
    }
    wcscpy_s(cfg->processes[cfg->processCount].name, MAX_PROCESS_NAME, name);
    cfg->processes[cfg->processCount].enabled = true;
    cfg->processCount++;
    return true;
}
