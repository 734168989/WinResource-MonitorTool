// ConfigManager.cpp - JSON config with hand-written parser
#include "ConfigManager.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>

ConfigManager& ConfigManager::Instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() : m_loaded(false) {
    InitDefaultConfig(&m_config);
}

ConfigManager::~ConfigManager() {
    FreeConfig(&m_config);
}

// ---- Minimal JSON Parser ----
const wchar_t* ConfigManager::SkipWhitespace(const wchar_t* p) {
    while (*p && (*p == L' ' || *p == L'\t' || *p == L'\n' || *p == L'\r'))
        p++;
    return p;
}

const wchar_t* ConfigManager::ParseString(const wchar_t* p, wchar_t* buf, int bufSize) {
    p = SkipWhitespace(p);
    if (*p != L'"') return nullptr;
    p++;
    int i = 0;
    while (*p && *p != L'"' && i < bufSize - 1) {
        if (*p == L'\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case L'"':  buf[i++] = L'"'; break;
                case L'\\': buf[i++] = L'\\'; break;
                case L'/':  buf[i++] = L'/'; break;
                case L'n':  buf[i++] = L'\n'; break;
                case L'r':  buf[i++] = L'\r'; break;
                case L't':  buf[i++] = L'\t'; break;
                case L'u':  buf[i++] = L'?'; p += 4; break; // skip unicode escapes
                default:    buf[i++] = L'\\'; buf[i++] = *p; break; // preserve unknown escapes
            }
        } else {
            buf[i++] = *p;
        }
        p++;
    }
    buf[i] = L'\0';
    if (*p == L'"') p++;
    return p;
}

// Forward declare helper
static const wchar_t* ParseAnyValue(const wchar_t* p, MonitorConfig& cfg, const wchar_t* key);

const wchar_t* ConfigManager::ParseValue(const wchar_t* p, MonitorConfig& cfg) {
    p = SkipWhitespace(p);
    if (*p == L'{') {
        p++;
        p = SkipWhitespace(p);
        while (*p && *p != L'}') {
            wchar_t key[256];
            p = ParseString(p, key, 256);
            if (!p) return nullptr;
            p = SkipWhitespace(p);
            if (*p != L':') return nullptr;
            p++;
            p = ParseAnyValue(p, cfg, key);
            if (!p) return nullptr;
            p = SkipWhitespace(p);
            if (*p == L',') p++;
            p = SkipWhitespace(p);
        }
        if (*p == L'}') p++;
    }
    return p;
}

static const wchar_t* ParseAnyValue(const wchar_t* p, MonitorConfig& cfg, const wchar_t* key) {
    p = ConfigManager::SkipWhitespace(p);

    if (wcscmp(key, L"monitorProcesses") == 0) {
        // Parse array of objects
        if (*p != L'[') return nullptr;
        p++;
        p = ConfigManager::SkipWhitespace(p);
        while (*p && *p != L']') {
            if (*p == L'{') {
                p++;
                wchar_t name[260] = L"";
                bool enabled = true;
                p = ConfigManager::SkipWhitespace(p);
                while (*p && *p != L'}') {
                    wchar_t ik[64];
                    p = ConfigManager::ParseString(p, ik, 64);
                    if (!p) return nullptr;
                    p = ConfigManager::SkipWhitespace(p);
                    if (*p != L':') return nullptr;
                    p++;
                    if (wcscmp(ik, L"name") == 0) {
                        p = ConfigManager::ParseString(p, name, 260);
                    } else if (wcscmp(ik, L"enabled") == 0) {
                        p = ConfigManager::SkipWhitespace(p);
                        if (*p == L't' && wcsncmp(p, L"true", 4) == 0) { enabled = true; p += 4; }
                        else if (*p == L'f' && wcsncmp(p, L"false", 5) == 0) { enabled = false; p += 5; }
                    } else {
                        // skip unknown value
                        if (*p == L'"') { p++; while (*p && *p != L'"') p++; if (*p) p++; }
                        else if (*p == L'{') { int d = 1; p++; while (*p && d) { if (*p == L'{') d++; if (*p == L'}') d--; p++; } }
                        else if (*p == L'[') { int d = 1; p++; while (*p && d) { if (*p == L'[') d++; if (*p == L']') d--; p++; } }
                        else { while (*p && *p != L',' && *p != L'}' && *p != L']') p++; }
                    }
                    if (!p) return nullptr;
                    p = ConfigManager::SkipWhitespace(p);
                    if (*p == L',') p++;
                    p = ConfigManager::SkipWhitespace(p);
                }
                if (*p == L'}') p++;
                if (name[0]) {
                    ConfigAddProcess(&cfg, name);
                    // Set enabled status on the newly added process
                    for (int i = 0; i < cfg.processCount; i++) {
                        if (_wcsicmp(cfg.processes[i].name, name) == 0) {
                            cfg.processes[i].enabled = enabled;
                            break;
                        }
                    }
                }
            }
            p = ConfigManager::SkipWhitespace(p);
            if (*p == L',') p++;
            p = ConfigManager::SkipWhitespace(p);
        }
        if (*p == L']') p++;
        return p;
    }
    else if (wcscmp(key, L"monitorItems") == 0) {
        if (*p != L'{') return nullptr;
        p++;
        p = ConfigManager::SkipWhitespace(p);
        while (*p && *p != L'}') {
            wchar_t ik[32];
            p = ConfigManager::ParseString(p, ik, 32);
            if (!p) return nullptr;
            p = ConfigManager::SkipWhitespace(p);
            if (*p != L':') return nullptr;
            p++;
            p = ConfigManager::SkipWhitespace(p);
            bool val = (*p == L't');
            if (*p == L't') p += 4; else if (*p == L'f') p += 5;
            if (wcscmp(ik, L"cpu") == 0) cfg.monitorCpu = val;
            else if (wcscmp(ik, L"memory") == 0) cfg.monitorMemory = val;
            else if (wcscmp(ik, L"network") == 0) cfg.monitorNetwork = val;
            p = ConfigManager::SkipWhitespace(p);
            if (*p == L',') p++;
            p = ConfigManager::SkipWhitespace(p);
        }
        if (*p == L'}') p++;
        return p;
    }
    else if (wcscmp(key, L"samplePeriod") == 0) {
        p = ConfigManager::SkipWhitespace(p);
        int val = 0;
        while (*p >= L'0' && *p <= L'9') { val = val * 10 + (*p - L'0'); p++; }
        if (val >= 1 && val <= 60) cfg.samplePeriod = val;
        return p;
    }
    else if (wcscmp(key, L"netUnit") == 0) {
        p = ConfigManager::ParseString(p, cfg.netUnit, 16);
        return p;
    }
    else if (wcscmp(key, L"netInterface") == 0) {
        p = ConfigManager::ParseString(p, cfg.netInterface, 256);
        return p;
    }
    else if (wcscmp(key, L"outputDir") == 0) {
        p = ConfigManager::ParseString(p, cfg.outputDir, MAX_PATH);
        return p;
    }

    // Unknown key: skip value
    p = ConfigManager::SkipWhitespace(p);
    if (*p == L'"') { p++; while (*p && *p != L'"') p++; if (*p) p++; }
    else if (*p == L'{') { int d = 1; p++; while (*p && d) { if (*p == L'{') d++; if (*p == L'}') d--; p++; } }
    else if (*p == L'[') { int d = 1; p++; while (*p && d) { if (*p == L'[') d++; if (*p == L']') d--; p++; } }
    else { while (*p && *p != L',' && *p != L'}' && *p != L']' && *p != L' ' && *p != L'\t' && *p != L'\n' && *p != L'\r') p++; }
    return p;
}

// ---- JSON string escaping for save ----
static int JsonEscapeString(const wchar_t* src, wchar_t* dst, int dstSize) {
    int pos = 0;
    dst[pos++] = L'"';
    while (*src && pos < dstSize - 2) {
        switch (*src) {
        case L'\\': dst[pos++] = L'\\'; dst[pos++] = L'\\'; break;
        case L'"':  dst[pos++] = L'\\'; dst[pos++] = L'"';  break;
        case L'\n': dst[pos++] = L'\\'; dst[pos++] = L'n';  break;
        case L'\r': dst[pos++] = L'\\'; dst[pos++] = L'r';  break;
        case L'\t': dst[pos++] = L'\\'; dst[pos++] = L't';  break;
        default:    dst[pos++] = *src; break;
        }
        src++;
    }
    dst[pos++] = L'"';
    dst[pos] = L'\0';
    return pos;
}

// ---- File I/O ----
bool ConfigManager::LoadConfig(const wchar_t* configPath) {
    // Try to open file
    HANDLE hFile = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        // File doesn't exist, use defaults (don't auto-create — only save on explicit "保存配置")
        FreeConfig(&m_config);
        InitDefaultConfig(&m_config);
        m_loaded = true;
        return true;
    }

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        FreeConfig(&m_config);
        InitDefaultConfig(&m_config);
        m_loaded = true;
        return true;
    }

    // Allocate buffer with extra space for null terminator
    char* buf = (char*)malloc(size + 2);
    if (!buf) {
        CloseHandle(hFile);
        return false;
    }
    memset(buf, 0, size + 2);

    DWORD read = 0;
    if (!ReadFile(hFile, buf, size, &read, nullptr) || read != size) {
        free(buf);
        CloseHandle(hFile);
        FreeConfig(&m_config);
        InitDefaultConfig(&m_config);
        m_loaded = true;
        return true;
    }
    CloseHandle(hFile);

    // Convert UTF-8 to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)size, nullptr, 0);
    wchar_t* wbuf = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
    if (!wbuf) { free(buf); return false; }
    MultiByteToWideChar(CP_UTF8, 0, buf, (int)size, wbuf, wlen);
    wbuf[wlen] = L'\0';
    free(buf);

    // Reset config and parse — start empty, let JSON fully populate
    FreeConfig(&m_config);
    InitEmptyConfig(&m_config);

    const wchar_t* parseResult = ParseValue(wbuf, m_config);
    free(wbuf);

    // Check if parsing succeeded
    if (!parseResult) {
        OutputDebugStringW(L"[ConfigManager] JSON parse error — using defaults\r\n");
        FreeConfig(&m_config);
        InitDefaultConfig(&m_config);
        m_loaded = true;
        return true;
    }

    // Diagnostic: log loaded processes
    {
        wchar_t dbg[512];
        swprintf_s(dbg, 512, L"[ConfigManager] Loaded %d process(es):\r\n", m_config.processCount);
        OutputDebugStringW(dbg);
        for (int i = 0; i < m_config.processCount; i++) {
            swprintf_s(dbg, 512, L"  [%d] %s (enabled=%d)\r\n",
                       i, m_config.processes[i].name,
                       m_config.processes[i].enabled ? 1 : 0);
            OutputDebugStringW(dbg);
        }
    }

    m_loaded = true;
    return true;
}

bool ConfigManager::SaveConfig(const wchar_t* configPath) {
    // Build JSON string with safe bounded formatting
    wchar_t buf[65536];
    wchar_t esc[520];  // buffer for escaped strings (max path * 2)
    int pos = 0;
    int remaining = 65536;
    int written;

    written = swprintf_s(buf + pos, remaining, L"{\r\n");
    if (written < 0) return false;
    pos += written; remaining -= written;

    // monitorProcesses array
    written = swprintf_s(buf + pos, remaining, L"  \"monitorProcesses\": [\r\n");
    if (written < 0) return false;
    pos += written; remaining -= written;

    for (int i = 0; i < m_config.processCount; i++) {
        JsonEscapeString(m_config.processes[i].name, esc, 520);
        written = swprintf_s(buf + pos, remaining, L"    {\r\n");
        if (written < 0) return false;
        pos += written; remaining -= written;

        written = swprintf_s(buf + pos, remaining, L"      \"name\": %s,\r\n", esc);
        if (written < 0) return false;
        pos += written; remaining -= written;

        written = swprintf_s(buf + pos, remaining, L"      \"enabled\": %s\r\n",
                            m_config.processes[i].enabled ? L"true" : L"false");
        if (written < 0) return false;
        pos += written; remaining -= written;

        written = swprintf_s(buf + pos, remaining, L"    }%s\r\n",
                            (i < m_config.processCount - 1) ? L"," : L"");
        if (written < 0) return false;
        pos += written; remaining -= written;
    }
    written = swprintf_s(buf + pos, remaining, L"  ],\r\n");
    if (written < 0) return false;
    pos += written; remaining -= written;

    // monitorItems
    written = swprintf_s(buf + pos, remaining, L"  \"monitorItems\": {\r\n");
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"    \"cpu\": %s,\r\n", m_config.monitorCpu ? L"true" : L"false");
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"    \"memory\": %s,\r\n", m_config.monitorMemory ? L"true" : L"false");
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"    \"network\": %s\r\n", m_config.monitorNetwork ? L"true" : L"false");
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"  },\r\n");
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"  \"samplePeriod\": %d,\r\n", m_config.samplePeriod);
    if (written < 0) return false;
    pos += written; remaining -= written;

    JsonEscapeString(m_config.netUnit, esc, 520);
    written = swprintf_s(buf + pos, remaining, L"  \"netUnit\": %s,\r\n", esc);
    if (written < 0) return false;
    pos += written; remaining -= written;

    JsonEscapeString(m_config.netInterface, esc, 520);
    written = swprintf_s(buf + pos, remaining, L"  \"netInterface\": %s,\r\n", esc);
    if (written < 0) return false;
    pos += written; remaining -= written;

    JsonEscapeString(m_config.outputDir, esc, 520);
    written = swprintf_s(buf + pos, remaining, L"  \"outputDir\": %s\r\n", esc);
    if (written < 0) return false;
    pos += written; remaining -= written;

    written = swprintf_s(buf + pos, remaining, L"}\r\n");
    if (written < 0) return false;
    pos += written;

    // Convert to UTF-8
    int clen = WideCharToMultiByte(CP_UTF8, 0, buf, pos, nullptr, 0, nullptr, nullptr);
    char* cbuf = (char*)malloc(clen + 1);
    if (!cbuf) return false;
    WideCharToMultiByte(CP_UTF8, 0, buf, pos, cbuf, clen, nullptr, nullptr);
    cbuf[clen] = '\0';

    HANDLE hFile = CreateFileW(configPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(cbuf);
        return false;
    }

    DWORD written2 = 0;
    WriteFile(hFile, cbuf, (DWORD)clen, &written2, nullptr);
    CloseHandle(hFile);
    free(cbuf);
    return written2 == (DWORD)clen;
}

// ---- Convenience methods ----
bool ConfigManager::AddProcess(const wchar_t* name) {
    return ConfigAddProcess(&m_config, name);
}

bool ConfigManager::RemoveProcess(int index) {
    if (index < 0 || index >= m_config.processCount) return false;
    for (int i = index; i < m_config.processCount - 1; i++)
        m_config.processes[i] = m_config.processes[i + 1];
    m_config.processCount--;
    return true;
}

void ConfigManager::ClearProcesses() {
    m_config.processCount = 0;
}

void ConfigManager::SetProcessEnabled(int index, bool enabled) {
    if (index >= 0 && index < m_config.processCount)
        m_config.processes[index].enabled = enabled;
}

void ConfigManager::SetSamplePeriod(int period) {
    if (period < 1) period = 1;
    if (period > 60) period = 60;
    m_config.samplePeriod = period;
}

void ConfigManager::SetNetUnit(const wchar_t* unit) {
    wcscpy_s(m_config.netUnit, 16, unit);
}

void ConfigManager::SetNetInterface(const wchar_t* iface) {
    wcscpy_s(m_config.netInterface, 256, iface);
}

void ConfigManager::SetOutputDir(const wchar_t* dir) {
    wcscpy_s(m_config.outputDir, MAX_PATH, dir);
}

void ConfigManager::SetMonitorItems(bool cpu, bool mem, bool net) {
    m_config.monitorCpu = cpu;
    m_config.monitorMemory = mem;
    m_config.monitorNetwork = net;
}
