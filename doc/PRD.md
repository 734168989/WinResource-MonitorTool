# 挂机电脑资源监测软件 V3.4 — 产品需求文档（PRD）

> **版本**: V3.4
> **最后更新**: 2026-07-20
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
10. [功能截图](#10-功能截图)
11. [已知约束与局限](#11-已知约束与局限)
12. [版本历史](#12-版本历史)

---

## 1. 产品概述

### 1.1 产品定位

一款 Windows 原生 C++17 系统资源实时监测工具。采用纯 Win32 API 实现，专为**长时间挂机场景**设计。可在无人值守状态下持续记录系统 CPU、内存、网络流量以及指定进程的资源占用，自动导出 Excel 文件（.xlsx）和 HTML 趋势报告，供后续分析与复盘。

**适用领域**：服务器运维监控、软件稳定性测试、游戏/业务挂机、性能基线采集。

### 1.2 核心能力一览

- 系统级 CPU / 内存 / 网络实时监控
- 进程级精细化监测（CPU、内存、网络）
- 多进程并行监控，独立页签展示
- 网卡选择与 ncpa.cpl 完全一致，已连接网卡绿色加粗
- TCP + UDP 全协议进程网络追踪（IPv4 + IPv6）
- 自动 Excel 导出，监测中实时写入防数据丢失
- HTML 可视化报告，趋势折线图一目了然
- 配置持久化，随开随用

### 1.3 核心价值

- **零依赖运行**：纯 Win32 API + 手写 JSON/XLSX/ZIP/HTML 生成器，不依赖任何第三方库或运行时框架
- **挂机场景优化**：长时间（数小时至数天）稳定监测，数据实时落盘防丢失
- **任务管理器级精度**：CPU 使用 QPC 高精度差分算法，内存使用 `PrivateWorkingSetSize`（与任务管理器一致），网卡列表与 ncpa.cpl 同源
- **即开即用**：配置持久化为 JSON 文件，一次设置永久复用

### 1.4 设计原则

| 原则 | 说明 |
|------|------|
| **零外部依赖** | 所有功能手工实现（JSON 解析、ZIP writer、OOXML 生成、HTML Canvas 2D 自绘图表） |
| **数据安全** | 监测中每 2 秒实时写入 Excel，即使崩溃也不丢失已保存数据 |
| **低资源占用** | 监测线程使用 `THREAD_PRIORITY_BELOW_NORMAL`，UI 200ms 定时刷新 |
| **精确一致** | 内存值使用专用工作集（与任务管理器一致），网卡列表与 ncpa.cpl 同源 |

---

## 2. 目标用户与使用场景

### 2.1 目标用户

| 用户类型 | 典型需求 |
|---------|---------|
| 运维人员 | 长期监控服务器/挂机电脑资源趋势，定位瓶颈 |
| 软件测试工程师 | 压测时记录被测程序的资源消耗曲线，检测内存泄漏 |
| 游戏工作室 | 监控多开游戏客户端的资源占用 |
| 普通用户 | 了解电脑资源使用状况，诊断卡顿原因 |

### 2.2 典型使用场景

1. **服务器 7×24 挂机监控** — 添加关键进程（如 Java/Tomcat/MySQL），保留数天资源趋势数据
2. **游戏客户端稳定性测试** — 监控游戏进程在长时间运行下的内存增长和 CPU 峰值
3. **网络流量审计** — 选择特定网卡/进程，精确追踪上行/下行流量
4. **临时性能诊断** — 采样周期设为 1~3 秒，快速观察短时峰值
5. **多进程对比** — chrome.exe 等多实例进程的资源占用对比分析

---

## 3. 功能需求

### 3.1 系统级资源监测

| ID | 功能 | 优先级 | 数据来源 | 精度 |
|----|------|--------|---------|------|
| FR-SYS-01 | CPU 使用率 | P0 | `GetSystemTimes()` + QPC 差分 | 0.01% |
| FR-SYS-02 | 内存总量/可用/已用/使用率 | P0 | `GlobalMemoryStatusEx()` | 0.01 GB |
| FR-SYS-03 | 网络发送/接收速度 | P0 | `GetIfTable2()` 64 位计数器 + QPC 差分 | 0.01 |
| FR-SYS-04 | 网卡筛选 | P1 | `INetConnectionManager` COM（ncpa.cpl 同源） + `GetIfTable2` 交叉匹配 | — |
| FR-SYS-05 | 速度单位切换 | P1 | Kbps / Mbps / Gbps，默认 Mbps | — |
| FR-SYS-06 | 已连接网卡标识 | P1 | `IfOperStatus` 按名称交叉匹配，绿色加粗 | — |

**网卡枚举技术方案**：采用**双数据源**策略，确保与系统网络连接面板显示完全一致：
1. `INetConnectionManager` COM 接口 → 获取连接名称（ncpa.cpl 自身使用的 API，改名实时生效）
2. `GetIfTable2` → 获取 `InterfaceIndex`（流量读取）和 `OperStatus`（连接状态）
3. 两者按 `Alias` 名称交叉匹配 → 列表一致 + 已连接标识

### 3.2 进程级资源监测

| ID | 功能 | 优先级 | 数据来源 | 说明 |
|----|------|--------|---------|------|
| FR-PROC-01 | 按进程名添加监控 | P0 | 用户输入（如 `chrome.exe`） | 不区分大小写精确匹配 |
| FR-PROC-02 | 进程 CPU 使用率 | P0 | `GetProcessTimes()` + QPC 差分 | 按 CPU 逻辑核心数归一化 |
| FR-PROC-03 | 进程内存（专用工作集） | P0 | `PrivateWorkingSetSize`（Win10 1809+） | 与任务管理器一致，低版本回落 `PrivateUsage` |
| FR-PROC-04 | 进程网络（TCP） | P0 | `GetExtendedTcpTable` + `GetPerTcpConnectionEStats` | IPv4+IPv6，精确到字节 |
| FR-PROC-05 | 进程网络（UDP） | P1 | `GetIpStatisticsEx` + `GetExtendedUdpTable` | 系统级统计按端点比例分配 |
| FR-PROC-06 | 同名多进程全记录 | P1 | `CreateToolhelp32Snapshot` 遍历匹配 PID | 按 PID 独立采集 |
| FR-PROC-07 | 进程存活检测 | P0 | 每周期重新搜索 PID，清理退出进程 | 进程退出后数据归零，监测不中断 |
| FR-PROC-08 | 进程列表管理 | P0 | 添加 / 删除 / 复选框启用禁用 / 配置保存 | 未运行进程提示确认 |
| FR-PROC-09 | 网络平滑显示 | P2 | 指数移动平均（即时响应 + 慢衰减 0.7） | 避免数值跳 0 |
| FR-PROC-10 | UI 展示优化 | P1 | 系统 10 万行 / 进程前 10 PID | Excel 全量保留不受限 |

### 3.3 数据展示

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-UI-01 | 系统资源 Tab（始终可见） | P0 | 时间 / 运行时长 / CPU / 内存 6 列 / 网络 2 列 |
| FR-UI-02 | 进程独立 Tab 页 | P0 | 每个启用进程一个 Tab，8 列数据 |
| FR-UI-03 | 200ms 定时刷新 | P0 | 增量更新 ListView，自动滚动 |
| FR-UI-04 | 数据上限控制 | P0 | 缓冲区 200 万行，日志显示 5 万行 |
| FR-UI-05 | 右键菜单 | P1 | 清除日志 / 全选 / 复制选中（Tab 分隔，可粘贴到 Excel） |
| FR-UI-06 | 彩色状态标签 | P1 | 绿色=就绪 / 蓝色=监测中 / 红色=异常 / 橙色=暂停 |
| FR-UI-07 | 窗口置顶切换 | P2 | 按钮切换，方便实时观察 |
| FR-UI-08 | 标题栏右键 | P2 | 「打开软件所在目录」 |

### 3.4 数据导出

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-EXPORT-01 | 自动导出 Excel (.xlsx) | P0 | 监测中每 2 秒实时写入，停止时最终保存 |
| FR-EXPORT-02 | 自动导出 HTML 报告 | P1 | 停止时自动生成，Canvas 2D 自绘折线图，支持 X 轴框选缩放、点击显示坐标值、图表过滤 |
| FR-EXPORT-03 | 多 Sheet 分页 | P0 | 系统 Sheet + 各进程独立 Sheet |
| FR-EXPORT-04 | 类型化单元格 | P1 | 日期列=日期序列号，整数=0 格式，小数=0.00 格式 |
| FR-EXPORT-05 | 表头样式 | P1 | 加粗 + 浅蓝背景（#DCE6F1），居中 |
| FR-EXPORT-06 | 列宽自适应 | P2 | 基于表头文字长度计算 |
| FR-EXPORT-07 | HTML 交互功能 | P2 | X 轴框选缩放、双击重置、点击 tooltip、图表过滤器 |

### 3.5 配置管理

| ID | 功能 | 优先级 | 说明 |
|----|------|--------|------|
| FR-CFG-01 | JSON 配置文件 | P0 | `config.json`，UTF-8 编码，exe 同目录 |
| FR-CFG-02 | 进程列表持久化 | P0 | 含进程名和启用/禁用状态 |
| FR-CFG-03 | 全部设定持久化 | P0 | 采样周期、网卡、单位、输出目录、监测项目、报告开关 |
| FR-CFG-04 | 手动保存 + 自动加载 | P0 | 启动时自动加载，手动保存配置 |
| FR-CFG-05 | 损坏容错 | P1 | JSON 解析失败 → 使用默认配置 |

### 3.6 帮助系统

软件内置 5 标签页帮助对话框（680×580），内容涵盖：

| 页签 | 内容 |
|------|------|
| 软件简介 | 作者、版本、编译时间、核心能力概述 |
| 功能说明 | CPU/内存/网络监控、进程监控、数据导出、配置管理等 6 大类功能详解 |
| 使用方法 | 7 步操作流程 + 数据保存规则说明 |
| 使用窍门 | 采样周期选择建议、网卡选择、窗口置顶、右键技巧、进程名获取方法等 |
| 注意事项 | 监测锁定、进程监控说明、网络流量追踪原理、数据保存机制、网卡兼容性 |

---

## 4. 非功能需求

### 4.1 性能要求

| 指标 | 要求 |
|------|------|
| UI 刷新频率 | 200ms（5 FPS） |
| 采样周期范围 | 1~60 秒（用户配置，推荐 5 秒） |
| 数据缓冲区上限 | 2,000,000 条（~115 天 @5s 采样） |
| Excel 实时刷新 | 2 秒（常规）/ 5 秒（积压 >2000 行） |
| HTML 报告生成 | 停止监测时 |
| 监测线程优先级 | `THREAD_PRIORITY_BELOW_NORMAL` |
| 自身 CPU 占用 | < 1%（@5s 采样周期） |
| 自身内存占用 | < 50 MB（含缓冲数据） |

### 4.2 兼容性

| 平台 | 状态 |
|------|------|
| Windows 11 (x64) | ✅ 完全支持 |
| Windows 10 1809+ (x64) | ✅ 完全支持 |
| Windows 10 <1809 (x64) | ✅ 支持（内存回落 PrivateUsage） |
| Windows 8/8.1 (x64) | ✅ 支持 |
| Windows 7 SP1 (x64) | ✅ 支持（`_WIN32_WINNT=0x0601`） |
| x86 (32位) | ❌ 不支持 |

### 4.3 安全性

| 要求 | 说明 |
|------|------|
| 管理员权限 | 进程网络 TCP EStats 需要 UAC 提权，嵌入 `requireAdministrator` manifest |
| 文件写入 | 仅写入 `config.json` 和输出目录的 .xlsx/.html 文件 |
| 进程访问 | `PROCESS_QUERY_LIMITED_INFORMATION` 只读，不修改目标进程 |

### 4.4 可靠性

| 要求 | 说明 |
|------|------|
| 崩溃安全 | 监测中实时写入 Excel，即使崩溃数据仍保留 |
| 配置容错 | JSON 损坏 → 自动回退默认配置 |
| 进程退出处理 | 自动清理状态和句柄，进程重启后自动绑定新 PID |
| 线程安全 | `CRITICAL_SECTION` 保护连接映射表，`SRWLOCK` 保护 DataBuffer |
| 长时间运行 | 环形缓冲区防内存无限增长，ListView 裁剪防 UI 卡顿 |

---

## 5. 技术架构

### 5.1 整体架构图

```
┌──────────────────────────────────────────────────────┐
│                  Win32 UI Layer                        │
│   MainWindow.cpp (1820行)  │  HelpDialog.cpp           │
│   窗口过程 / 动态布局 / 事件处理 / 显示更新               │
├──────────────────────────────────────────────────────┤
│                  Business Logic                        │
│   ConfigManager  │  ExcelExporter  │  HtmlChartExporter │
│   (手写JSON解析)  │  (ZIP+OOXML)    │  (Canvas2D自绘)    │
│   DataBuffer     │                 │                    │
│   (线程安全环形缓冲) │                 │                    │
├──────────────────────────────────────────────────────┤
│                Data Collection Layer                   │
│   SystemMonitor  │  ProcessMonitor  │ NetSpeedMonitor  │
│   (系统CPU/内存/网速)│ (进程CPU/内存)    │ (TCP EStats+UDP) │
├──────────────────────────────────────────────────────┤
│                  Windows API                           │
│   kernel32.dll / iphlpapi.dll / psapi.dll              │
│   netcon.dll (COM) / ws2_32.dll / gdi32.dll           │
└──────────────────────────────────────────────────────┘
```

### 5.2 模块职责详解

| 模块 | 源文件 | 职责 |
|------|--------|------|
| **入口** | `src/main.cpp` | COM(STA) 初始化、Common Controls、窗口注册、消息循环 |
| **主窗口** | `src/ui/MainWindow.cpp` | 全部 UI 创建/布局/事件/监测线程/显示更新/Excel 定时写入 |
| **帮助窗口** | `src/ui/HelpDialog.cpp` | 5 标签页帮助对话框（TabControl + Edit 只读） |
| **系统监控** | `src/core/SystemMonitor.cpp` | CPU(QPC差分) / 内存(GlobalMemoryStatusEx) / 网络(GetIfTable2) / 网卡枚举(ncpa.cpl同源) |
| **进程监控** | `src/core/ProcessMonitor.cpp` | 多 PID 进程发现 / CPU(GetProcessTimes) / 内存(PrivateWorkingSetSize) / 句柄缓存 |
| **网络追踪** | `src/core/NetSpeedMonitor.cpp` | TCP 每连接 EStats(精确到字节) + UDP 系统级按比例分配 + 连接 GC |
| **数据缓冲** | `src/utils/DataBuffer.cpp` | 线程安全环形缓冲区(200万行)，SRWLOCK 读写锁 |
| **配置管理** | `src/utils/ConfigManager.cpp` | 手写递归下降 JSON 解析器 + UTF-8 文件读写 + 单例模式 |
| **Excel 导出** | `src/utils/ExcelExporter.cpp` | 自研 ZIP writer(store) + OOXML 流式生成 / 实时增量刷新 / 多 Sheet |
| **HTML 图表** | `src/utils/HtmlChartExporter.cpp` | 自包含 HTML + Canvas 2D 折线图 / 框选缩放 + 坐标 tooltip / 图表过滤器 / 浅色主题 |
| **数据模型** | `include/utils/DataModels.h` | SystemMonitorData / ProcessMonitorData / MonitorConfig 结构体 |
| **资源文件** | `res/monitor.rc` + `res/resource.h` | 图标(IDI_MAIN_ICON)、版本信息、控件 ID 常量 |

### 5.3 关键技术选型

| 技术选择 | 理由 |
|---------|------|
| 纯 Win32 API | 零外部依赖，单 exe 部署，Windows 7+ 裸机可运行 |
| C++17 | 现代语法（结构化绑定、if constexpr），无需额外运行时 |
| QPC 高精度计时 | `QueryPerformanceCounter` 亚微秒级时间戳 |
| 手写 JSON 解析器 | 配置结构简单，< 200 行代码，零依赖 |
| 手写 ZIP + OOXML | 自研 ZIP writer(store 无压缩) + OpenXML，< 400 行 |
| Canvas 2D 自绘 | HTML 报告中自包含折线图，零外部依赖，浏览器直接打开，支持交互式缩放和坐标显示 |
| `INetConnectionManager` COM | 与 ncpa.cpl 完全同源，列表一致性保证 |
| CRITICAL_SECTION | 比 `std::mutex` 更轻量，适合高频短临界区 |

### 5.4 线程模型

| 线程 | 职责 | 周期 |
|------|------|------|
| 主线程 | UI 消息循环 + WM_TIMER(200ms) 显示刷新 + Excel 定时写入 | — |
| 监测线程 | SystemMonitor/ProcessMonitor 数据采集 → DataBuffer.Write | 1~60s |
| DataBuffer 锁 | `SRWLOCK` 读写锁 | 采集(写) / UI(读) |

---

## 6. 数据流设计

### 6.1 主数据流

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ MonitorThread │────→│  DataBuffer  │────→│  UI Thread   │
│  (1-60s 采集) │     │ (环形缓冲)    │     │ (200ms 刷新)  │
└──────────────┘     └──────┬───────┘     └──────────────┘
                            │
                     ┌──────┴───────┐
                     ▼              ▼
              ┌────────────┐ ┌────────────┐
              │ ExcelExport │ │ HTML Export │
              │ (每2s刷新)   │ │ (停止时生成) │
              └────────────┘ └────────────┘
```

### 6.2 进程网络数据流

```
ProcessMonitor::Collect(runSeconds, netMon)
  └─ NetSpeedMonitor::QueryDelta(pid, sent, recv)
       │
       ├─ TCP: GetExtendedTcpTable → 过滤 PID
       │    └─ GetPerTcpConnectionEStats(DataBytesOut/In)
       │         └─ delta = current - baseline → 累计
       │
       └─ UDP: GetIpStatisticsEx(每800ms更新全局基线)
            └─ GetExtendedUdpTable → PID端点比例分配
                 └─ 每包 ≈ 1400 字节估算
```

### 6.3 核心数据结构

```cpp
// 系统采集数据（每周期一条）
struct SystemMonitorData {
    wchar_t timestamp[20];       // "YYYY/M/D HH:MM:SS"
    double  runSeconds;          // 自监测开始秒数
    double  cpuUsage;            // 0-100%
    double  memoryTotalGB;       // 内存总量
    double  memoryAvailableGB;   // 可用内存
    double  memoryUsedGB;        // 已用内存
    double  memoryUsage;         // 内存使用率 %
    double  netSendSpeed;        // 上传速度(所选单位)
    double  netRecvSpeed;        // 下载速度(所选单位)
};

// 进程采集数据（每 PID 每周期一条）
struct ProcessMonitorData {
    wchar_t timestamp[20];
    double  runSeconds;
    DWORD   pid;                 // 进程ID（区分同名多进程）
    double  cpuUsage;
    double  memoryUsage;         // %
    double  memoryUsedMB;        // 专用工作集 MB
    double  netSendSpeed;
    double  netRecvSpeed;
};
```

---

## 7. UI 布局规范

### 7.1 主窗口布局（880 × 780，居中启动）

```
┌──────────────────────────────────────────────────────────┐
│  挂机电脑资源监测软件 V3.4 by 无人机                   [_][X]│
├──────────────────────────────────────────────────────────┤
│  ┌─ 进程配置 ──────────────────────────────────────────┐ │
│  │ [__________进程名输入框__________] [添加] [删除] [保存]│ │
│  │ ┌────┬──────────────────┬──────┐                    │ │
│  │ │ ☑  │ 进程名称          │ 操作  │  ← 复选框进程列表  │ │
│  │ │ ☑  │ chrome.exe       │ 删除 │                    │ │
│  │ └────┴──────────────────┴──────┘                    │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌─ 监测项目与参数 ────────────────────────────────────┐ │
│  │ ☑CPU ☑内存 ☑网络 [Mbps▼] 网卡:[全部▼][刷新]        │ │
│  │ 采样周期:[5]秒  输出目录:[__________][浏览]          │ │
│  │ ☑自动生成HTML监测报告              [帮助说明]        │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌─ 数据展示 ─────────────────────────────────────────┐ │
│  │ [系统资源] [chrome] [java] ...    ← 页签（底部对齐） │ │
│  │ ┌──────┬──────┬────┬──────┬──────┬──────┬──────┐   │ │
│  │ │ 时间  │运行秒│CPU%│内存总│可用  │已用  │...   │   │ │
│  │ ├──────┼──────┼────┼──────┼──────┼──────┼──────┤   │ │
│  │ │ .... │ .... │ .. │ .... │ .... │ .... │ .... │   │ │
│  │ └──────┴──────┴────┴──────┴──────┴──────┴──────┘   │ │
│  └────────────────────────────────────────────────────┘ │
│  ● 就绪                     [置顶] [开始监测] [停止监测] │
└──────────────────────────────────────────────────────────┘
```

### 7.2 ListView 列规范

**系统 Tab（9 列）：**

| # | 列名 | 宽度 | 数据源 |
|---|------|------|--------|
| 0 | 时间 | 135px | timestamp |
| 1 | 运行时间(秒) | auto | runSeconds |
| 2 | CPU(%) | auto | cpuUsage |
| 3 | 内存总量(GB) | auto | memoryTotalGB |
| 4 | 内存可用(GB) | auto | memoryAvailableGB |
| 5 | 内存使用(GB) | auto | memoryUsedGB |
| 6 | 内存使用率(%) | auto | memoryUsage |
| 7 | 网络发送 | auto | netSendSpeed |
| 8 | 网络接收 | auto | netRecvSpeed |

**进程 Tab（8 列）：**

| # | 列名 | 宽度 | 数据源 |
|---|------|------|--------|
| 0 | 时间 | 135px | timestamp |
| 1 | 运行时间(秒) | auto | runSeconds |
| 2 | 进程ID | auto | pid |
| 3 | CPU(%) | auto | cpuUsage |
| 4 | 内存使用率(%) | auto | memoryUsage |
| 5 | 内存使用(MB) | auto | memoryUsedMB |
| 6 | 网络发送 | auto | netSendSpeed |
| 7 | 网络接收 | auto | netRecvSpeed |

---

## 8. 配置文件规范

### 8.1 文件位置与格式

- **位置**：`<exe所在目录>\config.json`
- **编码**：UTF-8（无 BOM）
- **格式**：JSON

### 8.2 完整示例

```json
{
  "monitorProcesses": [
    { "name": "chrome.exe", "enabled": true },
    { "name": "java.exe",   "enabled": false }
  ],
  "monitorItems": {
    "cpu": true,
    "memory": true,
    "network": true
  },
  "samplePeriod": 5,
  "netUnit": "Mbps",
  "netInterface": "全部",
  "outputDir": "D:\\monitor_output",
  "generateReport": true
}
```

### 8.3 字段说明

| 字段 | 类型 | 必填 | 默认值 | 取值范围 |
|------|------|------|--------|---------|
| `monitorProcesses[].name` | String | 是 | — | 含 `.exe` 后缀 |
| `monitorProcesses[].enabled` | Boolean | 是 | `true` | — |
| `monitorItems.cpu` | Boolean | 是 | `true` | — |
| `monitorItems.memory` | Boolean | 是 | `true` | — |
| `monitorItems.network` | Boolean | 是 | `true` | — |
| `samplePeriod` | Integer | 是 | `5` | 1~60 |
| `netUnit` | String | 是 | `"Mbps"` | `"Kbps"` / `"Mbps"` / `"Gbps"` |
| `netInterface` | String | 是 | `"全部"` | 网卡名称或 `"全部"` |
| `outputDir` | String | 是 | exe 目录 | 有效路径 |
| `generateReport` | Boolean | 是 | `true` | — |

### 8.4 加载策略

1. 文件不存在 → 使用默认配置
2. 文件存在且格式正确 → 完整还原
3. 文件存在但解析失败 → 输出调试日志 + 回退默认配置
4. `outputDir` 为 `".\\"` 或空 → 自动修正为 exe 所在目录

---

## 9. 数据导出规范

### 9.1 Excel (.xlsx)

- **格式**：Office Open XML（ZIP store 无压缩 + XML 工作表）
- **编码**：UTF-8 XML
- **字体**：宋体 11pt

**内部结构**：
```
monitor_data_20260719_143022.xlsx
├── [Content_Types].xml
├── _rels/.rels
├── xl/
│   ├── workbook.xml
│   ├── _rels/workbook.xml.rels
│   ├── sharedStrings.xml
│   ├── styles.xml
│   └── worksheets/
│       ├── sheet1.xml   (系统资源)
│       ├── sheet2.xml   (进程1，Sheet 名去 .exe 后缀)
│       └── ...
```

**样式规范**：

| 元素 | 格式 |
|------|------|
| 表头行 | 加粗、居中、宋体 11pt、背景色 #DCE6F1 |
| 日期列 | Excel 日期序列号，格式 `yyyy/m/d h:mm:ss` |
| 小数数列 | 数值格式 `0.00`，居中 |
| 整数列 | 数值格式 `0`，居中 |

**文件命名**：`monitor_data_YYYYMMDD_HHMMSS.xlsx`

### 9.2 HTML 趋势报告

- **格式**：自包含单文件 HTML（Canvas 2D 自绘 + 内联 CSS/JS），零外部依赖，无需联网
- **内容**：系统 CPU / 内存 / 网络发送 / 网络接收趋势折线图 + 每个进程的 CPU / 内存 / 网络图表
- **交互功能**：X 轴框选缩放、点击折线显示坐标值、双击重置缩放、图表过滤器（按软件名/PID/指标类型筛选）、窗口 resize 自适应重绘
- **主题**：浅色背景（`#f5f6fa`），白色卡片式布局，响应式设计
- **文件名**：`monitor_data_YYYYMMDDHHmmss.html`（与 Excel 命名一致）

---

## 10. 功能截图

> **注**：以下为功能界面示意图。实际运行截图请从软件中截取。

### 10.1 主界面

- 880×780 窗口，进程配置区 + 监测项目设置区 + 数据展示区 + 控制区
- 底部状态栏显示彩色状态标签（就绪/运行中/异常）
- Tab 页签底部对齐，系统 Tab 始终可见

### 10.2 帮助对话框

- 680×580 弹出窗口
- 5 个标签页（TabControl）：软件简介、功能说明、使用方法、使用窍门、注意事项
- 只读文本框，带滚动条

### 10.3 导出文件

- Excel：多 Sheet 结构，系统 + 各进程独立 Sheet，表头加粗蓝底
- HTML：深色主题折线图，Chart.js 渲染，浏览器直接打开

---

## 11. 已知约束与局限

| 约束 | 影响 | 缓解措施 |
|------|------|---------|
| 仅支持 Windows x64 | 不可跨平台 | — |
| 需管理员权限 | 进程 TCP 网络需要 UAC 提权 | manifest 内嵌 `requireAdministrator` |
| 进程名精确匹配（含 .exe） | 不支持通配符 | 任务管理器复制进程名 |
| UDP 流量为估算值 | 非精确到字节 | 按端点比例 + 每包 1400 字节估算 |
| 缓冲区上限 200 万行 | 超过丢弃最旧数据（~115天@5s） | 可调节 `MAX_BUFFER_ROWS` |
| UI 仅展示前 10 PID | 同名 >10 进程时 UI 精简 | Excel 保留全部 PID |
| 监测期间配置锁定 | 无法中途修改设置 | 停止后可修改 |
| Excel 文件随数据量增长 | 大文件可能影响性能 | 建议定期分段监测 |

---

## 12. 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V1.0 | — | 初始版本，基础系统监控功能 |
| V2.0 | — | 添加进程级监控、多 Tab 页 UI |
| V2.1 | — | 修复 TCP per-PID baselines、Excel 实时导出 |
| V2.6 | — | 多 PID 并行、专用工作集内存、Excel 类型化单元格、配置完整持久化、管理员提权 |
| V2.7 | — | 日志时间格式化、Excel 字体宋体 11pt、数据类型转换、单元格居中 |
| V2.8 | — | 运行时间整数显示、Excel 整数列 0 格式、进程 ID 整数化 |
| V3.0 | — | 架构重构（core/ui/utils 分层）、HTML 趋势报告（Chart.js 折线图）、UDP 进程监控 |
| V3.1 | — | ncpa.cpl 同源网卡列表、已连接绿色加粗（OwnerDraw）、帮助系统完善（5 标签页） |
| **V3.3** | 2026-07 | TCP/UDP 网络追踪修复、Excel/HTML 导出格式优化、DataBuffer 线程安全、文档完善 |
| **V3.4** | 2026-07 | HTML 折线图重构为 Canvas 2D 自绘（X 轴框选缩放、点击坐标值、图表过滤器、双击重置）、HTML 文件命名与 Excel 统一、Y 轴比例自适应、X 轴 -45° 斜角标签、帮助系统完善、文档更新 |

---

*文档最后更新：2026-07-20*
