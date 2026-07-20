// HelpDialog.cpp - Help/About dialog
#include "HelpDialog.h"
#include "build_timestamp.h"
#include <commctrl.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "comctl32.lib")

static const wchar_t* BuildTimestamp() {
#ifdef TIMESTAMP_SHORT
    return BUILD_TIMESTAMP_SHORT;
#else
    return BUILD_TIMESTAMP_FULL;
#endif
}

static wchar_t g_helpTab0[1024]; static bool g_helpTab0Init = false;
static const wchar_t* GetHelpTab0() {
    if (!g_helpTab0Init) {
        swprintf_s(g_helpTab0, 1024,
            L"【挂机电脑资源监测软件 V3.4】\r\n"
            L"\r\n"
            L"作者：无人机\r\n"
            L"编译时间：%s\r\n"
            L"\r\n"
            L"本软件是一款专业的 Windows 系统资源监测工具，专为挂机场\r\n"
            L"景设计。可在无人值守的情况下持续记录系统 CPU、内存、网\r\n"
            L"络流量以及指定进程的资源占用情况，并自动导出为 Excel 文\r\n"
            L"件和 HTML 趋势报告，方便后续分析。\r\n"
            L"\r\n"
            L"核心能力：\r\n"
            L"  • 系统级 CPU / 内存 / 网络流量实时监控\r\n"
            L"  • 进程级精细化资源监测（CPU、内存、网络）\r\n"
            L"  • 多进程并行监控，独立数据展示\r\n"
            L"  • 网卡选择与 ncpa.cpl 完全一致，已连接网卡绿色加粗\r\n"
            L"  • TCP+UDP 全协议进程网络追踪\r\n"
            L"  • 进程网速按网卡占比缩放（选未连接网卡时进程网速为 0）\r\n"
            L"  • 自动 Excel 导出 + HTML 趋势报告（Canvas 折线图）\r\n"
            L"  • X 轴框选缩放、点击显示坐标值\r\n"
            L"  • 关闭窗口即完全退出，不留后台进程\r\n"
            L"  • 配置持久化，随开随用",
            BuildTimestamp());
        g_helpTab0Init = true;
    }
    return g_helpTab0;
}

static const wchar_t* g_helpTitles[] = {
    L"软件简介",
    L"功能说明",
    L"使用方法",
    L"使用窍门",
    L"注意事项"
};

static const wchar_t* g_helpContents[] = {
    // [0] Tab 1: 软件简介
    L"【软件简介】\r\n"
    L"\r\n"
    L"作者：无人机\r\n"
    L"\r\n"
    L"本软件是一款专业的 Windows 系统资源监测工具，专为挂机场\r\n"
    L"景设计。可在无人值守的情况下持续记录系统 CPU、内存、网\r\n"
    L"络流量以及指定进程的资源占用情况，并自动导出为 Excel 文\r\n"
    L"件和 HTML 趋势报告，方便后续分析。\r\n"
    L"\r\n"
    L"核心能力：\r\n"
    L"  • 系统级 CPU / 内存 / 网络流量实时监控\r\n"
    L"  • 进程级精细化资源监测（CPU、内存、网络）\r\n"
    L"  • 多进程并行监控，独立数据展示\r\n"
    L"  • 网卡选择与 ncpa.cpl 完全一致，已连接网卡绿色加粗\r\n"
    L"  • TCP+UDP 全协议进程网络追踪\r\n"
    L"  • 进程网速按网卡占比缩放（选未连接网卡时进程网速为 0）\r\n"
    L"  • 自动 Excel 导出 + HTML 趋势报告（Canvas 折线图）\r\n"
    L"  • X 轴框选缩放、点击显示坐标值\r\n"
    L"  • 关闭窗口即完全退出，不留后台进程\r\n"
    L"  • 配置持久化，随开随用",

    // [1] Tab 2: 功能说明
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
    L"   • 已连接网卡以绿色加粗字体标识，一目了然\r\n"
    L"   • 默认\"全部\"汇总所有物理网卡流量\r\n"
    L"   • 支持 Kbps、Mbps、Gbps 三种单位切换\r\n"
    L"   • 显示发送/接收双向速率\r\n"
    L"   • Excel/HTML 导出表头显示当前选定网卡名称\r\n"
    L"   • 进程网速按网卡占比自动缩放：选中未连接网卡→进程网速为 0\r\n"
    L"\r\n"
    L"4. 进程级资源监控\r\n"
    L"   • 支持添加任意正在运行的进程名称\r\n"
    L"   • 自动检测进程是否存在，智能提示确认\r\n"
    L"   • 监测项：CPU、内存（专用工作集，MB）、网络速率\r\n"
    L"   • TCP+UDP 全协议进程网络追踪（IPv4+IPv6）\r\n"
    L"   • 同名多进程按 PID 全部独立记录\r\n"
    L"   • 支持勾选启用/禁用单个进程，日志页签实时更新\r\n"
    L"   • 网速使用指数移动平均平滑显示，避免跳 0\r\n"
    L"   • 首个采样周期用于建立基线（首行不再为 0）\r\n"
    L"   • UI 展示前 10 个 PID（保证界面流畅），Excel 导出全量\r\n"
    L"\r\n"
    L"5. 数据保存与日志\r\n"
    L"   ■ 日志显示（界面 ListView）\r\n"
    L"     • 监测过程中实时刷新，保留最近 5 万行数据用于查看\r\n"
    L"     • 超过 5 万行自动裁剪最旧行，不影响数据采集和导出\r\n"
    L"     • 右键菜单支持「清除日志」「全选」「复制选中」\r\n"
    L"   ■ Excel 自动保存\r\n"
    L"     • 监测中每 2 秒自动将全部数据实时写入 .xlsx 文件\r\n"
    L"     • 文件被锁定（外部只能以只读方式打开查看最新数据）\r\n"
    L"     • 点击「停止监测」时执行最终保存，关闭文件锁定\r\n"
    L"     • 文件名格式：monitor_data_开始时间戳.xlsx\r\n"
    L"     • 保存在用户指定的输出目录（默认 exe 所在目录）\r\n"
    L"     • 即使软件崩溃，已写入的数据不会丢失\r\n"
    L"     • 每次开始新监测会创建新文件（新时间戳），不会覆盖旧文件\r\n"
    L"   ■ 数据结构\r\n"
    L"     • Sheet1「系统资源」：时间/运行时间/CPU/内存各项/网络收发\r\n"
    L"     • Sheet2+ 各进程独立工作表（Sheet 名自动去掉 .exe 后缀）\r\n"
    L"     • 表头含网卡名（如\"网络发送(Mbps)[WLAN2]\"），加粗、居中\r\n"
    L"     • 列宽自适应、宋体 11 号字体\r\n"
    L"   ■ 数据容量\r\n"
    L"     • 内存缓冲区上限 200 万行（约 115 天 @5s 采样）\r\n"
    L"     • Excel 文件包含全部数据，可长期运行不断采集\r\n"
    L"\r\n"
    L"6. HTML 趋势报告（Canvas 折线图可视化）\r\n"
    L"   • 监测停止时自动生成一份完整的 HTML 趋势报告\r\n"
    L"   • 自包含 Canvas 2D 绘制，无需联网加载任何外部依赖\r\n"
    L"   • 交互式折线图：支持 X 轴框选缩放、点击显示坐标值\r\n"
    L"   • 浅色主题，响应式布局，适配不同屏幕尺寸\r\n"
    L"   • 系统资源图表：CPU 使用率 / 内存使用 / 网络发送 / 网络接收\r\n"
    L"   • 图表标题含网卡名（如\"系统网络发送 [WLAN2]\"）\r\n"
    L"   • 每个监测进程的 CPU / 内存 / 网络独立趋势图\r\n"
    L"   • 图表过滤器：按软件名/PID/指标类型筛选显示\r\n"
    L"   • 文件名：monitor_data_YYYYMMDDHHmmss.html，不覆盖旧报告\r\n"
    L"   • 用浏览器（Chrome / Firefox / Edge）直接打开即可查看\r\n"
    L"   • 可部署到 Web 服务器作为在线监控面板，多人同时查看\r\n"
    L"\r\n"
    L"7. 其他功能\r\n"
    L"   • 窗口置顶：点击\"置顶\"按钮使窗口始终在最前\r\n"
    L"   • 「生成监测报告」复选框：勾选后自动生成 HTML 趋势报告\r\n"
    L"   • 配置保存：手动保存/自动加载配置文件\r\n"
    L"   • 右键菜单：日志列表支持清除/全选/复制",

    // [2] Tab 3: 使用方法
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
    L"  在\"监测配置\"区域输入进程名称（如 chrome.exe），点击\r\n"
    L"  \"添加\"。可添加多个进程，勾选需要的进程启用监测。使用\r\n"
    L"  \"删除\"移除单个进程，\"全部删除\"清空列表。\r\n"
    L"\r\n"
    L"步骤 5 — 设置输出目录与报告选项\r\n"
    L"  输入 Excel/HTML 文件保存路径，或点击\"浏览\"选择目录。默认\r\n"
    L"  为软件所在目录。勾选\"生成监测报告\"可自动生成 HTML 报告。\r\n"
    L"\r\n"
    L"步骤 6 — 开始监测\r\n"
    L"  点击\"开始监测\"按钮，软件开始采集数据。监测中配置项被\r\n"
    L"  锁定，状态标签变为红色\"状态: 监测中\"。\r\n"
    L"\r\n"
    L"步骤 7 — 停止与导出\r\n"
    L"  点击\"停止监测\"按钮，数据自动导出为 Excel 文件和 HTML\r\n"
    L"  趋势报告并弹出提示。监测过程中数据已实时写入文件，可在\r\n"
    L"  输出目录以只读方式打开查看最新数据。\r\n"
    L"\r\n"
    L"  数据保存规则：\r\n"
    L"  • 监测中：每 2 秒自动刷新 Excel 文件（实时保存，防崩溃丢失）\r\n"
    L"  • 停止时：最终保存 Excel + HTML 报告，释放文件锁定\r\n"
    L"  • 界面日志：保留最近 5 万行，旧数据自动裁剪（数据仍完整导出）\r\n"
    L"  • 文件位置：输出目录 + 「monitor_data_时间戳.xlsx」\r\n"
    L"  • HTML 报告：输出目录 + 「monitor_data_时间戳.html」",

    // [3] Tab 4: 使用窍门
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
    L"   • 选择未连接网卡时，系统网速和进程网速均显示 0（正确行为）\r\n"
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
    L"   找到目标进程 → 复制\"名称\"列的值粘贴到软件中。\r\n"
    L"\r\n"
    L"8. HTML 报告查看\r\n"
    L"   停止监测后，用浏览器打开输出目录中的 HTML 文件即可查看\r\n"
    L"   完整的系统/进程资源趋势折线图。支持 X 轴框选缩放和点击\r\n"
    L"   显示具体数值，直观分析监测结果。",

    // [4] Tab 5: 注意事项
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
    L"   • 首个采样周期用于建立基线（首行不再为 0）\r\n"
    L"\r\n"
    L"3. 进程网络流量\r\n"
    L"   • TCP：通过 GetPerTcpConnectionEStats 精确到字节\r\n"
    L"   • UDP：通过系统级统计按端点比例估算\r\n"
    L"   • 需管理员权限运行（UAC 提权）才能启用进程网络追踪\r\n"
    L"   • 未提权时进程网络显示 0.00，系统网络不受影响\r\n"
    L"   • 网卡过滤说明：进程网速按网卡占比自动缩放\r\n"
    L"     - 选择\"全部\"：显示进程全部网络流量\r\n"
    L"     - 选择具体网卡：按该网卡占全网总流量比例缩放\r\n"
    L"     - 选择未连接网卡：进程网速为 0（无流量经过该网卡）\r\n"
    L"\r\n"
    L"4. 数据保存与日志说明\r\n"
    L"   ■ 日志（界面 ListView 显示）\r\n"
    L"     • 显示最近 5 万行数据，旧数据自动裁剪以保持界面流畅\r\n"
    L"     • 裁剪仅影响界面显示，DataBuffer 中数据完整保留用于导出\r\n"
    L"     • 右键菜单可「清除日志」清空当前显示（不清除后台数据）\r\n"
    L"   ■ Excel 实时保存\r\n"
    L"     • 监测开始即创建 Excel 文件，每 2 秒刷新写入全部数据\r\n"
    L"     • 文件被程序锁定（FILE_SHARE_READ），外部只能只读打开\r\n"
    L"     • 即使软件异常崩溃，最后一次写入的数据不会丢失\r\n"
    L"     • 点击「停止监测」执行最终保存并关闭锁定\r\n"
    L"   ■ 文件命名规则\r\n"
    L"     • 格式：monitor_data_YYYYMMDDHHmmss.xlsx\r\n"
    L"     • 时间戳为监测开始时间，每次开始新监测创建新文件\r\n"
    L"     • 文件保存在用户指定的输出目录（默认 exe 所在目录）\r\n"
    L"   ■ HTML 趋势报告\r\n"
    L"     • 格式：monitor_data_YYYYMMDDHHmmss.html\r\n"
    L"     • 监测停止时自动生成，自包含无需联网加载依赖\r\n"
    L"     • 交互功能：X 轴框选缩放、点击折线显示坐标值\r\n"
    L"     • 图表过滤器：按软件名/PID/指标类型筛选\r\n"
    L"     • 图表标题含网卡名称\r\n"
    L"     • 可部署到 Web 服务器作为在线监控面板，多人同时访问\r\n"
    L"   ■ 数据容量\r\n"
    L"     • 内存缓冲区上限 200 万行（约 115 天 @5s 采样）\r\n"
    L"     • 超过上限后最旧数据被丢弃（DataBuffer 环形缓冲）\r\n"
    L"     • Excel 文件大小随数据量增长，建议定期停止监测导出新文件\r\n"
    L"\r\n"
    L"5. 网卡选择\r\n"
    L"   • 下拉列表与 ncpa.cpl 完全一致，改名后需点击\"刷新\"更新\r\n"
    L"   • 已连接网卡以绿色加粗字体标识\r\n"
    L"   • 选择未连接网卡时，系统及进程网速均为 0（正确行为）\r\n"
    L"\r\n"
    L"6. 程序退出\r\n"
    L"   点击右上角关闭按钮将完全退出程序，不留后台进程。监测中关\r\n"
    L"   闭会弹出确认对话框，确认后自动保存数据并退出。\r\n"
    L"\r\n"
    L"7. 权限说明\r\n"
    L"   进程级网络监测需要管理员权限。程序已嵌入提权清单，启动时\r\n"
    L"   自动弹出 UAC 提示。若拒绝提权，CPU/内存/系统网络不受影响。\r\n"
    L"\r\n"
    L"8. 兼容性\r\n"
    L"   支持 Windows 7 / 8 / 10 / 11（x64）操作系统。建议使用 \r\n"
    L"   Windows 10 及以上版本以获得最佳体验。"
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
