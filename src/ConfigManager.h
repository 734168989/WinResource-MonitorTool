// ConfigManager.h - JSON config file management (singleton)
#pragma once
#include <windows.h>
#include "DataModels.h"

class ConfigManager {
public:
    static ConfigManager& Instance();

    bool LoadConfig(const wchar_t* configPath);
    bool SaveConfig(const wchar_t* configPath);
    MonitorConfig& GetConfig() { return m_config; }

    // Convenience methods
    bool AddProcess(const wchar_t* name);
    bool RemoveProcess(int index);
    void ClearProcesses();
    void SetProcessEnabled(int index, bool enabled);
    void SetSamplePeriod(int period);     // clamps to 1-60
    void SetNetUnit(const wchar_t* unit);
    void SetNetInterface(const wchar_t* iface);
    void SetOutputDir(const wchar_t* dir);
    void SetMonitorItems(bool cpu, bool mem, bool net);

    // Minimal JSON parser (public for use by parse helpers)
    static const wchar_t* SkipWhitespace(const wchar_t* p);
    static const wchar_t* ParseString(const wchar_t* p, wchar_t* buf, int bufSize);
    static const wchar_t* ParseValue(const wchar_t* p, MonitorConfig& cfg);

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    MonitorConfig m_config;
    bool m_loaded;
};
