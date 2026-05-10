#include "win32_process_memory_snapshot.h"
#include <cstring>
#include <fstream>

Win32ProcessMemorySnapshot::Win32ProcessMemorySnapshot(const std::string& path, std::map<uint64_t, size_t> index)
    : m_path(path), m_index(std::move(index)) {
    initMapping();
}

Win32ProcessMemorySnapshot::~Win32ProcessMemorySnapshot() {
    if (m_pBuffer) UnmapViewOfFile(m_pBuffer);
    if (m_hMapping) CloseHandle(m_hMapping);
    if (m_hFile != (HANDLE)(LONG_PTR)-1) CloseHandle(m_hFile);
}

void Win32ProcessMemorySnapshot::initMapping() {
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
}

bool Win32ProcessMemorySnapshot::readData(uint64_t address, uint8_t* buffer, size_t size) const {
    auto it = m_index.upper_bound(address);
    if (it == m_index.begin()) return false;
    --it;

    size_t readOffset = it->second + (address - it->first);

    // 优先使用内存映射读取
    if (m_pBuffer && (readOffset + size <= m_fileSize)) {
        std::memcpy(buffer, m_pBuffer + readOffset, size);
        return true;
    }

    // 回退方案
    thread_local std::ifstream localStream;
    if (!localStream.is_open()) localStream.open(m_path, std::ios::binary);
    if (!localStream) return false;

    localStream.seekg(readOffset);
    localStream.read(reinterpret_cast<char*>(buffer), size);
    return localStream.gcount() == static_cast<std::streamsize>(size);
}