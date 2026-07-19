// HelpDialog.cpp - Help/About dialog
#include "HelpDialog.h"
#include <commctrl.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "comctl32.lib")

static const wchar_t* BuildTimestamp() {
    static wchar_t buf[64]; static bool done = false;
    if (!done) {
        char ds[64]; snprintf(ds, sizeof(ds), "%s %s", __DATE__, __TIME__);
        int n = MultiByteToWideChar(CP_UTF8, 0, ds, -1, buf, 64);
        if (n > 0 && n <= 64) buf[n-1] = L'\0';
        done = true;
    }
    return buf;
}

static wchar_t g_helpTab0[1024]; static bool g_helpTab0Init = false;
static const wchar_t* GetHelpTab0() {
    if (!g_helpTab0Init) {
        swprintf_s(g_helpTab0, 1024,
            L"【挂机电脑资源监测软件 V3.2】\r\n\r\n"
            L"作者：无人机\r\n编译时间：%s\r\n\r\n"
            L"本软件是一款专业的 Windows 系统资源监测工具，专为挂机场\r\n"
            L"景设计。可无人值守持续记录 CPU、内存、网络流量及指定进\r\n"
            L"程的资源占用，自动导出 Excel 文件供后续分析。\r\n\r\n"
            L"核心能力：\r\n"
            L"  • 系统级 CPU/内存/网络实时监控\r\n"
            L"  • 进程级精细化监测（CPU、内存、网络）\r\n"
            L"  • 多进程并行监控，独立页签展示\r\n"
            L"  • 网卡选择与 ncpa.cpl 完全一致，已连接网卡绿色加粗\r\n"
            L"  • TCP+UDP 全协议进程网络追踪\r\n"
            L"  • 自动 Excel 导出，监测中实时写入防数据丢失\r\n"
            L"  • 配置持久化，随开随用",
            BuildTimestamp());
        g_helpTab0Init = true;
    }
    return g_helpTab0;
}

static const wchar_t* g_helpTitles[] = {
    L"软件简介", L"功能说明", L"使用方法", L"使用窍门", L"注意事项"
};

static const wchar_t* g_helpContents[] = {
    L"【软件简介】\r\n\r\n"
    L"作者：无人机\r\n\r\n"
    L"专业 Windows 系统资源监测工具，专为挂机场景设计。无人值守\r\n"
    L"持续记录 CPU、内存、网络流量及指定进程的资源占用，自动导出\r\n"
    L"Excel 文件。\r\n\r\n"
    L"核心能力：\r\n"
    L"  • 系统级 CPU/内存/网络实时监控\r\n"
    L"  • 进程级精细化监测（CPU、内存、网络）\r\n"
    L"  • 多进程并行监控，独立页签展示\r\n"
    L"  • 网卡选择与 ncpa.cpl 完全一致，已连接网卡绿色加粗\r\n"
    L"  • TCP+UDP 全协议进程网络追踪\r\n"
    L"  • 自动 Excel 导出，监测中实时写入防数据丢失",

    L"【功能说明】\r\n\r\n"
    L"1. CPU 使用率监控\r\n"
    L"   实时显示系统整体 CPU 负载，精度 0.01%。\r\n\r\n"
    L"2. 内存监控\r\n"
    L"   总量/可用/已用（GB）+ 使用率（%），来源 GlobalMemory\r\n"
    L"   StatusEx API。\r\n\r\n"
    L"3. 网络流量监控\r\n"
    L"   • 网卡下拉列表与 ncpa.cpl（网络连接）完全一致\r\n"
    L"   • 已连接网卡以绿色加粗字体标识\r\n"
    L"   • 支持 Kbps/Mbps/Gbps 三种单位切换\r\n"
    L"   • 默认\"全部\"汇总所有物理网卡流量\r\n\r\n"
    L"4. 进程级资源监控\r\n"
    L"   • 按进程名添加，监测 CPU/内存/网络\r\n"
    L"   • 内存使用「专用工作集」与任务管理器一致\r\n"
    L"   • 同名多进程全部独立记录（按 PID 区分）\r\n"
    L"   • TCP+UDP 全协议进程网络追踪（IPv4+IPv6）\r\n"
    L"   • 网速使用指数移动平均平滑显示，避免跳 0\r\n"
    L"   • UI 展示前 10 个 PID（保证界面流畅），Excel 导出全量\r\n\r\n"
    L"5. 数据导出与日志\r\n"
    L"   • 监测中每 2 秒实时写入 Excel，防崩溃丢失\r\n"
    L"   • 停止时最终保存，文件名含时间戳，不会覆盖旧文件\r\n"
    L"   • 系统/进程分 Sheet 独立存储\r\n"
    L"   • 日志界面保留最近 5 万行，旧数据自动裁剪\r\n"
    L"   • 右键菜单：清除日志/全选/复制选中\r\n\r\n"
    L"6. 其他功能\r\n"
    L"   • 窗口置顶切换、彩色状态标签、配置保存/加载\r\n"
    L"   • 标题栏右键快捷打开软件所在目录",

    L"【使用方法】\r\n\r\n"
    L"1. 勾选监测项目（CPU/内存/网络）\r\n"
    L"2. 选择流量单位和要监控的网卡（默认\"全部\"）\r\n"
    L"3. 设置采样周期（1~60 秒，推荐 5 秒）\r\n"
    L"4. 输入进程名（如 chrome.exe）点击\"添加\"\r\n"
    L"5. 设置输出目录（默认 exe 所在目录）\r\n"
    L"6. 点击\"开始监测\"\r\n"
    L"7. 点击\"停止监测\"自动导出 Excel\r\n\r\n"
    L"数据保存规则：\r\n"
    L"  • 监测中每 2 秒自动刷新 Excel（实时保存）\r\n"
    L"  • 停止时最终保存并释放文件锁定\r\n"
    L"  • 界面日志保留 5 万行，旧行自动裁剪",

    L"【使用窍门】\r\n\r\n"
    L"1. 采样周期：短时观察用 1~3s，日常推荐 5~10s，长挂机 30~60s\r\n"
    L"2. 网卡选择：默认\"全部\"满足大多数场景，选特定网卡可看单独流量\r\n"
    L"3. 窗口置顶：底部按钮切换，方便实时观察\r\n"
    L"4. 右键菜单：数据列表右键可清除/全选/复制（Tab 分隔）\r\n"
    L"5. 标题栏右键：快速打开软件所在目录\r\n"
    L"6. 配置保存：设好后保存，下次自动恢复\r\n"
    L"7. 获取进程名：任务管理器→详细信息→复制名称列的值",

    L"【注意事项】\r\n\r\n"
    L"1. 监测期间配置项被锁定，需停止后才能修改\r\n\r\n"
    L"2. 进程监控说明\r\n"
    L"   • 按进程名精确匹配（不区分大小写）\r\n"
    L"   • 进程退出后数据归零，监测不中断\r\n"
    L"   • UI 展示前 10 个 PID，Excel 中保留全部\r\n"
    L"   • 网速显示经指数移动平均平滑处理\r\n\r\n"
    L"3. 进程网络流量\r\n"
    L"   • TCP：通过 GetPerTcpConnectionEStats 精确到字节\r\n"
    L"   • UDP：通过系统级统计按端点比例估算\r\n"
    L"   • 需管理员权限运行（UAC 提权）\r\n\r\n"
    L"4. 数据保存\r\n"
    L"   • 监测中每 2 秒实时写入，即使崩溃数据仍保留\r\n"
    L"   • 缓冲区上限 200 万行，超过自动丢弃最旧数据\r\n"
    L"   • Excel 文件大小随数据增长，建议定期分段监测\r\n\r\n"
    L"5. 网卡选择\r\n"
    L"   • 下拉列表与 ncpa.cpl 完全一致\r\n"
    L"   • 已连接网卡以绿色加粗字体标识\r\n"
    L"   • 点击\"刷新\"可重新扫描\r\n"
    L"   • 在 ncpa.cpl 中改名后，刷新即可更新\r\n\r\n"
    L"6. 兼容性\r\n"
    L"   支持 Windows 7/8/10/11（x64），需管理员权限。"
};

static const int g_helpTabCount = 5;

LRESULT CALLBACK HelpDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hTab = nullptr, hText = nullptr;
    static HFONT hFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);
        HDC hdc = GetDC(hWnd);
        int fh = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hWnd, hdc);
        hFont = CreateFontW(fh,0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        hTab = CreateWindowW(WC_TABCONTROLW, nullptr,
            WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
            10,10, w-20,28, hWnd,nullptr,hi,nullptr);
        SendMessageW(hTab, WM_SETFONT, (WPARAM)hFont, TRUE);
        for (int i=0; i<g_helpTabCount; i++) {
            TCITEMW t={}; t.mask=TCIF_TEXT; t.pszText=(LPWSTR)g_helpTitles[i];
            SendMessageW(hTab, TCM_INSERTITEMW, i, (LPARAM)&t);
        }

        hText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD|WS_VISIBLE|ES_LEFT|ES_MULTILINE|ES_READONLY|
            ES_AUTOVSCROLL|WS_VSCROLL,
            10,44, w-20, h-90, hWnd,nullptr,hi,nullptr);
        SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hText, EM_SETMARGINS, EC_LEFTMARGIN|EC_RIGHTMARGIN, MAKELPARAM(10,10));
        SetWindowTextW(hText, GetHelpTab0());

        CreateWindowW(WC_BUTTONW, L"关闭", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            w/2-40, h-38, 80,28, hWnd,(HMENU)IDCANCEL,hi,nullptr);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* nm=(NMHDR*)lParam;
        if (nm->code==TCN_SELCHANGE) {
            int sel=(int)SendMessageW(hTab, TCM_GETCURSEL,0,0);
            if (sel>=0 && sel<g_helpTabCount)
                SetWindowTextW(hText, sel==0 ? GetHelpTab0() : g_helpContents[sel]);
        }
        break;
    }
    case WM_DESTROY: if (hFont) { DeleteObject(hFont); hFont=nullptr; } break;
    case WM_CLOSE: case WM_COMMAND:
        if (msg==WM_COMMAND && LOWORD(wParam)!=IDCANCEL) break;
        EnableWindow((HWND)GetWindowLongPtrW(hWnd,GWLP_HWNDPARENT), TRUE);
        SetForegroundWindow((HWND)GetWindowLongPtrW(hWnd,GWLP_HWNDPARENT));
        DestroyWindow(hWnd);
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowHelpDialog(HWND hParent) {
    static bool reg=false;
    if (!reg) {
        WNDCLASSEXW wc={}; wc.cbSize=sizeof(WNDCLASSEXW);
        wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=HelpDlgProc;
        wc.hInstance=GetModuleHandleW(nullptr); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName=L"MonitorToolHelpDlg";
        RegisterClassExW(&wc); reg=true;
    }
    RECT r={0,0,680,580}; AdjustWindowRect(&r, WS_POPUP|WS_CAPTION|WS_SYSMENU, FALSE);
    int w=r.right-r.left, h=r.bottom-r.top;
    RECT pr; GetWindowRect(hParent, &pr);
    HWND dlg=CreateWindowExW(0, L"MonitorToolHelpDlg", L"帮助说明",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        pr.left+(pr.right-pr.left-w)/2, pr.top+(pr.bottom-pr.top-h)/2, w, h,
        hParent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!dlg) return;
    ShowWindow(dlg, SW_SHOW); UpdateWindow(dlg);
    EnableWindow(hParent, FALSE);
}
