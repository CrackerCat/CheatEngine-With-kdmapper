#pragma once
#include <cstdint>
#include <string>
#include <bit>        // std::endian (C++20)
#include <algorithm>  // std::swap

class IMemoryAccessor
{
public:
    virtual ~IMemoryAccessor() = default;

    virtual bool attach(uint32_t pid) = 0;
    virtual void detach() = 0;

    virtual bool read(uint64_t addr, void* buffer, size_t size) = 0;
    virtual bool write(uint64_t addr, const void* buffer, size_t size) = 0;
    virtual void* nativeHandle() = 0;
    virtual std::string name() const = 0;   // ★ 新增
    // ★ 新增：检测目标进程是否存活
    virtual bool isProcessAlive() const = 0;

    // ──────────────────────────────────────────────────
    // ★ 小端序数值读取模板
    //   从 addr 读取 sizeof(T) 字节，若主机为大端序则自动交换字节，
    //   确保返回的 value 是小端序字节序解释的结果。
    //   在 x86/x64 上等同于 read()（因为 x86 本身就是小端序）。
    // ──────────────────────────────────────────────────
    template <typename T>
    bool readLE(uint64_t addr, T& value)
    {
        if (!read(addr, &value, sizeof(T)))
            return false;

        if constexpr (std::endian::native == std::endian::big) {
            // 主机是大端序 → 交换到小端序
            uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
            for (size_t i = 0; i < sizeof(T) / 2; ++i)
                std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
        }
        return true;
    }
};


