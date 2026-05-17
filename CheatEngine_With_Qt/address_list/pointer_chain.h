#pragma once
#include <QString>
#include <cstdint>
#include <vector>
#include <memory>

// 前向声明
class IMemoryAccessor;

/// @brief 单级指针层次（偏移量）
struct PointerLevel
{
    int64_t offset = 0;

    bool operator==(const PointerLevel& other) const noexcept
    {
        return offset == other.offset;
    }
    bool operator!=(const PointerLevel& other) const noexcept
    {
        return !(*this == other);
    }
};

/// @brief 多级指针链（描述如何从基址解引用到最终地址）
struct PointerChain
{
    QString baseAddressText;
    uint64_t baseAddress = 0;
    std::vector<PointerLevel> levels;

    /// 判断是否有效（基址文本非空）
    bool isValid() const {
        return !baseAddressText.isEmpty();
    }

    /// 格式化为可读字符串
    QString toString() const;

    bool operator==(const PointerChain& other) const noexcept
    {
        return baseAddress == other.baseAddress
            && levels == other.levels;
    }
    bool operator!=(const PointerChain& other) const noexcept
    {
        return !(*this == other);
    }
};

// ==================== 指针解析工具函数 ====================

/// @brief 解析指针链获取最终的可读地址
/// @param chain  指针链
/// @param mem    内存访问器
/// @param finalAddress [out] 最终可读地址
/// @return true 解析成功
bool resolvePointerAddress(const PointerChain& chain,
                           const std::shared_ptr<IMemoryAccessor>& mem,
                           uint64_t& finalAddress);
