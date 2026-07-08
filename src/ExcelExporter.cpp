// ExcelExporter.cpp - XLSX export (multi-sheet, one process per sheet)
//
// Generates a valid .xlsx file directly without external libraries.
// XLSX = ZIP of XML files (store, no compression).
// Uses inline strings (no sharedStrings.xml needed).
//
#include "ExcelExporter.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")

// ============================================================================
// CRC-32 (standard ZIP / PKZIP polynomial)
// ============================================================================
static uint32_t g_crcTable[256];
static bool     g_crcReady = false;

static void BuildCrcTable() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        g_crcTable[i] = crc;
    }
    g_crcReady = true;
}

static uint32_t Crc32(const void* data, size_t len) {
    if (!g_crcReady) BuildCrcTable();
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ g_crcTable[(crc ^ p[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================================
// Minimal ZIP writer (store-only, single-disk)
// ============================================================================
struct ZipEntry {
    std::string name;   // UTF-8 path within ZIP (forward slashes)
    std::string data;   // raw file content
};

#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t sig;           // 0x04034b50
    uint16_t verNeed;       // 20
    uint16_t flags;         // 0
    uint16_t compr;         // 0 (store)
    uint16_t modTime;       // 0
    uint16_t modDate;       // 0
    uint32_t crc32;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t extraLen;      // 0
};
struct ZipCentralDir {
    uint32_t sig;           // 0x02014b50
    uint16_t verMade;       // 20
    uint16_t verNeed;       // 20
    uint16_t flags;         // 0
    uint16_t compr;         // 0
    uint16_t modTime;       // 0
    uint16_t modDate;       // 0
    uint32_t crc32;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t extraLen;      // 0
    uint16_t commentLen;    // 0
    uint16_t diskStart;     // 0
    uint16_t intAttr;       // 0
    uint32_t extAttr;       // 0
    uint32_t localOff;
};
struct ZipEOCD {
    uint32_t sig;           // 0x06054b50
    uint16_t diskNum;       // 0
    uint16_t startDisk;     // 0
    uint16_t entriesDisk;   // N
    uint16_t totalEntries;  // N
    uint32_t centralSize;
    uint32_t centralOff;
    uint16_t commentLen;    // 0
};
#pragma pack(pop)

static bool WriteZipFile(const wchar_t* path, const std::vector<ZipEntry>& entries) {
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    auto wr = [&](const void* p, DWORD sz) -> bool {
        return WriteFile(hFile, p, sz, &written, nullptr) && written == sz;
    };

    std::vector<uint32_t> offsets;
    offsets.reserve(entries.size());

    // ---- Local file headers + data ----
    for (const auto& e : entries) {
        uint32_t crc = Crc32(e.data.data(), e.data.size());
        offsets.push_back(SetFilePointer(hFile, 0, nullptr, FILE_CURRENT));

        ZipLocalHeader hdr = {};
        hdr.sig        = 0x04034b50;
        hdr.verNeed    = 20;
        hdr.crc32      = crc;
        hdr.compSize   = (uint32_t)e.data.size();
        hdr.uncompSize = (uint32_t)e.data.size();
        hdr.nameLen    = (uint16_t)e.name.size();

        if (!wr(&hdr, sizeof(hdr)))        { CloseHandle(hFile); return false; }
        if (!wr(e.name.data(), hdr.nameLen)) { CloseHandle(hFile); return false; }
        if (!wr(e.data.data(), hdr.compSize)) { CloseHandle(hFile); return false; }
    }

    uint32_t centralOff = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);

    // ---- Central directory ----
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        uint32_t crc = Crc32(e.data.data(), e.data.size());

        ZipCentralDir cd = {};
        cd.sig        = 0x02014b50;
        cd.verMade    = 20;
        cd.verNeed    = 20;
        cd.crc32      = crc;
        cd.compSize   = (uint32_t)e.data.size();
        cd.uncompSize = (uint32_t)e.data.size();
        cd.nameLen    = (uint16_t)e.name.size();
        cd.localOff   = offsets[i];

        if (!wr(&cd, sizeof(cd)))          { CloseHandle(hFile); return false; }
        if (!wr(e.name.data(), cd.nameLen)) { CloseHandle(hFile); return false; }
    }

    uint32_t centralEnd = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);

    // ---- EOCD ----
    ZipEOCD eocd = {};
    eocd.sig          = 0x06054b50;
    eocd.entriesDisk  = (uint16_t)entries.size();
    eocd.totalEntries = (uint16_t)entries.size();
    eocd.centralSize  = centralEnd - centralOff;
    eocd.centralOff   = centralOff;

    wr(&eocd, sizeof(eocd));
    CloseHandle(hFile);

    // ---- Verify: re-open and validate ZIP signatures ----
    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        uint32_t sig = 0;
        DWORD rd;
        // Check first 4 bytes = local file header signature
        ReadFile(hFile, &sig, 4, &rd, nullptr);
        if (sig != 0x04034b50) {
            CloseHandle(hFile);
            return false;
        }
        // Seek to EOCD (last 22 bytes of file)
        LARGE_INTEGER li;
        li.QuadPart = -22;
        SetFilePointerEx(hFile, li, nullptr, FILE_END);
        sig = 0;
        ReadFile(hFile, &sig, 4, &rd, nullptr);
        CloseHandle(hFile);
        if (sig != 0x06054b50) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// XML helpers
// ============================================================================
static std::string XmlEscape(const std::wstring& ws) {
    // Convert wstring → UTF-8, escaping XML special chars
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
        case '\'': out += "&apos;"; break;
        default:   out += buf[i];   break;
        }
    }
    free(buf);
    return out;
}

// ============================================================================
// XLSX sheet XML builder
// ============================================================================
static std::string BuildSheetXml(const std::vector<std::wstring>& headers,
                                  const std::vector<std::vector<std::wstring>>& rows) {
    // Column letters A-Z (supports up to 26 columns)
    const char COL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
    xml += "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\r\n";
    xml += "<cols>\r\n";
    for (size_t c = 0; c < headers.size(); c++) {
        // Calculate column width based on header text length
        // CJK characters are ~2 Excel units wide, ASCII ~1 unit
        double width = 0;
        for (size_t k = 0; k < headers[c].length(); k++) {
            width += (headers[c][k] > 0x7F) ? 2.2 : 1.1;
        }
        width += 3.0;  // padding
        if (width < 8.0) width = 8.0;  // minimum width

        char buf[128];
        snprintf(buf, sizeof(buf),
            "<col min=\"%d\" max=\"%d\" width=\"%.1f\" bestFit=\"1\" customWidth=\"1\"/>\r\n",
            (int)(c + 1), (int)(c + 1), width);
        xml += buf;
    }
    xml += "</cols>\r\n";
    xml += "<sheetData>\r\n";

    // Header row
    xml += "<row r=\"1\">\r\n";
    for (size_t c = 0; c < headers.size(); c++) {
        char ref[16];
        snprintf(ref, sizeof(ref), "%c1", COL[c]);
        xml += "<c r=\"";
        xml += ref;
        xml += "\" t=\"inlineStr\" s=\"1\"><is><t>";
        xml += XmlEscape(headers[c]);
        xml += "</t></is></c>\r\n";
    }
    xml += "</row>\r\n";

    // Data rows
    for (size_t r = 0; r < rows.size(); r++) {
        char rowTag[32];
        snprintf(rowTag, sizeof(rowTag), "<row r=\"%u\">\r\n", (unsigned)(r + 2));
        xml += rowTag;
        for (size_t c = 0; c < headers.size() && c < rows[r].size(); c++) {
            char ref[16];
            snprintf(ref, sizeof(ref), "%c%u", COL[c], (unsigned)(r + 2));
            xml += "<c r=\"";
            xml += ref;
            xml += "\" t=\"inlineStr\" s=\"0\"><is><t>";
            xml += XmlEscape(rows[r][c]);
            xml += "</t></is></c>\r\n";
        }
        xml += "</row>\r\n";
    }

    xml += "</sheetData>\r\n";
    xml += "</worksheet>";
    return xml;
}

// ============================================================================
// XLSX boilerplate XML
// ============================================================================
static std::string BuildContentTypes(int sheetCount) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
    xml += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\r\n";
    xml += "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\r\n";
    xml += "<Default Extension=\"xml\"  ContentType=\"application/xml\"/>\r\n";
    xml += "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\r\n";
    xml += "<Override PartName=\"/xl/styles.xml\"  ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>\r\n";
    for (int i = 1; i <= sheetCount; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\r\n", i);
        xml += buf;
    }
    xml += "</Types>";
    return xml;
}

static std::string BuildRels() {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\r\n"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\r\n"
        "</Relationships>";
}

static std::string BuildWorkbookRels(int sheetCount) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
    xml += "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\r\n";
    for (int i = 1; i <= sheetCount; i++) {
        char buf[192];
        snprintf(buf, sizeof(buf),
            "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%d.xml\"/>\r\n",
            i, i);
        xml += buf;
    }
    xml += "<Relationship Id=\"rId99\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>\r\n";
    xml += "</Relationships>";
    return xml;
}

static std::string BuildWorkbookXml(const std::vector<std::wstring>& sheetNames) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
    xml += "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\r\n";
    xml += "<sheets>\r\n";
    for (size_t i = 0; i < sheetNames.size(); i++) {
        char buf[256];
        std::string escName = XmlEscape(sheetNames[i]);
        snprintf(buf, sizeof(buf),
            "<sheet name=\"%s\" sheetId=\"%zu\" r:id=\"rId%zu\"/>\r\n",
            escName.c_str(), i + 1, i + 1);
        xml += buf;
    }
    xml += "</sheets>\r\n";
    xml += "</workbook>";
    return xml;
}

static std::string BuildStylesXml() {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\r\n"
        "<fonts count=\"2\">\r\n"
        "<font><sz val=\"11\"/><name val=\"Microsoft YaHei\"/></font>\r\n"
        "<font><b/><sz val=\"11\"/><name val=\"Microsoft YaHei\"/></font>\r\n"
        "</fonts>\r\n"
        "<fills count=\"2\">\r\n"
        "<fill><patternFill patternType=\"none\"/></fill>\r\n"
        "<fill><patternFill patternType=\"gray125\"/></fill>\r\n"
        "</fills>\r\n"
        "<borders count=\"1\">\r\n"
        "<border><left/><right/><top/><bottom/><diagonal/></border>\r\n"
        "</borders>\r\n"
        "<cellStyleXfs count=\"1\">\r\n"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\r\n"
        "</cellStyleXfs>\r\n"
        "<cellXfs count=\"2\">\r\n"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment horizontal=\"center\" vertical=\"center\"/></xf>\r\n"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\">"
        "<alignment horizontal=\"center\" vertical=\"center\"/></xf>\r\n"
        "</cellXfs>\r\n"
        "</styleSheet>";
}

// ============================================================================
// ExcelExporter implementation
// ============================================================================
ExcelExporter::ExcelExporter() {
    wcscpy_s(m_netUnit, 16, L"KB/s");
    m_hFile = INVALID_HANDLE_VALUE;
    m_lastFilePath[0] = L'\0';
}

ExcelExporter::~ExcelExporter() {}

void ExcelExporter::SetNetUnit(const wchar_t* unit) {
    wcscpy_s(m_netUnit, 16, unit);
}

// Remove .exe suffix and sanitize for Excel sheet name
static std::wstring MakeSheetName(const wchar_t* processName) {
    std::wstring name = processName;
    // Remove .exe extension
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = name.substr(dot);
        if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".com") == 0)
            name = name.substr(0, dot);
    }
    // Excel sheet name: max 31 chars, no brackets, colons, etc.
    if (name.length() > 31) name = name.substr(0, 31);
    for (auto& ch : name) {
        if (ch == L'[' || ch == L']' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'/' || ch == L'\\')
            ch = L'_';
    }
    return name;
}

bool ExcelExporter::Export(const wchar_t* outputDir, double startTimestamp,
                           const std::vector<SystemMonitorData>& systemData,
                           const std::vector<MonitorProcess>& processes,
                           const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
                           const wchar_t* netUnit)
{
    // Build filename with timestamp
    time_t startT = (time_t)startTimestamp;
    struct tm tm_start;
    localtime_s(&tm_start, &startT);

    wchar_t timestamp[32];
    wcsftime(timestamp, 32, L"%Y%m%d%H%M%S", &tm_start);

    wchar_t filePath[MAX_PATH];
    swprintf_s(filePath, MAX_PATH, L"%s\\monitor_data_%s.xlsx", outputDir, timestamp);
    wcscpy_s(m_lastFilePath, MAX_PATH, filePath);

    // Normalize path separators
    for (int i = 0; filePath[i]; i++) {
        if (filePath[i] == L'/') filePath[i] = L'\\';
    }

    // ---- Build sheet list ----
    std::vector<std::wstring> sheetNames;
    std::vector<std::vector<std::wstring>> sheetHeaders;
    std::vector<std::vector<std::vector<std::wstring>>> sheetRows;

    // Sheet 1: 系统资源
    {
        sheetNames.push_back(L"系统资源");
        std::vector<std::wstring> headers = {
            L"时间戳", L"运行时间(秒)", L"CPU使用率(%)", L"内存总量(GB)",
            L"内存可用(GB)", L"内存使用(GB)", L"内存使用率(%)"
        };
        wchar_t netHdr[64];
        swprintf_s(netHdr, 64, L"网络发送(%s)", netUnit);
        headers.push_back(netHdr);
        swprintf_s(netHdr, 64, L"网络接收(%s)", netUnit);
        headers.push_back(netHdr);
        sheetHeaders.push_back(headers);

        std::vector<std::vector<std::wstring>> rows;
        for (const auto& d : systemData) {
            std::vector<std::wstring> row;
            row.push_back(d.timestamp);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%.2f", d.runSeconds);    row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.cpuUsage);       row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryTotalGB);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryAvailableGB); row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryUsedGB);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryUsage);    row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);   row.push_back(buf);
            rows.push_back(row);
        }
        sheetRows.push_back(rows);
    }

    // Process sheets — one per monitored process
    for (size_t pi = 0; pi < processes.size(); pi++) {
        if (!processes[pi].enabled) continue;
        if (pi >= allProcessData.size()) break;

        sheetNames.push_back(MakeSheetName(processes[pi].name));

        std::vector<std::wstring> headers = {
            L"时间戳", L"运行时间(秒)", L"进程ID", L"CPU使用率(%)",
            L"内存使用率(%)", L"内存使用(MB)"
        };
        wchar_t netHdr[64];
        swprintf_s(netHdr, 64, L"网络发送(%s)", netUnit);
        headers.push_back(netHdr);
        swprintf_s(netHdr, 64, L"网络接收(%s)", netUnit);
        headers.push_back(netHdr);
        sheetHeaders.push_back(headers);

        std::vector<std::vector<std::wstring>> rows;
        for (const auto& d : allProcessData[pi]) {
            std::vector<std::wstring> row;
            row.push_back(d.timestamp);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%.2f",  d.runSeconds);    row.push_back(buf);
            swprintf_s(buf, 64, L"%lu",    d.pid);           row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.cpuUsage);      row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.memoryUsage);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.memoryUsedMB);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.netSendSpeed);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.netRecvSpeed);  row.push_back(buf);
            rows.push_back(row);
        }
        sheetRows.push_back(rows);
    }

    int totalSheets = (int)sheetNames.size();

    // ---- Build ZIP entries ----
    std::vector<ZipEntry> zip;

    // [Content_Types].xml
    zip.push_back({"[Content_Types].xml", BuildContentTypes(totalSheets)});

    // _rels/.rels
    zip.push_back({"_rels/.rels", BuildRels()});

    // xl/workbook.xml
    zip.push_back({"xl/workbook.xml", BuildWorkbookXml(sheetNames)});

    // xl/_rels/workbook.xml.rels
    zip.push_back({"xl/_rels/workbook.xml.rels", BuildWorkbookRels(totalSheets)});

    // xl/styles.xml
    zip.push_back({"xl/styles.xml", BuildStylesXml()});

    // xl/worksheets/sheetN.xml
    for (int i = 0; i < totalSheets; i++) {
        char name[64];
        snprintf(name, sizeof(name), "xl/worksheets/sheet%d.xml", i + 1);
        zip.push_back({name, BuildSheetXml(sheetHeaders[i], sheetRows[i])});
    }

    // ---- Write .xlsx (ZIP) file ----
    return WriteZipFile(filePath, zip);
}

// ============================================================================
// Real-time export — file-locked streaming
// ============================================================================
static bool WriteZipToHandle(HANDLE hFile, const std::vector<ZipEntry>& entries) {
    // Seek to beginning and truncate
    SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(hFile);

    DWORD written;
    auto wr = [&](const void* p, DWORD sz) -> bool {
        return WriteFile(hFile, p, sz, &written, nullptr) && written == sz;
    };

    std::vector<uint32_t> offsets;
    offsets.reserve(entries.size());

    // ---- Local file headers + data ----
    for (const auto& e : entries) {
        uint32_t crc = Crc32(e.data.data(), e.data.size());
        offsets.push_back(SetFilePointer(hFile, 0, nullptr, FILE_CURRENT));

        ZipLocalHeader hdr = {};
        hdr.sig        = 0x04034b50;
        hdr.verNeed    = 20;
        hdr.crc32      = crc;
        hdr.compSize   = (uint32_t)e.data.size();
        hdr.uncompSize = (uint32_t)e.data.size();
        hdr.nameLen    = (uint16_t)e.name.size();

        if (!wr(&hdr, sizeof(hdr)))           return false;
        if (!wr(e.name.data(), hdr.nameLen))  return false;
        if (!wr(e.data.data(), hdr.compSize)) return false;
    }

    uint32_t centralOff = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);

    // ---- Central directory ----
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        uint32_t crc = Crc32(e.data.data(), e.data.size());

        ZipCentralDir cd = {};
        cd.sig        = 0x02014b50;
        cd.verMade    = 20;
        cd.verNeed    = 20;
        cd.crc32      = crc;
        cd.compSize   = (uint32_t)e.data.size();
        cd.uncompSize = (uint32_t)e.data.size();
        cd.nameLen    = (uint16_t)e.name.size();
        cd.localOff   = offsets[i];

        if (!wr(&cd, sizeof(cd)))           return false;
        if (!wr(e.name.data(), cd.nameLen)) return false;
    }

    uint32_t centralEnd = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);

    // ---- EOCD ----
    ZipEOCD eocd = {};
    eocd.sig          = 0x06054b50;
    eocd.entriesDisk  = (uint16_t)entries.size();
    eocd.totalEntries = (uint16_t)entries.size();
    eocd.centralSize  = centralEnd - centralOff;
    eocd.centralOff   = centralOff;

    if (!wr(&eocd, sizeof(eocd))) return false;
    FlushFileBuffers(hFile);
    return true;
}

bool ExcelExporter::BeginExport(const wchar_t* outputDir, double startTimestamp) {
    // Build filename
    time_t startT = (time_t)startTimestamp;
    struct tm tm_start;
    localtime_s(&tm_start, &startT);
    wchar_t timestamp[32];
    wcsftime(timestamp, 32, L"%Y%m%d%H%M%S", &tm_start);
    swprintf_s(m_lastFilePath, MAX_PATH, L"%s\\monitor_data_%s.xlsx", outputDir, timestamp);

    // Open file with write lock (FILE_SHARE_READ → external open is read-only)
    m_hFile = CreateFileW(m_lastFilePath, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_hFile == INVALID_HANDLE_VALUE) return false;

    return true;
}

static void BuildZipEntries(
    std::vector<ZipEntry>& zip,
    const std::vector<SystemMonitorData>& systemData,
    const std::vector<MonitorProcess>& processes,
    const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
    const wchar_t* netUnit)
{
    std::vector<std::wstring> sheetNames;
    std::vector<std::vector<std::wstring>> sheetHeaders;
    std::vector<std::vector<std::vector<std::wstring>>> sheetRows;

    // Sheet 1: 系统资源
    {
        sheetNames.push_back(L"系统资源");
        std::vector<std::wstring> headers = {
            L"时间戳", L"运行时间(秒)", L"CPU使用率(%)", L"内存总量(GB)",
            L"内存可用(GB)", L"内存使用(GB)", L"内存使用率(%)"
        };
        wchar_t netHdr[64];
        swprintf_s(netHdr, 64, L"网络发送(%s)", netUnit);
        headers.push_back(netHdr);
        swprintf_s(netHdr, 64, L"网络接收(%s)", netUnit);
        headers.push_back(netHdr);
        sheetHeaders.push_back(headers);

        std::vector<std::vector<std::wstring>> rows;
        for (const auto& d : systemData) {
            std::vector<std::wstring> row;
            row.push_back(d.timestamp);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%.2f", d.runSeconds);    row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.cpuUsage);       row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryTotalGB);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryAvailableGB); row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryUsedGB);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.memoryUsage);    row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.netSendSpeed);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f", d.netRecvSpeed);   row.push_back(buf);
            rows.push_back(row);
        }
        sheetRows.push_back(rows);
    }

    // Process sheets
    for (size_t pi = 0; pi < processes.size(); pi++) {
        if (!processes[pi].enabled) continue;
        if (pi >= allProcessData.size()) break;

        // MakeSheetName function... let me use inline logic
        std::wstring sname = processes[pi].name;
        size_t dot = sname.rfind(L'.');
        if (dot != std::wstring::npos) {
            std::wstring ext = sname.substr(dot);
            if (_wcsicmp(ext.c_str(), L".exe") == 0 || _wcsicmp(ext.c_str(), L".com") == 0)
                sname = sname.substr(0, dot);
        }
        if (sname.length() > 31) sname = sname.substr(0, 31);
        for (auto& ch : sname) {
            if (ch == L'[' || ch == L']' || ch == L':' || ch == L'*' ||
                ch == L'?' || ch == L'/' || ch == L'\\') ch = L'_';
        }
        sheetNames.push_back(sname);

        std::vector<std::wstring> headers = {
            L"时间戳", L"运行时间(秒)", L"进程ID", L"CPU使用率(%)",
            L"内存使用率(%)", L"内存使用(MB)"
        };
        wchar_t netHdr[64];
        swprintf_s(netHdr, 64, L"网络发送(%s)", netUnit);
        headers.push_back(netHdr);
        swprintf_s(netHdr, 64, L"网络接收(%s)", netUnit);
        headers.push_back(netHdr);
        sheetHeaders.push_back(headers);

        std::vector<std::vector<std::wstring>> rows;
        for (const auto& d : allProcessData[pi]) {
            std::vector<std::wstring> row;
            row.push_back(d.timestamp);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%.2f",  d.runSeconds);    row.push_back(buf);
            swprintf_s(buf, 64, L"%lu",    d.pid);           row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.cpuUsage);      row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.memoryUsage);   row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.memoryUsedMB);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.netSendSpeed);  row.push_back(buf);
            swprintf_s(buf, 64, L"%.2f",  d.netRecvSpeed);  row.push_back(buf);
            rows.push_back(row);
        }
        sheetRows.push_back(rows);
    }

    int totalSheets = (int)sheetNames.size();

    zip.push_back({"[Content_Types].xml", BuildContentTypes(totalSheets)});
    zip.push_back({"_rels/.rels", BuildRels()});
    zip.push_back({"xl/workbook.xml", BuildWorkbookXml(sheetNames)});
    zip.push_back({"xl/_rels/workbook.xml.rels", BuildWorkbookRels(totalSheets)});
    zip.push_back({"xl/styles.xml", BuildStylesXml()});
    for (int i = 0; i < totalSheets; i++) {
        char name[64];
        snprintf(name, sizeof(name), "xl/worksheets/sheet%d.xml", i + 1);
        zip.push_back({name, BuildSheetXml(sheetHeaders[i], sheetRows[i])});
    }
}

bool ExcelExporter::FlushExport(
    const std::vector<SystemMonitorData>& systemData,
    const std::vector<MonitorProcess>& processes,
    const std::vector<std::vector<ProcessMonitorData>>& allProcessData,
    const wchar_t* netUnit)
{
    if (m_hFile == INVALID_HANDLE_VALUE) return false;

    std::vector<ZipEntry> zip;
    BuildZipEntries(zip, systemData, processes, allProcessData, netUnit);
    return WriteZipToHandle(m_hFile, zip);
}

void ExcelExporter::EndExport() {
    if (m_hFile == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(m_hFile);
    CloseHandle(m_hFile);
    m_hFile = INVALID_HANDLE_VALUE;
}
