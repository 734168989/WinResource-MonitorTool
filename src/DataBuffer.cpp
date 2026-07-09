// DataBuffer.cpp
#include "DataBuffer.h"

DataBuffer::DataBuffer() {
    InitializeCriticalSection(&m_cs);
    // Reserve a reasonable starting capacity; vector will grow as needed.
    // MAX_ROWS (2M) is a safety cap, not the initial allocation.
    m_systemData.reserve(10000);
}

DataBuffer::~DataBuffer() {
    DeleteCriticalSection(&m_cs);
}

void DataBuffer::AddSystemData(const SystemMonitorData& data) {
    EnterCriticalSection(&m_cs);
    m_systemData.push_back(data);
    // Ring buffer: remove oldest when exceeding max
    if (m_systemData.size() > MAX_ROWS) {
        size_t excess = m_systemData.size() - MAX_ROWS;
        m_systemData.erase(m_systemData.begin(), m_systemData.begin() + excess);
    }
    LeaveCriticalSection(&m_cs);
}

void DataBuffer::AddProcessData(const std::wstring& name, const ProcessMonitorData& data) {
    EnterCriticalSection(&m_cs);
    auto& vec = m_processData[name];
    vec.push_back(data);
    if (vec.size() > MAX_ROWS) {
        size_t excess = vec.size() - MAX_ROWS;
        vec.erase(vec.begin(), vec.begin() + excess);
    }
    LeaveCriticalSection(&m_cs);
}

const std::vector<SystemMonitorData>& DataBuffer::GetSystemDataRef() {
    return m_systemData;
}

const std::vector<ProcessMonitorData>* DataBuffer::GetProcessDataRef(const std::wstring& name) {
    auto it = m_processData.find(name);
    if (it != m_processData.end())
        return &it->second;
    return nullptr;
}

std::vector<std::wstring> DataBuffer::GetProcessNames() {
    EnterCriticalSection(&m_cs);
    std::vector<std::wstring> names;
    for (auto& kv : m_processData)
        names.push_back(kv.first);
    LeaveCriticalSection(&m_cs);
    return names;
}

std::vector<SystemMonitorData> DataBuffer::GetSystemDataCopy() {
    EnterCriticalSection(&m_cs);
    auto copy = m_systemData;
    LeaveCriticalSection(&m_cs);
    return copy;
}

std::vector<ProcessMonitorData> DataBuffer::GetProcessDataCopy(const std::wstring& name) {
    EnterCriticalSection(&m_cs);
    std::vector<ProcessMonitorData> copy;
    auto it = m_processData.find(name);
    if (it != m_processData.end())
        copy = it->second;
    LeaveCriticalSection(&m_cs);
    return copy;
}

size_t DataBuffer::GetSystemCount() {
    EnterCriticalSection(&m_cs);
    size_t n = m_systemData.size();
    LeaveCriticalSection(&m_cs);
    return n;
}

size_t DataBuffer::GetProcessCount(const std::wstring& name) {
    EnterCriticalSection(&m_cs);
    size_t n = 0;
    auto it = m_processData.find(name);
    if (it != m_processData.end())
        n = it->second.size();
    LeaveCriticalSection(&m_cs);
    return n;
}

size_t DataBuffer::GetTotalCount() {
    EnterCriticalSection(&m_cs);
    size_t total = m_systemData.size();
    for (auto& kv : m_processData)
        total += kv.second.size();
    LeaveCriticalSection(&m_cs);
    return total;
}

void DataBuffer::Clear() {
    EnterCriticalSection(&m_cs);
    m_systemData.clear();
    m_processData.clear();
    LeaveCriticalSection(&m_cs);
}

void DataBuffer::ClearProcess(const std::wstring& name) {
    EnterCriticalSection(&m_cs);
    auto it = m_processData.find(name);
    if (it != m_processData.end())
        it->second.clear();
    LeaveCriticalSection(&m_cs);
}
