#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <vector>

class IProcessMemorySnapshot {
public:
    virtual ~IProcessMemorySnapshot() = default;

    // 核心读取接口
    virtual bool readData(uint64_t address, uint8_t* buffer, size_t size) const = 0;

    // 模板辅助函数
    template <typename T>
    bool readValue(uint64_t addr, T& outVal) const {
        return readData(addr, reinterpret_cast<uint8_t*>(&outVal), sizeof(T));
    }

    virtual const std::string& path() const = 0;
    virtual const std::map<uint64_t, size_t>& index() const = 0;
};