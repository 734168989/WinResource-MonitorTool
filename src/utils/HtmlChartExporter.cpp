// HtmlChartExporter.cpp - Generate standalone HTML with trend line charts (CPU + Memory + Network)
#include "HtmlChartExporter.h"
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <string>

static std::string EscapeHtml(const std::wstring& ws) {
    int clen = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(),
                                   nullptr, 0, nullptr, nullptr);
    if (clen <= 0) return "";
    std::string out;
    out.reserve(clen + 16);
    char* buf = (char*)malloc(clen + 1);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.length(), buf, clen, nullptr, nullptr);
    buf[clen] = '\0';
    for (int i = 0; i < clen; i++) {
        switch (buf[i]) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += buf[i];   break;
        }
    }
    free(buf);
    return out;
}

static std::string FormatTimestamp(const wchar_t* ts) {
    return EscapeHtml(ts);
}

// Strip .exe for display
static std::wstring StripExe(const wchar_t* name) {
    std::wstring s = name;
    size_t dot = s.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = s.substr(dot);
        if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".com") == 0)
            s = s.substr(0, dot);
    }
    return s;
}

// ============================================================================
// ChartData: one chart = one metric for one subject
// ============================================================================
struct ChartData {
    std::wstring title;          // e.g. "系统 CPU 使用率"
    std::wstring subtitle;       // e.g. "峰值: 85%  当前: 32%"
    std::wstring yLabel;         // Y-axis label, e.g. "CPU (%)"
    std::wstring software;       // e.g. "MPrintExp" or "系统"
    std::wstring pid;            // e.g. "1234" or "" for system charts
    std::wstring metric;         // e.g. "CPU", "内存", "网络发送", "网络接收"
    double      peakValue;
    std::vector<std::wstring> labels;   // X-axis timestamps
    std::vector<double>     values;     // Y-axis values
};

// Helper: compute peak value from a ChartData
static double PeakValue(const ChartData& cd) {
    if (cd.values.empty()) return 0;
    double m = cd.values[0];
    for (double v : cd.values) if (v > m) m = v;
    return m;
}

// Helper: current (last) value
static double CurrValue(const ChartData& cd) {
    return cd.values.empty() ? 0 : cd.values.back();
}

// ============================================================================
// BuildHtml — generates complete HTML with charts for all metrics
// ============================================================================
std::string HtmlChartExporter::BuildHtml(
    double startTimestamp,
    const std::vector<SystemMonitorData>& systemData,
    const std::vector<MonitorProcess>& processes,
    const std::vector<std::vector<ProcessMonitorData>>& allProcessData)
{
    if (systemData.empty()) return "";

    time_t startT = (time_t)startTimestamp;
    struct tm tm_start;
    localtime_s(&tm_start, &startT);
    wchar_t fileTimeLabel[64];
    wcsftime(fileTimeLabel, 64, L"%Y/%m/%d %H:%M:%S", &tm_start);

    std::vector<ChartData> charts;

    // ---- System-level charts ----
    {
        ChartData cdCpu;
        cdCpu.title = L"系统 CPU 使用率";
        cdCpu.yLabel = L"CPU (%)";
        cdCpu.software = L"系统";
        cdCpu.pid = L"";
        cdCpu.metric = L"CPU";
        for (const auto& d : systemData) {
            cdCpu.labels.push_back(d.timestamp);
            cdCpu.values.push_back(d.cpuUsage);
        }
        cdCpu.peakValue = PeakValue(cdCpu);
        wchar_t buf[128];
        swprintf_s(buf, 128, L"峰值: %.1f %%  当前: %.1f %%  采样: %zu 点",
            PeakValue(cdCpu), CurrValue(cdCpu), cdCpu.labels.size());
        cdCpu.subtitle = buf;
        charts.push_back(cdCpu);
    }
    {
        ChartData cdMem;
        cdMem.title = L"系统内存使用";
        cdMem.yLabel = L"内存 (MB)";
        cdMem.software = L"系统";
        cdMem.pid = L"";
        cdMem.metric = L"内存";
        for (const auto& d : systemData) {
            cdMem.labels.push_back(d.timestamp);
            cdMem.values.push_back(d.memoryUsedGB * 1024.0);  // GB -> MB
        }
        cdMem.peakValue = PeakValue(cdMem);
        wchar_t buf[128];
        swprintf_s(buf, 128, L"峰值: %.0f MB  当前: %.0f MB  采样: %zu 点",
            PeakValue(cdMem), CurrValue(cdMem), cdMem.labels.size());
        cdMem.subtitle = buf;
        charts.push_back(cdMem);
    }
    {
        ChartData cdSend;
        cdSend.title = L"系统网络发送";
        cdSend.yLabel = L"发送 (Mbps)";
        cdSend.software = L"系统";
        cdSend.pid = L"";
        cdSend.metric = L"网络发送";
        for (const auto& d : systemData) {
            cdSend.labels.push_back(d.timestamp);
            cdSend.values.push_back(d.netSendSpeed);
        }
        cdSend.peakValue = PeakValue(cdSend);
        wchar_t buf[128];
        swprintf_s(buf, 128, L"峰值: %.2f Mbps  当前: %.2f Mbps  采样: %zu 点",
            cdSend.peakValue, CurrValue(cdSend), cdSend.labels.size());
        cdSend.subtitle = buf;
        charts.push_back(cdSend);
    }
    {
        ChartData cdRecv;
        cdRecv.title = L"系统网络接收";
        cdRecv.yLabel = L"接收 (Mbps)";
        cdRecv.software = L"系统";
        cdRecv.pid = L"";
        cdRecv.metric = L"网络接收";
        for (const auto& d : systemData) {
            cdRecv.labels.push_back(d.timestamp);
            cdRecv.values.push_back(d.netRecvSpeed);
        }
        cdRecv.peakValue = PeakValue(cdRecv);
        wchar_t buf[128];
        swprintf_s(buf, 128, L"峰值: %.2f Mbps  当前: %.2f Mbps  采样: %zu 点",
            cdRecv.peakValue, CurrValue(cdRecv), cdRecv.labels.size());
        cdRecv.subtitle = buf;
        charts.push_back(cdRecv);
    }

    // ---- Per-process-PID charts ----
    for (size_t pi = 0; pi < processes.size(); pi++) {
        if (!processes[pi].enabled) continue;
        if (pi >= allProcessData.size()) break;
        if (allProcessData[pi].empty()) continue;

        // Group rows by PID
        std::map<DWORD, std::vector<size_t>> pidGroups;
        for (size_t ri = 0; ri < allProcessData[pi].size(); ri++) {
            pidGroups[allProcessData[pi][ri].pid].push_back(ri);
        }

        std::wstring baseName = StripExe(processes[pi].name);

        for (const auto& kv : pidGroups) {
            DWORD pid = kv.first;
            const auto& indices = kv.second;
            std::wstring pidLabel = (pid != 0)
                ? baseName + L" (PID:" + std::to_wstring(pid) + L")"
                : baseName;

            // CPU
            {
                ChartData cd;
                cd.title = pidLabel + L" — CPU 使用率";
                cd.yLabel = L"CPU (%)";
                cd.software = baseName;
                cd.pid = std::to_wstring(pid);
                cd.metric = L"CPU";
                for (size_t ri : indices) {
                    cd.labels.push_back(allProcessData[pi][ri].timestamp);
                    cd.values.push_back(allProcessData[pi][ri].cpuUsage);
                }
                cd.peakValue = PeakValue(cd);
                wchar_t buf[128];
                swprintf_s(buf, 128, L"PID: %lu  峰值: %.1f %%  当前: %.1f %%  采样: %zu 点",
                    pid, cd.peakValue, CurrValue(cd), indices.size());
                cd.subtitle = buf;
                charts.push_back(cd);
            }
            // Memory
            {
                ChartData cd;
                cd.title = pidLabel + L" — 内存使用";
                cd.yLabel = L"内存 (MB)";
                cd.software = baseName;
                cd.pid = std::to_wstring(pid);
                cd.metric = L"内存";
                for (size_t ri : indices) {
                    cd.labels.push_back(allProcessData[pi][ri].timestamp);
                    cd.values.push_back(allProcessData[pi][ri].memoryUsedMB);
                }
                cd.peakValue = PeakValue(cd);
                wchar_t buf[128];
                swprintf_s(buf, 128, L"PID: %lu  峰值: %.1f MB  当前: %.1f MB  采样: %zu 点",
                    pid, cd.peakValue, CurrValue(cd), indices.size());
                cd.subtitle = buf;
                charts.push_back(cd);
            }
            // Network Send
            {
                ChartData cd;
                cd.title = pidLabel + L" — 网络发送";
                cd.yLabel = L"发送 (Mbps)";
                cd.software = baseName;
                cd.pid = std::to_wstring(pid);
                cd.metric = L"网络发送";
                for (size_t ri : indices) {
                    cd.labels.push_back(allProcessData[pi][ri].timestamp);
                    cd.values.push_back(allProcessData[pi][ri].netSendSpeed);
                }
                cd.peakValue = PeakValue(cd);
                wchar_t buf[128];
                swprintf_s(buf, 128, L"PID: %lu  峰值: %.2f Mbps  当前: %.2f Mbps  采样: %zu 点",
                    pid, cd.peakValue, CurrValue(cd), indices.size());
                cd.subtitle = buf;
                charts.push_back(cd);
            }
            // Network Receive
            {
                ChartData cd;
                cd.title = pidLabel + L" — 网络接收";
                cd.yLabel = L"接收 (Mbps)";
                cd.software = baseName;
                cd.pid = std::to_wstring(pid);
                cd.metric = L"网络接收";
                for (size_t ri : indices) {
                    cd.labels.push_back(allProcessData[pi][ri].timestamp);
                    cd.values.push_back(allProcessData[pi][ri].netRecvSpeed);
                }
                cd.peakValue = PeakValue(cd);
                wchar_t buf[128];
                swprintf_s(buf, 128, L"PID: %lu  峰值: %.2f Mbps  当前: %.2f Mbps  采样: %zu 点",
                    pid, cd.peakValue, CurrValue(cd), indices.size());
                cd.subtitle = buf;
                charts.push_back(cd);
            }
        }
    }

    if (charts.empty()) return "";

    // ---- Build HTML ----
    std::string html;
    html += "<!DOCTYPE html>\r\n<!-- saved from url=(0014)about:internet -->\r\n<html lang=\"zh-CN\">\r\n<head>\r\n";
    html += "<meta charset=\"UTF-8\">\r\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n";
    html += "<title>资源监测趋势报告</title>\r\n";
    html += "<style>\r\n";
    html += "*{margin:0;padding:0;box-sizing:border-box}\r\n";
    html += "body{font-family:'Microsoft YaHei','PingFang SC',sans-serif;background:#f5f6fa;color:#2d3436;padding:24px}\r\n";
    html += ".header{max-width:1600px;margin:0 auto 24px;background:#fff;border-radius:12px;padding:24px 32px;box-shadow:0 1px 3px rgba(0,0,0,.08)}\r\n";
    html += ".header h1{font-size:22px;color:#1a1a2e;margin-bottom:6px}\r\n";
    html += ".header .time{font-size:13px;color:#636e72}\r\n";
    html += ".charts{max-width:1600px;margin:0 auto;display:flex;flex-direction:column;gap:24px}\r\n";
    html += ".card{background:#fff;border-radius:12px;padding:24px 28px 16px;box-shadow:0 1px 3px rgba(0,0,0,.08);position:relative}\r\n";
    html += ".card h2{font-size:16px;color:#2d3436;margin-bottom:2px}\r\n";
    html += ".card .sub{font-size:12px;color:#b2bec3;margin-bottom:12px}\r\n";
    html += ".card canvas{width:100%;height:400px;display:block}\r\n";
    html += ".filter-panel{max-width:1600px;margin:0 auto 16px;background:#fff;border-radius:12px;padding:18px 32px;box-shadow:0 1px 3px rgba(0,0,0,.08);position:relative;z-index:10}\r\n";
    html += ".filter-title{font-size:15px;font-weight:bold;color:#1a1a2e;margin-bottom:12px}\r\n";
    html += ".filter-row{display:flex;flex-wrap:wrap;gap:10px 16px;align-items:center}\r\n";
    html += ".f-drop{position:relative;user-select:none}\r\n";
    html += ".f-drop-btn{min-width:140px;padding:7px 28px 7px 10px;border:1px solid #dfe6e9;border-radius:6px;background:#fff;font-size:13px;text-align:left;cursor:pointer;position:relative}\r\n";
    html += ".f-drop-btn::after{content:'▾';position:absolute;right:8px;top:50%;transform:translateY(-50%);font-size:10px;color:#636e72}\r\n";
    html += ".f-drop-btn:hover{border-color:#3498db}\r\n";
    html += ".f-drop-panel{display:none;position:absolute;top:100%;left:0;margin-top:2px;min-width:200px;max-height:260px;overflow-y:auto;background:#fff;border:1px solid #dfe6e9;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,.12);z-index:20;padding:6px 0}\r\n";
    html += ".f-drop-panel.show{display:block}\r\n";
    html += ".f-drop-item{display:flex;align-items:center;gap:6px;padding:5px 12px;cursor:pointer;font-size:13px;color:#2d3436}\r\n";
    html += ".f-drop-item:hover{background:#f0f3f5}\r\n";
    html += ".f-drop-item input[type=checkbox]{margin:0;accent-color:#3498db}\r\n";
    html += ".f-drop-actions{display:flex;gap:8px;padding:6px 12px;border-bottom:1px solid #ecf0f1;margin-bottom:4px}\r\n";
    html += ".f-drop-actions button{font-size:11px;padding:2px 8px;border:1px solid #dfe6e9;border-radius:4px;background:#fff;cursor:pointer;color:#636e72}\r\n";
    html += ".f-drop-actions button:hover{background:#f0f3f5;border-color:#3498db}\r\n";
    html += ".f-drop-confirm{border-top:1px solid #ecf0f1;padding:8px 12px;margin-top:4px;display:flex;gap:8px}\r\n";
    html += ".f-drop-confirm button{padding:5px 16px;border:none;border-radius:5px;font-size:12px;cursor:pointer}\r\n";
    html += ".f-drop-confirm .ok{background:#3498db;color:#fff}\r\n";
    html += ".f-drop-confirm .ok:hover{background:#2980b9}\r\n";
    html += ".f-drop-confirm .cancel{background:#f0f3f5;color:#636e72}\r\n";
    html += ".f-drop-confirm .cancel:hover{background:#dfe6e9}\r\n";
    html += ".f-pid-sw{font-size:10px;color:#b2bec3;margin-left:2px}\r\n";
    html += ".f-checks{display:flex;gap:6px 14px;flex-wrap:wrap}\r\n";
    html += ".f-check{font-size:13px;color:#636e72;display:flex;align-items:center;gap:4px;cursor:pointer}\r\n";
    html += ".f-check input[type=checkbox]{accent-color:#3498db}\r\n";
    html += ".fbtn{padding:7px 16px;border:none;border-radius:6px;background:#3498db;color:#fff;font-size:13px;cursor:pointer;white-space:nowrap}\r\n";
    html += ".fbtn:hover{background:#2980b9}\r\n";
    html += ".filter-info{font-size:12px;color:#b2bec3;margin-top:8px}\r\n";
    html += ".card.hidden{display:none}\r\n";
    html += "/* ---- Zoom & Tooltip ---- */\r\n";
    html += ".brush-overlay{position:absolute;pointer-events:none;z-index:6;display:none;background:rgba(52,152,219,0.12);border:1.5px solid rgba(52,152,219,0.55);border-radius:2px;box-shadow:0 0 0 1px rgba(255,255,255,0.5),inset 0 0 0 1px rgba(255,255,255,0.3)}\r\n";
    html += ".zoom-reset{display:none;position:absolute;top:8px;right:16px;padding:4px 12px;border:1px solid #3498db;border-radius:4px;background:#ebf5fb;color:#3498db;font-size:12px;cursor:pointer;z-index:5}\r\n";
    html += ".zoom-reset:hover{background:#3498db;color:#fff}\r\n";
    html += ".chart-tooltip{display:none;position:fixed;z-index:999;background:rgba(44,62,80,0.92);color:#fff;padding:8px 14px;border-radius:6px;font-size:13px;line-height:1.8;pointer-events:none;white-space:nowrap;box-shadow:0 2px 8px rgba(0,0,0,.2)}\r\n";
    html += ".card canvas{cursor:crosshair}\r\n";
    html += "</style>\r\n</head>\r\n<body>\r\n";

    // ---- Collect software→PIDs mapping for filter dropdowns ----
    std::map<std::wstring, std::set<std::wstring>> swPidMap;  // software → {PID, ...}
    swPidMap[L"系统"].insert(L"");  // system charts
    for (const auto& cd : charts) {
        if (!cd.pid.empty())
            swPidMap[cd.software].insert(cd.pid);
    }

    // ---- Build JS data arrays for filter dropdowns ----
    std::string swDataJs = "var swData=[";
    bool firstSw = true;
    for (const auto& kv : swPidMap) {
        if (!firstSw) swDataJs += ",";
        firstSw = false;
        swDataJs += "{n:'" + EscapeHtml(kv.first) + "',pids:[";
        bool firstPid = true;
        for (const auto& p : kv.second) {
            if (!firstPid) swDataJs += ",";
            firstPid = false;
            swDataJs += "'" + EscapeHtml(p) + "'";
        }
        swDataJs += "]}";
    }
    swDataJs += "];\r\n";
    // PID→software lookup for display hints
    std::string pidSwJs = "var pidSwMap={";
    bool firstPidSw = true;
    for (const auto& kv : swPidMap) {
        for (const auto& p : kv.second) {
            if (p.empty()) continue;
            if (!firstPidSw) pidSwJs += ",";
            firstPidSw = false;
            pidSwJs += "'" + EscapeHtml(p) + "':'" + EscapeHtml(kv.first) + "'";
        }
    }
    pidSwJs += "};\r\n";
    wchar_t endTimeLabel[64] = L"--";
    if (!systemData.empty()) {
        wcscpy_s(endTimeLabel, 64, systemData.back().timestamp);
    }
    html += "<div class=\"header\">\r\n";
    html += "<h1>资源监测趋势报告</h1>\r\n";
    html += "<div style=\"font-size:13px;color:#3498db;margin-bottom:6px\">工具: 挂机电脑资源监测软件 V3.4</div>\r\n";
    html += "<div class=\"time\">监测开始: ";
    html += EscapeHtml(fileTimeLabel);
    html += "  →  结束: ";
    html += EscapeHtml(endTimeLabel);
    html += "  |  采样点数: ";
    char numBuf[32];
    snprintf(numBuf, sizeof(numBuf), "%zu", systemData.size());
    html += numBuf;
    html += "  |  图表数: ";
    snprintf(numBuf, sizeof(numBuf), "%zu", charts.size());
    html += numBuf;
    html += " 个</div>\r\n";
    html += "</div>\r\n";

    // ---- Filter Panel ----
    html += "<div class=\"filter-panel\">\r\n";
    html += "<div class=\"filter-title\">📊 图表过滤</div>\r\n";
    html += "<div class=\"filter-row\">\r\n";
    // Software multi-select dropdown
    html += "<div class=\"f-drop\" id=\"fSwDrop\">\r\n";
    html += "<button class=\"f-drop-btn\" id=\"fSwBtn\" type=\"button\">软件: 全部 ▾</button>\r\n";
    html += "<div class=\"f-drop-panel\" id=\"fSwPanel\"></div>\r\n";
    html += "</div>\r\n";
    // PID multi-select dropdown
    html += "<div class=\"f-drop\" id=\"fPidDrop\">\r\n";
    html += "<button class=\"f-drop-btn\" id=\"fPidBtn\" type=\"button\">PID: 全部 ▾</button>\r\n";
    html += "<div class=\"f-drop-panel\" id=\"fPidPanel\"></div>\r\n";
    html += "</div>\r\n";
    // Metric checkboxes
    html += "<div class=\"f-checks\">\r\n";
    html += "<label class=\"f-check\"><input type=\"checkbox\" class=\"f-metric\" value=\"CPU\"> CPU</label>\r\n";
    html += "<label class=\"f-check\"><input type=\"checkbox\" class=\"f-metric\" value=\"内存\" checked> 内存</label>\r\n";
    html += "<label class=\"f-check\"><input type=\"checkbox\" class=\"f-metric\" value=\"网络发送\"> 网速发送</label>\r\n";
    html += "<label class=\"f-check\"><input type=\"checkbox\" class=\"f-metric\" value=\"网络接收\"> 网速接收</label>\r\n";
    html += "</div>\r\n";
    html += "<button id=\"fClear\" class=\"fbtn\">清除过滤</button>\r\n";
    html += "</div>\r\n";
    html += "<div class=\"filter-info\" id=\"fInfo\"></div>\r\n";
    html += "</div>\r\n";

    html += "<div class=\"tip-box\" id=\"tipBox\"></div>\r\n";

    html += "<div class=\"charts\">\r\n";

    // Colors per chart
    const char* colors[] = {
        "#e74c3c","#3498db","#e67e22","#2ecc71","#9b59b6",
        "#1abc9c","#f39c12","#34495e","#e91e63","#00bcd4"
    };
    int nColors = sizeof(colors) / sizeof(colors[0]);

    for (size_t ci = 0; ci < charts.size(); ci++) {
        const auto& cd = charts[ci];
        html += "<div class=\"card\" data-software=\"";
        html += EscapeHtml(cd.software);
        html += "\" data-pid=\"";
        html += EscapeHtml(cd.pid);
        html += "\" data-metric=\"";
        html += EscapeHtml(cd.metric);
        char peakBuf[32];
        snprintf(peakBuf, sizeof(peakBuf), "%.2f", cd.peakValue);
        html += "\" data-peak=\"";
        html += peakBuf;
        html += "\">\r\n";
        html += "<h2>";
        html += EscapeHtml(cd.title);
        html += "</h2>\r\n";
        html += "<div class=\"sub\">";
        html += EscapeHtml(cd.subtitle);
        html += "</div>\r\n";
        char idxBuf[16];
        snprintf(idxBuf, sizeof(idxBuf), "%zu", ci);
        html += "<button class=\"zoom-reset\" id=\"rz_c";
        html += idxBuf;
        html += "\" onclick=\"window.resetZoom('c";
        html += idxBuf;
        html += "')\">↩ 重置缩放</button>\r\n";
        html += "<div style=\"position:relative\">\r\n";
        html += "<canvas id=\"c";
        html += idxBuf;
        html += "\"></canvas>\r\n";
        html += "<div class=\"brush-overlay\" id=\"bo_c";
        html += idxBuf;
        html += "\"></div>\r\n";
        html += "</div>\r\n";
        html += "</div>\r\n";
    }

    html += "</div>\r\n";
    html += "<div class=\"chart-tooltip\" id=\"chartTip\"></div>\r\n";

    // ---- Inline JavaScript: canvas line chart ----
    html += "<script>\r\n";
    html += "var allCharts=[];\r\n";  // stores draw params for re-render after filter
    html += swDataJs;
    html += pidSwJs;
    html += "// ---- Chart filter logic ----\r\n";
    html += "(function(){\r\n";
    html += "var cards=document.querySelectorAll('.card');\r\n";
    html += "var fInfo=document.getElementById('fInfo');\r\n";
    // Confirmed selections (what's actually used for filtering)
    html += "var selectedSw=new Set(swData.map(function(d){return d.n;}));\r\n";
    html += "var selectedPid=new Set();\r\n";
    html += "swData.forEach(function(d){d.pids.forEach(function(p){selectedPid.add(p);});});\r\n";
    // Pending selections (what's shown in dropdown checkboxes before confirming)
    html += "var pendingSw=new Set(selectedSw);\r\n";
    html += "var pendingPid=new Set(selectedPid);\r\n";

    // Build software dropdown
    html += "var swPanel=document.getElementById('fSwPanel');\r\n";
    html += "var swBtn=document.getElementById('fSwBtn');\r\n";
    html += "function updateSwBtn(){\r\n";
    html += "var cnt=0;swData.forEach(function(d){if(selectedSw.has(d.n))cnt++;});\r\n";
    html += "swBtn.textContent='软件: '+(cnt>=swData.length?'全部':cnt+'/'+swData.length)+' ▾';\r\n";
    html += "}\r\n";
    html += "function buildSwPanel(){\r\n";
    html += "var allSel=swData.every(function(d){return pendingSw.has(d.n);});\r\n";
    html += "var h='<div class=\"f-drop-actions\"><button onclick=\"window.swAll()\">'+(allSel?'取消全选':'全选')+'</button></div>';\r\n";
    html += "swData.forEach(function(d){\r\n";
    html += "var chk=pendingSw.has(d.n)?' checked':'';\r\n";
    html += "h+='<label class=\"f-drop-item\"><input type=\"checkbox\" value=\"'+d.n+'\"'+chk+' onchange=\"window.toggleSw(this)\">'+d.n+'</label>';\r\n";
    html += "});\r\n";
    html += "h+='<div class=\"f-drop-confirm\"><button class=\"ok\" onclick=\"window.confirmSw()\">确认</button><button class=\"cancel\" onclick=\"window.cancelSw()\">取消</button></div>';\r\n";
    html += "swPanel.innerHTML=h;\r\n";
    html += "}\r\n";

    // Build PID dropdown
    html += "var pidPanel=document.getElementById('fPidPanel');\r\n";
    html += "var pidBtn=document.getElementById('fPidBtn');\r\n";
    html += "function updatePidBtn(){\r\n";
    html += "var pidSet=new Set();\r\n";
    html += "swData.forEach(function(d){if(selectedSw.has(d.n))d.pids.forEach(function(p){pidSet.add(p);});});\r\n";
    html += "var cnt=0;pidSet.forEach(function(p){if(selectedPid.has(p))cnt++;});\r\n";
    html += "pidBtn.textContent='PID: '+(cnt>=pidSet.size?'全部':cnt+'/'+pidSet.size)+' ▾';\r\n";
    html += "}\r\n";
    html += "function buildPidPanel(){\r\n";
    html += "var pidSet=new Set();\r\n";
    html += "swData.forEach(function(d){if(selectedSw.has(d.n))d.pids.forEach(function(p){pidSet.add(p);});});\r\n";
    html += "var allPids=Array.from(pidSet).filter(function(p){return p!=='';}).sort(function(a,b){return parseInt(a)-parseInt(b);});\r\n";
    html += "var pidAllSel=Array.from(pidSet).every(function(p){return pendingPid.has(p);});\r\n";
    html += "var sysHas=pidSet.has('') && selectedSw.has('系统');\r\n";
    html += "var h='<div class=\"f-drop-actions\"><button onclick=\"window.pidAll()\">'+(pidAllSel?'取消全选':'全选')+'</button></div>';\r\n";
    html += "if(sysHas)h+='<label class=\"f-drop-item\"><input type=\"checkbox\" value=\"\"'+ (pendingPid.has('')?' checked':'') +' onchange=\"window.togglePid(this)\">系统</label>';\r\n";
    html += "allPids.forEach(function(p){\r\n";
    html += "var chk=pendingPid.has(p)?' checked':'';\r\n";
    html += "var swHint=pidSwMap[p]||'';\r\n";
    html += "var hintHtml=swHint?' <span class=\"f-pid-sw\">('+swHint+')</span>':'';\r\n";
    html += "h+='<label class=\"f-drop-item\"><input type=\"checkbox\" value=\"'+p+'\"'+chk+' onchange=\"window.togglePid(this)\">PID: '+p+hintHtml+'</label>';\r\n";
    html += "});\r\n";
    html += "h+='<div class=\"f-drop-confirm\"><button class=\"ok\" onclick=\"window.confirmPid()\">确认</button><button class=\"cancel\" onclick=\"window.cancelPid()\">取消</button></div>';\r\n";
    html += "pidPanel.innerHTML=h;\r\n";
    html += "}\r\n";

    // Toggle handlers (only update pending, no apply)
    html += "window.toggleSw=function(cb){\r\n";
    html += "if(cb.checked)pendingSw.add(cb.value);else pendingSw.delete(cb.value);\r\n";
    html += "};\r\n";
    html += "window.togglePid=function(cb){\r\n";
    html += "if(cb.checked)pendingPid.add(cb.value);else pendingPid.delete(cb.value);\r\n";
    html += "};\r\n";

    // Confirm / Cancel
    html += "window.confirmSw=function(){\r\n";
    html += "selectedSw=new Set(pendingSw);\r\n";
    html += "updateSwBtn();\r\n";
    html += "swPanel.classList.remove('show');\r\n";
    // Reset PID to all for newly visible software
    html += "pendingPid.clear();swData.forEach(function(d){if(selectedSw.has(d.n))d.pids.forEach(function(p){pendingPid.add(p);});});\r\n";
    html += "selectedPid=new Set(pendingPid);\r\n";
    html += "updatePidBtn();\r\n";
    html += "applyFilter();\r\n";
    html += "};\r\n";
    html += "window.cancelSw=function(){\r\n";
    html += "pendingSw=new Set(selectedSw);\r\n";
    html += "buildSwPanel();\r\n";
    html += "swPanel.classList.remove('show');\r\n";
    html += "};\r\n";
    html += "window.confirmPid=function(){\r\n";
    html += "selectedPid=new Set(pendingPid);\r\n";
    html += "updatePidBtn();\r\n";
    html += "pidPanel.classList.remove('show');\r\n";
    html += "applyFilter();\r\n";
    html += "};\r\n";
    html += "window.cancelPid=function(){\r\n";
    html += "pendingPid=new Set(selectedPid);\r\n";
    html += "buildPidPanel();\r\n";
    html += "pidPanel.classList.remove('show');\r\n";
    html += "};\r\n";

    // Select all / none (only pending)
    html += "window.swAll=function(){\r\n";
    html += "var allSel=swData.every(function(d){return pendingSw.has(d.n);});\r\n";
    html += "if(allSel){swData.forEach(function(d){pendingSw.delete(d.n);});}else{swData.forEach(function(d){pendingSw.add(d.n);});}\r\n";
    html += "buildSwPanel();\r\n";
    html += "};\r\n";
    html += "window.swNone=function(){pendingSw.clear();buildSwPanel();};\r\n";
    html += "window.pidAll=function(){\r\n";
    html += "var pidSet=new Set();\r\n";
    html += "swData.forEach(function(d){if(selectedSw.has(d.n))d.pids.forEach(function(p){pidSet.add(p);});});\r\n";
    html += "var allSel=Array.from(pidSet).every(function(p){return pendingPid.has(p);});\r\n";
    html += "if(allSel){Array.from(pidSet).forEach(function(p){pendingPid.delete(p);});}else{Array.from(pidSet).forEach(function(p){pendingPid.add(p);});}\r\n";
    html += "buildPidPanel();\r\n";
    html += "};\r\n";
    html += "window.pidNone=function(){pendingPid.clear();buildPidPanel();};\r\n";

    // Dropdown toggle — reset pending to selected when opening
    html += "swBtn.addEventListener('click',function(e){e.stopPropagation();\r\n";
    html += "if(swPanel.classList.contains('show')){swPanel.classList.remove('show');return;}\r\n";
    html += "pendingSw=new Set(selectedSw);buildSwPanel();\r\n";
    html += "swPanel.classList.add('show');pidPanel.classList.remove('show');\r\n";
    html += "});\r\n";
    html += "pidBtn.addEventListener('click',function(e){e.stopPropagation();\r\n";
    html += "if(pidPanel.classList.contains('show')){pidPanel.classList.remove('show');return;}\r\n";
    html += "pendingPid=new Set(selectedPid);buildPidPanel();\r\n";
    html += "pidPanel.classList.add('show');swPanel.classList.remove('show');\r\n";
    html += "});\r\n";
    // Prevent clicks inside panels from bubbling to document
    html += "swPanel.addEventListener('click',function(e){e.stopPropagation();});\r\n";
    html += "pidPanel.addEventListener('click',function(e){e.stopPropagation();});\r\n";
    // Click outside panel → close without confirming
    html += "document.addEventListener('click',function(){\r\n";
    html += "if(swPanel.classList.contains('show')){pendingSw=new Set(selectedSw);swPanel.classList.remove('show');}\r\n";
    html += "if(pidPanel.classList.contains('show')){pendingPid=new Set(selectedPid);pidPanel.classList.remove('show');}\r\n";
    html += "});\r\n";

    // Filter function
    html += "function applyFilter(){\r\n";
    html += "var metrics=[];\r\n";
    html += "document.querySelectorAll('.f-metric:checked').forEach(function(c){metrics.push(c.value);});\r\n";
    html += "var visible=0,total=0;\r\n";
    html += "cards.forEach(function(c){\r\n";
    html += "total++;\r\n";
    html += "var swCard=c.getAttribute('data-software');\r\n";
    html += "var pidCard=c.getAttribute('data-pid');\r\n";
    html += "var metricCard=c.getAttribute('data-metric');\r\n";
    html += "var show=true;\r\n";
    html += "if(!selectedSw.has(swCard))show=false;\r\n";
    html += "if(!selectedPid.has(pidCard))show=false;\r\n";
    html += "if(metrics.indexOf(metricCard)===-1)show=false;\r\n";
    html += "if(show){c.classList.remove('hidden');visible++;}else{c.classList.add('hidden');}\r\n";
    html += "});\r\n";
    html += "fInfo.textContent='显示 '+visible+' / '+total+' 个图表';\r\n";
    html += "// 延迟重绘可见图表，确保 display:block 已生效、canvas 尺寸已更新\r\n";
    html += "setTimeout(function(){allCharts.forEach(function(cd){var c=document.getElementById(cd.id);if(!c)return;var p=c.closest('.card');if(p&&p.classList.contains('hidden'))return;window.draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);});},60);\r\n";
    html += "}\r\n";

    // Clear
    html += "document.getElementById('fClear').addEventListener('click',function(){\r\n";
    html += "selectedSw.clear();swData.forEach(function(d){selectedSw.add(d.n);});\r\n";
    html += "pendingSw=new Set(selectedSw);\r\n";
    html += "selectedPid.clear();swData.forEach(function(d){d.pids.forEach(function(p){selectedPid.add(p);});});\r\n";
    html += "pendingPid=new Set(selectedPid);\r\n";
    html += "document.querySelectorAll('.f-metric').forEach(function(c){c.checked=true;});\r\n";
    html += "updateSwBtn();updatePidBtn();\r\n";
    html += "applyFilter();\r\n";
    html += "});\r\n";

    // Metric checkbox change
    html += "document.querySelectorAll('.f-metric').forEach(function(c){c.addEventListener('change',applyFilter);});\r\n";

    // Init
    html += "updateSwBtn();updatePidBtn();\r\n";
    html += "applyFilter();\r\n";
    html += "})();\r\n";
    html += "\r\n";
    html += "// ---- Chart drawing ----\r\n";
    html += "(function(){\r\n";
    html += "var draw=function(canvasId,labels,data,color,yLabel){\r\n";
    html += "var c=document.getElementById(canvasId);if(!c)return;\r\n";
    html += "c.style.width='';c.style.height='';\r\n";  // 清除上次可能写入的0px内联样式，让CSS width:100%生效
    html += "var dpr=window.devicePixelRatio||1;\r\n";
    html += "var W=c.clientWidth,H=c.clientHeight;\r\n";
    html += "c.width=W*dpr;c.height=H*dpr;\r\n";
    html += "c.style.width=W+'px';c.style.height=H+'px';\r\n";
    html += "var ctx=c.getContext('2d');ctx.scale(dpr,dpr);\r\n";
    html += "var n=data.length;if(n<2)return;\r\n";
    html += "var pad={t:16,r:24,b:120,l:70};\r\n";
    html += "var pw=W-pad.l-pad.r,ph=H-pad.t-pad.b;\r\n";

    // Find min/max — proportional margin, no fixed-range distortion
    html += "var ymin=1e99,ymax=-1e99;\r\n";
    html += "for(var i=0;i<n;i++){var v=data[i];if(v<ymin)ymin=v;if(v>ymax)ymax=v;}\r\n";
    html += "var yr=ymax-ymin;if(yr<1e-6)yr=1;\r\n";
    html += "var margin=yr*0.12;ymin-=margin;ymax+=margin;\r\n";
    html += "if(ymin<0)ymin=0;\r\n";

    // Background
    html += "ctx.fillStyle='#fff';ctx.fillRect(0,0,W,H);\r\n";

    // ---- Y-axis grid + ticks ----
    html += "var ySteps=5;\r\n";
    html += "ctx.strokeStyle='#ecf0f1';ctx.lineWidth=1;\r\n";
    html += "ctx.font='13px sans-serif';ctx.fillStyle='#95a5a6';ctx.textAlign='right';\r\n";
    html += "for(var i=0;i<=ySteps;i++){\r\n";
    html += "var yv=ymin+(ymax-ymin)*i/ySteps;\r\n";
    html += "var yp=pad.t+ph*(1-i/ySteps);\r\n";
    html += "ctx.beginPath();ctx.moveTo(pad.l,yp);ctx.lineTo(pad.l+pw,yp);ctx.stroke();\r\n";
    html += "ctx.fillText(yv.toFixed(1),pad.l-8,yp+4);\r\n";
    html += "}\r\n";

    // ---- Y axis label ----
    html += "ctx.save();ctx.translate(12,H/2);ctx.rotate(-Math.PI/2);\r\n";
    html += "ctx.font='bold 14px sans-serif';ctx.fillStyle='#636e72';ctx.textAlign='center';\r\n";
    html += "ctx.fillText(yLabel,0,0);ctx.restore();\r\n";

    // ---- X axis line ----
    html += "ctx.strokeStyle='#bdc3c7';ctx.lineWidth=1;\r\n";
    html += "ctx.beginPath();ctx.moveTo(pad.l,pad.t+ph);ctx.lineTo(pad.l+pw,pad.t+ph);ctx.stroke();\r\n";

    // ---- X axis labels (-45° angled, full timestamp, below axis line) ----
    html += "var xTickCount=Math.min(8,Math.max(4,Math.ceil(n/20)));\r\n";
    html += "var xStep=Math.max(1,Math.floor(n/xTickCount));\r\n";
    html += "ctx.font='10px sans-serif';ctx.fillStyle='#7f8c8d';ctx.textAlign='right';\r\n";
    html += "for(var i=0;i<n;i+=xStep){\r\n";
    html += "var xp=pad.l+pw*i/(n-1);\r\n";
    html += "ctx.save();ctx.translate(xp,pad.t+ph+10);ctx.rotate(-Math.PI/4);\r\n";
    html += "ctx.fillText(labels[i],0,0);\r\n";
    html += "ctx.restore();\r\n";
    html += "}\r\n";

    // ---- X axis title (horizontal, below all labels) ----
    html += "ctx.font='bold 13px sans-serif';ctx.fillStyle='#636e72';ctx.textAlign='center';\r\n";
    html += "ctx.fillText('时间',pad.l+pw/2,H-6);\r\n";

    // ---- Data line + area fill ----
    html += "ctx.strokeStyle=color;ctx.lineWidth=2.5;ctx.lineJoin='round';\r\n";
    html += "ctx.beginPath();\r\n";
    html += "for(var i=0;i<n;i++){\r\n";
    html += "var xp=pad.l+pw*i/(n-1);\r\n";
    html += "var yp=pad.t+ph*(1-(data[i]-ymin)/(ymax-ymin));\r\n";
    html += "if(i===0)ctx.moveTo(xp,yp);else ctx.lineTo(xp,yp);\r\n";
    html += "}\r\nctx.stroke();\r\n";

    html += "ctx.globalAlpha=0.08;\r\n";
    html += "ctx.fillStyle=color;ctx.lineTo(pad.l+pw,pad.t+ph);\r\n";
    html += "ctx.lineTo(pad.l,pad.t+ph);ctx.closePath();ctx.fill();\r\n";
    html += "ctx.globalAlpha=1;\r\n";

    html += "c._geo={pad:pad,pw:pw,ph:ph,ymin:ymin,ymax:ymax,n:n,labels:labels,data:data,color:color,yLabel:yLabel};\r\n";
    html += "}\r\n";
    html += "window.draw=draw;\r\n";  // expose for filter redraw

    // Build chartDefs array: stores all chart params for initial draw + re-draw after filter
    html += "var chartDefs=[\r\n";
    for (size_t ci = 0; ci < charts.size(); ci++) {
        const auto& cd = charts[ci];
        if (ci > 0) html += ",";
        html += "{id:'c";
        char ib[16];
        snprintf(ib, sizeof(ib), "%zu", ci);
        html += ib;
        html += "',labels:[";

        int step = 1;
        size_t n = cd.labels.size();
        if (n > 500) step = (int)(n / 500) + 1;
        for (size_t i = 0; i < n; i += step) {
            if (i > 0) html += ",";
            html += "'";
            html += FormatTimestamp(cd.labels[i].c_str());
            html += "'";
        }
        html += "],data:[";
        for (size_t i = 0; i < n; i += step) {
            if (i > 0) html += ",";
            char vb[32];
            snprintf(vb, sizeof(vb), "%.2f", cd.values[i]);
            html += vb;
        }
        html += "],color:'";
        html += colors[ci % nColors];
        html += "',yLabel:'";
        html += EscapeHtml(cd.yLabel);
        html += "',fullLabels:null,fullData:null,zoomed:false}\r\n";
    }
    html += "];\r\n";
    // Draw all charts initially
    html += "chartDefs.forEach(function(cd){cd.fullLabels=cd.labels.slice();cd.fullData=cd.data.slice();cd.zoomed=false;});\r\n";
    html += "chartDefs.forEach(function(cd){draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);});\r\n";
    html += "allCharts=chartDefs;\r\n";

    // === Brush Zoom + Click Tooltip (V3.4) ===
    html += "var tipBox=document.getElementById('chartTip');\r\n";
    html += "var brush={on:false,c:null,sx:0,cx:0,si:0,ei:0,cd:null};\r\n";
    html += "function gpos(c,ev){var r=c.getBoundingClientRect();return{x:ev.clientX-r.left,y:ev.clientY-r.top};}\r\n";
    html += "function fidx(mx,g){return Math.max(0,Math.min(g.n-1,Math.round((mx-g.pad.l)/g.pw*(g.n-1))));}\r\n";

    // Bind events to each chart
    html += "chartDefs.forEach(function(cd){\r\n";
    html += "var c=document.getElementById(cd.id);if(!c)return;\r\n";
    html += "var bo=document.getElementById('bo_'+cd.id);\r\n";

    // mousedown: start brush
    html += "c.addEventListener('mousedown',function(ev){\r\n";
    html += "if(!c._geo)return;var p=gpos(c,ev);var g=c._geo;\r\n";
    html += "if(p.x<g.pad.l||p.x>g.pad.l+g.pw||p.y<g.pad.t||p.y>g.pad.t+g.ph)return;\r\n";
    html += "tipBox.style.display='none';brush.on=true;brush.c=c;brush.sx=p.x;brush.cx=p.x;brush.cd=cd;\r\n";
    html += "brush.si=fidx(p.x,g);brush.ei=brush.si;ev.preventDefault();\r\n";
    html += "});\r\n";

    // mousemove: update brush overlay div (NO canvas redraw!)
    html += "c.addEventListener('mousemove',function(ev){\r\n";
    html += "if(!brush.on||brush.cd!==cd)return;\r\n";
    html += "var g=brush.c._geo;var p=gpos(brush.c,ev);\r\n";
    html += "brush.cx=Math.max(g.pad.l,Math.min(g.pad.l+g.pw,p.x));\r\n";
    html += "brush.ei=fidx(brush.cx,g);\r\n";
    html += "var x1=Math.min(brush.sx,brush.cx),x2=Math.max(brush.sx,brush.cx);\r\n";
    html += "bo.style.left=x1+'px';bo.style.top=g.pad.t+'px';\r\n";
    html += "bo.style.width=(x2-x1)+'px';bo.style.height=g.ph+'px';\r\n";
    html += "bo.style.display='block';\r\n";
    html += "});\r\n";

    // mouseup: complete brush zoom or show tooltip
    html += "c.addEventListener('mouseup',function(ev){\r\n";
    html += "if(!brush.on)return;\r\n";
    html += "var g=brush.c._geo;var dx=Math.abs(brush.cx-brush.sx);\r\n";
    html += "bo.style.display='none';brush.on=false;\r\n";
    html += "if(dx<5){\r\n";
    // Click → show tooltip at nearest point (no dot!)
    html += "var idx=fidx(brush.sx,g);\r\n";
    html += "var lbl=g.labels[idx];var val=g.data[idx].toFixed(2);\r\n";
    html += "tipBox.innerHTML='<b>'+lbl+'</b><br>'+g.yLabel+': <b>'+val+'</b>';\r\n";
    html += "var cx=ev.clientX,cy=ev.clientY;\r\n";
    html += "tipBox.style.display='block';tipBox.style.left='';tipBox.style.top='';\r\n";
    // Read size after display:block
    html += "var tw=tipBox.offsetWidth,th=tipBox.offsetHeight;\r\n";
    html += "var tx=cx+14,ty=cy-th-10;\r\n";
    html += "if(tx+tw>window.innerWidth-8)tx=cx-tw-14;\r\n";
    html += "if(ty<8)ty=cy+14;\r\n";
    html += "tipBox.style.left=tx+'px';tipBox.style.top=ty+'px';\r\n";
    html += "clearTimeout(tipBox._t);tipBox._t=setTimeout(function(){tipBox.style.display='none';},4000);\r\n";
    html += "draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);\r\n";
    html += "}else{\r\n";
    // Brush → apply zoom
    html += "var i1=Math.min(brush.si,brush.ei),i2=Math.max(brush.si,brush.ei);\r\n";
    html += "if(i2-i1>=2)doZoom(cd,i1,i2);\r\n";
    html += "}\r\n";
    html += "});\r\n";

    // mouseleave: cancel brush
    html += "c.addEventListener('mouseleave',function(ev){\r\n";
    html += "if(brush.on&&brush.cd===cd){bo.style.display='none';brush.on=false;}\r\n";
    html += "});\r\n";

    // double-click: reset zoom
    html += "c.addEventListener('dblclick',function(ev){window.resetZoom(cd.id);});\r\n";
    html += "});\r\n";

    // doZoom: slice data and redraw (handles multi-level zoom via offset tracking)
    html += "function doZoom(cd,i1,i2){\r\n";
    html += "if(!cd.fullLabels){cd.fullLabels=cd.labels.slice();cd.fullData=cd.data.slice();cd._off=0;}\r\n";
    html += "var off=cd._off||0;\r\n";
    html += "var fi1=off+i1,fi2=off+i2;\r\n";
    html += "cd.labels=cd.fullLabels.slice(fi1,fi2+1);cd.data=cd.fullData.slice(fi1,fi2+1);\r\n";
    html += "cd._off=fi1;cd.zoomed=true;\r\n";
    html += "var rz=document.getElementById('rz_'+cd.id);if(rz)rz.style.display='inline-block';\r\n";
    html += "draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);}\r\n";

    // resetZoom
    html += "window.resetZoom=function(cid){\r\n";
    html += "var cd=null;for(var i=0;i<allCharts.length;i++){if(allCharts[i].id===cid){cd=allCharts[i];break;}}\r\n";
    html += "if(!cd||!cd.zoomed)return;cd.labels=cd.fullLabels.slice();cd.data=cd.fullData.slice();cd.zoomed=false;cd._off=0;\r\n";
    html += "var rz=document.getElementById('rz_'+cid);if(rz)rz.style.display='none';\r\n";
    html += "draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);}\r\n";

    // Hide tooltip on outside click or scroll
    html += "document.addEventListener('click',function(ev){if(!ev.target.closest('canvas'))tipBox.style.display='none';});\r\n";
    html += "window.addEventListener('scroll',function(){tipBox.style.display='none';},{passive:true});\r\n";

    // Resize → redraw all visible charts
    html += "var rtimer=null;window.addEventListener('resize',function(){clearTimeout(rtimer);rtimer=setTimeout(function(){allCharts.forEach(function(cd){var c=document.getElementById(cd.id);if(!c)return;var p=c.closest('.card');if(p&&p.classList.contains('hidden'))return;draw(cd.id,cd.labels,cd.data,cd.color,cd.yLabel);});},200);});\r\n";
    html += "})();\r\n";
    html += "</script>\r\n</body>\r\n</html>";

    return html;
}

// ============================================================================
// Export — write HTML to file
// ============================================================================
std::wstring HtmlChartExporter::Export(
    const wchar_t* outputDir, double startTimestamp,
    const std::vector<SystemMonitorData>& systemData,
    const std::vector<MonitorProcess>& processes,
    const std::vector<std::vector<ProcessMonitorData>>& allProcessData)
{
    std::string html = BuildHtml(startTimestamp, systemData, processes, allProcessData);
    if (html.empty()) return L"";

    time_t startT = (time_t)startTimestamp;
    struct tm tm_start;
    localtime_s(&tm_start, &startT);
    wchar_t timestamp[32];
    wcsftime(timestamp, 32, L"%Y%m%d%H%M%S", &tm_start);
    wchar_t filePath[MAX_PATH];
    swprintf_s(filePath, MAX_PATH, L"%s\\monitor_data_%s.html", outputDir, timestamp);

    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return L"";

    DWORD written = 0;
    WriteFile(hFile, html.data(), (DWORD)html.size(), &written, nullptr);
    CloseHandle(hFile);

    return (written == html.size()) ? filePath : L"";
}
