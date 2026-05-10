#include "process_memory_snapshot.h"
#include <algorithm>
#include <iostream>
#include <cstring>

ProcessMemorySnapshot::ProcessMemorySnapshot(const std::string& path, std::map<uint64_t, size_t> index)
    : m_path(path), m_index(std::move(index))
{
    initMapping();
}

ProcessMemorySnapshot::~ProcessMemorySnapshot() {
#ifdef _WIN32
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapping) CloseHandle(m_hMapping);
    if (m_hFile != (HANDLE)(LONG_PTR)-1) CloseHandle(m_hFile);
#endif
}

void ProcessMemorySnapshot::initMapping() {
#ifdef _WIN32
    m_hFile = CreateFileA(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == (HANDLE)(LONG_PTR)-1) return;

    LARGE_INTEGER size;
    if (GetFileSizeEx(m_hFile, &size)) {
        m_fileSize = static_cast<size_t>(size.QuadPart);
        m_hMapping = CreateFileMapping(m_hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (m_hMapping) {
            m_pBuffer = static_cast<uint8_t*>(MapViewOfFile(m_hMapping, FILE_MAP_READ, 0, 0, 0));
        }
    }
#endif
}

bool ProcessMemorySnapshot::readData(uint64_t address, uint8_t* buffer, size_t size) const {
    // 1. 计算文件偏移
    auto it = m_index.upper_bound(address);
    if (it == m_index.begin()) return false;
    --it;

    size_t readOffset = it->second + (address - it->first);

    // 2. 策略 A: 如果 Windows MMAP 映射成功，直接内存拷贝 (极致性能，天然线程安全)
#ifdef _WIN32
    if (m_pBuffer && (readOffset + size <= m_fileSize)) {
        std::memcpy(buffer, m_pBuffer + readOffset, size);
        return true;
    }
#endif

    // 3. 策略 B: 跨平台/回退方案，使用 Thread Local 句柄 (保证线程安全)
    thread_local std::ifstream localStream;
    if (!localStream.is_open()) {
        localStream.open(m_path, std::ios::binary);
    }

    if (!localStream) return false;

    localStream.seekg(readOffset);
    localStream.read(reinterpret_cast<char*>(buffer), size);

    return localStream.gcount() == static_cast<std::streamsize>(size);
}