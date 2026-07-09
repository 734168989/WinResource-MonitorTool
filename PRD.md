# 挂机电脑资源监测软件 — 产品需求文档 (PRD)

> **版本**: V2.6  
> **最后更新**: 2026-07-09  
> **作者**: 无人机  
> **平台**: Windows 7+ x64  

---

## 目录

1. [产品概述](#1-产品概述)
2. [目标用户与使用场景](#2-目标用户与使用场景)
3. [功能需求](#3-功能需求)
4. [非功能需求](#4-非功能需求)
5. [技术架构](#5-技术架构)
6. [数据流设计](#6-数据流设计)
7. [UI 布局规范](#7-ui-布局规范)
8. [配置文件规范](#8-配置文件规范)
9. [数据导出规范](#9-数据导出规范)
10. [已知约束与局限](#10-已知约束与局限)
11. [版本历史](#11-版本历史)

---

## 1. 产品概述

### 1.1 产品定位

一款 Windows 原生 C++17 系统资源实时监测工具。专为**长时间挂机场景**设计，在无人值守状态下持续记录系统 CPU、内存、网络流量以及指定进程的资源占用，监测结束后自动导出为 Excel 文件。

### 1.2 核心价值

- **零依赖运行**：纯 Win32 API 实现，不依赖任何第三方库或运行时框架，单个 exe 即可运行
- **挂机场景优化**：长时间（数小时至数天）稳定监测，数据自动保存，无内存泄漏
- **任务管理器级精度**：采集算法与 Windows 任务管理器保持一致
- **即开即用**：配置持久化为 JSON 文件，一次设置永久复用

### 1.3 设计原则

| 原则 | 说明 |
|------|------|
| **零外部依赖** | 所有功能手工实现（JSON 解析、XLSX 生成、CRC32、ETW 追踪） |
| **数据安全** | 监测中实时写入 Excel（文件锁定），即使崩溃也不丢失数据 |
| **低资源占用** | 监测线程使用 `THREAD_PRIORITY_BELOW_NORMAL`，UI 200ms 定时刷新 |
| **精确一致** | 内存值使用 `PrivateWorkingSetSize`，CPU 使用 QPC 高精度差分，与任务管理器对齐 |
| **配置完整** | 所有设置（进程列表、勾选状态、采样周期、网卡、输出目录）完整持久化 |

---

## 2. 目标用户与使用场景

### 2.1 目标用户

| 用户类型 | 特征 |
|---------|------|
| 挂机任务执行者 | 需要运行脚本/程序数小时，事后分析系统负载 |
| 性能测试工程师 | 批量测试中追踪目标进程的资源占用 |
| IT 运维人员 | 远程/无人值守服务器的轻量级资源监控 |
| 普通用户 | 了解电脑资源占用情况，优化软件配置 |

### 2.2 典型使用场景

1. **挂机脚本监测**（主要场景）：夜间运行自动化脚本，监控脚本进程及系统整体资源占用，次日查看 Excel 报告
2. **游戏/应用性能追踪**：记录特定应用长时间运行时的 CPU、内存趋势
3. **多进程对比**：同时监控多个同类进程（如多个 Python 实例），按 PID 区分
4. **网络流量审计**：记录特定进程的网络带宽消耗

---

## 3. 功能需求

### 3.1 系统级资源监测

| ID | 功能 | 优先级 | API | 精度 |
|----|------|--------|-----|------|
| FR-SYS-01 | CPU 使用率 | P0 | `GetSystemTimes()` + QPC 差分 | 0.01% |
| FR-SYS-02 | 内存总量/可用/已用/使用率 | P0 | `GlobalMemoryStatusEx()` | 0.01 GB |
| FR-SYS-03 | 网络发送/接收速度 | P0 | `GetIfTable2()` 64位计数器差分 | 0.01 Kbps |
| FR-SYS-04 | 网卡筛选 | P1 | `GetAdaptersAddresses(flags=0)` | — |
| FR-SYS-05 | 速度单位切换 | P1 | Kbps / Mbps / Gbps，默认 Mbps | — |

**网卡过滤规则**: `flags=0` (仅 TCP/IP 绑定适配器) → `IfType` 过滤为物理类型 (以太网 / Wi-Fi) → `HardwareInterface==TRUE` (排除虚拟适配器)。结果与 `ncpa.cpl`（网络连接）完全一致。

### 3.2 进程级资源监测

| ID | 功能 | 优先级 | API | 精度 |
|----|------|--------|-----|------|
| FR-PROC-01 | 按进程名添加监控 | P0 | 输入进程名（如 `QQ.exe`） | — |
| FR-PROC-02 | 进程 CPU 使用率 | P0 | `GetProcessTimes()` + QPC 差分 | 0.01% |
| FR-PROC-03 | 进程内存（专用工作集） | P0 | `PROCESS_MEMORY_COUNTERS_EX2.PrivateWorkingSetSize` | 0.01 MB |
| FR-PROC-04 | 进程网络速度 | P0 | `GetPerTcpConnectionEStats` TCP连接字节计数器 | 0.01 Kbps |
| FR-PROC-05 | 同名多进程全记录 | P1 | `CreateToolhelp32Snapshot` 遍历全部匹配 PID | — |
| FR-PROC-06 | 进程存活检测 | P0 | `QueryFullProcessImageNameW` 校验 + 自动重新绑定 | — |
| FR-PROC-07 | 进程列表管理 | P0 | 添加/删除/全部删除/勾选启用禁用/配置保存 | — |
| FR-PROC-08 | 日志页签实时更新 | P1 | 勾选时显示页签，取消时隐藏 | — |
| FR-PROC-09 | 页签名称显示 | P2 | 自动去除 `.exe` 后缀 | — |

**进程内存说明**: 使用 `PrivateWorkingSetSize`（专用工作集），与任务管理器「详细信息」→「内存(专用工作集)」列一致。老旧 Windows (Win7) 自动回退到 `PrivateUsage`（提交大小）。

**进程网络说明**: 通过 `GetExtendedTcpTable` 枚举所有 TCP 连接（按 PID 过滤），再使用 `GetPerTcpConnectionEStats` 直接读取每个连接的 `DataBytesOut`/`DataBytesIn` 字节计数器。两次采样间计算增量即为网络速度。需管理员权限调用 `SetPerTcpConnectionEStats` 启用统计追踪。

### 3.3 数据展示

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-UI-01 | 系统资源 Tab（始终显示） | P0 | 时间/运行时间/CPU/内存共6列/网络共2列 |
| FR-UI-02 | 进程独立 Tab 页 | P0 | 每个启用的监测进程一个 Tab，显示时间/运行时间/PID/CPU/内存/网络 |
| FR-UI-03 | 实时数据刷新 | P0 | 200ms 定时器增量更新 ListView |
| FR-UI-04 | 自动滚动 | P1 | 新数据自动滚动到底部 |
| FR-UI-05 | 数据上限 | P0 | DataBuffer 环形缓冲区 200 万行（~115天@5s），ListView 显示窗口 5 万行 |
| FR-UI-06 | 列宽自适应 | P2 | 时间列固定 135px，其余列 `LVSCW_AUTOSIZE_USEHEADER` |
| FR-UI-07 | 右键菜单 | P1 | 清除日志 / 全选 / 复制选中（Tab 分隔，可直接粘贴到 Excel） |
| FR-UI-08 | 彩色状态标签 | P1 | 就绪=绿色 / 监测中=红色 / 保存中=橙色 |
| FR-UI-09 | 日志显示规则 | P1 | ListView 保留最近 5 万行（性能优化），DataBuffer 保留全部（最多 200 万行）；清除日志仅清空 ListView 显示，不影响 DataBuffer 和 Excel 导出 |

### 3.4 数据导出

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-EXPORT-01 | 自动导出 XLSX | P0 | 监测中每 2 秒实时写入，停止时最终保存；含时间戳文件名 |
| FR-EXPORT-02 | 多 Sheet 支持 | P0 | Sheet1=系统资源，Sheet2+=各进程独立工作表 |
| FR-EXPORT-03 | 实时写入 | P0 | 监测中每 2 秒刷新 Excel 文件（文件锁定，外部只读可查看） |
| FR-EXPORT-04 | 单元格居中 | P2 | 表头和数据均水平垂直居中 |
| FR-EXPORT-05 | 列宽自适应 | P2 | 基于表头文字长度计算（CJK 字符 ~2.2 单位，ASCII ~1.1 单位） |
| FR-EXPORT-06 | 中文支持 | P1 | Microsoft YaHei 字体，工作表和表头均支持中文 |

### 3.5 配置管理

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-CFG-01 | JSON 配置文件 | P0 | `config.json`，UTF-8 编码，位于 exe 同目录 |
| FR-CFG-02 | 进程列表持久化 | P0 | 含进程名和启用状态（`enabled: true/false`） |
| FR-CFG-03 | 全部设定持久化 | P0 | 采样周期、网卡选择、网速单位、输出目录、监测项目 |
| FR-CFG-04 | 手动保存 + 自动加载 | P0 | 用户点击「保存配置」写入，启动时自动加载 |
| FR-CFG-05 | 损坏容错 | P1 | 配置文件损坏或不存在时使用默认配置 |

### 3.6 辅助功能

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-AUX-01 | 窗口置顶 | P2 | 切换按钮，文字在「置顶」↔「取消置顶」间切换 |
| FR-AUX-02 | 标题栏右键菜单 | P2 | 「打开软件所在目录」 |
| FR-AUX-03 | 帮助对话框 | P2 | 5 个 Tab 页（简介/功能/方法/窍门/注意事项） |
| FR-AUX-04 | 管理员提权 | P0 | 嵌入 `requireAdministrator` 清单，启动时 UAC 提示 |

---

## 4. 非功能需求

### 4.1 性能要求

| 指标 | 要求 |
|------|------|
| UI 刷新频率 | 200ms |
| 采样周期范围 | 1-60 秒（用户配置） |
| 数据缓冲区上限 | 2000000 条/进程（~115 天 @5s 采样） |
| Excel 实时刷新间隔 | 2 秒 |
| 监测线程优先级 | `THREAD_PRIORITY_BELOW_NORMAL` |
| 内存占用（程序自身） | < 200 MB（含 500000 条数据） |
| CPU 占用（程序自身） | < 1%（@5s 采样周期） |

### 4.2 兼容性要求

| 平台 | 状态 |
|------|------|
| Windows 11 (x64) | ✅ 完全支持 |
| Windows 10 (x64) | ✅ 完全支持 |
| Windows 8/8.1 (x64) | ✅ 支持（进程网络需 Win10+ ETW） |
| Windows 7 SP1 (x64) | ✅ 支持（进程网络降级为 0，内存回退 PrivateUsage） |
| x86 (32位) | ❌ 不支持 |

### 4.3 安全性要求

| 要求 | 说明 |
|------|------|
| 管理员权限 | 进程级网络需要 ETW 内核会话，嵌入 `requireAdministrator` 清单 |
| 权限降级 | 无管理员时网络监测降级为 0，其他功能正常 |
| 文件写入 | 仅写入 `config.json` 和指定输出目录的 `.xlsx` 文件 |
| 进程访问 | `PROCESS_QUERY_LIMITED_INFORMATION` / `PROCESS_QUERY_INFORMATION`，不修改目标进程 |

### 4.4 可靠性要求

| 要求 | 说明 |
|------|------|
| 崩溃安全 | 监测中实时写入 Excel，即使崩溃也不丢失已采集数据 |
| 配置损坏容错 | JSON 解析失败时回退默认配置 |
| 进程退出处理 | 目标进程退出后数据归零但不中断监测，进程重启后自动重新绑定 |
| 线程安全 | `CRITICAL_SECTION` 保护 DataBuffer，UI 线程与监测线程分离 |
| 长时间运行 | 环形缓冲区防止内存无限增长，索引追踪使用 `ListView_GetItemCount()` 避免漂移 |

---

## 5. 技术架构

### 5.1 整体架构

```
┌──────────────────────────────────────────────────┐
│                   MainWindow                      │
│          (Win32 主窗口 + 消息循环)                  │
│  ┌──────────┬──────────┬──────────┬───────────┐  │
│  │ 监测配置  │ 监测项目  │ 监测控制  │ 数据Tab页  │  │
│  │ 进程列表  │ CPU/内存 │ 开始/停止 │ 系统/进程  │  │
│  │ 添加/删除 │ /网络    │ 状态标签  │ ListView   │  │
│  └──────────┴──────────┴──────────┴───────────┘  │
├──────────────────────────────────────────────────┤
│  SystemMonitor    │  ProcessMonitor (multi-PID)   │
│  · GetSystemTimes │  · CreateToolhelp32Snapshot   │
│  · GlobalMemory   │  · GetProcessTimes (per-PID)  │
│  · GetIfTable2    │  · PROC_MEM_COUNTERS_EX2      │
│  · QPC 计时       │  · QPC 计时                   │
├──────────────────┴────────────────────────────────┤
│  NetSpeedMonitor    │  DataBuffer (thread-safe)       │
│  · GetExtendedTcpTable   · CRITICAL_SECTION           │
│  · Per-TCP-estats (API)  · Ring buffer (2000000 rows)   │
│  · Per-PID 字节聚合      · System + per-process data  │
├───────────────────────────────────────────────────┤
│  ConfigManager    │  ExcelExporter                │
│  · JSON 解析器    │  · XLSX 多Sheet 生成器         │
│  · UTF-8 读写     │  · ZIP writer (store)         │
│  · 单例模式       │  · CRC32 校验                  │
└───────────────────────────────────────────────────┘
```

### 5.2 模块职责

| 模块 | 文件 | 职责 |
|------|------|------|
| **MainWindow** | `MainWindow.h/cpp` | Win32 窗口管理、控件创建、消息处理、监测线程控制、ListView 显示更新 |
| **SystemMonitor** | `SystemMonitor.h/cpp` | 系统级 CPU/内存/网络采集，网卡枚举与过滤 |
| **ProcessMonitor** | `ProcessMonitor.h/cpp` | 进程级 CPU/内存采集，多 PID 管理，Per-PID CPU 基线 |
| **NetSpeedMonitor** | `NetSpeedMonitor.h/cpp` | TCP连接统计，GetExtendedTcpTable 枚举连接，GetPerTcpConnectionEStats 读取字节计数器 |
| **DataBuffer** | `DataBuffer.h/cpp` | 线程安全环形缓冲区，系统数据和进程数据分开存储 |
| **ConfigManager** | `ConfigManager.h/cpp` | JSON 配置文件读写，手写解析器/序列化器 |
| **ExcelExporter** | `ExcelExporter.h/cpp` | OpenXML `.xlsx` 生成（无外部库），多 Sheet，实时刷新模式 |
| **DataModels** | `DataModels.h` | 核心数据结构（监测数据、配置结构体），初始化/清理函数 |

### 5.3 技术选型理由

| 技术选择 | 理由 |
|---------|------|
| 纯 Win32 API | 零外部依赖，单文件部署，Windows 7+ 裸机可运行 |
| C++17 | 现代化语法（`std::wstring`、`auto`、range-for）但不需要运行时库 |
| QPC 高精度计时 | `QueryPerformanceCounter` 提供亚微秒级时间戳 |
| 手写 JSON | 避免引入 JSON 库依赖，配置结构简单（< 200 行代码） |
| 手写 XLSX | 避免 minizip/libxlsxwriter 等库，ZIP store 模式实现 < 300 行 |
| ETW 内核追踪 | 唯一可靠的 Windows 进程级网络流量获取方式 |
| CRITICAL_SECTION | 比 `std::mutex` 更轻量，适合高频短临界区 |
| 环形缓冲区 | 内存可控，长期运行不会 OOM |

---

## 6. 数据流设计

### 6.1 监测数据流

```
MonitorThreadProc (线程)              UI Timer (200ms)
     │                                      │
     ├─ systemMonitor.Collect()             │
     │  └─ SystemMonitorData ──→ DataBuffer  │
     │                                      │
     ├─ processMonitor[i].Collect()         │
     │  └─ ProcessMonitorData[] ──→ DataBuffer
     │                                      │
     └─ Sleep(samplePeriod)                 │
                                            │
                         ┌──────────────────┘
                         │
                    UpdateDisplay()
                         │
                    ┌────┴────┐
              UpdateSystem  UpdateProcess
              ListView()    ListView()
                         │
                    ┌────┴────┐
               每2秒 Flush   ExcelExporter
               Export()      FlushExport()
```

### 6.2 进程网络数据流

```
ProcessMonitor::Collect(runSeconds, netMon)
     │
     └─ netMon->QueryDelta(pid, deltaSent, deltaRecv)
        │
        ├─ GetExtendedTcpTable(TCP_TABLE_OWNER_PID_CONNECTIONS)
        │  └─ 过滤 dwOwningPid == pid 的 TCP 连接
        │
        ├─ SetPerTcpConnectionEStats() [首次] 启用统计追踪
        └─ GetPerTcpConnectionEStats(Rod=DATA_ROD_v0)
           └─ DataBytesOut / DataBytesIn → 求和 → 与上次采样求增量
```

### 6.3 配置数据流

```
保存: UI控件 ──→ SyncConfigFromUI() ──→ ConfigManager ──→ config.json
                                     (含 ListView 勾选状态)

加载: config.json ──→ ConfigManager::LoadConfig() ──→ SyncUIFromConfig()
                                                     ──→ RefreshProcessList()
                                                     ──→ RebuildProcessTabs()
```

### 6.4 数据结构

```cpp
// 系统监测数据（一条记录）
struct SystemMonitorData {
    wchar_t timestamp[20];     // "YYYY-MM-DD HH:MM:SS"
    double  runSeconds;        // 自监测开始已运行秒数
    double  cpuUsage;          // CPU 使用率 (0-100%)
    double  memoryTotalGB;     // 内存总量
    double  memoryAvailableGB; // 可用内存
    double  memoryUsedGB;      // 已用内存
    double  memoryUsage;       // 内存使用率 (0-100%)
    double  netSendSpeed;      // 网络发送速度
    double  netRecvSpeed;      // 网络接收速度
};

// 进程监测数据（一条记录，一个 PID）
struct ProcessMonitorData {
    wchar_t timestamp[20];
    double  runSeconds;
    DWORD   pid;               // 进程 ID（区分同名多进程）
    double  cpuUsage;
    double  memoryUsage;       // 内存使用率
    double  memoryUsedMB;      // 专用工作集 (MB)
    double  netSendSpeed;
    double  netRecvSpeed;
};

// 配置结构体
struct MonitorConfig {
    MonitorProcess* processes; // 动态数组（realloc 扩容）
    int    processCount;
    int    processCapacity;
    bool   monitorCpu, monitorMemory, monitorNetwork;
    int    samplePeriod;       // 1-60 秒
    wchar_t netUnit[16];       // "Kbps" / "Mbps" / "Gbps"
    wchar_t netInterface[256]; // 网卡名 或 "全部"
    wchar_t outputDir[MAX_PATH];
};
```

---

## 7. UI 布局规范

### 7.1 主窗口

```
┌─────────────────────────────────────────────────┐
│ 挂机电脑资源监测软件 V2.6 by 无人机    [_][□][×]  │
├─────────────────────────────────────────────────┤
│ ┌─ 监测配置 ──────────────────────────────────┐ │
│ │ [进程名输入框] [添加][全部删除][保存配置][?]  │ │
│ │ ┌──┬──────────┬──────┐                      │ │
│ │ │☑│ 进程名    │ 操作  │                      │ │
│ │ │☐│ QQ.exe   │ 删除  │                      │ │
│ │ └──┴──────────┴──────┘                      │ │
│ └────────────────────────────────────────────┘ │
│ ┌─ 监测项目设置 ──────────────────────────────┐ │
│ │ ☑CPU ☑内存 ☑网速 [Mbps▼]                   │ │
│ │ 采样周期:[5]秒   网卡选择:[全部▼][刷新]      │ │
│ │ 输出目录:[___________][浏览]                 │ │
│ └────────────────────────────────────────────┘ │
│ ┌─ 监测控制 ──────────────────────────────────┐ │
│ │ [开始监测] [停止监测]        状态: 就绪      │ │
│ └────────────────────────────────────────────┘ │
│ ┌─────────────────────────────────────────────┐ │
│ │ 系统资源 │ QQ │ WeChat │                    │ │
│ │─────────────────────────────────────────────│ │
│ │ 时间           │运行时间│CPU%│内存总量│...    │ │
│ │ 2026-07-09... │1234.56│5.23│15.87  │...    │ │
│ └─────────────────────────────────────────────┘ │
│                                       [置顶]    │
└─────────────────────────────────────────────────┘
```

### 7.2 控件约束

| 控件 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| 进程名输入框 | Edit | 空 | 输入后点「添加」 |
| 进程列表 | ListView (Checkboxes) | — | 第1列=勾选，第2列=名称，第3列=操作(删除) |
| 采样周期 | Edit (ES_NUMBER) | 5 | 1-60 秒 |
| 网速单位 | ComboBox | Mbps | Kbps / Mbps / Gbps |
| 网卡选择 | ComboBox | 全部 | 动态获取，点「刷新」更新 |
| 输出目录 | Edit | exe所在目录 | 点「浏览」选择文件夹 |

### 7.3 日志 ListView 列规范

**系统资源 Tab:**

| 列 | 内容 | 宽度 | 对齐 |
|----|------|------|------|
| 时间 | YYYY-MM-DD HH:MM:SS | 135px | 左 |
| 运行时间(秒) | 浮点数 | auto (header) | 左 |
| CPU(%) | 0.00-100.00 | auto | 左 |
| 内存总量(GB) | 浮点数 | auto | 左 |
| 内存可用(GB) | 浮点数 | auto | 左 |
| 内存使用(GB) | 浮点数 | auto | 左 |
| 内存使用率(%) | 0.00-100.00 | auto | 左 |
| 网络发送(Kbps) | 浮点数 | auto | 左 |
| 网络接收(Kbps) | 浮点数 | auto | 左 |

**进程 Tab:**

| 列 | 内容 | 宽度 | 对齐 |
|----|------|------|------|
| 时间 | YYYY-MM-DD HH:MM:SS | 135px | 左 |
| 运行时间(秒) | 浮点数 | auto | 左 |
| 进程ID | DWORD | auto | 左 |
| CPU(%) | 0.00-100.00 | auto | 左 |
| 内存使用率(%) | 0.00-100.00 | auto | 左 |
| 内存使用(MB) | 浮点数 | auto | 左 |
| 网络发送(Kbps) | 浮点数 | auto | 左 |
| 网络接收(Kbps) | 浮点数 | auto | 左 |

---

## 8. 配置文件规范

### 8.1 文件位置

`<exe目录>\config.json`，UTF-8 编码（无 BOM）。

### 8.2 JSON Schema

```json
{
  "monitorProcesses": [
    {
      "name": "QQ.exe",
      "enabled": true
    }
  ],
  "monitorItems": {
    "cpu": true,
    "memory": true,
    "network": true
  },
  "samplePeriod": 5,
  "netUnit": "Mbps",
  "netInterface": "全部",
  "outputDir": "C:\\MonitorData"
}
```

### 8.3 字段规范

| 字段 | 类型 | 必填 | 默认值 | 取值范围 |
|------|------|------|--------|---------|
| `monitorProcesses` | array[object] | 是 | `[]` | 每个对象含 `name`(string) 和 `enabled`(bool) |
| `monitorItems.cpu` | bool | 是 | `true` | — |
| `monitorItems.memory` | bool | 是 | `true` | — |
| `monitorItems.network` | bool | 是 | `true` | — |
| `samplePeriod` | int | 是 | `5` | 1-60 |
| `netUnit` | string | 是 | `"Mbps"` | `"Kbps"` / `"Mbps"` / `"Gbps"` |
| `netInterface` | string | 是 | `"全部"` | 网卡名或 `"全部"` |
| `outputDir` | string | 是 | exe所在目录 | 有效路径 |

### 8.4 加载策略

1. 文件不存在 → 使用默认配置（MPrintExp.exe 默认启用）
2. 文件存在且格式正确 → 完整还原
3. 文件存在但解析失败 → 输出调试日志 + 回退默认配置
4. `monitorProcesses` 为空数组 `[]` → 保留空列表（不自动添加默认进程）

---

## 9. 数据导出规范

### 9.1 文件格式

- 格式：OpenXML Spreadsheet (`.xlsx`)
- 容器：ZIP（store 无压缩模式）
- 编码：UTF-8 XML
- 字体：Microsoft YaHei 11pt（表头加粗）

### 9.2 文件结构

```
monitor_data_20260709_143022.xlsx
├── [Content_Types].xml
├── _rels/.rels
├── xl/
│   ├── workbook.xml
│   ├── _rels/workbook.xml.rels
│   ├── styles.xml
│   └── worksheets/
│       ├── sheet1.xml   (系统资源)
│       ├── sheet2.xml   (进程1，Sheet名去.exe)
│       └── ...
```

### 9.3 样式规范

| 元素 | 样式 |
|------|------|
| 表头行 | 加粗、居中（水平+垂直）、Microsoft YaHei 11pt |
| 数据行 | 正常、居中（水平+垂直）、Microsoft YaHei 11pt |
| 列宽 | 根据表头文字计算（CJK ≈2.2单位，ASCII ≈1.1单位，最小8单位） |

### 9.4 实时刷新与保存规则

**监测过程中（实时保存）**：
- 监测开始时调用 `BeginExport()`，创建文件并保持句柄打开（`GENERIC_READ|GENERIC_WRITE`，`FILE_SHARE_READ`）
- 每 2 秒调用 `FlushExport()` 重写整个 ZIP 到同一文件句柄（增量覆盖）
- 外部程序可以只读方式打开查看（实时更新需关闭后重新打开）
- 即使软件异常崩溃，最后一次成功写入的数据保留在磁盘上

**停止监测时（最终保存）**：
- 调用 `FlushExport()` 执行最后一次完整写入
- 调用 `EndExport()` 关闭文件句柄，释放文件锁定
- 弹出提示框，显示文件保存路径

**文件命名规则**：
- 格式：`monitor_data_YYYYMMDD_HHMMSS.xlsx`
- 时间戳为监测**开始**时间，每次开始新监测创建新文件
- 文件保存在用户指定的输出目录（默认 exe 所在目录）
- 同一目录下多次监测不会互相覆盖（时间戳不同）

**数据完整性**：
- 内存 DataBuffer 上限 200 万行（~115 天 @5s 采样），超过后环形裁剪最旧数据
- ListView 显示窗口 5 万行，仅影响界面显示，不影响 DataBuffer 和 Excel 导出
- 右键「清除日志」仅清空 ListView 显示行，不清除 DataBuffer 数据

---

## 10. 已知约束与局限

| 约束 | 影响 | 缓解措施 |
|------|------|---------|
| ETW 需要管理员权限 | 进程网络监测需要 UAC 提权 | 嵌入 `requireAdministrator` 清单；无管理员时网络降级为 0 |
| 进程网络 = TCP/IP 层 | 不包含 UDP 或其他协议流量 | 对多数应用（HTTP/HTTPS/WebSocket）影响小 |
| 无进程磁盘 I/O | 不采集磁盘读写 | 大多数挂机场景对磁盘不敏感 |
| 同名进程匹配依赖进程名 | 必须精确匹配（不区分大小写），不支持通配符 | 提供智能确认（未运行的进程名提示确认） |
| 数据缓冲区 200 万行上限 | 超过后最旧数据被丢弃（~115 天 @5s） | 可调节 `MAX_ROWS` 常量 |
| ListView 5 万行显示窗口 | 超过后裁剪最旧行，DataBuffer 数据完整保留 | 配合 DataBuffer 独立裁剪 |
| Excel 无压缩 | 文件体积较大 | store 模式保持代码简单，文件大小通常可接受 |

---

## 11. 版本历史

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| V2.1 | — | 初始版本 |
| V2.6 | 2026-07 | 多 PID 支持、ETW 进程网络监测、专用工作集内存、Excel 居中自适应、配置持久化修复、长时间监测显示修复、页签实时更新、管理员提权、去 .exe 后缀 |
