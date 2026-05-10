#pragma once
#include <string>
#include <map>
#include <fstream>
#include <vector>
#include <memory>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX 
#include <windows.h>
#endif

class ProcessMemorySnapshot {
public:
    ProcessMemorySnapshot(const std::string& path, std::map<uint64_t, size_t> index);
    ~ProcessMemorySnapshot();

    // 模板函数必须在头文件中实现，以避免 LNK2019 错误
    template <typename T>
    bool readValue(uint64_t addr, T& outVal) const {
        return readData(addr, reinterpret_cast<uint8_t*>(&outVal), sizeof(T));
    }

    /**
     * @brief 核心读取逻辑：优先使用 MMAP，失败或非 Windows 平台自动回退到 TLS 句柄
     */
    bool readData(uint64_t address, uint8_t* buffer, size_t size) const;

    const std::string& path() const { return m_path; }
    const std::map<uint64_t, size_t>& index() const { return m_index; }

private:
    std::string m_path;
    std::map<uint64_t, size_t> m_index;

#ifdef _WIN32
    // Windows 内存映射相关成员
    HANDLE m_hFile = (HANDLE)(LONG_PTR)-1; // INVALID_HANDLE_VALUE
    HANDLE m_hMapping = nullptr;
    uint8_t* m_pBuffer = nullptr;
    size_t m_fileSize = 0;
#endif

    // 内部初始化函数
    void initMapping();
};