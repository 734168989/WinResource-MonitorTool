# 挂机电脑资源监测软件 V3.4

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/)
[![Platform](https://img.shields.io/badge/Platform-Windows%207%2B%20x64-lightgrey)](https://www.microsoft.com/windows)
[![Build](https://img.shields.io/badge/Build-CMake%20%2B%20MSVC-brightgreen)](#)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

> 一款 Windows 原生 C++17 系统资源实时监测工具。**纯 Win32 API，零外部运行时依赖，单个 exe 即可运行。**
>
> 适用于挂机任务期间的系统负载追踪、进程性能分析、批量测试数据采集等场景。

---

## 目录

- [功能概览](#功能概览)
- [系统要求](#系统要求)
- [依赖说明](#依赖说明)
- [快速开始](#快速开始)
- [项目结构](#项目结构)
- [技术架构](#技术架构)
- [核心模块详解](#核心模块详解)
- [配置文件说明](#配置文件说明)
- [使用指南](#使用指南)
- [常见问题](#常见问题)
- [开发指南](#开发指南)

---

## 功能概览

| 功能 | 说明 |
|------|------|
| **系统 CPU** | `GetSystemTimes()` + QPC 高精度差分算法，0.01% 精度 |
| **系统内存** | 总量 / 可用 / 已用（GB）+ 使用率（%），`GlobalMemoryStatusEx()` |
| **系统网络** | 发送 / 接收速度，Kbps / Mbps / Gbps 三档，`GetIfTable2()` 64 位计数器 |
| **网卡选择** | 与 ncpa.cpl（网络连接面板）共用 `INetConnectionManager` COM 接口，列表完全一致；已连接网卡绿色加粗 |
| **进程 CPU** | 按进程名添加，`GetProcessTimes()` + QPC 差分，多核归一化 |
| **进程内存** | `PrivateWorkingSetSize`（专用工作集），与任务管理器一致 |
| **进程网络** | TCP `GetPerTcpConnectionEStats` 精确到字节 + UDP 系统级按端点比例分配，IPv4/IPv6 双栈 |
| **多进程并行** | 同名多进程按 PID 独立采集，独立 Tab 页签展示 |
| **Excel 导出** | 监测中每 2 秒实时写入（防崩溃丢失），停止时最终保存，系统+进程分 Sheet，类型化单元格 |
| **HTML 报告** | Canvas 2D 自绘折线图，X 轴框选缩放、点击显示坐标值，自包含无需联网 |
| **配置持久化** | JSON 配置文件（UTF-8），所有设置完整保存，启动自动加载 |
| **帮助系统** | 5 页签帮助对话框（软件简介 / 功能说明 / 使用方法 / 使用窍门 / 注意事项） |
| **窗口置顶** | 按钮切换，方便实时观察 |
| **彩色状态** | 绿色=就绪 / 蓝色=监测中 / 红色=异常 / 橙色=暂停 |
| **右键菜单** | 数据列表右键：清除日志 / 全选 / 复制选中（Tab 分隔，可粘贴到 Excel） |
| **数据容量** | 缓冲区 200 万行（~115 天 @5s 采样），日志显示 5 万行 |

---

## 系统要求

| 项目 | 最低要求 | 推荐 |
|------|---------|------|
| 操作系统 | Windows 7 SP1 (x64) | Windows 10/11 (x64) |
| 编译器 | MSVC 2019 (v142) 或 GCC 9+ | MSVC 2022 (v143) |
| CMake | 3.16+ | 3.28+ |
| C++ 标准 | C++17 | C++17 |
| 运行权限 | **管理员权限**（UAC 提权） | — |

---

## 依赖说明

### 零第三方依赖

本项目的所有功能均**手工实现**，无需任何第三方库：

| 实现 | 用途 | 代码量 |
|------|------|--------|
| 手写递归下降解析器 | JSON 配置读写 | ~200 行 |
| 自研 ZIP writer (store) | XLSX 容器打包 | ~150 行 |
| 手写 OOXML 流式生成 | Excel 工作表 XML | ~400 行 |
| Canvas 2D 自绘 | HTML 趋势折线图 | ~600 行 |

### 系统 API 依赖

所有依赖均为 Windows 系统 DLL，裸机即可运行：

| 系统 DLL | 用途 |
|----------|------|
| `kernel32.dll` | 线程、文件 I/O、系统时间、进程句柄 |
| `user32.dll` / `gdi32.dll` | 窗口管理、GDI 绘制、字体渲染 |
| `comctl32.dll` | ListView、Tab、Button、ComboBox 等通用控件 |
| `comdlg32.dll` / `shell32.dll` | 文件夹浏览对话框、打开资源管理器 |
| `ole32.dll` / `oleaut32.dll` | COM 基础（ncpa.cpl 网卡枚举） |
| `iphlpapi.dll` | 网卡枚举、TCP/UDP 连接表、流量统计 |
| `psapi.dll` | 进程内存查询 |
| `ws2_32.dll` | Winsock 类型定义 |

---

## 快速开始

### 一键编译

```batch
build_all.bat
```

### Visual Studio 2022（推荐）

在 **Developer Command Prompt for VS 2022** 中：

```powershell
cd D:\path\to\MonitorTool3.0
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

编译产物：`out\MonitorTool.exe`

### VS Code + CMake 插件

1. 安装插件：**C/C++** + **CMake Tools**
2. `文件` → `打开文件夹` → 选择项目根目录
3. 底部状态栏选择：`Visual Studio 2022 Release amd64`
4. `F7` 编译

> 项目已预配置 `.vscode/`（c_cpp_properties、tasks、launch、settings），开箱即用。

### 构建配置

| 配置 | 说明 |
|------|------|
| `Debug` | 完整调试信息（/Od /ZI /RTC1），用于断点调试 |
| `Release` | 全优化（/O2 /GL /LTCG），体积小速度快 |
| `RelWithDebInfo` | 优化 + 调试符号（推荐日常开发使用） |

---

## 项目结构

```
MonitorTool3.0/
├── CMakeLists.txt                     # CMake 构建（MSVC + MinGW 双编译器）
├── MonitorTool.sln                    # Visual Studio 解决方案
├── build_all.bat                      # 一键编译脚本
├── README.md                          # 本文件
│
├── res/                               # 资源文件
│   ├── monitor.rc                     # 资源脚本（图标、版本信息）
│   ├── resource.h                     # 控件 ID 常量宏定义
│   ├── MonitorTool.ico                # 程序图标
│   └── MonitorTool.manifest           # 应用程序清单（requireAdministrator）
│
├── include/                           # 头文件
│   ├── core/
│   │   ├── SystemMonitor.h            # 系统级监控接口
│   │   ├── ProcessMonitor.h           # 进程级监控接口
│   │   └── NetSpeedMonitor.h          # 进程网络追踪接口
│   ├── ui/
│   │   ├── MainWindow.h               # 主窗口类 + 控件枚举 + UI 函数声明
│   │   └── HelpDialog.h               # 帮助对话框接口
│   └── utils/
│       ├── DataModels.h               # 核心数据结构 + 配置初始化函数
│       ├── DataBuffer.h               # 线程安全环形缓冲区
│       ├── ConfigManager.h            # JSON 配置读写（单例）
│       ├── ExcelExporter.h            # XLSX 导出接口
│       └── HtmlChartExporter.h        # HTML 报告生成接口
│
├── src/                               # 源文件
│   ├── main.cpp                       # WinMain 入口（COM/CommonControls/消息循环）
│   ├── core/
│   │   ├── SystemMonitor.cpp          # CPU(QPC)/内存(GlobalMemoryStatusEx)/网络(GetIfTable2)/网卡枚举
│   │   ├── ProcessMonitor.cpp         # 多PID进程发现/CPU(GetProcessTimes)/内存(PrivateWorkingSetSize)
│   │   └── NetSpeedMonitor.cpp        # TCP EStats(字节级)+UDP系统级比例分配+连接GC
│   ├── ui/
│   │   ├── MainWindow.cpp             # 主窗口实现（~1820行：布局/事件/监测线程/显示刷新/Excel定时写入）
│   │   └── HelpDialog.cpp             # 5 页签帮助对话框
│   └── utils/
│       ├── DataBuffer.cpp             # 环形缓冲区实现（SRWLOCK 读写锁）
│       ├── ConfigManager.cpp          # 手写 JSON 解析器 + UTF-8 文件读写
│       ├── ExcelExporter.cpp          # ZIP store + OOXML 流式生成 + 实时增量刷新
│       └── HtmlChartExporter.cpp      # Canvas 2D 自绘 HTML + 交互式趋势报告
│
└── doc/
    ├── PRD.md                         # 产品需求文档
    └── README.md                      # 本文件
```

---

## 技术架构

### 分层架构

```
┌──────────────────────────────────────────────────────┐
│                  Win32 UI Layer                        │
│   MainWindow.cpp (1820行)  │  HelpDialog.cpp           │
│   窗口过程 / 动态布局 / 事件处理 / 显示更新               │
├──────────────────────────────────────────────────────┤
│                  Business Logic                        │
│   ConfigManager (手写JSON解析)  │  ExcelExporter (ZIP+OOXML) │
│   DataBuffer (线程安全环形缓冲)   │  HtmlChartExporter (Canvas 2D) │
├──────────────────────────────────────────────────────┤
│                Data Collection Layer                   │
│   SystemMonitor  │  ProcessMonitor  │ NetSpeedMonitor  │
│   (系统CPU/内存/网速)│ (进程CPU/内存)    │ (TCP EStats+UDP) │
├──────────────────────────────────────────────────────┤
│                  Windows API                           │
│   kernel32 / iphlpapi / psapi / netcon(COM) / ws2_32  │
└──────────────────────────────────────────────────────┘
```

### 线程模型

| 线程 | 职责 | 周期 |
|------|------|------|
| **主线程（UI）** | 消息循环 + WM_TIMER(200ms) 显示刷新 + Excel 定时写入 | — |
| **监测线程** | `MonitorThreadProc` 按采样周期循环采集数据 | 1~60s（可配置） |
| **同步机制** | DataBuffer 使用 `SRWLOCK` 读写锁 | 采集线程写 / UI 线程读 |

### 核心算法

#### CPU 使用率
```
系统: cpu% = (totalTicks - idleTicks) / totalTicks × 100
      来源: GetSystemTimes() 内核/用户/空闲 tick 差值

进程: cpu% = (kernelDiff + userDiff) / 10^7 / elapsedSec / cpuCount × 100
      来源: GetProcessTimes() (100ns 单位) + QPC 差分
```

#### 网络速度
```
speed = (currentBytes - lastBytes) / (currentQpc - lastQpc) × qpcFrequency
      来源: GetIfTable2() 64位 InOctets/OutOctets 计数器
      单位: bytes/s × 8 / 1000 (Kbps) / 1,000,000 (Mbps) / 1,000,000,000 (Gbps)
```

#### 进程网络（TCP）
```
每周期 → GetExtendedTcpTable 枚举该 PID 所有 ESTABLISHED 连接
       → SetPerTcpConnectionEStats (首次启用统计)
       → GetPerTcpConnectionEStats 读取 DataBytesOut/DataBytesIn
       → delta = current - baseline → 累计到 sent/recv
       → 每 PID 独立 baseline 追踪
```

#### 进程网络（UDP）
```
每 800ms → GetIpStatisticsEx 更新系统级 UDP 收发统计
         → GetExtendedUdpTable 统计该 PID 的 UDP 端点数量
         → delta = (系统增量 × PID端点比例 × 1400 字节/包)
```

#### 网络平滑
```
if (currentValue > 0.005 Mbps || currentValue > smoothedValue)
    smoothedValue = currentValue          // 即时响应
else
    smoothedValue = smoothedValue × 0.7   // 慢衰减，避免跳 0
```

---

## 核心模块详解

### SystemMonitor — 系统级监控

| 函数 | API | 说明 |
|------|-----|------|
| `GetCpuUsage()` | `GetSystemTimes()` | 内核/用户/空闲 tick 差值，排除 Idle 时间 |
| `GetMemoryInfo()` | `GlobalMemoryStatusEx()` | 总量/可用/已用 GB + 使用率 % |
| `GetNetworkSpeed()` | `GetIfTable2()` | 遍历物理网卡累计 64 位计数器，QPC 差分 |
| `GetNetworkInterfaces()` | `INetConnectionManager` COM | 与 ncpa.cpl 同源，改名实时生效 |
| `IsInterfaceConnected()` | `IfOperStatus` | 已连接网卡绿色加粗标识 |

### ProcessMonitor — 进程级监控

| 函数 | API | 说明 |
|------|-----|------|
| `FindAllProcessPids()` | `CreateToolhelp32Snapshot()` | 按进程名枚举全部匹配 PID |
| `GetProcessCpuUsage()` | `GetProcessTimes()` + QPC | 按逻辑核心数归一化，per-PID 基线 |
| `GetProcessMemory()` | `PROCESS_MEMORY_COUNTERS_EX2` | PrivateWorkingSetSize（专用工作集），Win7 回落 PrivateUsage |
| `Collect()` | 以上全部 + NetSpeedMonitor | 单次采集全部 PID 数据 |

### NetSpeedMonitor — 进程网络追踪

| 协议 | 技术 | 精度 |
|------|------|------|
| TCP IPv4 | `GetExtendedTcpTable(AF_INET)` → `SetPerTcpConnectionEStats` → `GetPerTcpConnectionEStats` | 精确到字节 |
| TCP IPv6 | `GetExtendedTcpTable(AF_INET6)` → `SetPerTcp6ConnectionEStats` → `GetPerTcp6ConnectionEStats` | 精确到字节 |
| UDP | `GetIpStatisticsEx` 系统级统计 + `GetExtendedUdpTable` 端点比例分配 | 估算值 |
| GC | 每 10 次查询清理已关闭连接（遍历 TCP 表验证存活） | 防内存泄漏 |

### ExcelExporter — XLSX 导出

- **容器**：手写 ZIP store 模式（LocalHeader + CentralDir + EOCD）
- **CRC32**：标准 0xEDB88320 多项式查表法
- **Sheet 命名**：去 `.exe` 后缀，限长 31 字符
- **样式**：表头加粗 + 浅蓝背景（#DCE6F1）+ 居中 + 宋体 11pt
- **列类型**：日期序列号 / 整数(0) / 小数(0.00) / 字符串(inlineStr)
- **实时写入**：`BeginExport` 锁定 → 每 2s `FlushExport` 全量覆写 → `EndExport` 释放

### HtmlChartExporter — HTML 报告

- **绘制方式**：Canvas 2D 自绘，零外部依赖，无需联网加载
- **主题**：浅色背景（`#f5f6fa`），响应式布局
- **内容**：系统 CPU/内存/网络 + 每进程 CPU/内存/网络 折线图
- **交互功能**：X 轴框选缩放、点击折线显示坐标值、图表过滤器（软件名/PID/指标类型）
- **文件命名**：`monitor_data_YYYYMMDDHHmmss.html`

### ConfigManager — 配置管理

- **解析器**：手写递归下降 JSON 解析器（支持嵌套对象/数组/字符串转义/Unicode 跳过）
- **序列化**：JSON 格式化输出，UTF-8 编码
- **容错**：文件不存在/损坏/格式异常 → 回退默认配置
- **模式**：单例模式，全局唯一实例

---

## 配置文件说明

程序启动时自动加载 exe 同目录下的 `config.json`（UTF-8 编码）。

### 完整示例

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

### 字段说明

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `monitorProcesses[].name` | String | — | 进程文件名（含 `.exe`，不区分大小写） |
| `monitorProcesses[].enabled` | Boolean | `true` | 是否启用该进程监控 |
| `monitorItems.cpu` | Boolean | `true` | 监控系统 CPU |
| `monitorItems.memory` | Boolean | `true` | 监控系统内存 |
| `monitorItems.network` | Boolean | `true` | 监控系统网速 |
| `samplePeriod` | Integer | `5` | 采样周期（1~60 秒） |
| `netUnit` | String | `"Mbps"` | 网速单位（`Kbps`/`Mbps`/`Gbps`） |
| `netInterface` | String | `"全部"` | 网卡名称或 `"全部"`（汇总所有物理网卡） |
| `outputDir` | String | exe 目录 | Excel/HTML 输出路径 |
| `generateReport` | Boolean | `true` | 是否自动生成 HTML 报告 |

---

## 使用指南

### 基本操作流程

1. 勾选监测项目（CPU / 内存 / 网络）
2. 选择流量单位和网卡（默认「全部」）
3. 设置采样周期（1~60 秒，推荐 5 秒）
4. 输入进程名（如 `chrome.exe`）点击「添加」
5. 设置输出目录（默认 exe 所在目录）
6. 点击「开始监测」
7. 点击「停止监测」自动导出 Excel

### 数据保存规则

- 监测中每 2 秒自动刷新 Excel（实时保存）
- 停止时最终保存并释放文件锁定
- 界面日志保留 5 万行，旧行自动裁剪
- 缓冲区上限 200 万行，超过自动丢弃最旧数据

### 使用窍门

| 场景 | 建议 |
|------|------|
| 短时观察 | 采样周期 1~3 秒 |
| 日常监测 | 采样周期 5~10 秒 |
| 长时挂机 | 采样周期 30~60 秒 |
| 网卡选择 | 默认「全部」满足大多数场景 |
| 获取进程名 | 任务管理器 → 详细信息 → 复制名称列 |
| 数据粘贴 | 右键 → 复制选中（Tab 分隔），直接粘贴到 Excel |
| 快速定位 | 标题栏右键 → 打开软件所在目录 |

### 注意事项

1. **监测期间锁定**：配置项被锁定，需停止后才能修改
2. **进程网络**：TCP 精确到字节，UDP 为估算值
3. **管理员权限**：进程网络监控需要 UAC 提权
4. **进程退出**：不中断监测，数据归零继续采集
5. **Excel 文件**：随数据增长，建议定期分段监测
6. **网卡改名**：在 ncpa.cpl 中改名后需点击「刷新」

---

## 常见问题

### Q1：编译报错「无法打开包括文件: windows.h」

缺少 Windows SDK。安装 Visual Studio 时勾选「使用 C++ 的桌面开发」工作负载，确保在 **Developer Command Prompt** 中编译。

### Q2：运行后网络速度始终显示 0

1. 确保网卡下拉选中了正在使用的网卡（如 WLAN/Ethernet）
2. 点击「刷新」重新扫描网卡
3. 选择「全部」汇总所有物理网卡

### Q3：进程网络流量始终为 0

进程网络（TCP EStats）需**管理员权限**。启动时 UAC 提示须点「是」。此外：
- 确认进程确实有活跃的 TCP 连接
- 首次采集需建立基线，等待一个采样周期

### Q4：进程内存数值与任务管理器不一致

程序使用 `PrivateWorkingSetSize`（专用工作集），与任务管理器「详细信息」→「内存(专用工作集)」列一致。确认对比的是**同一列**（非「工作集」或「提交大小」）。

### Q5：导出的 Excel 文件打不开

- 监测中文件被锁定（`FILE_SHARE_READ`），只能**只读**打开
- 需 Excel 2007+ / WPS / LibreOffice
- 停止监测后锁定解除

### Q6：Excel 文件体积太大

- 增大采样周期（如 30~60 秒）
- 减少监控进程数量
- 分时段监测

### Q7：HTML 报告图表不显示

HTML 使用自包含 Canvas 2D 绘制，无需联网。直接用浏览器（Chrome/Firefox/Edge）打开即可查看。

### Q8：网卡列表与实际不符

点击「刷新」重新扫描。列表与 ncpa.cpl（网络连接）共用 `INetConnectionManager` COM 接口，改名后需刷新。

### Q9：进程退出后监控中断？

不会。进程退出后该进程数据归零，监控继续。重启后按新 PID 继续采集。UI 展示前 10 PID，Excel 保留全部。

### Q10：如何添加开机自启动？

`Win+R` → `shell:startup` → 将 `MonitorTool.exe` 快捷方式放入启动文件夹。

---

## 开发指南

### 代码风格

- **标准**：C++17，纯 Win32 API 风格
- **分层**：`core`（采集）/ `ui`（界面）/ `utils`（工具）
- **编码**：源码 UTF-8，运行时 UTF-16 (`wchar_t`)，配置文件 UTF-8
- **线程安全**：`CRITICAL_SECTION`（网络连接映射）、`SRWLOCK`（DataBuffer）
- **内存管理**：原始指针 + RAII，显式 `malloc`/`free`，无智能指针依赖

### 调试技巧

- Debug 配置获取完整调试符号（`/Od /ZI /RTC1`）
- `OutputDebugStringW` 输出通过 [DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview) 或 VS 输出窗口查看
- 关键诊断点：网卡枚举、TCP baselines、JSON 解析、DataBuffer 读写

### 构建命令

```powershell
# MSVC (推荐)
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo

# 清理
cmake --build build --target clean
```

### 添加新监控指标

1. 在 `include/utils/DataModels.h` 的 `SystemMonitorData` / `ProcessMonitorData` 中添加字段
2. 在对应的 Monitor 类中添加采集函数
3. 在 `src/ui/MainWindow.cpp` 的 ListView 列定义中添加新列
4. 在 `src/utils/ExcelExporter.cpp` 中添加对应的列写入逻辑
5. 在 `src/utils/HtmlChartExporter.cpp` 中添加对应的图表

---

**作者**：无人机

**版本**：V3.4

**License**：MIT
