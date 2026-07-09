// MainWindow.cpp - Full Win32 UI implementation
#include "MainWindow.h"
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

    return CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_WINDOWEDGE,
        L"MonitorToolMainWindow",
        L"挂机电脑资源监测软件 V2.6 by 无人机",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );
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
        s->statusBarHeight = 28;

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

        InitMainWindow(hWnd, s);

        // Set a display update timer (200ms)
        SetTimer(hWnd, IDT_DISPLAY_UPDATE, 200, nullptr);

        return 0;
    }

    case WM_SIZE: {
        if (s) {
            LayoutControls(s);
            // 确保窗口大小变化时所有区域被重绘
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
        // 状态标签：根据内容显示不同颜色
        if (s && (HWND)lParam == s->hStatusLabel) {
            SetTextColor((HDC)wParam, s->statusColor);
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        // 其他 STATIC 控件用系统背景色
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_TIMER: {
        if (wParam == IDT_DISPLAY_UPDATE && s && s->isMonitoring) {
            UpdateDisplay(s);
            // Flush Excel every 2 seconds minimum (avoid UI lag on large datasets)
            DWORD tick = GetTickCount();
            int sysCount = (int)s->dataBuffer.GetSystemDataRef().size();
            if (sysCount > s->lastFlushSystemIndex && (tick - s->lastFlushTick) >= 2000) {
                s->lastFlushSystemIndex = sysCount;
                s->lastFlushTick = tick;
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
                s->excelExporter.FlushExport(sysCopy, procs, procData, cfg.netUnit);
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
                RebuildProcessTabs(s);
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
            for (auto& iface : ifaces)
                ComboBox_AddString(s->hNetInterfaceCombo, iface.c_str());
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
        }
        return 0;
    }

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

    case WM_CLOSE: {
        if (s && s->isMonitoring) {
            if (MessageBoxW(hWnd, L"监测正在进行中，确定要退出吗？", L"确认",
                           MB_YESNO | MB_ICONQUESTION) == IDNO)
                return 0;
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

    // Initialize system monitor baseline
    s->systemMonitor.Initialize();

    return true;
}

// ============================================================================
// Create all child controls
// ============================================================================
void CreateChildControls(HWND hParent, MainWindowState* s) {
    HINSTANCE hi = s->hInst;
    int yBase = 8;

    // ---- Monitor Config Group ----
    s->hConfigGroup = CreateWindowExW(0, L"BUTTON", L"监测配置",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        8, yBase, 874, 150, hParent, (HMENU)IDC_MONITOR_CONFIG_GROUP, hi, nullptr);

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
    s->hProcessListView = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | LVS_EX_CHECKBOXES,
        20, yBase + gy, 834, 90, hParent, (HMENU)IDC_PROCESS_LIST, hi, nullptr);
    ListView_SetExtendedListViewStyle(s->hProcessListView, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // Columns: enabled, name, actions
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_CENTER;
    lvc.cx = 40;
    lvc.pszText = (LPWSTR)L"";
    ListView_InsertColumn(s->hProcessListView, 0, &lvc);
    lvc.cx = 200;
    lvc.fmt = LVCFMT_LEFT;
    lvc.pszText = (LPWSTR)L"软件名称";
    ListView_InsertColumn(s->hProcessListView, 1, &lvc);
    lvc.cx = 50;
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
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
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

    // ---- Monitor Control Group ----
    int ctrlGroupY = itemsGroupY + 130;
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
        s->hCpuCheck, s->hMemoryCheck, s->hNetworkCheck,
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

    // Config group
    int configH = 150;
    SetWindowPos(s->hConfigGroup, nullptr, margin, margin, contentW, configH, SWP_NOZORDER);
    SetWindowPos(s->hProcessListView, nullptr, margin + 12, margin + 55,
                 contentW - 24, 85, SWP_NOZORDER);

    // Items group
    int itemsY = margin + configH + 8;
    int itemsH = 120;
    SetWindowPos(s->hItemsGroup, nullptr, margin, itemsY, contentW, itemsH, SWP_NOZORDER);

    // Control group
    int ctrlY = itemsY + itemsH + 8;
    int ctrlH = 68;
    SetWindowPos(s->hControlGroup, nullptr, margin, ctrlY, contentW, ctrlH, SWP_NOZORDER);

    // Status label inside control group — right side
    SetWindowPos(s->hStatusLabel, nullptr, w - 280, ctrlY + 21, 250, 26, SWP_NOZORDER);

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
        // Create tab and refresh layout
        AddProcessTab(s, name);
        LayoutControls(s);
    } else if (!enabled && tabExists) {
        // Remove tab and refresh layout
        RemoveProcessTab(s, name);
        // If the removed tab was selected, switch to system tab
        if (TabCtrl_GetCurSel(s->hTab) >= TabCtrl_GetItemCount(s->hTab)) {
            TabCtrl_SetCurSel(s->hTab, 0);
        }
        OnTabChanged(s);
        LayoutControls(s);
    }
}

void RebuildProcessTabs(MainWindowState* s) {
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

    TabCtrl_SetCurSel(s->hTab, 0);
    OnTabChanged(s);
    LayoutControls(s);
}

void OnTabChanged(MainWindowState* s) {
    int sel = TabCtrl_GetCurSel(s->hTab);

    // Hide all listviews
    ShowWindow(s->hSystemListView, SW_HIDE);
    for (auto& tab : s->processTabs)
        ShowWindow(tab.hListView, SW_HIDE);

    if (sel == 0) {
        ShowWindow(s->hSystemListView, SW_SHOW);
    } else {
        for (auto& tab : s->processTabs) {
            if (tab.tabIndex == sel) {
                ShowWindow(tab.hListView, SW_SHOW);
                break;
            }
        }
    }

    LayoutControls(s);
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
    for (auto& tab : s->processTabs)
        ListView_DeleteAllItems(tab.hListView);

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

    // Start ETW-based per-process network monitor (requires admin)
    if (!s->netSpeedMonitor.Start()) {
        OutputDebugStringW(L"[MainWindow] NetSpeedMonitor failed — process network will be 0\r\n");
    }

    // Begin real-time Excel export (file locked, read-only for external viewers)
    s->excelExporter.SetNetUnit(cfg.netUnit);
    s->excelExporter.BeginExport(cfg.outputDir, s->monitorStartTime);

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

    // Wait for thread to finish
    if (s->hMonitorThread) {
        WaitForSingleObject(s->hMonitorThread, 10000); // 10 second timeout
        CloseHandle(s->hMonitorThread);
        s->hMonitorThread = nullptr;
    }

    if (s->hStopEvent) {
        CloseHandle(s->hStopEvent);
        s->hStopEvent = nullptr;
    }

    s->isMonitoring = false;

    // Show saving indicator
    UpdateStatus(s, L"状态: 正在保存中…", RGB(255, 165, 0));  // orange

    // Final flush of all data to Excel
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
        s->excelExporter.FlushExport(sysCopy, procs, procData, cfg.netUnit);
    }
    s->excelExporter.EndExport();

    {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"数据已导出到 Excel 文件\r\n路径: %s",
                   s->excelExporter.GetLastFilePath());
        MessageBoxW(s->hMainWnd, msg, L"成功", MB_OK | MB_ICONINFORMATION);
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

    while (WaitForSingleObject(s->hStopEvent, 0) == WAIT_TIMEOUT) {
        DWORD iterStart = GetTickCount();

        double runSeconds = (double)time(nullptr) - s->monitorStartTime;

        // Collect system data
        if (cfg.monitorCpu || cfg.monitorMemory || cfg.monitorNetwork) {
            SystemMonitorData sysData = s->systemMonitor.Collect(runSeconds);
            s->dataBuffer.AddSystemData(sysData);
        }

        // Collect process data (one entry per PID for same-name processes)
        for (size_t i = 0; i < s->processMonitors.size(); i++) {
            auto procDataList = s->processMonitors[i]->Collect(runSeconds, &s->netSpeedMonitor);
            for (auto& procData : procDataList) {
                s->dataBuffer.AddProcessData(s->processMonitors[i]->GetProcessName(), procData);
            }
        }

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
    const auto& sysData = s->dataBuffer.GetSystemDataRef();
    int totalCount = (int)sysData.size();

    // Use actual ListView item count instead of a tracked index,
    // so display stays correct even when DataBuffer trims old entries.
    int displayedCount = ListView_GetItemCount(s->hSystemListView);

    // Add new items since last display
    for (int i = displayedCount; i < totalCount; i++) {
        const auto& d = sysData[i];

        wchar_t buf[64];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = i;

        item.iSubItem = SYS_COL_TIME;
        item.pszText = (LPWSTR)d.timestamp;
        if (i == displayedCount) {
            ListView_InsertItem(s->hSystemListView, &item);
        }
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_TIME, (LPWSTR)d.timestamp);

        swprintf_s(buf, 64, L"%.2f", d.runSeconds);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_RUN_SEC, buf);

        swprintf_s(buf, 64, L"%.2f", d.cpuUsage);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_CPU, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryTotalGB);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_MEM_TOTAL, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryAvailableGB);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_MEM_AVAIL, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsedGB);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_MEM_USED, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsage);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_MEM_USAGE, buf);

        swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_NET_SEND, buf);

        swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);
        ListView_SetItemText(s->hSystemListView, i, SYS_COL_NET_RECV, buf);
    }

    // Trim if too many items (keep last 10000)
    int currentItems = ListView_GetItemCount(s->hSystemListView);
    int maxItems = 10000;
    while (currentItems > maxItems) {
        ListView_DeleteItem(s->hSystemListView, 0);
        currentItems--;
    }

    // Auto-scroll to bottom
    if (currentItems > 0)
        ListView_EnsureVisible(s->hSystemListView, currentItems - 1, FALSE);
}

void UpdateProcessListView(MainWindowState* s, ProcessTabInfo& tab) {
    const auto* procData = s->dataBuffer.GetProcessDataRef(tab.processName);
    if (!procData) return;

    int totalCount = (int)procData->size();

    // Use actual ListView item count to stay in sync with DataBuffer trimming
    int displayedCount = ListView_GetItemCount(tab.hListView);

    for (int i = displayedCount; i < totalCount; i++) {
        const auto& d = (*procData)[i];

        wchar_t buf[64];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = i;

        if (i == displayedCount) {
            item.iSubItem = PROC_COL_TIME;
            item.pszText = (LPWSTR)d.timestamp;
            ListView_InsertItem(tab.hListView, &item);
        }

        ListView_SetItemText(tab.hListView, i, PROC_COL_TIME, (LPWSTR)d.timestamp);

        swprintf_s(buf, 64, L"%.2f", d.runSeconds);
        ListView_SetItemText(tab.hListView, i, PROC_COL_RUN_SEC, buf);

        swprintf_s(buf, 64, L"%lu", d.pid);
        ListView_SetItemText(tab.hListView, i, PROC_COL_PID, buf);

        swprintf_s(buf, 64, L"%.2f", d.cpuUsage);
        ListView_SetItemText(tab.hListView, i, PROC_COL_CPU, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsage);
        ListView_SetItemText(tab.hListView, i, PROC_COL_MEM_USAGE, buf);

        swprintf_s(buf, 64, L"%.2f", d.memoryUsedMB);
        ListView_SetItemText(tab.hListView, i, PROC_COL_MEM_USED, buf);

        swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);
        ListView_SetItemText(tab.hListView, i, PROC_COL_NET_SEND, buf);

        swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);
        ListView_SetItemText(tab.hListView, i, PROC_COL_NET_RECV, buf);
    }

    // Trim if too many items (keep last 10000)
    int currentItems = ListView_GetItemCount(tab.hListView);
    while (currentItems > 10000) {
        ListView_DeleteItem(tab.hListView, 0);
        currentItems--;
    }

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
                                systemData, processes, allProcessData, cfg.netUnit)) {
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
        } else {
            for (auto& tab : s->processTabs) {
                if (tab.hListView == hListView) {
                    s->dataBuffer.ClearProcess(tab.processName);
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
        ComboBox_AddString(s->hNetInterfaceCombo, ifaces[i].c_str());
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

// ============================================================================
// Help Dialog
// ============================================================================

// Build timestamp from compiler __DATE__ / __TIME__ macros
// __DATE__ = "Mmm DD YYYY"   __TIME__ = "HH:MM:SS"
static const wchar_t* BuildTimestamp() {
    static wchar_t buf[64];
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int month = 1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(__DATE__, months[i], 3) == 0) { month = i + 1; break; }
    }
    int day = atoi(__DATE__ + 4);
    int year = atoi(__DATE__ + 7);
    int h = atoi(__TIME__);
    int m = atoi(__TIME__ + 3);
    int s = atoi(__TIME__ + 6);
    swprintf_s(buf, 64, L"%d年%d月%d日 %02d时%02d分%02d秒", year, month, day, h, m, s);
    return buf;
}

// Buffer for Tab 0 (includes dynamic build timestamp)
static wchar_t g_helpContentTab0[1024];
static bool g_helpContentTab0Init = false;

static const wchar_t* GetHelpTab0Content() {
    if (!g_helpContentTab0Init) {
        swprintf_s(g_helpContentTab0, 1024,
            L"【挂机电脑资源监测软件  V2.6】\r\n"
            L"\r\n"
            L"作者：无人机\r\n"
            L"编译时间：%s\r\n"
            L"\r\n"
            L"本软件是一款专业的 Windows 系统资源监测工具，专为挂机场\r\n"
            L"景设计。可在无人值守的情况下持续记录系统 CPU、内存、网\r\n"
            L"络流量以及指定进程的资源占用情况，并在监测结束后自动导出\r\n"
            L"为 Excel 文件，方便后续分析。\r\n"
            L"\r\n"
            L"核心能力：\r\n"
            L"  • 系统级 CPU / 内存 / 网络流量实时监控\r\n"
            L"  • 进程级精细化资源监测（CPU、内存、网络）\r\n"
            L"  • 多进程并行监控，独立数据展示\r\n"
            L"  • 自动 Excel 数据导出\r\n"
            L"  • 配置持久化，随开随用",
            BuildTimestamp());
        g_helpContentTab0Init = true;
    }
    return g_helpContentTab0;
}

// Help content for each tab
static const wchar_t* g_helpTitles[] = {
    L"软件简介",
    L"功能说明",
    L"使用方法",
    L"使用窍门",
    L"注意事项"
};

static const wchar_t* g_helpContents[] = {
    // Tab 0: 软件简介
    L"【挂机电脑资源监测软件  V2.6】\r\n"
    L"\r\n"
    L"作者：无人机\r\n"
    L"\r\n"
    L"本软件是一款专业的 Windows 系统资源监测工具，专为挂机场\r\n"
    L"景设计。可在无人值守的情况下持续记录系统 CPU、内存、网\r\n"
    L"络流量以及指定进程的资源占用情况，并在监测结束后自动导出\r\n"
    L"为 Excel 文件，方便后续分析。\r\n"
    L"\r\n"
    L"核心能力：\r\n"
    L"  • 系统级 CPU / 内存 / 网络流量实时监控\r\n"
    L"  • 进程级精细化资源监测（CPU、内存、网络）\r\n"
    L"  • 多进程并行监控，独立数据展示\r\n"
    L"  • 自动 Excel 数据导出\r\n"
    L"  • 配置持久化，随开随用",

    // Tab 1: 功能说明
    L"【功能说明】\r\n"
    L"\r\n"
    L"1. CPU 使用率监控\r\n"
    L"   实时显示系统整体 CPU 负载百分比，精度保留两位小数。\r\n"
    L"\r\n"
    L"2. 内存监控\r\n"
    L"   显示内存总量、可用量、已用量（单位 GB）以及使用率（%），\r\n"
    L"   数据来源于 Windows 全局内存状态 API，准确可靠。\r\n"
    L"\r\n"
    L"3. 网络流量监控\r\n"
    L"   • 支持按网卡筛选：下拉列表与 ncpa.cpl（网络连接）完全一致\r\n"
    L"   • 默认\"全部\"汇总所有物理网卡流量\r\n"
    L"   • 支持 Kbps、Mbps、Gbps 三种单位切换\r\n"
    L"   • 显示发送/接收双向速率\r\n"
    L"\r\n"
    L"4. 进程级资源监控\r\n"
    L"   • 支持添加任意正在运行的进程名称\r\n"
    L"   • 自动检测进程是否存在，智能提示确认\r\n"
    L"   • 监测项：CPU、内存（专用工作集，MB）、网络速率\r\n"
    L"   • 同名多进程按 PID 全部独立记录\r\n"
    L"   • 支持勾选启用/禁用单个进程，日志页签实时更新\r\n"
    L"   • 页签名称自动隐藏 .exe 后缀\r\n"
    L"\r\n"
    L"5. Excel 数据导出\r\n"
    L"   停止监测后自动导出为 .xlsx 文件，单元格居中对齐、列宽\r\n"
    L"   自适应，包含系统工作表和各进程独立工作表，文件名带时戳。\r\n"
    L"\r\n"
    L"6. 其他功能\r\n"
    L"   • 窗口置顶：点击\"置顶\"按钮使窗口始终在最前\r\n"
    L"   • 配置保存：手动保存/自动加载配置文件\r\n"
    L"   • 右键菜单：日志列表支持清除/全选/复制",

    // Tab 2: 使用方法
    L"【使用方法】\r\n"
    L"\r\n"
    L"步骤 1 — 选择监测项目\r\n"
    L"  在\"监测项目设置\"区域勾选需要监测的项目（CPU / 内存 / 网络）。\r\n"
    L"\r\n"
    L"步骤 2 — 配置网络参数\r\n"
    L"  选择合适的流量单位（Kbps、Mbps、Gbps），从下拉列表中选择\r\n"
    L"  要监控的网卡（默认为\"全部\"）。可点击\"刷新\"按钮重新扫描。\r\n"
    L"\r\n"
    L"步骤 3 — 设置采样周期\r\n"
    L"  输入采样间隔秒数（1~60），建议日常使用 5 秒。周期越短数据\r\n"
    L"  越精细，但数据量越大。\r\n"
    L"\r\n"
    L"步骤 4 — 添加监测进程\r\n"
    L"  在\"监测配置\"区域输入进程名称（如 MPrintExp.exe），点击\r\n"
    L"  \"添加\"。可添加多个进程，勾选需要的进程启用监测。使用\r\n"
    L"  \"删除\"移除单个进程，\"全部删除\"清空列表。\r\n"
    L"\r\n"
    L"步骤 5 — 设置输出目录\r\n"
    L"  输入 Excel 文件保存路径，或点击\"浏览\"选择目录。默认为\r\n"
    L"  软件所在目录。\r\n"
    L"\r\n"
    L"步骤 6 — 开始监测\r\n"
    L"  点击\"开始监测\"按钮，软件开始采集数据。监测中配置项被\r\n"
    L"  锁定，状态标签变为红色\"状态: 监测中\"。\r\n"
    L"\r\n"
    L"步骤 7 — 停止与导出\r\n"
    L"  点击\"停止监测\"按钮，数据自动导出为 Excel 文件并弹出提示。",

    // Tab 3: 使用窍门
    L"【使用窍门】\r\n"
    L"\r\n"
    L"1. 采样周期选择\r\n"
    L"   • 1~3 秒：适合短时间精细观察，数据密度高\r\n"
    L"   • 5~10 秒：日常使用推荐，平衡精度与性能 ★\r\n"
    L"   • 30~60 秒：长时间挂机，数据文件较小\r\n"
    L"\r\n"
    L"2. 网卡选择技巧\r\n"
    L"   • 默认\"全部\"汇总所有物理网卡流量，满足大多数场景\r\n"
    L"   • 仅关注某个网卡时（如 VPN、外网），选择对应网卡即可\r\n"
    L"\r\n"
    L"3. 窗口置顶\r\n"
    L"   底部\"置顶\"按钮可使窗口始终在前，方便实时观察数据。再次\r\n"
    L"   点击取消置顶。\r\n"
    L"\r\n"
    L"4. 右键快捷操作\r\n"
    L"   在数据列表上右键可访问：\r\n"
    L"   • \"清除日志\" — 清空当前显示的数据\r\n"
    L"   • \"全选\" — 选中所有行\r\n"
    L"   • \"复制选中\" — 复制到剪贴板（Tab 分隔，可粘贴到 Excel）\r\n"
    L"\r\n"
    L"5. 标题栏右键\r\n"
    L"   右键点击标题栏可快速\"打开软件所在目录\"，方便访问配置\r\n"
    L"   文件和导出数据。\r\n"
    L"\r\n"
    L"6. 配置保存\r\n"
    L"   设置好参数后点击\"保存配置\"，下次启动自动恢复所有设置，\r\n"
    L"   无需重复配置。\r\n"
    L"\r\n"
    L"7. 进程名称获取\r\n"
    L"   打开任务管理器（Ctrl+Shift+Esc）→\"详细信息\"标签页 → \r\n"
    L"   找到目标进程 → 复制\"名称\"列的值粘贴到软件中。",

    // Tab 4: 注意事项
    L"【注意事项】\r\n"
    L"\r\n"
    L"1. 监测期间限制\r\n"
    L"   监测进行中，所有配置项（监测项目、进程列表、采样周期、\r\n"
    L"   网卡选择、输出目录）将被锁定，不可修改。需先停止监测。\r\n"
    L"\r\n"
    L"2. 进程监控说明\r\n"
    L"   • 进程监控依赖进程名精确匹配（不区分大小写）\r\n"
    L"   • 若进程退出，对应数据将归零，但监测不会中断\r\n"
    L"   • 同名多进程（不同 PID）全部独立记录，逐行显示\r\n"
    L"   • 内存使用「专用工作集」，与任务管理器显示一致\r\n"
    L"\r\n"
    L"3. 进程网络流量\r\n"
    L"   • 通过 TCP 连接统计 API 读取每个连接字节计数器\r\n"
    L"   • 两次采样间计算增量，换算为 Kbps / Mbps / Gbps\r\n"
    L"   • 需管理员权限（启动时 UAC 提权）才能启用统计追踪\r\n"
    L"   • 未提权时进程网络显示 0.00，系统网络不受影响\r\n"
    L"\r\n"
    L"4. Excel 导出\r\n"
    L"   • 仅在停止监测时自动导出\r\n"
    L"   • 文件名格式：资源监测数据_YYYYMMDD_HHMMSS.xlsx\r\n"
    L"   • 导出需要目标目录写入权限\r\n"
    L"\r\n"
    L"5. 权限说明\r\n"
    L"   进程级网络监测需要 ETW 内核追踪会话，必须以管理员权限\r\n"
    L"   运行。程序已嵌入提权清单，启动时自动弹出 UAC 提示。\r\n"
    L"   若拒绝提权，CPU/内存/系统网络不受影响。\r\n"
    L"\r\n"
    L"6. 性能影响\r\n"
    L"   软件使用低优先级线程采集数据，对系统性能影响极小。采样\r\n"
    L"   周期越短、监控进程越多，资源消耗越大。\r\n"
    L"\r\n"
    L"7. 配置文件\r\n"
    L"   config.json 保存在软件所在目录。如遇配置异常，删除该文件\r\n"
    L"   后重启软件即可恢复默认设置。\r\n"
    L"\r\n"
    L"8. 兼容性\r\n"
    L"   支持 Windows 7 / 8 / 10 / 11 操作系统。建议使用 Windows 10\r\n"
    L"   及以上版本以获得最佳体验。"
};

static const int g_helpTabCount = 5;

// Help dialog window proc
LRESULT CALLBACK HelpDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hTab = nullptr;
    static HWND hText = nullptr;
    static HWND hCloseBtn = nullptr;
    static HFONT hFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);

        // Create a nice readable font (9pt Microsoft YaHei)
        HDC hdc = GetDC(hWnd);
        int fontH = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hWnd, hdc);
        hFont = CreateFontW(
            fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        RECT rc;
        GetClientRect(hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;

        int margin = 14;
        int btnH = 32;
        int btnW = 100;
        int tabH = ch - margin - btnH - margin;  // tab height: leave room for button

        // Tab control
        hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
            margin, margin, cw - margin * 2, tabH,
            hWnd, (HMENU)IDC_HELP_TAB, hi, nullptr);
        SendMessageW(hTab, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Insert tabs
        TCITEMW tci = {};
        tci.mask = TCIF_TEXT;
        for (int i = 0; i < g_helpTabCount; i++) {
            tci.pszText = (LPWSTR)g_helpTitles[i];
            TabCtrl_InsertItem(hTab, i, &tci);
        }

        // Calculate text area inside the tab's display region
        RECT displayRc;
        GetClientRect(hTab, &displayRc);
        TabCtrl_AdjustRect(hTab, FALSE, &displayRc);
        // displayRc is now the area where we can draw, relative to hTab's client

        int pad = 6;
        // Convert to dialog-relative coordinates (tab is at margin,margin in dialog)
        int tx = margin + displayRc.left + pad;
        int ty = margin + displayRc.top + pad;
        int tw = (displayRc.right - displayRc.left) - pad * 2;
        int th = (displayRc.bottom - displayRc.top) - pad * 2;

        // Read-only multi-line text area
        hText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
            WS_VSCROLL | ES_AUTOVSCROLL,
            tx, ty, tw, th,
            hWnd, (HMENU)IDC_HELP_TEXT, hi, nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Close button — bottom center
        hCloseBtn = CreateWindowExW(0, L"BUTTON", L"关  闭(&C)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            (cw - btnW) / 2, ch - margin - btnH, btnW, btnH,
            hWnd, (HMENU)IDC_HELP_CLOSE, hi, nullptr);
        SendMessageW(hCloseBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Show first tab content
        SetWindowTextW(hText, GetHelpTab0Content());

        // Set focus to close button
        SetFocus(hCloseBtn);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;
        int margin = 14;
        int btnH = 32;
        int btnW = 100;

        // Resize tab
        SetWindowPos(hTab, nullptr, margin, margin,
            cw - margin * 2, ch - margin - btnH - margin,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Resize text area (inside tab display region)
        RECT displayRc;
        GetClientRect(hTab, &displayRc);
        TabCtrl_AdjustRect(hTab, FALSE, &displayRc);
        int pad = 6;
        SetWindowPos(hText, nullptr,
            margin + displayRc.left + pad,
            margin + displayRc.top + pad,
            (displayRc.right - displayRc.left) - pad * 2,
            (displayRc.bottom - displayRc.top) - pad * 2,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Reposition close button
        SetWindowPos(hCloseBtn, nullptr,
            (cw - btnW) / 2, ch - margin - btnH, btnW, btnH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nmh = (NMHDR*)lParam;
        if (nmh->idFrom == IDC_HELP_TAB && nmh->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(hTab);
            if (sel >= 0 && sel < g_helpTabCount) {
                if (sel == 0)
                    SetWindowTextW(hText, GetHelpTab0Content());
                else
                    SetWindowTextW(hText, g_helpContents[sel]);
            }
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_HELP_CLOSE || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (hFont) DeleteObject(hFont);
        hTab = nullptr;
        hText = nullptr;
        hCloseBtn = nullptr;
        hFont = nullptr;
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowHelpDialog(HWND hParent) {
    // Register help dialog class (once)
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = HelpDlgProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hIcon = nullptr;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = L"MonitorToolHelpDialog";
        wc.hIconSm = nullptr;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // Calculate dialog size — use AdjustWindowRect so client area is exactly what we want
    RECT desiredClient = { 0, 0, 680, 560 };
    AdjustWindowRect(&desiredClient, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE);
    int dlgW = desiredClient.right - desiredClient.left;
    int dlgH = desiredClient.bottom - desiredClient.top;

    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int x = parentRc.left + ((parentRc.right - parentRc.left) - dlgW) / 2;
    int y = parentRc.top + ((parentRc.bottom - parentRc.top) - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        0,
        L"MonitorToolHelpDialog",
        L"帮助说明",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hParent, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hDlg) return;

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // Disable parent window for modal behavior
    EnableWindow(hParent, FALSE);

    // Modal message loop
    MSG dlgMsg;
    while (IsWindow(hDlg)) {
        if (GetMessageW(&dlgMsg, nullptr, 0, 0)) {
            if (!IsDialogMessageW(hDlg, &dlgMsg)) {
                TranslateMessage(&dlgMsg);
                DispatchMessageW(&dlgMsg);
            }
        }
    }

    // Re-enable parent
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);
}
