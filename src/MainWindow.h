// MainWindow.h - Main application window (Win32)
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <map>
#include <string>
#include "DataBuffer.h"
#include "SystemMonitor.h"
#include "ProcessMonitor.h"
#include "ExcelExporter.h"
#include "ConfigManager.h"
#include "NetSpeedMonitor.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

// Column indices for system ListView
enum SystemCol {
    SYS_COL_TIME = 0,
    SYS_COL_RUN_SEC,
    SYS_COL_CPU,
    SYS_COL_MEM_TOTAL,
    SYS_COL_MEM_AVAIL,
    SYS_COL_MEM_USED,
    SYS_COL_MEM_USAGE,
    SYS_COL_NET_SEND,
    SYS_COL_NET_RECV,
    SYS_COL_COUNT
};

// Column indices for process ListView
enum ProcessCol {
    PROC_COL_TIME = 0,
    PROC_COL_RUN_SEC,
    PROC_COL_PID,
    PROC_COL_CPU,
    PROC_COL_MEM_USAGE,
    PROC_COL_MEM_USED,
    PROC_COL_NET_SEND,
    PROC_COL_NET_RECV,
    PROC_COL_COUNT
};

// Per-process tab data
struct ProcessTabInfo {
    std::wstring processName;
    HWND hListView;
    int tabIndex; // index in the tab control
};

// Main window state
struct MainWindowState {
    HWND hMainWnd;
    HINSTANCE hInst;

    // Group boxes
    HWND hConfigGroup;
    HWND hItemsGroup;
    HWND hControlGroup;

    // Config controls
    HWND hProcessNameEdit;
    HWND hAddBtn;
    HWND hDeleteAllBtn;
    HWND hSaveConfigBtn;
    HWND hProcessListView;  // Process config list (with checkboxes)

    // Monitor item controls
    HWND hCpuCheck;
    HWND hMemoryCheck;
    HWND hNetworkCheck;
    HWND hNetUnitCombo;

    // Sample & interface
    HWND hSamplePeriodEdit;
    HWND hNetInterfaceCombo;
    HWND hNetInterfaceLabel;
    HWND hRefreshInterfaceBtn;
    HWND hSampleLabel;
    HWND hSecondLabel;
    HWND hOutputDirLabel;
    HWND hHelpBtn;

    // Output
    HWND hOutputDirEdit;
    HWND hBrowseDirBtn;

    // Control buttons
    HWND hStartBtn;
    HWND hStopBtn;

    // Data display
    HWND hTab;              // Tab control
    HWND hSystemListView;   // System data ListView (always visible in tab 0)
    std::vector<ProcessTabInfo> processTabs;

    // Status bar
    HWND hStatusBar;
    HWND hTopmostBtn;
    HWND hStatusLabel;     // 监测状态下方的彩色状态文字
    int statusBarHeight;
    COLORREF statusColor;  // 当前状态文字颜色

    // State
    bool isMonitoring;
    bool isTopmost;
    bool suppressListEvents;  // suppress LVN_ITEMCHANGED during RefreshProcessList
    HANDLE hMonitorThread;
    HANDLE hStopEvent;
    double monitorStartTime;

    // Core components
    DataBuffer dataBuffer;
    SystemMonitor systemMonitor;
    NetSpeedMonitor netSpeedMonitor;
    std::vector<ProcessMonitor*> processMonitors;
    ExcelExporter excelExporter;

    // Display tracking
    int lastFlushSystemIndex;
    DWORD lastFlushTick;

    // Fonts
    HFONT hDefaultFont;
    HFONT hBoldFont;           // 粗体状态标签

    // Display offset tracking: DataBuffer index of the first ListView row.
    // Fixes index sync when ListView is trimmed for display performance.
    int m_sysDisplayOffset;
    std::map<std::wstring, int> m_procDisplayOffsets;

    // Config path
    wchar_t configPath[MAX_PATH];

    // System columns widths
    int sysColWidths[SYS_COL_COUNT];
    int procColWidths[PROC_COL_COUNT];
};

// Window procedure
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Window registration & creation
bool RegisterMainWindowClass(HINSTANCE hInstance);
HWND CreateMainWindow(HINSTANCE hInstance);

// Initialization
bool InitMainWindow(HWND hWnd, MainWindowState* s);

// Layout helpers
void LayoutControls(MainWindowState* s);
void CreateChildControls(HWND hWnd, MainWindowState* s);
void InitSystemListView(MainWindowState* s);
HWND CreateProcessDataListView(HWND hParent, MainWindowState* s);

// Process config list
void RefreshProcessList(MainWindowState* s);

// Tab management
void AddProcessTab(MainWindowState* s, const wchar_t* name);
void RemoveProcessTab(MainWindowState* s, const wchar_t* name);
void ToggleProcessTab(MainWindowState* s, const wchar_t* name, bool enabled);
void RebuildProcessTabs(MainWindowState* s);
void OnTabChanged(MainWindowState* s);

// Monitor control
void StartMonitoring(MainWindowState* s);
void StopMonitoring(MainWindowState* s);
DWORD WINAPI MonitorThreadProc(LPVOID param);

// Display update
void UpdateDisplay(MainWindowState* s);
void UpdateSystemListView(MainWindowState* s);
void UpdateProcessListView(MainWindowState* s, ProcessTabInfo& tab);

// UI state
void SetControlsEnabled(MainWindowState* s, bool enabled);
void UpdateNetworkControlsEnabled(MainWindowState* s);
void UpdateNetUnitHeaders(MainWindowState* s, const wchar_t* unit);
void UpdateStatus(MainWindowState* s, const wchar_t* status, COLORREF color);

// Data export
void ExportData(MainWindowState* s);

// Context menu
void ShowContextMenu(HWND hWnd, HWND hListView, int x, int y, MainWindowState* s);

// Config sync
void SyncUIFromConfig(MainWindowState* s);
void SyncConfigFromUI(MainWindowState* s);

// Help dialog
void ShowHelpDialog(HWND hParent);
