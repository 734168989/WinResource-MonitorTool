// MainWindow.cpp - Full Win32 UI implementation
#include "MainWindow.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <tlhelp32.h>

// ============================================================================
// Global pointer for window proc access
// ============================================================================
static MainWindowState* g_state = nullptr;

// ============================================================================
// Window Registration & Creation
// ============================================================================
bool RegisterMainWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = 0;  // 不使用 CS_HREDRAW/CS_VREDRAW 避免拖动时花屏
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = L"MonitorToolMainWindow";
    wc.hIconSm = nullptr;

    if (!RegisterClassExW(&wc)) {
        // Try without icon if it fails
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        return RegisterClassExW(&wc) != 0;
    }
    return true;
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 880;
    int winH = 780;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
        L"MonitorToolMainWindow",
        L"挂机电脑资源监测软件 V3.5",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    // Force taskbar to show the correct icon immediately
    if (hWnd) {
        HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
        if (hIcon) {
            SendMessageW(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
            SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
    return hWnd;
}

// ============================================================================
// Window Procedure
// ============================================================================
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindowState* s = g_state;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        s = new MainWindowState();
        g_state = s;
        s->hMainWnd = hWnd;
        s->hInst = cs->hInstance;

        // Build config path
        GetModuleFileNameW(nullptr, s->configPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(s->configPath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(s->configPath, MAX_PATH, L"config.json");

        // Load config
        ConfigManager::Instance().LoadConfig(s->configPath);

        // 修正默认输出目录：如果旧配置文件里存的是 ".\\"，替换为 exe 所在目录
        {
            auto& cfg = ConfigManager::Instance().GetConfig();
            if (wcscmp(cfg.outputDir, L".\\") == 0 || cfg.outputDir[0] == L'\0') {
                wchar_t exeDir[MAX_PATH];
                wcscpy_s(exeDir, MAX_PATH, s->configPath);
                wchar_t* sep = wcsrchr(exeDir, L'\\');
                if (sep) *sep = L'\0';
                wcscpy_s(cfg.outputDir, MAX_PATH, exeDir);
            }
        }

        // Initialize state
        s->isMonitoring = false;
        s->isTopmost = false;
        s->suppressListEvents = false;
        s->statusColor = RGB(0, 180, 0);  // green = 就绪
        s->hMonitorThread = nullptr;
        s->hStopEvent = nullptr;
        s->monitorStartTime = 0.0;
        s->lastFlushSystemIndex = 0;
        s->lastFlushTick = 0;
        s->lastHtmlFlushTick = 0;
        s->lastDataTrimTick = 0;
        s->isFlushing = false;
        s->rotationPart = 0;
        s->statusBarHeight = 28;

        // Display offset tracking
        s->m_sysDisplayOffset = 0;

        // Default listview column widths
        s->sysColWidths[0] = 135; s->sysColWidths[1] = 100;
        s->sysColWidths[2] = 70;  s->sysColWidths[3] = 110;
        s->sysColWidths[4] = 110;  s->sysColWidths[5] = 110;
        s->sysColWidths[6] = 110;  s->sysColWidths[7] = 110;
        s->sysColWidths[8] = 110;

        s->procColWidths[0] = 135; s->procColWidths[1] = 100;
        s->procColWidths[2] = 70;  s->procColWidths[3] = 70;
        s->procColWidths[4] = 100;  s->procColWidths[5] = 110;
        s->procColWidths[6] = 110;  s->procColWidths[7] = 110;

        // Create font
        s->hDefaultFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // NetCombo fonts: bold green for connected, normal for disconnected
        {
            LOGFONTW lf = {};
            HFONT hStock = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            if (GetObjectW(hStock, sizeof(lf), &lf)) {
                lf.lfWeight = FW_BOLD;
                s->hNetComboBoldFont = CreateFontIndirectW(&lf);
            } else {
                s->hNetComboBoldFont = nullptr;
            }
        }
        s->hNetComboNormalFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        InitMainWindow(hWnd, s);

        // Set a display update timer (200ms)
        SetTimer(hWnd, IDT_DISPLAY_UPDATE, 200, nullptr);

        return 0;
    }

    case WM_SIZE: {
        if (s) {
            LayoutControls(s);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        // 交由 DefWindowProc 使用窗口类的 hbrBackground 进行标准背景擦除
        break;

    case WM_CTLCOLORBTN: {
        // 让 Group Box 用系统背景色填充内部，避免黑色区域
        wchar_t className[64];
        GetClassNameW((HWND)lParam, className, 64);
        if (wcscmp(className, L"Button") == 0) {
            DWORD style = GetWindowStyle((HWND)lParam);
            if ((style & BS_TYPEMASK) == BS_GROUPBOX) {
                SetBkMode((HDC)wParam, TRANSPARENT);
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        // Status label: use dynamic color
        if (s && hCtrl == s->hStatusLabel) {
            SetTextColor(hdc, s->statusColor);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        // Save-path labels: link-like blue
        if (s && (hCtrl == s->hSavePathExcel || hCtrl == s->hSavePathHtml)) {
            SetTextColor(hdc, RGB(0, 120, 215));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_TIMER: {
        if (wParam == IDT_DISPLAY_UPDATE && s && s->isMonitoring) {
            UpdateDisplay(s);
            // Flush Excel with adaptive interval + re-entrancy guard
            DWORD tick = GetTickCount();
            size_t sysCount = s->dataBuffer.GetSystemCount();
            if ((int)sysCount > s->lastFlushSystemIndex && !s->isFlushing) {
                int newRows = (int)sysCount - s->lastFlushSystemIndex;
                // Interval: 10s normal, 30s when idle.  Much less frequent than
                // the old 2s to avoid excessive memory churn from full copies.
                DWORD interval = (newRows > 50) ? 10000 : 10000;
                if ((tick - s->lastFlushTick) >= interval) {
                    s->isFlushing = true;
                    s->lastFlushSystemIndex = (int)sysCount;
                    s->lastFlushTick = tick;
                    {
                        auto& cfg = ConfigManager::Instance().GetConfig();
                        auto sysCopy = s->dataBuffer.GetSystemDataCopy();
                        std::vector<MonitorProcess> procs;
                        std::vector<std::vector<ProcessMonitorData>> procData;
                        for (auto* pm : s->processMonitors) {
                            MonitorProcess mp = {};
                            wcscpy_s(mp.name, MAX_PROCESS_NAME, pm->GetProcessName());
                            mp.enabled = true;
                            procs.push_back(mp);
                            procData.push_back(s->dataBuffer.GetProcessDataCopy(pm->GetProcessName()));
                        }
                        s->excelExporter.FlushExport(sysCopy, procs, procData, cfg.netUnit, cfg.netInterface);
                        // Hint to the allocator that large vectors can release pages
                        sysCopy.clear(); sysCopy.shrink_to_fit();
                        for (auto& v : procData) { v.clear(); v.shrink_to_fit(); }
                    }
                    s->isFlushing = false;
                }
            }
            // File rotation: when DataBuffer trimmed old rows, finalize current
            // file and start a new _PartN file to avoid silent data loss.
            if (s->dataBuffer.HasTrimmed() && !s->isFlushing) {
                s->isFlushing = true;
                s->dataBuffer.ResetTrimmed();
                // Finalize current file
                auto& cfg = ConfigManager::Instance().GetConfig();
                auto sysCopy = s->dataBuffer.GetSystemDataCopy();
                std::vector<MonitorProcess> procs;
                std::vector<std::vector<ProcessMonitorData>> procData;
                for (auto* pm : s->processMonitors) {
                    MonitorProcess mp = {};
                    wcscpy_s(mp.name, MAX_PROCESS_NAME, pm->GetProcessName());
                    mp.enabled = true;
                    procs.push_back(mp);
                    procData.push_back(s->dataBuffer.GetProcessDataCopy(pm->GetProcessName()));
                }
                s->excelExporter.FlushExport(sysCopy, procs, procData, cfg.netUnit, cfg.netInterface);
                s->excelExporter.EndExport();
                // Clear buffer and start new part
                s->dataBuffer.Clear();
                s->lastFlushSystemIndex = 0;
                s->m_sysDisplayOffset = 0;
                for (auto& tab : s->processTabs)
                    tab.lastDataIdx = -1;
                s->rotationPart++;
                s->excelExporter.SetNetUnit(cfg.netUnit);
                s->excelExporter.SetNetInterface(cfg.netInterface);
                s->excelExporter.BeginExportPart(cfg.outputDir, s->monitorStartTime, s->rotationPart);
                s->isFlushing = false;
            }
            // HTML report: regenerate every 2 minutes
            auto& reportCfg = ConfigManager::Instance().GetConfig();
            if (reportCfg.generateReport && (tick - s->lastHtmlFlushTick) >= 120000) {
                s->lastHtmlFlushTick = tick;
                {
                    auto sysCopy2 = s->dataBuffer.GetSystemDataCopy();
                    std::vector<MonitorProcess> procs2;
                    std::vector<std::vector<ProcessMonitorData>> procData2;
                    for (auto* pm : s->processMonitors) {
                        MonitorProcess mp = {};
                        wcscpy_s(mp.name, MAX_PROCESS_NAME, pm->GetProcessName());
                        mp.enabled = true;
                        procs2.push_back(mp);
                        procData2.push_back(s->dataBuffer.GetProcessDataCopy(pm->GetProcessName()));
                    }
                    HtmlChartExporter::Export(reportCfg.outputDir, s->monitorStartTime, sysCopy2, procs2, procData2, reportCfg.netInterface);
                    // Release memory back to OS after large export
                    sysCopy2.clear(); sysCopy2.shrink_to_fit();
                    for (auto& v : procData2) { v.clear(); v.shrink_to_fit(); }
                }
            }

            // Periodic compaction + trim: shrink_to_fit every 2 min,
            // then trim DataBuffer to ~15 min window (data is safe on disk)
            if ((tick - s->lastDataTrimTick) >= 120000) {
                s->lastDataTrimTick = tick;
                size_t before = s->dataBuffer.GetTotalCount();
                s->dataBuffer.CompactCapacity();
                auto& trimCfg = ConfigManager::Instance().GetConfig();
                int period = trimCfg.samplePeriod;
                if (period < 1) period = 1;
                size_t keepRows = (size_t)((5 * 60) / period);
                s->dataBuffer.TrimOldData(keepRows);
                size_t after = s->dataBuffer.GetTotalCount();
                wchar_t dbg[256];
                PROCESS_MEMORY_COUNTERS_EX pmc = { sizeof(pmc) };
                SIZE_T privMB = 0;
                if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
                    privMB = pmc.PrivateUsage / (1024 * 1024);
                // Compact process heap to release freed pages
                HeapCompact(GetProcessHeap(), 0);
                swprintf_s(dbg, 256, L"[MemTrim] rows:%zu→%zu removed:%zu  PrivateUsage:%zuMB\r\n",
                           before, after, before - after, privMB);
                OutputDebugStringW(dbg);
                // Reset display offsets since indices shifted
                s->m_sysDisplayOffset = 0;
                s->m_procDisplayOffsets.clear();
                for (auto& tab : s->processTabs)
                    tab.lastDataIdx = -1;
                s->lastFlushSystemIndex = 0;
                ListView_DeleteAllItems(s->hSystemListView);
                for (auto& tab : s->processTabs)
                    ListView_DeleteAllItems(tab.hListView);
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (!s) break;

        switch (id) {
        case IDC_ADD_BTN: {
            wchar_t name[260];
            GetWindowTextW(s->hProcessNameEdit, name, 260);
            // Trim
            wchar_t* trim = name;
            while (*trim == L' ') trim++;
            int len = (int)wcslen(trim);
            while (len > 0 && trim[len-1] == L' ') trim[--len] = L'\0';
            if (len == 0) {
                MessageBoxW(hWnd, L"请输入软件名称", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            // Check if process exists
            bool exists = false;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, trim) == 0) {
                            exists = true;
                            break;
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }

            if (!exists) {
                wchar_t confirmMsg[512];
                swprintf_s(confirmMsg, 512, L"未检测到进程 '%s' 正在运行，是否确认添加？", trim);
                if (MessageBoxW(hWnd, confirmMsg, L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    break;
            }

            if (ConfigManager::Instance().AddProcess(trim)) {
                RefreshProcessList(s);
                SetWindowTextW(s->hProcessNameEdit, L"");
                RebuildProcessTabs(s, trim);  // 切换到新添加的软件页签
            } else {
                MessageBoxW(hWnd, L"该软件已在监测列表中", L"提示", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case IDC_DELETE_ALL_BTN: {
            auto& cfg = ConfigManager::Instance().GetConfig();
            if (cfg.processCount == 0) break;
            if (MessageBoxW(hWnd, L"确定要删除所有监测软件吗？", L"确认",
                           MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ConfigManager::Instance().ClearProcesses();
                RefreshProcessList(s);
                RebuildProcessTabs(s);
            }
            break;
        }

        case IDC_SAVE_CONFIG_BTN: {
            SyncConfigFromUI(s);
            if (ConfigManager::Instance().SaveConfig(s->configPath))
                MessageBoxW(hWnd, L"配置已保存", L"成功", MB_OK | MB_ICONINFORMATION);
            else
                MessageBoxW(hWnd, L"保存配置失败", L"错误", MB_OK | MB_ICONERROR);
            break;
        }

        case IDC_NETWORK_CHECK:
            UpdateNetworkControlsEnabled(s);
            break;

        case IDC_REFRESH_INTERFACE_BTN: {
            auto ifaces = s->systemMonitor.GetNetworkInterfaces();
            ComboBox_ResetContent(s->hNetInterfaceCombo);
            for (auto& iface : ifaces) {
                int idx = ComboBox_AddString(s->hNetInterfaceCombo, iface.c_str());
                bool connected = s->systemMonitor.IsInterfaceConnected(iface);
                ComboBox_SetItemData(s->hNetInterfaceCombo, idx, connected ? 1 : 0);
            }
            ComboBox_SetCurSel(s->hNetInterfaceCombo, 0);
            break;
        }

        case IDC_BROWSE_DIR_BTN: {
            // 暂停定时器，防止浏览对话框的消息循环触发 UpdateDisplay
            // 与监控线程产生临界区竞争导致卡死
            KillTimer(hWnd, IDT_DISPLAY_UPDATE);

            wchar_t startPath[MAX_PATH];
            GetWindowTextW(s->hOutputDirEdit, startPath, MAX_PATH);

            BROWSEINFOW bi = {};
            bi.hwndOwner = hWnd;
            bi.lpszTitle = L"选择输出目录";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
            bi.lpfn = nullptr;
            bi.lParam = 0;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(s->hOutputDirEdit, path);
                }
                CoTaskMemFree(pidl);
            }

            // 恢复定时器
            if (s->isMonitoring)
                SetTimer(hWnd, IDT_DISPLAY_UPDATE, 200, nullptr);
            break;
        }

        case IDC_START_BTN:
            StartMonitoring(s);
            break;

        case IDC_STOP_BTN:
            StopMonitoring(s);
            break;

        case IDC_TOPMOST_BTN: {
            s->isTopmost = !s->isTopmost;
            SetWindowPos(hWnd, s->isTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            // Toggle button text for clear visual feedback
            SetWindowTextW(s->hTopmostBtn, s->isTopmost ? L"取消置顶" : L"置顶");
            break;
        }

        case IDC_HELP_BTN:
            ShowHelpDialog(hWnd);
            break;

        case IDC_NET_UNIT_COMBO:
            if (code == CBN_SELCHANGE) {
                int idx = ComboBox_GetCurSel(s->hNetUnitCombo);
                if (idx >= 0) {
                    wchar_t unit[16];
                    ComboBox_GetLBText(s->hNetUnitCombo, idx, unit);
                    s->systemMonitor.SetNetUnit(unit);
                    s->excelExporter.SetNetUnit(unit);
                    for (auto* pm : s->processMonitors)
                        pm->SetNetUnit(unit);
                    UpdateNetUnitHeaders(s, unit);
                }
            }
            break;

        }      // end switch(id)

        // Click save-path label → open folder (only if click is on text)
        if (code == STN_CLICKED &&
            (id == IDC_SAVE_PATH_EXCEL || id == IDC_SAVE_PATH_HTML)) {
            HWND hLabel = (id == IDC_SAVE_PATH_EXCEL) ? s->hSavePathExcel : s->hSavePathHtml;
            wchar_t buf[MAX_PATH];
            GetWindowTextW(hLabel, buf, MAX_PATH);
            if (buf[0]) {
                HDC hdc = GetDC(hLabel);
                HFONT hFont = (HFONT)SendMessageW(hLabel, WM_GETFONT, 0, 0);
                if (hFont) SelectObject(hdc, hFont);
                SIZE sz = {}; GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
                ReleaseDC(hLabel, hdc);
                DWORD pos = GetMessagePos();
                POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
                ScreenToClient(hLabel, &pt);
                if (pt.x >= 0 && pt.x < sz.cx) {
                    const std::wstring& path = (id == IDC_SAVE_PATH_EXCEL)
                        ? s->savedExcelPath : s->savedHtmlPath;
                    if (!path.empty()) {
                        std::wstring dir = path;
                        size_t p = dir.find_last_of(L"\\/");
                        if (p != std::wstring::npos) dir.resize(p);
                        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
            }
        }
        return 0;
    }          // end case WM_COMMAND

    case WM_NCRBUTTONDOWN: {
        // Right-click on title bar → suppress default system menu, show our own
        if (wParam == HTCAPTION) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_OPEN_APP_FOLDER, L"打开软件所在目录(&O)");
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
            if (cmd == IDM_OPEN_APP_FOLDER) {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                wchar_t* lastSlash = wcsrchr(exePath, L'\\');
                if (lastSlash) {
                    *lastSlash = L'\0';
                    ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;  // Prevent DefWindowProc from showing default system menu
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR* nmh = (NMHDR*)lParam;
        if (!s) break;

        // Process list - handle delete click and checkbox toggle
        if (nmh->idFrom == IDC_PROCESS_LIST) {
            // LVN_ITEMCHANGED: 系统自动切换勾选状态后同步到配置
            if (nmh->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
                if ((nmlv->uChanged & LVIF_STATE) && nmlv->iItem >= 0) {
                    // 仅当勾选状态确实变化时才处理
                    UINT oldImg = (nmlv->uOldState & LVIS_STATEIMAGEMASK) >> 12;
                    UINT newImg = (nmlv->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                    if (oldImg != newImg && !s->suppressListEvents) {
                        auto& cfg = ConfigManager::Instance().GetConfig();
                        if (nmlv->iItem < cfg.processCount) {
                            cfg.processes[nmlv->iItem].enabled = (newImg == 2);
                            // 实时更新对应的日志页签
                            ToggleProcessTab(s, cfg.processes[nmlv->iItem].name,
                                           cfg.processes[nmlv->iItem].enabled);
                        }
                    }
                }
                return 0;
            }
            // NM_CLICK: 仅处理"删除"列
            if (nmh->code == NM_CLICK || nmh->code == NM_DBLCLK) {
                NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)lParam;
                if (nmia->iItem >= 0 && nmia->iItem < ConfigManager::Instance().GetConfig().processCount) {
                    if (nmia->iSubItem == 2) {
                        ConfigManager::Instance().RemoveProcess(nmia->iItem);
                        RefreshProcessList(s);
                        RebuildProcessTabs(s);
                    }
                }
            }
            return 0;
        }

        // Tab control selection changed
        if (nmh->idFrom == IDC_DATA_TAB && nmh->code == TCN_SELCHANGE) {
            OnTabChanged(s);
            return 0;
        }

        // Tooltip text for save-path labels
        if (nmh->code == TTN_NEEDTEXTW) {
            NMTTDISPINFOW* ttdi = (NMTTDISPINFOW*)lParam;
            if (ttdi->hdr.idFrom == (UINT_PTR)s->hSavePathExcel)
                ttdi->lpszText = (LPWSTR)s->savedExcelPath.c_str();
            else if (ttdi->hdr.idFrom == (UINT_PTR)s->hSavePathHtml)
                ttdi->lpszText = (LPWSTR)s->savedHtmlPath.c_str();
            return 0;
        }
        break;
    }

    case WM_CONTEXTMENU: {
        if (!s) break;
        HWND hTarget = (HWND)wParam;
        if (hTarget == s->hSystemListView) {
            ShowContextMenu(hWnd, hTarget, LOWORD(lParam), HIWORD(lParam), s);
        }
        // Check if target is one of our process listviews
        for (auto& tab : s->processTabs) {
            if (hTarget == tab.hListView) {
                ShowContextMenu(hWnd, hTarget, LOWORD(lParam), HIWORD(lParam), s);
                break;
            }
        }
        return 0;
    }

    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
        if (mis->CtlID == IDC_NET_INTERFACE_COMBO)
            mis->itemHeight = 20;
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == IDC_NET_INTERFACE_COMBO && dis->itemID != (UINT)-1) {
            wchar_t buf[256];
            ComboBox_GetLBText(dis->hwndItem, dis->itemID, buf);
            bool connected = (ComboBox_GetItemData(dis->hwndItem, dis->itemID) != 0);

            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            bool selected = (dis->itemState & ODS_SELECTED);

            // Background
            if (selected) {
                FillRect(hdc, &rc, (HBRUSH)(COLOR_HIGHLIGHT + 1));
                SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            } else {
                FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
                SetTextColor(hdc, connected ? RGB(0, 150, 0) : GetSysColor(COLOR_WINDOWTEXT));
            }

            SetBkMode(hdc, TRANSPARENT);
            HFONT hFont = (connected && s && s->hNetComboBoldFont)
                ? s->hNetComboBoldFont : s->hNetComboNormalFont;
            SelectObject(hdc, hFont);

            rc.left += 2;
            DrawTextW(hdc, buf, -1, &rc,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            if (dis->itemState & ODS_FOCUS && !selected)
                DrawFocusRect(hdc, &dis->rcItem);

            return TRUE;
        }
        break;
    }

    case WM_CLOSE: {
        if (s && s->isMonitoring) {
            if (MessageBoxW(hWnd, L"监测正在进行中，确定要退出吗？",
                           L"确认", MB_YESNO | MB_ICONQUESTION) == IDNO)
                return 0;
            // Kill the display-update timer BEFORE StopMonitoring so the
            // WM_TIMER handler cannot fire while we are tearing down
            // processMonitors / DataBuffer / NetSpeedMonitor.
            KillTimer(hWnd, IDT_DISPLAY_UPDATE);
            StopMonitoring(s);
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        if (s) {
            KillTimer(hWnd, IDT_DISPLAY_UPDATE);
            if (s->isMonitoring)
                StopMonitoring(s);
            for (auto* pm : s->processMonitors)
                delete pm;
            s->processMonitors.clear();
            if (s->hBoldFont) DeleteObject(s->hBoldFont);
            if (s->hNetComboBoldFont) DeleteObject(s->hNetComboBoldFont);
            delete s;
            g_state = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Initialization
// ============================================================================
bool InitMainWindow(HWND hWnd, MainWindowState* s) {
    CreateChildControls(hWnd, s);
    InitSystemListView(s);
    SyncUIFromConfig(s);
    RefreshProcessList(s);
    RebuildProcessTabs(s);

    // Initialize system monitor baseline — SetNetInterface before Initialize()
    {
        auto& netCfg = ConfigManager::Instance().GetConfig();
        s->systemMonitor.SetNetUnit(netCfg.netUnit);
        s->systemMonitor.SetNetInterface(netCfg.netInterface);
        s->systemMonitor.Initialize();
    }

    return true;
}

// ============================================================================
// Create all child controls
// ============================================================================
void CreateChildControls(HWND hParent, MainWindowState* s) {
    HINSTANCE hi = s->hInst;
    int yBase = 8;

    // ---- Monitor Config (no outer group box) ----
    int gy = 22;
    s->hProcessNameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        20, yBase + gy, 300, 24, hParent, (HMENU)IDC_PROCESS_NAME_EDIT, hi, nullptr);

    s->hAddBtn = CreateWindowExW(0, L"BUTTON", L"添加",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        328, yBase + gy, 60, 24, hParent, (HMENU)IDC_ADD_BTN, hi, nullptr);

    s->hDeleteAllBtn = CreateWindowExW(0, L"BUTTON", L"全部删除",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        394, yBase + gy, 80, 24, hParent, (HMENU)IDC_DELETE_ALL_BTN, hi, nullptr);

    s->hSaveConfigBtn = CreateWindowExW(0, L"BUTTON", L"保存配置",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        480, yBase + gy, 80, 24, hParent, (HMENU)IDC_SAVE_CONFIG_BTN, hi, nullptr);

    // Help button — right-aligned on the same row
    s->hHelpBtn = CreateWindowExW(0, L"BUTTON", L"?",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        810, yBase + gy, 30, 24, hParent, (HMENU)IDC_HELP_BTN, hi, nullptr);

    gy += 30;
    // Process ListView with checkboxes
    s->hProcessListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS,
        20, yBase + gy, 834, 90, hParent, (HMENU)IDC_PROCESS_LIST, hi, nullptr);
    ListView_SetExtendedListViewStyle(s->hProcessListView, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Smaller font so ~3 items fit without scrolling
    {
        HDC hdc = GetDC(hParent);
        int ptH = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hParent, hdc);
        HFONT hFont = CreateFontW(ptH, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        if (hFont)
            SendMessageW(s->hProcessListView, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // Columns: checkbox, name, action — widths fill ListView for aligned gridlines
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_CENTER;
    lvc.cx = 24;
    lvc.pszText = (LPWSTR)L"";
    ListView_InsertColumn(s->hProcessListView, 0, &lvc);
    lvc.cx = 740;
    lvc.fmt = LVCFMT_LEFT;
    lvc.pszText = (LPWSTR)L"软件名称";
    ListView_InsertColumn(s->hProcessListView, 1, &lvc);
    lvc.cx = 60;
    lvc.fmt = LVCFMT_CENTER;
    lvc.pszText = (LPWSTR)L"操作";
    ListView_InsertColumn(s->hProcessListView, 2, &lvc);

    // ---- Monitor Items Group ----
    int itemsGroupY = yBase + 160;
    s->hItemsGroup = CreateWindowExW(0, L"BUTTON", L"监测项目设置",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        8, itemsGroupY, 874, 120, hParent, (HMENU)IDC_MONITOR_ITEMS_GROUP, hi, nullptr);

    int iy = 22;
    s->hCpuCheck = CreateWindowExW(0, L"BUTTON", L"CPU",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        20, itemsGroupY + iy, 60, 22, hParent, (HMENU)IDC_CPU_CHECK, hi, nullptr);

    s->hMemoryCheck = CreateWindowExW(0, L"BUTTON", L"内存",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        85, itemsGroupY + iy, 60, 22, hParent, (HMENU)IDC_MEMORY_CHECK, hi, nullptr);

    s->hNetworkCheck = CreateWindowExW(0, L"BUTTON", L"网速",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        150, itemsGroupY + iy, 50, 22, hParent, (HMENU)IDC_NETWORK_CHECK, hi, nullptr);

    // Unit combo — right next to the checkbox, no separate label
    s->hNetUnitCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        203, itemsGroupY + iy, 80, 200, hParent, (HMENU)IDC_NET_UNIT_COMBO, hi, nullptr);
    ComboBox_AddString(s->hNetUnitCombo, L"Kbps");
    ComboBox_AddString(s->hNetUnitCombo, L"Mbps");
    ComboBox_AddString(s->hNetUnitCombo, L"Gbps");
    ComboBox_SetCurSel(s->hNetUnitCombo, 1); // default: Mbps

    // "生成监测报告" 复选框 — 紧跟在网速单位后面
    s->hGenerateReportCheck = CreateWindowExW(0, L"BUTTON", L"生成监测报告",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        295, itemsGroupY + iy, 110, 22, hParent, (HMENU)IDC_GENERATE_REPORT_CHECK, hi, nullptr);
    SendMessageW(s->hGenerateReportCheck, BM_SETCHECK, BST_CHECKED, 0);  // 默认勾选

    iy += 28;
    s->hSampleLabel = CreateWindowExW(0, L"STATIC", L"采样周期: ",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        20, itemsGroupY + iy, 60, 22, hParent, nullptr, hi, nullptr);

    s->hSamplePeriodEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5",
        WS_CHILD | WS_VISIBLE | ES_NUMBER,
        85, itemsGroupY + iy, 50, 22, hParent, (HMENU)IDC_SAMPLE_PERIOD_EDIT, hi, nullptr);

    s->hSecondLabel = CreateWindowExW(0, L"STATIC", L"秒",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        140, itemsGroupY + iy, 25, 22, hParent, nullptr, hi, nullptr);

    s->hNetInterfaceLabel = CreateWindowExW(0, L"STATIC", L"网卡选择: ",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        330, itemsGroupY + iy, 60, 22, hParent, nullptr, hi, nullptr);

    s->hNetInterfaceCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
        400, itemsGroupY + iy, 200, 200, hParent, (HMENU)IDC_NET_INTERFACE_COMBO, hi, nullptr);

    s->hRefreshInterfaceBtn = CreateWindowExW(0, L"BUTTON", L"刷新",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        610, itemsGroupY + iy, 45, 22, hParent, (HMENU)IDC_REFRESH_INTERFACE_BTN, hi, nullptr);

    iy += 28;
    s->hOutputDirLabel = CreateWindowExW(0, L"STATIC", L"输出目录:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        20, itemsGroupY + iy, 60, 22, hParent, nullptr, hi, nullptr);

    s->hOutputDirEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L".\\",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        85, itemsGroupY + iy, 520, 22, hParent, (HMENU)IDC_OUTPUT_DIR_EDIT, hi, nullptr);

    s->hBrowseDirBtn = CreateWindowExW(0, L"BUTTON", L"浏览",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        612, itemsGroupY + iy, 45, 22, hParent, (HMENU)IDC_BROWSE_DIR_BTN, hi, nullptr);

    // Save-path display lines — match log ListView font/style
    iy += 28;
    // Save-path labels — clickable on text only, tooltip on hover
    s->hSavePathExcel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS | SS_NOTIFY,
        20, itemsGroupY + iy, 830, 18, hParent, (HMENU)IDC_SAVE_PATH_EXCEL, hi, nullptr);
    iy += 20;
    s->hSavePathHtml = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS | SS_NOTIFY,
        20, itemsGroupY + iy, 830, 18, hParent, (HMENU)IDC_SAVE_PATH_HTML, hi, nullptr);

    // Standard tooltip — shows full path on hover
    s->hSavePathTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hParent, nullptr, hi, nullptr);
    SendMessageW(s->hSavePathTip, TTM_SETMAXTIPWIDTH, 0, 800);
    SendMessageW(s->hSavePathTip, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
    {
        TOOLINFOW ti = { sizeof(ti) };
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = hParent;
        ti.uId = (UINT_PTR)s->hSavePathExcel;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(s->hSavePathTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        ti.uId = (UINT_PTR)s->hSavePathHtml;
        SendMessageW(s->hSavePathTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }

    // ---- Monitor Control Group ----
    int ctrlGroupY = itemsGroupY + 180;
    s->hControlGroup = CreateWindowExW(0, L"BUTTON", L"监测控制",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        8, ctrlGroupY, 874, 68, hParent, (HMENU)IDC_MONITOR_CONTROL_GROUP, hi, nullptr);

    s->hStartBtn = CreateWindowExW(0, L"BUTTON", L"开始监测",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, ctrlGroupY + 20, 100, 28, hParent, (HMENU)IDC_START_BTN, hi, nullptr);

    s->hStopBtn = CreateWindowExW(0, L"BUTTON", L"停止监测",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
        130, ctrlGroupY + 20, 100, 28, hParent, (HMENU)IDC_STOP_BTN, hi, nullptr);

    // Status label — inside control group, right-aligned
    s->hStatusLabel = CreateWindowExW(0, L"STATIC", L"状态: 就绪",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        600, ctrlGroupY + 21, 240, 26, hParent, (HMENU)IDC_STATUS_LABEL, hi, nullptr);

    // ---- Data Tab Control ----
    int tabY = ctrlGroupY + 78;
    s->hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_BOTTOM,
        8, tabY, 874, 100, hParent, (HMENU)IDC_DATA_TAB, hi, nullptr);

    // Create bold font for status label
    // Clone the label's current font (or system default) with FW_BOLD
    {
        HFONT hCurFont = (HFONT)SendMessageW(s->hStatusLabel, WM_GETFONT, 0, 0);
        if (!hCurFont)
            hCurFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        LOGFONTW lf = {};
        if (GetObjectW(hCurFont, sizeof(lf), &lf) == 0) {
            // Ultimate fallback: create a standard bold font
            HDC hdc = GetDC(hParent);
            lf.lfHeight = -MulDiv(11, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hParent, hdc);
            lf.lfWeight = FW_BOLD;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Microsoft YaHei");
        } else {
            lf.lfWeight = FW_BOLD;
            lf.lfQuality = CLEARTYPE_QUALITY;
        }

        s->hBoldFont = CreateFontIndirectW(&lf);
        if (s->hBoldFont)
            SendMessageW(s->hStatusLabel, WM_SETFONT, (WPARAM)s->hBoldFont, TRUE);

        // Verify
        HFONT hVerify = (HFONT)SendMessageW(s->hStatusLabel, WM_GETFONT, 0, 0);
        LOGFONTW vf = {};
        if (hVerify && GetObjectW(hVerify, sizeof(vf), &vf)) {
            wchar_t dbg[256];
            swprintf_s(dbg, 256, L"[BoldFont] face=%s h=%ld wt=%ld ok=%d\n",
                vf.lfFaceName, vf.lfHeight, vf.lfWeight, vf.lfWeight >= FW_BOLD);
            OutputDebugStringW(dbg);
        }
    }

    // ---- Status Bar Area (bottom, 置顶 button only) ----
    s->hStatusBar = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 100, 28, hParent, (HMENU)IDC_STATUS_LABEL, hi, nullptr);

    s->hTopmostBtn = CreateWindowExW(0, L"BUTTON", L"置顶",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 50, 24, hParent, (HMENU)IDC_TOPMOST_BTN, hi, nullptr);

    // Set fonts on all controls (status label gets bold font separately)
    HWND controls[] = {
        s->hProcessNameEdit, s->hAddBtn, s->hDeleteAllBtn, s->hSaveConfigBtn,
        s->hCpuCheck, s->hMemoryCheck, s->hNetworkCheck, s->hGenerateReportCheck,
        s->hNetUnitCombo, s->hSamplePeriodEdit,
        s->hNetInterfaceCombo, s->hRefreshInterfaceBtn,
        s->hOutputDirEdit, s->hBrowseDirBtn,
        s->hStartBtn, s->hStopBtn, s->hTopmostBtn, s->hHelpBtn,
        s->hNetInterfaceLabel, s->hSampleLabel, s->hSecondLabel, s->hOutputDirLabel,
        s->hTab
    };
    for (auto c : controls)
        SendMessageW(c, WM_SETFONT, (WPARAM)s->hDefaultFont, TRUE);
}

void InitSystemListView(MainWindowState* s) {
    // System ListView is placed inside the tab control area
    s->hSystemListView = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS,
        0, 0, 100, 100, s->hTab, (HMENU)IDC_DATA_LIST, s->hInst, nullptr);
    ListView_SetExtendedListViewStyle(s->hSystemListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    const wchar_t* headers[] = {
        L"时间", L"运行时间(秒)", L"CPU(%)", L"内存总量(GB)",
        L"内存可用(GB)", L"内存使用(GB)", L"内存使用率(%)", L"网络发送(Mbps)", L"网络接收(Mbps)"
    };

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    for (int i = 0; i < SYS_COL_COUNT; i++) {
        lvc.cx = s->sysColWidths[i];
        lvc.pszText = (LPWSTR)headers[i];
        ListView_InsertColumn(s->hSystemListView, i, &lvc);
    }

    // Auto-size columns to fit (skip timestamp col — use fixed width for full datetime)
    ListView_SetColumnWidth(s->hSystemListView, SYS_COL_TIME, 135);
    for (int i = 1; i < SYS_COL_COUNT; i++) {
        ListView_SetColumnWidth(s->hSystemListView, i, LVSCW_AUTOSIZE_USEHEADER);
    }
}

HWND CreateProcessDataListView(HWND hParent, MainWindowState* s) {
    HWND hLV = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 100, 100, hParent, (HMENU)IDC_DATA_LIST, s->hInst, nullptr);
    ListView_SetExtendedListViewStyle(hLV,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    const wchar_t* headers[] = {
        L"时间", L"运行时间(秒)", L"进程ID", L"CPU(%)",
        L"内存使用率(%)", L"内存使用(MB)", L"网络发送(Mbps)", L"网络接收(Mbps)"
    };

    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    for (int i = 0; i < PROC_COL_COUNT; i++) {
        lvc.cx = s->procColWidths[i];
        lvc.pszText = (LPWSTR)headers[i];
        ListView_InsertColumn(hLV, i, &lvc);
    }

    // Auto-size columns (skip timestamp col — use fixed width for full datetime)
    ListView_SetColumnWidth(hLV, PROC_COL_TIME, 135);
    for (int i = 1; i < PROC_COL_COUNT; i++) {
        ListView_SetColumnWidth(hLV, i, LVSCW_AUTOSIZE_USEHEADER);
    }

    ShowWindow(hLV, SW_HIDE); // Hidden initially until tab selected
    return hLV;
}

// ============================================================================
// Layout
// ============================================================================
void LayoutControls(MainWindowState* s) {
    RECT rc;
    GetClientRect(s->hMainWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int margin = 8;
    int contentW = w - margin * 2;

    // Config area — no outer group box, ListView has its own clean border
    SetWindowPos(s->hProcessListView, nullptr, margin + 4, margin + 55,
                 contentW - 8, 85, SWP_NOZORDER);
    // Fill columns to ListView width so gridlines align edge-to-edge
    int lvWidth = contentW - 8 - GetSystemMetrics(SM_CXVSCROLL) - 4;
    ListView_SetColumnWidth(s->hProcessListView, 0, 24);
    ListView_SetColumnWidth(s->hProcessListView, 2, 60);
    ListView_SetColumnWidth(s->hProcessListView, 1, lvWidth - 24 - 60);

    // Items group — includes save-path display
    int itemsY = margin + 55 + 85 + 8;
    int itemsH = 148;
    SetWindowPos(s->hItemsGroup, nullptr, margin, itemsY, contentW, itemsH, SWP_NOZORDER);

    {
        int y0 = itemsY + 24;
        SetWindowPos(s->hCpuCheck,       nullptr, 20,  y0,     60, 22, SWP_NOZORDER);
        SetWindowPos(s->hMemoryCheck,    nullptr, 85,  y0,     60, 22, SWP_NOZORDER);
        SetWindowPos(s->hNetworkCheck,   nullptr, 150, y0,     50, 22, SWP_NOZORDER);
        SetWindowPos(s->hNetUnitCombo,   nullptr, 203, y0,     80, 200, SWP_NOZORDER);
        SetWindowPos(s->hGenerateReportCheck, nullptr, 295, y0, 110, 22, SWP_NOZORDER);

        int y1 = itemsY + 50;
        SetWindowPos(s->hSampleLabel,       nullptr, 20,  y1, 60, 22, SWP_NOZORDER);
        SetWindowPos(s->hSamplePeriodEdit,  nullptr, 85,  y1, 50, 22, SWP_NOZORDER);
        SetWindowPos(s->hSecondLabel,       nullptr, 140, y1, 25, 22, SWP_NOZORDER);
        SetWindowPos(s->hNetInterfaceLabel, nullptr, 330, y1, 60, 22, SWP_NOZORDER);
        SetWindowPos(s->hNetInterfaceCombo, nullptr, 400, y1, 200, 200, SWP_NOZORDER);
        SetWindowPos(s->hRefreshInterfaceBtn,nullptr, 610, y1, 45, 22, SWP_NOZORDER);

        int y2 = itemsY + 76;
        SetWindowPos(s->hOutputDirLabel, nullptr, 20,  y2, 60, 22, SWP_NOZORDER);
        SetWindowPos(s->hOutputDirEdit,  nullptr, 85,  y2, 520, 22, SWP_NOZORDER);
        SetWindowPos(s->hBrowseDirBtn,   nullptr, 612, y2, 45, 22, SWP_NOZORDER);

        int y3 = itemsY + 104;
        SetWindowPos(s->hSavePathExcel, nullptr, 20, y3, contentW - 46, 18, SWP_NOZORDER);
        int y4 = itemsY + 124;
        SetWindowPos(s->hSavePathHtml,  nullptr, 20, y4, contentW - 46, 18, SWP_NOZORDER);
    }

    // Control group
    int ctrlY = itemsY + itemsH + 8;
    int ctrlH = 68;
    SetWindowPos(s->hControlGroup, nullptr, margin, ctrlY, contentW, ctrlH, SWP_NOZORDER);

    // Reposition buttons and status label — vertically centered inside control group
    {
        int cy = ctrlY + 22;
        SetWindowPos(s->hStartBtn,    nullptr, 20,  cy, 100, 28, SWP_NOZORDER);
        SetWindowPos(s->hStopBtn,     nullptr, 130, cy, 100, 28, SWP_NOZORDER);
        SetWindowPos(s->hStatusLabel, nullptr, w - 280, cy + 1, 250, 26, SWP_NOZORDER);
    }

    // Tab control: right below control group
    int tabY = ctrlY + ctrlH + 8;
    int tabH = h - tabY - s->statusBarHeight - margin;
    SetWindowPos(s->hTab, nullptr, margin, tabY, contentW, tabH, SWP_NOZORDER);

    // Size tab content area
    RECT tabRc;
    GetClientRect(s->hTab, &tabRc);
    TabCtrl_AdjustRect(s->hTab, FALSE, &tabRc);
    int lvW = tabRc.right - tabRc.left - 4;
    int lvH = tabRc.bottom - tabRc.top - 4;

    SetWindowPos(s->hSystemListView, nullptr, tabRc.left + 2, tabRc.top + 2, lvW, lvH, SWP_NOZORDER);
    for (auto& tab : s->processTabs) {
        SetWindowPos(tab.hListView, nullptr, tabRc.left + 2, tabRc.top + 2, lvW, lvH, SWP_NOZORDER);
    }

    // Status bar
    SetWindowPos(s->hStatusBar, nullptr, 0, h - s->statusBarHeight, w - 120, s->statusBarHeight, SWP_NOZORDER);
    // Ensure button is on top of status bar
    SetWindowPos(s->hTopmostBtn, HWND_TOP, w - 70, h - s->statusBarHeight + 2, 55, 22, 0);
}

// ============================================================================
// Process List Management
// ============================================================================
void RefreshProcessList(MainWindowState* s) {
    // Suppress LVN_ITEMCHANGED events during programmatic updates
    // to prevent InsertItem (which defaults to unchecked) from
    // overwriting the loaded config's enabled state.
    s->suppressListEvents = true;

    ListView_DeleteAllItems(s->hProcessListView);

    auto& cfg = ConfigManager::Instance().GetConfig();
    for (int i = 0; i < cfg.processCount; i++) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)L"";
        ListView_InsertItem(s->hProcessListView, &item);

        // Name
        ListView_SetItemText(s->hProcessListView, i, 1, cfg.processes[i].name);

        // Action
        ListView_SetItemText(s->hProcessListView, i, 2, (LPWSTR)L"删除");

        // Checkbox
        ListView_SetCheckState(s->hProcessListView, i, cfg.processes[i].enabled ? TRUE : FALSE);
    }

    s->suppressListEvents = false;

    // 立即刷新列表视图，确保在后续操作冻结画布前已渲染
    UpdateWindow(s->hProcessListView);
}

// ============================================================================
// Tab Management
// ============================================================================

// Strip ".exe" suffix from a process name for display purposes
static std::wstring GetDisplayName(const wchar_t* name) {
    std::wstring s(name);
    if (s.length() > 4) {
        std::wstring suffix = s.substr(s.length() - 4);
        if (suffix == L".exe" || suffix == L".EXE" || suffix == L".Exe") {
            s = s.substr(0, s.length() - 4);
        }
    }
    return s;
}

void AddProcessTab(MainWindowState* s, const wchar_t* name) {
    // Check if tab already exists
    for (auto& tab : s->processTabs) {
        if (tab.processName == name) return;
    }

    ProcessTabInfo info;
    info.processName = name;
    info.hListView = CreateProcessDataListView(s->hTab, s);
    info.lastDataIdx = -1;  // no DataBuffer entries processed yet

    // Add tab — display name without .exe suffix
    std::wstring displayName = GetDisplayName(name);
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = (LPWSTR)displayName.c_str();
    info.tabIndex = TabCtrl_GetItemCount(s->hTab);
    TabCtrl_InsertItem(s->hTab, info.tabIndex, &tci);

    s->processTabs.push_back(info);
}

void RemoveProcessTab(MainWindowState* s, const wchar_t* name) {
    for (auto it = s->processTabs.begin(); it != s->processTabs.end(); ++it) {
        if (it->processName == name) {
            DestroyWindow(it->hListView);
            int idx = it->tabIndex;
            TabCtrl_DeleteItem(s->hTab, idx);
            // Adjust tab indices for remaining items
            for (auto& tab : s->processTabs) {
                if (tab.tabIndex > idx) tab.tabIndex--;
            }
            s->processTabs.erase(it);
            break;
        }
    }
}

void ToggleProcessTab(MainWindowState* s, const wchar_t* name, bool enabled) {
    // Check if tab already exists for this process
    bool tabExists = false;
    for (auto& tab : s->processTabs) {
        if (tab.processName == name) {
            tabExists = true;
            break;
        }
    }

    if (enabled && !tabExists) {
        // 创建页签，立即切换到新页签，刷新数据
        AddProcessTab(s, name);

        // 监测中且为新启用的进程，创建 ProcessMonitor 以开始采集数据
        if (s->isMonitoring) {
            auto* pm = new ProcessMonitor(name);
            pm->SetNetUnit(ConfigManager::Instance().GetConfig().netUnit);
            s->processMonitors.push_back(pm);
        }

        // 找到新页签的 tabIndex 并切换
        for (auto& tab : s->processTabs) {
            if (tab.processName == name) {
                TabCtrl_SetCurSel(s->hTab, tab.tabIndex);
                break;
            }
        }
        OnTabChanged(s);
        LayoutControls(s);

        // 立即用 DataBuffer 中已有数据填充新页签
        for (auto& tab : s->processTabs) {
            if (tab.processName == name) {
                UpdateProcessListView(s, tab);
                break;
            }
        }

        // 强制立即重绘，确保页签和日志界面即时刷新
        RedrawWindow(s->hMainWnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    } else if (!enabled && tabExists) {
        // 监测中移除对应 ProcessMonitor
        if (s->isMonitoring) {
            for (auto it = s->processMonitors.begin(); it != s->processMonitors.end(); ++it) {
                if (_wcsicmp((*it)->GetProcessName(), name) == 0) {
                    delete *it;
                    s->processMonitors.erase(it);
                    break;
                }
            }
        }

        // 记录当前选中页签和被删页签的 index
        int curSel = TabCtrl_GetCurSel(s->hTab);
        int removingIdx = -1;
        for (auto& tab : s->processTabs) {
            if (tab.processName == name) {
                removingIdx = tab.tabIndex;
                break;
            }
        }

        RemoveProcessTab(s, name);

        // 如果被删的是当前选中页签，或选中已越界，切回系统资源(tab 0)
        if (curSel == removingIdx || TabCtrl_GetCurSel(s->hTab) >= TabCtrl_GetItemCount(s->hTab)) {
            TabCtrl_SetCurSel(s->hTab, 0);
        }
        OnTabChanged(s);
        LayoutControls(s);

        // 强制立即重绘，确保页签栏和日志界面即时刷新
        RedrawWindow(s->hMainWnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

void RebuildProcessTabs(MainWindowState* s, const wchar_t* selectName) {
    // Freeze redraw during tab destruction/recreation to prevent flickering
    SendMessageW(s->hMainWnd, WM_SETREDRAW, FALSE, 0);

    // Remove all existing process tabs
    for (auto& tab : s->processTabs)
        DestroyWindow(tab.hListView);
    s->processTabs.clear();

    // Remove all tabs from tab control
    while (TabCtrl_GetItemCount(s->hTab) > 0)
        TabCtrl_DeleteItem(s->hTab, 0);

    // Add system tab
    TCITEMW tci = {};
    tci.mask = TCIF_TEXT;
    tci.pszText = (LPWSTR)L"系统资源";
    TabCtrl_InsertItem(s->hTab, 0, &tci);

    // Add process tabs
    auto& cfg = ConfigManager::Instance().GetConfig();
    for (int i = 0; i < cfg.processCount; i++) {
        if (cfg.processes[i].enabled) {
            AddProcessTab(s, cfg.processes[i].name);
        }
    }

    // 如果有指定要选中的进程，切换到对应页签；否则选中系统资源(tab 0)
    int targetSel = 0;
    if (selectName && selectName[0] != L'\0') {
        for (auto& tab : s->processTabs) {
            if (tab.processName == selectName) {
                targetSel = tab.tabIndex;
                break;
            }
        }
    }
    TabCtrl_SetCurSel(s->hTab, targetSel);

    // 监测中同步 ProcessMonitor 列表（新增/删除软件时更新采集线程的数据源）
    if (s->isMonitoring) {
        for (auto* pm : s->processMonitors)
            delete pm;
        s->processMonitors.clear();
        auto& cfg2 = ConfigManager::Instance().GetConfig();
        for (int i = 0; i < cfg2.processCount; i++) {
            if (cfg2.processes[i].enabled) {
                auto* pm = new ProcessMonitor(cfg2.processes[i].name);
                pm->SetNetUnit(cfg2.netUnit);
                s->processMonitors.push_back(pm);
            }
        }
    }

    // Resume redraw BEFORE laying out controls and populating data
    SendMessageW(s->hMainWnd, WM_SETREDRAW, TRUE, 0);

    OnTabChanged(s);
    LayoutControls(s);

    // Immediately populate all tabs with existing DataBuffer data
    UpdateSystemListView(s);
    for (auto& tab : s->processTabs)
        UpdateProcessListView(s, tab);

    // 强制立即重绘整个窗口及所有子控件，确保界面即时刷新
    RedrawWindow(s->hMainWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void OnTabChanged(MainWindowState* s) {
    int sel = TabCtrl_GetCurSel(s->hTab);

    // 隐藏所有 ListView
    ShowWindow(s->hSystemListView, SW_HIDE);
    for (auto& tab : s->processTabs)
        ShowWindow(tab.hListView, SW_HIDE);

    // 只显示选中的 ListView
    if (sel == 0) {
        ShowWindow(s->hSystemListView, SW_SHOW);
        // 立即刷新系统资源页签数据
        UpdateSystemListView(s);
        // 强制立即绘制（确保列标题等即时可见）
        UpdateWindow(s->hSystemListView);
    } else {
        for (auto& tab : s->processTabs) {
            if (tab.tabIndex == sel) {
                ShowWindow(tab.hListView, SW_SHOW);
                // 立即刷新该进程页签数据
                UpdateProcessListView(s, tab);
                // 强制立即绘制（确保列标题等即时可见）
                UpdateWindow(tab.hListView);
                break;
            }
        }
    }
}

// ============================================================================
// Monitor Control
// ============================================================================
void StartMonitoring(MainWindowState* s) {
    auto& cfg = ConfigManager::Instance().GetConfig();

    // Validate: at least one monitoring item selected
    if (!cfg.monitorCpu && !cfg.monitorMemory && !cfg.monitorNetwork) {
        MessageBoxW(s->hMainWnd, L"请至少选择一项监测项目", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    // Sync config from UI
    SyncConfigFromUI(s);

    // Ensure output directory exists
    wchar_t absPath[MAX_PATH];
    if (GetFullPathNameW(cfg.outputDir, MAX_PATH, absPath, nullptr) == 0) {
        MessageBoxW(s->hMainWnd, L"输出目录路径无效", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    wcscpy_s(cfg.outputDir, MAX_PATH, absPath);

    // Try to create directory
    if (!CreateDirectoryW(cfg.outputDir, nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            MessageBoxW(s->hMainWnd, L"无法创建输出目录", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // Disable config controls
    SetControlsEnabled(s, false);
    EnableWindow(s->hStartBtn, FALSE);
    EnableWindow(s->hStopBtn, TRUE);

    // Update status
    UpdateStatus(s, L"状态: 监测中", RGB(255, 0, 0));

    // Setup monitoring state
    s->isMonitoring = true;
    s->monitorStartTime = (double)time(nullptr);

    // Clear previous data
    s->dataBuffer.Clear();
    ListView_DeleteAllItems(s->hSystemListView);
    for (auto& tab : s->processTabs) {
        ListView_DeleteAllItems(tab.hListView);
        tab.lastDataIdx = -1;
    }

    // Reset display offsets
    s->m_sysDisplayOffset = 0;
    s->m_procDisplayOffsets.clear();

    // Setup system monitor — SetNetInterface MUST be called before Initialize()
    // because Initialize() captures the baseline using m_netInterface
    s->systemMonitor.SetNetUnit(cfg.netUnit);
    s->systemMonitor.SetNetInterface(cfg.netInterface);
    s->systemMonitor.Initialize();

    // Setup process monitors
    s->processMonitors.clear();
    for (int i = 0; i < cfg.processCount; i++) {
        if (cfg.processes[i].enabled) {
            auto* pm = new ProcessMonitor(cfg.processes[i].name);
            pm->SetNetUnit(cfg.netUnit);
            s->processMonitors.push_back(pm);
        }
    }

    s->lastFlushSystemIndex = 0;
    s->lastFlushTick = 0;
    s->lastHtmlFlushTick = GetTickCount();
    s->lastDataTrimTick = GetTickCount();

    // Start per-process network monitor
    if (!s->netSpeedMonitor.Start()) {
        OutputDebugStringW(L"[MainWindow] NetSpeedMonitor failed — process network will be 0\r\n");
    }

    // Begin real-time Excel export (file locked, read-only for external viewers)
    s->excelExporter.SetNetUnit(cfg.netUnit);
    s->excelExporter.SetNetInterface(cfg.netInterface);
    s->excelExporter.BeginExport(cfg.outputDir, s->monitorStartTime);

    // Begin disk-backed data logging
    s->dataLogger.BeginLog(cfg.outputDir, s->monitorStartTime);

    // Create stop event
    s->hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // Start monitor thread
    s->hMonitorThread = CreateThread(nullptr, 0, MonitorThreadProc, s, 0, nullptr);
    if (!s->hMonitorThread) {
        MessageBoxW(s->hMainWnd, L"无法创建监测线程", L"错误", MB_OK | MB_ICONERROR);
        s->isMonitoring = false;
        EnableWindow(s->hStartBtn, TRUE);
        EnableWindow(s->hStopBtn, FALSE);
        SetControlsEnabled(s, true);
        UpdateStatus(s, L"状态: 就绪", RGB(0, 180, 0));
        return;
    }

    // Lower thread priority to minimize impact on system
    SetThreadPriority(s->hMonitorThread, THREAD_PRIORITY_BELOW_NORMAL);
}

void StopMonitoring(MainWindowState* s) {
    if (!s->isMonitoring) return;

    // Signal stop
    if (s->hStopEvent) {
        SetEvent(s->hStopEvent);
    }

    // Wait for thread to finish with proper timeout handling.
    // If the monitor thread is stuck in a slow API call (e.g. GetIfTable2,
    // GetExtendedTcpTable), we must force-terminate it.  A zombie thread
    // holding a raw pointer to `s` will crash (use-after-free) or hang
    // indefinitely when `s` is deleted in WM_DESTROY, leaving a residual
    // process in Task Manager.
    if (s->hMonitorThread) {
        DWORD waitResult = WaitForSingleObject(s->hMonitorThread, 10000);
        if (waitResult == WAIT_TIMEOUT) {
            // Thread is stuck — force-kill it.  The thread is a pure data
            // collector; TerminateThread is safe here because:
            //   - It only holds CRITICAL_SECTION briefly inside QueryDelta()
            //   - No heap allocations are in-flight at the wait points
            //   - All resources are cleaned up below
            TerminateThread(s->hMonitorThread, 0);
        }
        CloseHandle(s->hMonitorThread);
        s->hMonitorThread = nullptr;
    }

    // Close the stop-event handle ONLY after the thread is confirmed dead.
    // Closing it while the thread is still calling WaitForSingleObject on it
    // is undefined behaviour (may return WAIT_FAILED / hang / crash).
    if (s->hStopEvent) {
        CloseHandle(s->hStopEvent);
        s->hStopEvent = nullptr;
    }

    s->isMonitoring = false;

    // Reset display offsets
    s->m_sysDisplayOffset = 0;
    s->m_procDisplayOffsets.clear();
    for (auto& tab : s->processTabs)
        tab.lastDataIdx = -1;

    // Show saving indicator
    UpdateStatus(s, L"状态: 正在保存中…", RGB(255, 165, 0));  // orange

    // Flush and close disk logger, then load full history for complete export
    s->dataLogger.EndLog();

    std::vector<SystemMonitorData> fullSys;
    std::vector<std::wstring> fullProcNames;
    std::vector<std::vector<ProcessMonitorData>> fullProcData;
    bool loadedFromDisk = s->dataLogger.LoadAll(fullSys, fullProcNames, fullProcData);

    // Fallback: if disk load fails, use whatever is in the in-memory DataBuffer
    if (!loadedFromDisk || fullSys.empty()) {
        fullSys = s->dataBuffer.GetSystemDataCopy();
        fullProcNames.clear();
        fullProcData.clear();
        for (auto* pm : s->processMonitors) {
            fullProcNames.push_back(pm->GetProcessName());
            fullProcData.push_back(s->dataBuffer.GetProcessDataCopy(pm->GetProcessName()));
        }
    }

    // Final flush of all data to Excel
    {
        auto& cfg = ConfigManager::Instance().GetConfig();
        std::vector<MonitorProcess> procs;
        for (auto& name : fullProcNames) {
            MonitorProcess mp = {};
            wcscpy_s(mp.name, MAX_PROCESS_NAME, name.c_str());
            mp.enabled = true;
            procs.push_back(mp);
        }
        s->excelExporter.FlushExport(fullSys, procs, fullProcData, cfg.netUnit, cfg.netInterface);
    }
    s->excelExporter.EndExport();

    // Final HTML report
    auto& reportCfg = ConfigManager::Instance().GetConfig();
    std::wstring htmlPath;
    if (reportCfg.generateReport) {
        std::vector<MonitorProcess> procs;
        for (auto& name : fullProcNames) {
            MonitorProcess mp = {};
            wcscpy_s(mp.name, MAX_PROCESS_NAME, name.c_str());
            mp.enabled = true;
            procs.push_back(mp);
        }
        htmlPath = HtmlChartExporter::Export(reportCfg.outputDir, s->monitorStartTime,
            fullSys, procs, fullProcData, reportCfg.netInterface);
    }

    // Delete temp log file after successful export
    s->dataLogger.DeleteLog();

    // Use a non‑blocking status update instead of MessageBoxW, which runs a
    // nested modal message loop.  If the user walks away, MessageBoxW blocks
    // WM_DESTROY → DestroyWindow → the window stays visible and the process
    // looks hung in Task Manager.
    {
        wchar_t msg[512];
        if (!htmlPath.empty()) {
            swprintf_s(msg, 512, L"数据已保存 — Excel + HTML 报告");
        } else {
            swprintf_s(msg, 512, L"数据已保存 — Excel: %s", s->excelExporter.GetLastFilePath());
        }
        UpdateStatus(s, msg, RGB(0, 180, 0));
    }

    // Show file paths in the save-path labels below output directory
    {
        const wchar_t* excel = s->excelExporter.GetLastFilePath();
        s->savedExcelPath = excel;
        s->savedHtmlPath = htmlPath;

        const wchar_t* p = wcsrchr(excel, L'\\');
        SetWindowTextW(s->hSavePathExcel, p ? p + 1 : excel);
        if (s->hBoldFont) {
            SendMessageW(s->hSavePathExcel, WM_SETFONT, (WPARAM)s->hBoldFont, TRUE);
            SendMessageW(s->hSavePathHtml,  WM_SETFONT, (WPARAM)s->hBoldFont, TRUE);
        }
        if (!htmlPath.empty()) {
            p = wcsrchr(htmlPath.c_str(), L'\\');
            SetWindowTextW(s->hSavePathHtml, p ? p + 1 : htmlPath.c_str());
            ShowWindow(s->hSavePathHtml, SW_SHOW);
        } else {
            SetWindowTextW(s->hSavePathHtml, L"");
            ShowWindow(s->hSavePathHtml, SW_HIDE);
        }
    }

    // Update UI state
    SetControlsEnabled(s, true);
    EnableWindow(s->hStartBtn, TRUE);
    EnableWindow(s->hStopBtn, FALSE);
    UpdateStatus(s, L"状态: 就绪", RGB(0, 180, 0));

    // Cleanup process monitors
    s->netSpeedMonitor.Stop();
    for (auto* pm : s->processMonitors)
        delete pm;
    s->processMonitors.clear();
}

DWORD WINAPI MonitorThreadProc(LPVOID param) {
    MainWindowState* s = (MainWindowState*)param;
    auto& cfg = ConfigManager::Instance().GetConfig();
    int samplePeriod = cfg.samplePeriod;

    // Warm-up: call Collect() once for each process monitor to establish
    // CPU baselines (cpuInitialized), elapsed-time baseline, and NetSpeedMonitor
    // TCP ESTATS baselines. Results are discarded.
    // Must run BEFORE the initial sleep so the first real collection has a
    // full samplePeriod of delta to work with.
    {
        double warmRunSec = (double)time(nullptr) - s->monitorStartTime;
        for (size_t i = 0; i < s->processMonitors.size(); i++) {
            s->processMonitors[i]->Collect(warmRunSec, &s->netSpeedMonitor);
        }
    }

    // Wait one sample period so baselines (both SystemMonitor::Initialize
    // and the process warm-up above) can accumulate meaningful deltas.
    // This avoids CPU/net being 0 on the first row.
    if (WaitForSingleObject(s->hStopEvent, samplePeriod * 1000) == WAIT_OBJECT_0)
        return 0;

    while (WaitForSingleObject(s->hStopEvent, 0) == WAIT_TIMEOUT) {
        DWORD iterStart = GetTickCount();

        double runSeconds = (double)time(nullptr) - s->monitorStartTime;

        // Collect system data (has NIC-filtered AND total network speeds)
        SystemMonitorData sysData = {};
        if (cfg.monitorCpu || cfg.monitorMemory || cfg.monitorNetwork) {
            sysData = s->systemMonitor.Collect(runSeconds);
            s->dataBuffer.AddSystemData(sysData);
        }

        // Early stop check: Collect() can be slow (GetIfTable2 enumerates
        // every NIC), and we don't want the shutdown timeout to fire.
        if (WaitForSingleObject(s->hStopEvent, 0) == WAIT_OBJECT_0)
            break;

        // Calculate NIC ratio for scaling per-process network data.
        // When a specific NIC is selected, per-process speeds are scaled by
        // (NIC traffic / total traffic).  For a disconnected NIC this ratio
        // is 0, so process network correctly shows 0.
        double nicSendRatio = 1.0, nicRecvRatio = 1.0;
        if (wcscmp(cfg.netInterface, L"全部") != 0) {
            double totalSend = s->systemMonitor.GetTotalNetSendSpeed();
            double totalRecv = s->systemMonitor.GetTotalNetRecvSpeed();
            if (totalSend > 0.0001)
                nicSendRatio = sysData.netSendSpeed / totalSend;
            if (totalRecv > 0.0001)
                nicRecvRatio = sysData.netRecvSpeed / totalRecv;
        }

        // Collect process data (one entry per PID for same-name processes)
        std::vector<std::wstring> procNames;
        std::vector<std::vector<ProcessMonitorData>> allProcData;
        for (size_t i = 0; i < s->processMonitors.size(); i++) {
            if (WaitForSingleObject(s->hStopEvent, 0) == WAIT_OBJECT_0)
                break;
            auto procDataList = s->processMonitors[i]->Collect(runSeconds, &s->netSpeedMonitor);
            std::vector<ProcessMonitorData> scaled;
            for (auto& pd : procDataList) {
                pd.netSendSpeed *= nicSendRatio;
                pd.netRecvSpeed *= nicRecvRatio;
                s->dataBuffer.AddProcessData(s->processMonitors[i]->GetProcessName(), pd);
                scaled.push_back(pd);
            }
            procNames.push_back(s->processMonitors[i]->GetProcessName());
            allProcData.push_back(std::move(scaled));
        }

        // Log sample to disk for data integrity
        if (cfg.monitorCpu || cfg.monitorMemory || cfg.monitorNetwork)
            s->dataLogger.AppendSample(sysData, procNames, allProcData);

        // Sleep for remaining sample period (interruptible)
        DWORD elapsed = GetTickCount() - iterStart;
        DWORD sleepMs = (DWORD)(samplePeriod * 1000);
        if (elapsed < sleepMs) {
            WaitForSingleObject(s->hStopEvent, sleepMs - elapsed);
        }
    }

    return 0;
}

// ============================================================================
// Display Update (called by WM_TIMER every 200ms)
// ============================================================================
void UpdateDisplay(MainWindowState* s) {
    UpdateSystemListView(s);
    for (auto& tab : s->processTabs)
        UpdateProcessListView(s, tab);
}

void UpdateSystemListView(MainWindowState* s) {
    // Use thread-safe slice — only copy new items since last display,
    // avoiding a full DataBuffer copy every 200ms.
    int displayedCount = ListView_GetItemCount(s->hSystemListView);
    size_t startIdx = s->m_sysDisplayOffset + displayedCount;
    auto sysData = s->dataBuffer.GetSystemDataSlice(startIdx);
    int totalNew = (int)sysData.size();

    // Add new items since last display
    for (int i = 0; i < totalNew; i++) {
        const auto& d = sysData[i];
        int lvIdx = displayedCount + i;

        wchar_t buf[64];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = lvIdx;
        item.iSubItem = SYS_COL_TIME;
        item.pszText = (LPWSTR)d.timestamp;
        ListView_InsertItem(s->hSystemListView, &item);

        swprintf_s(buf, 64, L"%.0f", d.runSeconds);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_RUN_SEC, buf);

        swprintf_s(buf, 64, L"%.2f", d.cpuUsage);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_CPU, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryTotalGB);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_MEM_TOTAL, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryAvailableGB);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_MEM_AVAIL, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsedGB);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_MEM_USED, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsage);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_MEM_USAGE, buf);

        swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_NET_SEND, buf);

        swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);
        ListView_SetItemText(s->hSystemListView, lvIdx, SYS_COL_NET_RECV, buf);
    }

    // Trim ListView for display performance (keep last N items).
    // This only affects display; DataBuffer retains all data for Excel export.
    const int MAX_DISPLAY_ROWS = 50000;
    int currentItems = ListView_GetItemCount(s->hSystemListView);
    while (currentItems > MAX_DISPLAY_ROWS) {
        ListView_DeleteItem(s->hSystemListView, 0);
        s->m_sysDisplayOffset++;  // first visible DataBuffer index moves forward
        currentItems--;
    }

    // Auto-scroll to bottom
    if (currentItems > 0)
        ListView_EnsureVisible(s->hSystemListView, currentItems - 1, FALSE);
}

void UpdateProcessListView(MainWindowState* s, ProcessTabInfo& tab) {
    // Use thread-safe slice — only copy new items since last display
    int startIdx = tab.lastDataIdx + 1;
    auto procData = s->dataBuffer.GetProcessDataSlice(tab.processName, startIdx);
    if (procData.empty()) return;

    int totalNew = (int)procData.size();

    // 收集新条目，按 runSeconds 分组（每个采集周期一组）
    struct PidGroup { double runSeconds; std::vector<int> indices; };
    std::vector<PidGroup> groups;
    int maxIdx = tab.lastDataIdx;

    for (int i = 0; i < totalNew; i++) {
        double rs = procData[i].runSeconds;
        if (groups.empty() || fabs(groups.back().runSeconds - rs) > 0.001) {
            groups.push_back({rs, {}});
        }
        groups.back().indices.push_back(i);
        if (startIdx + i > maxIdx) maxIdx = startIdx + i;
    }

    // 每组只保留活跃度最高的 5 个 PID（UI 精简显示，DataBuffer 保留完整数据供 Excel 导出）
    for (auto& group : groups) {
        if ((int)group.indices.size() > 5) {
            std::sort(group.indices.begin(), group.indices.end(), [&](int a, int b) {
                auto& da = procData[a];
                auto& db = procData[b];
                double sa = da.cpuUsage + da.memoryUsage +
                            fabs(da.netSendSpeed) + fabs(da.netRecvSpeed);
                double sb = db.cpuUsage + db.memoryUsage +
                            fabs(db.netSendSpeed) + fabs(db.netRecvSpeed);
                return sa > sb;  // 活跃度高的排前面
            });
            group.indices.resize(5);
        }
    }

    // 将过滤后的条目插入 ListView
    wchar_t buf[64];
    for (auto& group : groups) {
        for (int idx : group.indices) {
            const auto& d = procData[idx];
            int lvIdx = ListView_GetItemCount(tab.hListView);

            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = lvIdx;
            item.iSubItem = PROC_COL_TIME;
            item.pszText = (LPWSTR)d.timestamp;
            ListView_InsertItem(tab.hListView, &item);

            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_TIME, (LPWSTR)d.timestamp);

            swprintf_s(buf, 64, L"%.0f", d.runSeconds);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_RUN_SEC, buf);

            swprintf_s(buf, 64, L"%lu", d.pid);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_PID, buf);

            swprintf_s(buf, 64, L"%.2f", d.cpuUsage);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_CPU, buf);

            swprintf_s(buf, 64, L"%.2f", d.memoryUsage);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_MEM_USAGE, buf);

            swprintf_s(buf, 64, L"%.2f", d.memoryUsedMB);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_MEM_USED, buf);

            swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_NET_SEND, buf);

            swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);
            ListView_SetItemText(tab.hListView, lvIdx, PROC_COL_NET_RECV, buf);
        }
    }

    // 标记本次已处理的 DataBuffer 索引（包括被过滤掉的条目）
    tab.lastDataIdx = maxIdx;

    // Trim ListView for display performance (keep last N items)
    const int MAX_DISPLAY_ROWS = 50000;
    int currentItems = ListView_GetItemCount(tab.hListView);
    while (currentItems > MAX_DISPLAY_ROWS) {
        ListView_DeleteItem(tab.hListView, 0);
        currentItems--;
    }

    // Auto-scroll to bottom
    if (currentItems > 0)
        ListView_EnsureVisible(tab.hListView, currentItems - 1, FALSE);
}

// ============================================================================
// UI State Helpers
// ============================================================================
void SetControlsEnabled(MainWindowState* s, bool enabled) {
    EnableWindow(s->hProcessNameEdit, enabled);
    EnableWindow(s->hAddBtn, enabled);
    EnableWindow(s->hDeleteAllBtn, enabled);
    EnableWindow(s->hSaveConfigBtn, enabled);
    EnableWindow(s->hProcessListView, enabled);

    EnableWindow(s->hCpuCheck, enabled);
    EnableWindow(s->hMemoryCheck, enabled);
    EnableWindow(s->hNetworkCheck, enabled);

    EnableWindow(s->hSamplePeriodEdit, enabled);
    EnableWindow(s->hGenerateReportCheck, enabled);

    EnableWindow(s->hOutputDirEdit, enabled);
    EnableWindow(s->hBrowseDirBtn, enabled);

    // Network-dependent controls: enabled only when not monitoring AND checkbox is on
    UpdateNetworkControlsEnabled(s);
}

void UpdateNetworkControlsEnabled(MainWindowState* s) {
    bool netChecked = (SendMessageW(s->hNetworkCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    bool ctrlEnabled = IsWindowEnabled(s->hNetworkCheck);  // monitoring disables the checkbox
    bool enable = netChecked && ctrlEnabled;

    EnableWindow(s->hNetUnitCombo, enable);
    EnableWindow(s->hNetInterfaceLabel, enable);
    EnableWindow(s->hNetInterfaceCombo, enable);
    EnableWindow(s->hRefreshInterfaceBtn, enable);
}

void UpdateNetUnitHeaders(MainWindowState* s, const wchar_t* unit) {
    wchar_t sendHdr[64], recvHdr[64];
    swprintf_s(sendHdr, 64, L"网络发送(%s)", unit);
    swprintf_s(recvHdr, 64, L"网络接收(%s)", unit);

    // System ListView columns 7 & 8
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT;
    lvc.pszText = sendHdr;
    ListView_SetColumn(s->hSystemListView, SYS_COL_NET_SEND, &lvc);
    lvc.pszText = recvHdr;
    ListView_SetColumn(s->hSystemListView, SYS_COL_NET_RECV, &lvc);

    // All process ListViews columns 6 & 7
    for (auto& tab : s->processTabs) {
        lvc.pszText = sendHdr;
        ListView_SetColumn(tab.hListView, PROC_COL_NET_SEND, &lvc);
        lvc.pszText = recvHdr;
        ListView_SetColumn(tab.hListView, PROC_COL_NET_RECV, &lvc);
    }
}

void UpdateStatus(MainWindowState* s, const wchar_t* status, COLORREF color) {
    s->statusColor = color;
    SetWindowTextW(s->hStatusLabel, status);
    // Re-apply bold font in case SetWindowTextW resets it
    if (s->hBoldFont)
        SendMessageW(s->hStatusLabel, WM_SETFONT, (WPARAM)s->hBoldFont, TRUE);
    InvalidateRect(s->hStatusLabel, nullptr, TRUE);
}

// ============================================================================
// Data Export
// ============================================================================
void ExportData(MainWindowState* s) {
    auto& cfg = ConfigManager::Instance().GetConfig();

    s->excelExporter.SetNetUnit(cfg.netUnit);
    s->excelExporter.SetNetInterface(cfg.netInterface);

    auto systemData = s->dataBuffer.GetSystemDataCopy();

    // Prepare process data vectors
    std::vector<MonitorProcess> processes;
    std::vector<std::vector<ProcessMonitorData>> allProcessData;

    for (auto* pm : s->processMonitors) {
        MonitorProcess mp = {};
        wcscpy_s(mp.name, MAX_PROCESS_NAME, pm->GetProcessName());
        mp.enabled = true;
        processes.push_back(mp);

        auto pd = s->dataBuffer.GetProcessDataCopy(pm->GetProcessName());
        allProcessData.push_back(pd);
    }

    if (s->excelExporter.Export(cfg.outputDir, s->monitorStartTime,
                                systemData, processes, allProcessData, cfg.netUnit, cfg.netInterface)) {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"数据已导出到 Excel 文件\r\n路径: %s", s->excelExporter.GetLastFilePath());
        MessageBoxW(s->hMainWnd, msg, L"成功", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(s->hMainWnd, L"导出数据失败", L"错误", MB_OK | MB_ICONERROR);
    }
}

// ============================================================================
// Context Menu
// ============================================================================
void ShowContextMenu(HWND hWnd, HWND hListView, int x, int y, MainWindowState* s) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_CLEAR_LOG, L"清除日志");
    AppendMenuW(hMenu, MF_STRING, IDM_SELECT_ALL, L"全选");
    AppendMenuW(hMenu, MF_STRING, IDM_COPY_SELECTED, L"复制选中");

    // If no items, show at cursor position
    POINT pt;
    if (x == -1 && y == -1) {
        GetCursorPos(&pt);
    } else {
        pt.x = x;
        pt.y = y;
    }

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                             pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_CLEAR_LOG: {
        ListView_DeleteAllItems(hListView);
        if (hListView == s->hSystemListView) {
            s->dataBuffer.Clear();
            s->m_sysDisplayOffset = 0;
        } else {
            for (auto& tab : s->processTabs) {
                if (tab.hListView == hListView) {
                    s->dataBuffer.ClearProcess(tab.processName);
                    tab.lastDataIdx = -1;
                    break;
                }
            }
        }
        break;
    }
    case IDM_SELECT_ALL: {
        int cnt = ListView_GetItemCount(hListView);
        for (int i = 0; i < cnt; i++)
            ListView_SetItemState(hListView, i, LVIS_SELECTED, LVIS_SELECTED);
        break;
    }
    case IDM_COPY_SELECTED: {
        // Copy selected rows to clipboard as tab-separated text
        std::wstring clipboardStr;
        int cnt = ListView_GetItemCount(hListView);

        // Get header count
        int colCount = 0;
        {
            LVCOLUMNW lvc = {};
            lvc.mask = LVCF_WIDTH;
            while (ListView_GetColumn(hListView, colCount, &lvc))
                colCount++;
        }

        for (int i = 0; i < cnt; i++) {
            if (ListView_GetItemState(hListView, i, LVIS_SELECTED) & LVIS_SELECTED) {
                for (int j = 0; j < colCount; j++) {
                    if (j > 0) clipboardStr += L"\t";
                    wchar_t buf[256];
                    ListView_GetItemText(hListView, i, j, buf, 256);
                    clipboardStr += buf;
                }
                clipboardStr += L"\r\n";
            }
        }

        if (!clipboardStr.empty()) {
            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                int byteLen = (int)((clipboardStr.length() + 1) * sizeof(wchar_t));
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteLen);
                if (hMem) {
                    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                    wcscpy_s(pMem, clipboardStr.length() + 1, clipboardStr.c_str());
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
        }
        break;
    }
    }
}

// ============================================================================
// Config Sync
// ============================================================================
void SyncUIFromConfig(MainWindowState* s) {
    auto& cfg = ConfigManager::Instance().GetConfig();

    SendMessageW(s->hCpuCheck, BM_SETCHECK, cfg.monitorCpu ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(s->hMemoryCheck, BM_SETCHECK, cfg.monitorMemory ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(s->hNetworkCheck, BM_SETCHECK, cfg.monitorNetwork ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(s->hGenerateReportCheck, BM_SETCHECK, cfg.generateReport ? BST_CHECKED : BST_UNCHECKED, 0);

    // Sync network-dependent control enabled state
    UpdateNetworkControlsEnabled(s);

    wchar_t buf[16];
    swprintf_s(buf, 16, L"%d", cfg.samplePeriod);
    SetWindowTextW(s->hSamplePeriodEdit, buf);

    SetWindowTextW(s->hOutputDirEdit, cfg.outputDir);

    // Set net unit combo
    if (wcscmp(cfg.netUnit, L"Kbps") == 0) ComboBox_SetCurSel(s->hNetUnitCombo, 0);
    else if (wcscmp(cfg.netUnit, L"Mbps") == 0) ComboBox_SetCurSel(s->hNetUnitCombo, 1);
    else if (wcscmp(cfg.netUnit, L"Gbps") == 0) ComboBox_SetCurSel(s->hNetUnitCombo, 2);

    // Populate network interfaces
    auto ifaces = s->systemMonitor.GetNetworkInterfaces();
    ComboBox_ResetContent(s->hNetInterfaceCombo);
    int selIdx = 0;
    for (size_t i = 0; i < ifaces.size(); i++) {
        int idx = ComboBox_AddString(s->hNetInterfaceCombo, ifaces[i].c_str());
        bool connected = s->systemMonitor.IsInterfaceConnected(ifaces[i]);
        ComboBox_SetItemData(s->hNetInterfaceCombo, idx, connected ? 1 : 0);
        if (ifaces[i] == cfg.netInterface) selIdx = (int)i;
    }
    ComboBox_SetCurSel(s->hNetInterfaceCombo, selIdx);
}

void SyncConfigFromUI(MainWindowState* s) {
    auto& cfg = ConfigManager::Instance().GetConfig();

    // Sync process checkbox states from the ListView (authoritative source)
    for (int i = 0; i < cfg.processCount; i++) {
        cfg.processes[i].enabled =
            (ListView_GetCheckState(s->hProcessListView, i) == TRUE);
    }

    cfg.monitorCpu = (SendMessageW(s->hCpuCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.monitorMemory = (SendMessageW(s->hMemoryCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.monitorNetwork = (SendMessageW(s->hNetworkCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.generateReport = (SendMessageW(s->hGenerateReportCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t buf[64];
    GetWindowTextW(s->hSamplePeriodEdit, buf, 16);
    int period = _wtoi(buf);
    cfg.samplePeriod = (period >= 1 && period <= 60) ? period : 5;

    int unitIdx = ComboBox_GetCurSel(s->hNetUnitCombo);
    if (unitIdx >= 0) {
        const wchar_t* units[] = { L"Kbps", L"Mbps", L"Gbps" };
        wcscpy_s(cfg.netUnit, 16, units[unitIdx]);
    }

    int ifaceIdx = ComboBox_GetCurSel(s->hNetInterfaceCombo);
    if (ifaceIdx >= 0) {
        ComboBox_GetLBText(s->hNetInterfaceCombo, ifaceIdx, cfg.netInterface);
    }

    GetWindowTextW(s->hOutputDirEdit, cfg.outputDir, MAX_PATH);
}

