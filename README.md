# 挂机电脑资源检测工具

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/)
[![Platform](https://img.shields.io/badge/Platform-Windows%207%2B-x64-lightgrey)](https://www.microsoft.com/windows)
[![Build](https://img.shields.io/badge/Build-CMake%20%2B%20Ninja%20%2B%20MSVC-brightgreen)](#)

Windows 原生 C++17 系统资源实时监测工具。**纯 Win32 API，零外部运行时依赖，单个 exe 即可运行。**

> 适用于挂机任务期间的系统负载追踪、性能瓶颈复现、批量测试数据采集等场景。

---

## 目录

- [功能概览](#功能概览)
- [系统要求](#系统要求)
- [依赖说明](#依赖说明)
- [部署步骤](#部署步骤)
- [项目结构](#项目结构)
- [技术架构](#技术架构)
- [核心模块详解](#核心模块详解)
- [配置文件说明](#配置文件说明)
- [常见问题](#常见问题)

---

## 功能概览

| 功能 | 详细说明 |
|------|----------|
| **系统 CPU 监测** | 基于 `GetSystemTimes()` + QPC 高精度时间戳的差分算法，精确到 0.01% |
| **系统内存监测** | 总量 / 可用 / 已用 / 使用率，`GlobalMemoryStatusEx()` 采集，GB 精度 |
| **系统网络监测** | 发送 / 接收速度，支持 KB/s、MB/s、GB/s 三种单位，`GetIfTable2()` 64 位计数器 |
| **进程资源监测** | 按进程名添加，独立 Tab 页展示 PID / CPU / 内存 / 网络。同名多进程全部独立记录。内存使用「专用工作集」（与任务管理器一致） |
| **进程网络监测** | 基于 ETW 内核追踪实时捕获 TCP/IP 事件，按 PID 聚合网络流量。需管理员权限 |
| **网卡选择** | 与「控制面板 → 网络连接」完全一致的物理网卡列表，自动过滤虚拟适配器 |
| **数据导出** | 监测结束自动生成单个 `.xlsx` 文件，一个 Sheet 一个进程，单元格居中对齐，列宽自适应 |
| **配置持久化** | JSON 配置文件（UTF-8），保存监测项、进程列表（含启用状态）、采样周期、网卡选择、输出目录 |
| **窗口置顶** | 右下角切换按钮，文字在「置顶」↔「取消置顶」间切换 |
| **彩色状态** | 监测控制组下方粗体状态标签：「就绪」绿色 / 「监测中」红色 |
| **标题栏右键** | 弹出「打开软件所在目录」，用资源管理器打开 exe 所在文件夹 |
| **表格交互** | 右键菜单支持清除日志、全选、复制选中行（Tab 分隔，可直接粘贴到 Excel） |
| **线程安全** | 监测线程与 UI 线程分离，`CRITICAL_SECTION` 保护数据缓冲区 |

---

## 系统要求

| 项目 | 最低要求 | 推荐 |
|------|---------|------|
| 操作系统 | Windows 7 SP1 (x64) | Windows 10/11 (x64) |
| 编译器 | MSVC 2019 (v142) 或 GCC 9+ | MSVC 2022 (v143) |
| CMake | 3.16+ | 3.28+ |
| 构建工具 | Ninja 或 Visual Studio 2022 | Ninja |
| C++ 标准 | C++17 | C++17 |

> MinGW-w64 通过 GCC 工具链也可编译，`CMakeLists.txt` 已包含 GCC 编译选项。

---

## 依赖说明

### 编译时依赖

| 工具 | 用途 | 安装方式 |
|------|------|---------|
| **CMake ≥ 3.16** | 跨平台构建系统 | [cmake.org](https://cmake.org/download/) 或 `winget install Kitware.CMake` |
| **Visual Studio 2022** | MSVC 工具链 + Windows SDK | [visualstudio.com](https://visualstudio.microsoft.com/) — 安装时勾选「使用 C++ 的桌面开发」 |
| **Ninja** | 高速构建生成器 | VS 2022 自带，或 `winget install Ninja-build.Ninja` |
| **Git** | 版本控制 | [git-scm.com](https://git-scm.com/) |

### 运行时依赖

**零外部运行时依赖。** 程序链接的全部是 Windows 系统 DLL，任何 Windows 7+ x64 裸机均可直接运行：

| 系统 DLL | 用途 |
|----------|------|
| `kernel32.dll` | 线程、文件 I/O、系统时间 |
| `user32.dll` | 窗口管理、消息循环 |
| `gdi32.dll` | 字体渲染、GDI 绘图 |
| `comctl32.dll` | ListView、Tab、Button 等通用控件 |
| `comdlg32.dll` | 文件夹浏览对话框 |
| `shell32.dll` | `ShellExecuteW` 打开资源管理器 |
| `ole32.dll` / `oleaut32.dll` | COM 基础（SHBrowseForFolder） |
| `iphlpapi.dll` | 网卡枚举、流量统计 |
| `ws2_32.dll` | Winsock 类型定义 |

### 第三方库依赖

**无。** 本项目不使用任何第三方 C/C++ 库。所有功能（JSON 解析、XLSX 生成、CRC32）均手工实现。

---

## 部署步骤

### 前置准备（一次性）

```powershell
# 确认环境
cmake --version          # ≥ 3.16
git --version            # 任意版本

# 克隆项目
git clone https://github.com/your-username/MonitorTool.git
cd MonitorTool
```

---

### 方式 A：Visual Studio 2022（推荐，开箱即用）

```powershell
# 1. 在开始菜单搜索并打开 "Developer Command Prompt for VS 2022"
# 2. 进入项目目录
cd D:\path\to\MonitorTool

# 3. 配置 + 编译（Release）
cmake -B out/build/x64-Release -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --config Release
```

**产物**：`out\build\x64-Release\bin\Release\MonitorTool.exe`（约 300KB）

> 也可以直接在 VS 2022 中打开项目文件夹，VS 会识别 `CMakeSettings.json` 并自动配置。菜单 → 生成 → 全部生成。

---

### 方式 B：VS Code + CMake 插件

1. 打开 VS Code，安装以下插件：
   - **C/C++** (`ms-vscode.cpptools`)
   - **CMake Tools** (`ms-vscode.cmake-tools`)

2. `文件` → `打开文件夹` → 选择项目根目录

3. 底部状态栏点击构建配置下拉框：
   - 首次使用选 `[Unconfigured]` → 自动提示选择工具链 → 选 `Visual Studio 2022 Release amd64`

4. 按 `F7` 编译，或 `Ctrl+Shift+P` → `CMake: Build`

5. 按 `F5` 启动 Debug 调试

> 项目已预配置 `.vscode/` 目录（c_cpp_properties、tasks、launch、settings），开箱即用。

---

### 方式 C：命令行手动配置（灵活）

```powershell
# === Debug 构建（完整调试信息）===
cmake -B out/build/x64-Debug -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/x64-Debug --config Debug

# === RelWithDebInfo 构建（优化 + 调试符号，推荐开发用）===
cmake -B out/build/x64-RelWithDebInfo -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/build/x64-RelWithDebInfo

# === Release 构建（最大优化）===
cmake -B out/build/x64-Release -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/x64-Release --config Release

# === 清理构建产物 ===
cmake --build out/build/x64-Debug --target clean
```

**输出目录结构：**
```
out/build/<配置名>/bin/<配置名>/MonitorTool.exe
```

---

### 方式 D：MinGW-w64（适用于无 Visual Studio 的环境）

```powershell
# 确保 g++ 和 cmake 在 PATH 中
g++ --version  # ≥ 9.0

cmake -B out/build/mingw-Release -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/mingw-Release
```

---

## 项目结构

```
MonitorTool/
├── src/                        # 全部源代码
│   ├── main.cpp                # 程序入口，注册窗口类，启动消息循环
│   ├── MainWindow.h            # 主窗口类声明（HWND、控件句柄、状态）
│   ├── MainWindow.cpp          # Win32 GUI 实现（~1200 行）
│   │   ├── CreateChildControls # 创建全部子控件（~200 行）
│   │   ├── LayoutControls      # 响应 WM_SIZE 动态布局
│   │   ├── StartMonitoring     # 启动监测线程
│   │   ├── StopMonitoring      # 停止并导出数据
│   │   ├── UpdateDisplay       # 200ms 定时器刷新 ListView
│   │   └── MainWndProc         # 主窗口消息处理
│   ├── SystemMonitor.h/cpp     # 系统级资源采集（~250 行）
│   │   ├── GetCpuUsage         # GetSystemTimes 差分算法
│   │   ├── GetMemoryInfo       # GlobalMemoryStatusEx
│   │   ├── GetNetworkSpeed     # GetIfTable2 64位流量差
│   │   └── GetNetworkInterfaces# GetAdaptersAddresses 网卡枚举
│   ├── ProcessMonitor.h/cpp    # 进程级资源采集（多 PID 支持）
│   │   ├── FindAllProcessPids   # CreateToolhelp32Snapshot 搜索全部匹配 PID
│   │   ├── GetProcessCpuUsage   # GetProcessTimes 差分算法（per-PID基线）
│   │   └── GetProcessMemory     # PROCESS_MEMORY_COUNTERS_EX2 专用工作集
│   ├── NetSpeedMonitor.h/cpp    # ETW 内核追踪进程级网络流量
│   │   ├── TCP/IP 事件捕获      # TdhGetProperty 跨版本事件解析
│   │   └── Per-PID 字节聚合     # 发送/接收增量查询
│   ├── DataBuffer.h/cpp        # 线程安全环形缓冲区
│   │   ├── CRITICAL_SECTION    # 互斥锁保护
│   │   └── MAX_ROWS = 10000    # 自动裁剪最老数据
│   ├── DataModels.h            # 核心数据结构定义
│   │   ├── SystemMonitorData   # 系统级监测数据
│   │   ├── ProcessMonitorData  # 进程级监测数据
│   │   ├── MonitorProcess      # 进程列表项
│   │   ├── MonitorConfig       # 配置结构体
│   │   └── InitDefaultConfig   # 默认配置初始化
│   ├── ConfigManager.h/cpp     # JSON 配置读写（单例）
│   │   ├── LoadConfig          # UTF-8 → 宽字符 → 手写解析器
│   │   ├── SaveConfig          # 手写序列化 → UTF-8 BOM
│   │   └── JsonEscapeString    # JSON 字符串正确转义
│   ├── ExcelExporter.h/cpp     # XLSX 多 Sheet 导出（~400 行）
│   │   ├── BuildSheetXml       # OpenXML 工作表生成
│   │   ├── WriteZipFile        # 最小 ZIP 写入器（store 模式）
│   │   ├── Crc32               # 标准 CRC32 校验
│   │   └── 零外部库依赖
│   └── resource.h              # 菜单/控件/定时器 ID 宏定义
├── res/
│   └── monitor.rc              # Win32 资源文件（含应用图标）
├── .vscode/                    # VS Code 开发配置（开箱即用）
│   ├── c_cpp_properties.json   # IntelliSense 配置（MSVC x64 / C++17）
│   ├── tasks.json              # CMake 配置/构建/清理任务
│   ├── launch.json             # cppvsdbg 调试启动配置
│   └── settings.json           # CMake 生成器 / 文件过滤 / 格式化
├── CMakeLists.txt              # CMake 构建脚本
├── CMakeSettings.json          # Visual Studio CMake 集成配置
├── .gitignore                  # 忽略构建产物 / IDE 临时文件
└── README.md                   # 本文件
```

---

## 技术架构

```
┌──────────────────────────────────────────────────────────┐
│                     MainWindow                            │
│              (Win32 主窗口 + 消息循环)                      │
│                                                          │
│  ┌────────────┬──────────┬──────────┬────────────────┐   │
│  │ 监测配置    │ 监测项目  │ 监测控制  │  数据 Tab 页    │   │
│  │ 进程列表    │ CPU/内存  │ 开始/停止 │ 系统资源/进程   │   │
│  │ 添加/删除   │ /网络     │ 状态标签  │ ListView 表格   │   │
│  └────────────┴──────────┴──────────┴────────────────┘   │
├──────────────────────────────────────────────────────────┤
│  SystemMonitor           │     ProcessMonitor             │
│  · GetSystemTimes()      │     · CreateToolhelp32Snapshot │
│  · GlobalMemoryStatusEx()│     · GetProcessTimes()        │
│  · GetIfTable2()         │     · GetProcessMemoryInfo()   │
│  · GetAdaptersAddresses()│                                │
│  · QPC 高精度计时         │     · QPC 高精度计时            │
├──────────────────────────┴───────────────────────────────┤
│                    DataBuffer                             │
│       CRITICAL_SECTION 保护 · 环形缓冲 10000 行            │
│       SystemData: vector<SystemMonitorData>               │
│       ProcessData: map<wstring, vector<ProcessMonitorData>>│
├──────────────────────────────────────────────────────────┤
│  ConfigManager            │     ExcelExporter             │
│  · 手写 JSON 解析器       │     · 手写 XLSX 生成器          │
│  · UTF-8 BOM 编码         │     · 手写 ZIP writer          │
│  · 单例模式               │     · 手写 CRC32               │
│  · 正确转义反斜杠/引号     │     · 多 Sheet 支持            │
└──────────────────────────────────────────────────────────┘
```

### 采样算法

#### CPU 使用率

```
cpu% = (totalTicks - idleTicks) / totalTicks × 100

其中：
  totalTicks = (kernelTicks - lastKernelTicks) + (userTicks - lastUserTicks)
  idleTicks  = idleTicks - lastIdleTicks

精度：0.01%（floor 到两位小数）
来源：GetSystemTimes() 返回系统启动以来累计 tick 数
```

#### 网络速度

```
speed = (currentBytes - lastBytes) / (currentQpc - lastQpc) × qpcFrequency

使用 GetIfTable2() 的 64 位计数器（InOctets / OutOctets）
单位转换：KB/s = bytes/s / 1024  |  MB/s = bytes/s / 1048576  |  GB/s = bytes/s / 1073741824
```

#### 进程 CPU

```
processCpu% = (kernelDiff + userDiff) / 10000000 / elapsedSeconds / cpuCount × 100

kernelDiff / userDiff 来自 GetProcessTimes()，单位 100ns
elapsedSeconds 来自 QueryPerformanceCounter 差分
```

---

## 核心模块详解

### 1. MainWindow（主窗口 UI）

| 控件区域 | 控件列表 | ID 范围 |
|---------|---------|---------|
| 监测配置组 | 进程名输入框、添加/全部删除/保存配置按钮、进程 ListView（带 Checkbox） | 2001-2005 |
| 监测项目组 | CPU/Memory/Network 复选框、流量单位下拉、采样周期输入、网卡下拉+刷新、输出目录+浏览 | 2006-2014 |
| 监测控制组 | 开始/停止按钮、彩色粗体状态标签 | 2015-2020 |
| 数据展示 | Tab 控件 + 系统/进程 ListView | 2017-2018 |
| 底部 | 置顶切换按钮 | 2019 |

**定时器**：`IDT_DISPLAY_UPDATE (3001)` 每 200ms 从 DataBuffer 读取新数据并刷新 ListView。

---

### 2. SystemMonitor（系统资源采集）

| 函数 | Windows API | 说明 |
|------|------------|------|
| `GetCpuUsage()` | `GetSystemTimes()` | 内核/用户/空闲 tick 差分，返回 0.00-100.00 |
| `GetMemoryInfo()` | `GlobalMemoryStatusEx()` | 物理内存总量/可用/已用 GB 级 |
| `GetNetworkSpeed()` | `GetIfTable2()` | 64 位 InOctets/OutOctets QPC 差分 |
| `GetNetworkInterfaces()` | `GetAdaptersAddresses(flags=0)` | 仅 TCP/IP 绑定适配器，过滤回环 + 非物理类型 |
| `Initialize()` | 以上全部 | 采集初始基线（CPU tick、流量计数器、QPC 时间戳） |

**网卡过滤逻辑**：`flags=0` → 仅 TCP/IP 绑定适配器（与 ncpa.cpl 同源），再按 `IfType` 过滤为以太网 + Wi-Fi。

---

### 3. ProcessMonitor（进程资源采集）

| 函数 | Windows API | 说明 |
|------|------------|------|
| `FindProcessPid()` | `CreateToolhelp32Snapshot()` | 按进程名搜索 PID，重新绑定已终止的进程 |
| `GetProcessCpuUsage()` | `GetProcessTimes()` + QPC | 带 CPU 核心数归一化 |
| `GetProcessMemory()` | `PROCESS_MEMORY_COUNTERS_EX2` | PrivateWorkingSetSize（专用工作集），与任务管理器一致，Win7 回退到 PrivateUsage |
| `Collect()` | 以上全部 + ETW 网络 | 多实例全部采集，同名进程按 PID 区分，网络速度来自 ETW 内核追踪 |

> **管理员权限**：进程级网络监测需要 ETW 内核追踪，程序已嵌入 `requireAdministrator` 清单，启动时会提示 UAC 提权。无管理员权限时网络监测降级为 0，其他功能不受影响。

---

### 4. ExcelExporter（XLSX 导出）

| 组件 | 实现 |
|------|------|
| ZIP 容器 | 手写 ZIP writer（store 无压缩），LocalHeader + CentralDir + EOCD |
| CRC32 | 标准 0xEDB88320 多项式查表法 |
| 工作表 XML | OpenXML Spreadsheet 格式，`inlineStr` 内联字符串 |
| 样式 | 内置 styles.xml，粗体表头，Microsoft YaHei 字体 |
| Sheet 命名 | 自动去掉 `.exe` 后缀，限长 31 字符，非法字符替换为 `_` |

**XLSX 文件结构**：
```
monitor_data_20260101120000.xlsx
├── [Content_Types].xml
├── _rels/.rels
├── xl/
│   ├── workbook.xml
│   ├── _rels/workbook.xml.rels
│   ├── styles.xml
│   └── worksheets/
│       ├── sheet1.xml  (系统资源)
│       ├── sheet2.xml  (进程1)
│       ├── sheet3.xml  (进程2)
│       └── ...
```

---

### 5. ConfigManager（配置管理）

| 函数 | 实现 |
|------|------|
| `LoadConfig()` | `CreateFileW` 读取 → `MultiByteToWideChar(CP_UTF8)` 转码 → 手写递归下降解析器 |
| `SaveConfig()` | 手写 JSON 序列化 → `WideCharToMultiByte(CP_UTF8)` → `WriteFile`（含 0xEFBBBF BOM） |
| `JsonEscapeString()` | 正确转义 `\` → `\\`、`"` → `\"`、`\n` → `\\n`、`\r` → `\\r`、`\t` → `\\t` |

> 旧版配置文件存在反斜杠未转义的 bug。新版已修复：保存时 `D:\data` 写为 `D:\\data`，加载时正确还原。

---

## 配置文件说明

`config.json`（位于 exe 同目录）：

```json
{
  "monitorProcesses": [
    {
      "name": "notepad.exe",
      "enabled": true
    }
  ],
  "monitorItems": {
    "cpu": true,
    "memory": true,
    "network": true
  },
  "samplePeriod": 5,
  "netUnit": "KB/s",
  "netInterface": "全部",
  "outputDir": "D:\\MonitorData"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `monitorProcesses` | array | 监测进程列表，`name` 含 `.exe`，`enabled` 控制启用 |
| `monitorItems.cpu` | bool | 是否采集 CPU |
| `monitorItems.memory` | bool | 是否采集内存 |
| `monitorItems.network` | bool | 是否采集网络 |
| `samplePeriod` | int | 采样周期，1-60 秒 |
| `netUnit` | string | 网络单位：`KB/s` / `MB/s` / `GB/s` |
| `netInterface` | string | 网卡名（与 ncpa.cpl 一致）或 `全部` |
| `outputDir` | string | 数据导出目录，路径 |

---

## 常见问题

### Q1：编译报错「无法打开包括文件: windows.h」

**A**：缺少 Windows SDK。安装 Visual Studio 时需勾选「使用 C++ 的桌面开发」工作负载。如果已安装，确保在 **Developer Command Prompt** 中编译，或使用 `_b.bat` 批处理。

### Q2：运行后网络速度始终显示 0

**A**：
1. 确保在「网卡选择」中选中了**正在使用的网卡**（如 WLAN），而非已断开的以太网。
2. 确认网卡列表正确识别了你的物理网卡（点击「刷新」按钮重新获取）。
3. 如果选中了「全部」但某张非活跃网卡也被统计，切换到活跃网卡即可。

### Q3：进程的网络流量始终显示 0

**A**：进程级网络监测需要 ETW 内核追踪，必须以**管理员权限**运行。程序已嵌入提权清单，启动时会有 UAC 提示。如果点击「否」拒绝提权，进程网络将始终显示 0，但 CPU/内存/系统网络均正常。

### Q4：进程的内存数值和任务管理器不一致

**A**：程序已使用 `PrivateWorkingSetSize`（专用工作集），与任务管理器「详细信息」页的「内存(专用工作集)」列一致。如果仍不一致，确认对比的是同一列（非「工作集」或「提交大小」）。

### Q5：导出的 Excel 文件打不开或乱码

**A**：本项目生成的是标准 OpenXML `.xlsx` 文件，需 Excel 2007+ 或 WPS 打开。如果旧版 Excel 打不开，请升级 Office 或使用 [LibreOffice](https://www.libreoffice.org/)。

### Q6：输出目录路径中的反斜杠丢失

**A**：旧版 `config.json` 存在此 bug。删除 exe 同目录的 `config.json`，让程序重新生成，或手动修正 `outputDir`。

### Q7：刷新网卡后下拉列表与 ncpa.cpl 不一致

**A**：程序使用 `GetAdaptersAddresses(flags=0)` 获取 TCP/IP 绑定适配器，理论上与 ncpa.cpl 同源。如果仍然不一致：
- 打开「控制面板 → 网络和共享中心 → 更改适配器设置」
- 查看你的物理网卡名称
- 对比程序下拉列表
- 如果某张网卡缺失，可能是该网卡未绑定 TCP/IP 协议

### Q8：窗口置顶按钮不生效

**A**：已修复。点击「置顶」后按钮文字会变为「取消置顶」并置顶窗口。如仍无效，检查是否有其他全屏程序强制覆盖（如游戏的全屏独占模式）。

### Q9：监测中 CPU 突然飙升到 100%

**A**：首次采样时基线未建立，第一个采样点可能返回 0。第二个采样点开始正常。如果持续 100%，检查是否有大量进程瞬时启动。

### Q10：Debug 构建运行缓慢

**A**：Debug 构建使用 `/Od`（无优化）+ `/RTC1`（运行时检查），比 Release 慢 3-5 倍。日常使用推荐 `RelWithDebInfo` 构建。

### Q11：如何添加开机自启动？

**A**：按 `Win+R` → `shell:startup` → 将 `MonitorTool.exe` 快捷方式放入启动文件夹即可。

---

## License

MIT © 2024
