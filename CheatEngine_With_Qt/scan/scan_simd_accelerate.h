#pragma once
#include <immintrin.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <intrin.h>

// 扫描比较操作类型
enum class SimdOp { Equal, Greater, Less, NotEqual };

// 反转 SIMD 操作符：Equal ↔ NotEqual, Greater ↔ Less
inline SimdOp invertSimdOp(SimdOp op) {
    switch (op) {
    case SimdOp::Equal:    return SimdOp::NotEqual;
    case SimdOp::NotEqual: return SimdOp::Equal;
    case SimdOp::Greater:  return SimdOp::Less;
    case SimdOp::Less:     return SimdOp::Greater;
    }
    return op;
}

/**
 * @brief SIMD 比较内核
 * @tparam DataType 被扫描的数据类型 (如 int8_t, int32_t, float, double)
 */

template<typename DataType>
struct SimdKernel {
    // ---------- 整数比较 ----------
    static inline __m256i compareIntegers(__m256i currentMemoryVector, __m256i targetValueVector, SimdOp operation) {
        if constexpr (sizeof(DataType) == 1) {
            if (operation == SimdOp::Equal)   return _mm256_cmpeq_epi8(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Greater) return _mm256_cmpgt_epi8(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Less)    return _mm256_cmpgt_epi8(targetValueVector, currentMemoryVector);
        }
        else if constexpr (sizeof(DataType) == 2) {
            if (operation == SimdOp::Equal)   return _mm256_cmpeq_epi16(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Greater) return _mm256_cmpgt_epi16(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Less)    return _mm256_cmpgt_epi16(targetValueVector, currentMemoryVector);
        }
        else if constexpr (sizeof(DataType) == 4) {
            if (operation == SimdOp::Equal)   return _mm256_cmpeq_epi32(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Greater) return _mm256_cmpgt_epi32(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Less)    return _mm256_cmpgt_epi32(targetValueVector, currentMemoryVector);
        }
        else if constexpr (sizeof(DataType) == 8) {
            if (operation == SimdOp::Equal)   return _mm256_cmpeq_epi64(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Greater) return _mm256_cmpgt_epi64(currentMemoryVector, targetValueVector);
            if (operation == SimdOp::Less)    return _mm256_cmpgt_epi64(targetValueVector, currentMemoryVector);
        }

        if (operation == SimdOp::NotEqual) {
            __m256i eq = compareIntegers(currentMemoryVector, targetValueVector, SimdOp::Equal);
            return _mm256_xor_si256(eq, _mm256_set1_epi8(0xFF)); // 按位取反
        }
        return _mm256_setzero_si256();
    }

    // ---------- 单精度浮点比较 ----------
    static inline __m256 compareFloats_ps(__m256 currentMemoryVector, __m256 targetValueVector, SimdOp operation) {
        switch (operation) {
        case SimdOp::Equal:    return _mm256_cmp_ps(currentMemoryVector, targetValueVector, _CMP_EQ_OQ);
        case SimdOp::Greater:  return _mm256_cmp_ps(currentMemoryVector, targetValueVector, _CMP_GT_OQ);
        case SimdOp::Less:     return _mm256_cmp_ps(currentMemoryVector, targetValueVector, _CMP_LT_OQ);
        case SimdOp::NotEqual: return _mm256_cmp_ps(currentMemoryVector, targetValueVector, _CMP_NEQ_OQ);
        default: return _mm256_setzero_ps();
        }
    }

    // ---------- 双精度浮点比较 ----------
    static inline __m256d compareDoubles_pd(__m256d currentMemoryVector, __m256d targetValueVector, SimdOp operation) {
        switch (operation) {
        case SimdOp::Equal:    return _mm256_cmp_pd(currentMemoryVector, targetValueVector, _CMP_EQ_OQ);
        case SimdOp::Greater:  return _mm256_cmp_pd(currentMemoryVector, targetValueVector, _CMP_GT_OQ);
        case SimdOp::Less:     return _mm256_cmp_pd(currentMemoryVector, targetValueVector, _CMP_LT_OQ);
        case SimdOp::NotEqual: return _mm256_cmp_pd(currentMemoryVector, targetValueVector, _CMP_NEQ_OQ);
        default: return _mm256_setzero_pd();
        }
    }
};

/**
 * @brief SIMD 加速扫描器
 */
class SimdScanner {
private:
    // ── 内部辅助：掩码展开与广播 ──

    // 从 32 字节整数比较结果中提取逐字节的 32-bit mask
    static inline uint32_t mask32_from_epi8(__m256i cmpResult) {
        return static_cast<uint32_t>(_mm256_movemask_epi8(cmpResult));
    }

    // 按类型尺寸将 per-element mask 展开为 per-byte mask
    // typeSize: 1,2,4,8
    // cmpResult: 该类型的 SIMD 比较结果
    // op: 操作类型（Equal/NotEqual/Greater/Less）
    static inline uint32_t expandMaskByType(__m256i cmpResult, size_t typeSize, SimdOp op) {
        uint32_t raw = mask32_from_epi8(cmpResult);
        if (typeSize == 1) return raw;
        uint32_t result = 0;
        const uint32_t allBits = (1u << typeSize) - 1u;
        // ★ 对于 _mm256_cmpeq_epi32/_epi64 等向量比较指令：
        //   - 元素相等时 → 所有位为 0xFF → movemask 返回 allBits（如 0xF）
        //   - 元素不等时 → 所有位为 0x00 → movemask 返回 0
        // ★ 对于 NotEqual: elemBits == 0 才代表"不相等"（原始值改变了）
        if (op == SimdOp::NotEqual) {
            for (size_t i = 0; i < 32; i += typeSize) {
                uint32_t elemBits = (raw >> i) & allBits;
                if (elemBits == 0)          // ★ 修复：elemBits == 0 → 元素不相等
                    result |= (allBits << i);
            }
        } else {
            for (size_t i = 0; i < 32; i += typeSize) {
                uint32_t elemBits = (raw >> i) & allBits;
                if (elemBits == allBits)    // elemBits == allBits → 元素相等（或条件真）
                    result |= (allBits << i);
            }
        }
        return result;
    }

    // 从浮点 movemask_ps(8bit) 展开为 per-byte mask
    static inline uint32_t expandF32Mask(uint32_t psMask8bit, SimdOp op) {
        uint32_t result = 0;
        for (int i = 0; i < 8; ++i) {
            if (psMask8bit & (1u << i)) {
                result |= (0xFu << (i * 4));
            }
        }
        return result;
    }

    // 从浮点 movemask_pd(4bit) 展开为 per-byte mask
    static inline uint32_t expandF64Mask(uint32_t pdMask4bit, SimdOp op) {
        uint32_t result = 0;
        for (int i = 0; i < 4; ++i) {
            if (pdMask4bit & (1u << i)) {
                result |= (0xFFu << (i * 8));
            }
        }
        return result;
    }

    // 从 64-bit 值按类型广播
    static inline __m256i broadcast_int8(uint64_t val) {
        return _mm256_set1_epi8(static_cast<int8_t>(val));
    }
    static inline __m256i broadcast_int16(uint64_t val) {
        return _mm256_set1_epi16(static_cast<int16_t>(val));
    }
    static inline __m256i broadcast_int32(uint64_t val) {
        return _mm256_set1_epi32(static_cast<int32_t>(val));
    }
    static inline __m256i broadcast_int64(uint64_t val) {
        return _mm256_set1_epi64x(static_cast<int64_t>(val));
    }
    static inline __m256 broadcast_float(uint64_t rawBits) {
        float f;
        std::memcpy(&f, &rawBits, sizeof(float));
        return _mm256_set1_ps(f);
    }
    static inline __m256d broadcast_double(uint64_t rawBits) {
        double d;
        std::memcpy(&d, &rawBits, sizeof(double));
        return _mm256_set1_pd(d);
    }

public:
    /**
     * @brief 对一块内存区域执行 SIMD 批量比较，记录所有匹配地址
     * @tparam ScalarType   要比较的数据类型 (int32_t, float …)
     * @param processMemory      进程当前内存数据的起始指针
     * @param targetFilledBlock  用目标值填充的内存块，大小与 processMemory 相同
     * @param memoryBlockSize    内存块字节数
     * @param baseAddress        该内存块在进程空间中的基地址
     * @param alignment          地址对齐要求 (字节)，0 表示按数据类型大小对齐
     * @param operation          比较操作
     * @param out_matchedAddresses  输出：所有匹配的地址
     */
    template<typename ScalarType>
    static void scanMemoryBlockForMatches(
        const uint8_t* processMemory,
        const uint8_t* targetFilledBlock,
        size_t                  memoryBlockSize,
        uint64_t                baseAddress,
        size_t                  alignment,
        SimdOp                  operation,
        std::vector<uint64_t>& out_matchedAddresses)
    {
        const size_t simdByteWidth = 32;           // 一次处理32字节(256位)
        const size_t scalarSize = sizeof(ScalarType);
        const size_t effectiveAlignment = (alignment > 0) ? alignment : scalarSize;

        // --- 主循环：每次处理一个 32 字节块 ---
        for (size_t offset = 0; offset + simdByteWidth <= memoryBlockSize; offset += simdByteWidth)
        {
            // 加载当前内存块和目标值块
            __m256i currentChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(processMemory + offset));
            __m256i targetChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(targetFilledBlock + offset));
            uint32_t matchBitmask = 0;

            // 根据数据类型调用对应的比较，生成匹配掩码
            if constexpr (std::is_same_v<ScalarType, float>) {
                matchBitmask = _mm256_movemask_ps(
                    SimdKernel<ScalarType>::compareFloats_ps(
                        _mm256_castsi256_ps(currentChunk),
                        _mm256_castsi256_ps(targetChunk),
                        operation));
            }
            else if constexpr (std::is_same_v<ScalarType, double>) {
                matchBitmask = _mm256_movemask_pd(
                    SimdKernel<ScalarType>::compareDoubles_pd(
                        _mm256_castsi256_pd(currentChunk),
                        _mm256_castsi256_pd(targetChunk),
                        operation));
            }
            else {
                matchBitmask = _mm256_movemask_epi8(
                    SimdKernel<ScalarType>::compareIntegers(currentChunk, targetChunk, operation));
            }

            // 如果有任意匹配，遍历块中的每一个元素
            if (matchBitmask != 0)
            {
                const size_t innerStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;

                for (size_t byteIndex = 0; byteIndex + scalarSize <= simdByteWidth; byteIndex += innerStep) {
                    uint64_t candidateAddress = baseAddress + offset + byteIndex;
                    if (candidateAddress % effectiveAlignment != 0) continue;

                    if (effectiveAlignment >= scalarSize) {
                        // 快速路径：利用 SIMD 掩码直接判断
                        constexpr size_t bitsPerElement = std::is_floating_point_v<ScalarType> ? 1 : scalarSize;
                        uint32_t currentElemMask = (1u << bitsPerElement) - 1u;
                        uint32_t shift;
                        if constexpr (std::is_floating_point_v<ScalarType>) {
                            shift = static_cast<uint32_t>(byteIndex / scalarSize);
                        } else {
                            shift = static_cast<uint32_t>(byteIndex);
                        }
                        uint32_t elementBits = (matchBitmask >> shift) & currentElemMask;
                        if (elementBits == currentElemMask) {
                            out_matchedAddresses.push_back(candidateAddress);
                        }
                    } else {
                        // 非对齐路径：逐字节标量比较
                        ScalarType currentValue, targetValue;
                        std::memcpy(&currentValue, processMemory + offset + byteIndex, scalarSize);
                        std::memcpy(&targetValue, targetFilledBlock + offset + byteIndex, scalarSize);

                        bool isMatch = false;
                        if (operation == SimdOp::Equal)        isMatch = (currentValue == targetValue);
                        else if (operation == SimdOp::Greater)  isMatch = (currentValue > targetValue);
                        else if (operation == SimdOp::Less)     isMatch = (currentValue < targetValue);
                        else if (operation == SimdOp::NotEqual) isMatch = (currentValue != targetValue);

                        if (isMatch)
                            out_matchedAddresses.push_back(candidateAddress);
                    }
                }
            }
        }

        // --- 尾部不足 32 字节的部分，用标量方式处理 ---
        size_t startOfTail = (memoryBlockSize / simdByteWidth) * simdByteWidth;
        const size_t tailStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;
        for (size_t byteIndex = startOfTail; byteIndex + scalarSize <= memoryBlockSize; byteIndex += tailStep)
        {
            uint64_t candidateAddress = baseAddress + byteIndex;
            if (candidateAddress % effectiveAlignment != 0)
                continue;

            ScalarType currentValue, targetValue;
            std::memcpy(&currentValue, processMemory + byteIndex, scalarSize);
            std::memcpy(&targetValue, targetFilledBlock + byteIndex, scalarSize);

            bool isMatch = false;
            if (operation == SimdOp::Equal)        isMatch = (currentValue == targetValue);
            else if (operation == SimdOp::Greater)  isMatch = (currentValue > targetValue);
            else if (operation == SimdOp::Less)     isMatch = (currentValue < targetValue);
            else if (operation == SimdOp::NotEqual) isMatch = (currentValue != targetValue);

            if (isMatch)
                out_matchedAddresses.push_back(candidateAddress);
        }
    }

    /**
     * @brief SIMD 范围扫描：找出 [rangeMin, rangeMax] 区间内的所有值
     * @tparam ScalarType   数据类型 (int32_t, float, double …)
     */
    template<typename ScalarType>
    static void scanMemoryBlockForRange(
        const uint8_t* processMemory,
        size_t                  memoryBlockSize,
        uint64_t                baseAddress,
        size_t                  alignment,
        ScalarType              rangeMin,
        ScalarType              rangeMax,
        std::vector<uint64_t>& out_matchedAddresses)
    {
        const size_t simdByteWidth = 32;
        const size_t scalarSize = sizeof(ScalarType);
        const size_t effectiveAlignment = (alignment > 0) ? alignment : scalarSize;

        for (size_t offset = 0; offset + simdByteWidth <= memoryBlockSize; offset += simdByteWidth)
        {
            __m256i currentChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(processMemory + offset));
            uint32_t matchBitmask = 0;

            if constexpr (std::is_same_v<ScalarType, float>) {
                __m256 valVec = _mm256_castsi256_ps(currentChunk);
                __m256 vMin = _mm256_set1_ps(rangeMin);
                __m256 vMax = _mm256_set1_ps(rangeMax);
                __m256 geMask = _mm256_cmp_ps(valVec, vMin, _CMP_GE_OQ);
                __m256 leMask = _mm256_cmp_ps(valVec, vMax, _CMP_LE_OQ);
                matchBitmask = _mm256_movemask_ps(_mm256_and_ps(geMask, leMask));
            }
            else if constexpr (std::is_same_v<ScalarType, double>) {
                __m256d valVec = _mm256_castsi256_pd(currentChunk);
                __m256d vMin = _mm256_set1_pd(rangeMin);
                __m256d vMax = _mm256_set1_pd(rangeMax);
                __m256d geMask = _mm256_cmp_pd(valVec, vMin, _CMP_GE_OQ);
                __m256d leMask = _mm256_cmp_pd(valVec, vMax, _CMP_LE_OQ);
                matchBitmask = _mm256_movemask_pd(_mm256_and_pd(geMask, leMask));
            }
            else {
                __m256i vMinVec, vMaxVec;
                if constexpr (sizeof(ScalarType) == 1) {
                    vMinVec = _mm256_set1_epi8(static_cast<int8_t>(rangeMin));
                    vMaxVec = _mm256_set1_epi8(static_cast<int8_t>(rangeMax));
                } else if constexpr (sizeof(ScalarType) == 2) {
                    vMinVec = _mm256_set1_epi16(static_cast<int16_t>(rangeMin));
                    vMaxVec = _mm256_set1_epi16(static_cast<int16_t>(rangeMax));
                } else if constexpr (sizeof(ScalarType) == 4) {
                    vMinVec = _mm256_set1_epi32(static_cast<int32_t>(rangeMin));
                    vMaxVec = _mm256_set1_epi32(static_cast<int32_t>(rangeMax));
                } else if constexpr (sizeof(ScalarType) == 8) {
                    vMinVec = _mm256_set1_epi64x(static_cast<int64_t>(rangeMin));
                    vMaxVec = _mm256_set1_epi64x(static_cast<int64_t>(rangeMax));
                }

                __m256i geMask = _mm256_xor_si256(
                    SimdKernel<ScalarType>::compareIntegers(vMinVec, currentChunk, SimdOp::Greater),
                    _mm256_set1_epi8(0xFF));
                __m256i leMask = _mm256_xor_si256(
                    SimdKernel<ScalarType>::compareIntegers(currentChunk, vMaxVec, SimdOp::Greater),
                    _mm256_set1_epi8(0xFF));
                matchBitmask = _mm256_movemask_epi8(_mm256_and_si256(geMask, leMask));
            }

            if (matchBitmask != 0)
            {
                const size_t innerStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;
                for (size_t byteIndex = 0; byteIndex + scalarSize <= simdByteWidth; byteIndex += innerStep) {
                    uint64_t candidateAddress = baseAddress + offset + byteIndex;
                    if (candidateAddress % effectiveAlignment != 0) continue;

                    if (effectiveAlignment >= scalarSize) {
                        constexpr size_t bitsPerElement = std::is_floating_point_v<ScalarType> ? 1 : scalarSize;
                        uint32_t currentElemMask = (1u << bitsPerElement) - 1u;
                        uint32_t shift;
                        if constexpr (std::is_floating_point_v<ScalarType>) {
                            shift = static_cast<uint32_t>(byteIndex / scalarSize);
                        } else {
                            shift = static_cast<uint32_t>(byteIndex);
                        }
                        uint32_t elementBits = (matchBitmask >> shift) & currentElemMask;
                        if (elementBits == currentElemMask) {
                            out_matchedAddresses.push_back(candidateAddress);
                        }
                    } else {
                        ScalarType currentValue;
                        std::memcpy(&currentValue, processMemory + offset + byteIndex, scalarSize);
                        if (currentValue >= rangeMin && currentValue <= rangeMax) {
                            out_matchedAddresses.push_back(candidateAddress);
                        }
                    }
                }
            }
        }

        size_t startOfTail = (memoryBlockSize / simdByteWidth) * simdByteWidth;
        const size_t tailStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;
        for (size_t byteIndex = startOfTail; byteIndex + scalarSize <= memoryBlockSize; byteIndex += tailStep)
        {
            uint64_t candidateAddress = baseAddress + byteIndex;
            if (candidateAddress % effectiveAlignment != 0) continue;

            ScalarType currentValue;
            std::memcpy(&currentValue, processMemory + byteIndex, scalarSize);
            if (currentValue >= rangeMin && currentValue <= rangeMax) {
                out_matchedAddresses.push_back(candidateAddress);
            }
        }
    }

    /**
     * @brief SIMD 批量比较两个内存块（当前 vs 上一次快照）
     */
    template<typename ScalarType>
    static void compareTwoMemoryBlocks(
        const uint8_t* currentMemory,
        const uint8_t* previousMemory,
        size_t                  memoryBlockSize,
        uint64_t                baseAddress,
        size_t                  alignment,
        SimdOp                  operation,
        std::vector<uint64_t>& out_matchedAddresses)
    {
        const size_t simdByteWidth = 32;
        const size_t scalarSize = sizeof(ScalarType);
        const size_t effectiveAlignment = (alignment > 0) ? alignment : scalarSize;

        for (size_t offset = 0; offset + simdByteWidth <= memoryBlockSize; offset += simdByteWidth)
        {
            __m256i currentChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(currentMemory + offset));
            __m256i previousChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(previousMemory + offset));
            uint32_t matchBitmask = 0;

            if constexpr (std::is_same_v<ScalarType, float>) {
                __m256 cur = _mm256_castsi256_ps(currentChunk);
                __m256 prev = _mm256_castsi256_ps(previousChunk);
                switch (operation) {
                case SimdOp::Equal:    matchBitmask = _mm256_movemask_ps(_mm256_cmp_ps(cur, prev, _CMP_EQ_OQ)); break;
                case SimdOp::NotEqual: matchBitmask = _mm256_movemask_ps(_mm256_cmp_ps(cur, prev, _CMP_NEQ_OQ)); break;
                case SimdOp::Greater:  matchBitmask = _mm256_movemask_ps(_mm256_cmp_ps(cur, prev, _CMP_GT_OQ)); break;
                case SimdOp::Less:     matchBitmask = _mm256_movemask_ps(_mm256_cmp_ps(cur, prev, _CMP_LT_OQ)); break;
                }
            } else if constexpr (std::is_same_v<ScalarType, double>) {
                __m256d cur = _mm256_castsi256_pd(currentChunk);
                __m256d prev = _mm256_castsi256_pd(previousChunk);
                switch (operation) {
                case SimdOp::Equal:    matchBitmask = _mm256_movemask_pd(_mm256_cmp_pd(cur, prev, _CMP_EQ_OQ)); break;
                case SimdOp::NotEqual: matchBitmask = _mm256_movemask_pd(_mm256_cmp_pd(cur, prev, _CMP_NEQ_OQ)); break;
                case SimdOp::Greater:  matchBitmask = _mm256_movemask_pd(_mm256_cmp_pd(cur, prev, _CMP_GT_OQ)); break;
                case SimdOp::Less:     matchBitmask = _mm256_movemask_pd(_mm256_cmp_pd(cur, prev, _CMP_LT_OQ)); break;
                }
            } else {
                switch (operation) {
                case SimdOp::Equal:    matchBitmask = _mm256_movemask_epi8(SimdKernel<ScalarType>::compareIntegers(currentChunk, previousChunk, SimdOp::Equal)); break;
                case SimdOp::NotEqual: matchBitmask = _mm256_movemask_epi8(SimdKernel<ScalarType>::compareIntegers(currentChunk, previousChunk, SimdOp::NotEqual)); break;
                case SimdOp::Greater:  matchBitmask = _mm256_movemask_epi8(SimdKernel<ScalarType>::compareIntegers(currentChunk, previousChunk, SimdOp::Greater)); break;
                case SimdOp::Less:     matchBitmask = _mm256_movemask_epi8(SimdKernel<ScalarType>::compareIntegers(currentChunk, previousChunk, SimdOp::Less)); break;
                }
            }

            if (matchBitmask != 0)
            {
                const size_t innerStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;
                for (size_t byteIndex = 0; byteIndex + scalarSize <= simdByteWidth; byteIndex += innerStep) {
                    uint64_t candidateAddress = baseAddress + offset + byteIndex;
                    if (candidateAddress % effectiveAlignment != 0) continue;

                    if (effectiveAlignment >= scalarSize) {
                        constexpr size_t bitsPerElement = std::is_floating_point_v<ScalarType> ? 1 : scalarSize;
                        uint32_t currentElemMask = (1u << bitsPerElement) - 1u;
                        uint32_t shift;
                        if constexpr (std::is_floating_point_v<ScalarType>) {
                            shift = static_cast<uint32_t>(byteIndex / scalarSize);
                        } else {
                            shift = static_cast<uint32_t>(byteIndex);
                        }
                        uint32_t elementBits = (matchBitmask >> shift) & currentElemMask;
                        if (elementBits == currentElemMask) {
                            out_matchedAddresses.push_back(candidateAddress);
                        }
                    } else {
                        ScalarType curVal, oldVal;
                        std::memcpy(&curVal, currentMemory + offset + byteIndex, scalarSize);
                        std::memcpy(&oldVal, previousMemory + offset + byteIndex, scalarSize);
                        bool isMatch = false;
                        if (operation == SimdOp::Equal)        isMatch = (curVal == oldVal);
                        else if (operation == SimdOp::NotEqual) isMatch = (curVal != oldVal);
                        else if (operation == SimdOp::Greater)  isMatch = (curVal > oldVal);
                        else if (operation == SimdOp::Less)     isMatch = (curVal < oldVal);
                        if (isMatch)
                            out_matchedAddresses.push_back(candidateAddress);
                    }
                }
            }
        }

        size_t startOfTail = (memoryBlockSize / simdByteWidth) * simdByteWidth;
        const size_t tailStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;
        for (size_t byteIndex = startOfTail; byteIndex + scalarSize <= memoryBlockSize; byteIndex += tailStep)
        {
            uint64_t candidateAddress = baseAddress + byteIndex;
            if (candidateAddress % effectiveAlignment != 0) continue;

            ScalarType curVal, oldVal;
            std::memcpy(&curVal, currentMemory + byteIndex, scalarSize);
            std::memcpy(&oldVal, previousMemory + byteIndex, scalarSize);
            bool isMatch = false;
            if (operation == SimdOp::Equal)        isMatch = (curVal == oldVal);
            else if (operation == SimdOp::NotEqual) isMatch = (curVal != oldVal);
            else if (operation == SimdOp::Greater)  isMatch = (curVal > oldVal);
            else if (operation == SimdOp::Less)     isMatch = (curVal < oldVal);
            if (isMatch)
                out_matchedAddresses.push_back(candidateAddress);
        }
    }

    /**
     * @brief 快速查找某个字节值在内存中所有出现的位置
     */
    static void findFirstChar(
        const uint8_t* memoryToSearch,
        size_t                  memorySize,
        uint8_t                 byteToFind,
        std::vector<size_t>& out_foundOffsets)
    {
        const size_t simdByteWidth = 32;
        __m256i broadcastByte = _mm256_set1_epi8(static_cast<char>(byteToFind));

        for (size_t offset = 0; offset + simdByteWidth <= memorySize; offset += simdByteWidth)
        {
            __m256i currentChunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(memoryToSearch + offset));
            uint32_t matchBitmask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(currentChunk, broadcastByte));

            while (matchBitmask != 0)
            {
                unsigned long bitPosition;
                if (_BitScanForward(&bitPosition, matchBitmask)) {
                    out_foundOffsets.push_back(offset + bitPosition);
                    matchBitmask &= ~(1 << bitPosition);
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  All 类型扫描专用批量加速接口
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief All 类型 首次扫描（ExactValue）批量加速
     *
     * 一次性加载 32 字节，同时做 6 种类型的 SIMD 比较。
     * 对每个偏移，记录所有能匹配的数据类型（完整 typeMask）。
     * 调用者负责处理尾部不足32字节的部分（标量兜底）。
     */
    static void scanAllTypesFirst(
        const uint8_t* memBuf, size_t memSize,
        uint64_t baseAddr,
        const uint64_t typeValues[6],
        SimdOp op,
        std::vector<std::pair<uint64_t, uint16_t>>& outPairs)
    {
        constexpr size_t BLOCK = 32;
        static constexpr uint8_t kTypeSize[] = { 1, 2, 4, 8, 4, 8 };

        for (size_t off = 0; off + BLOCK <= memSize; off += BLOCK) {
            __m256i chunk = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(memBuf + off));
            uint64_t chunkBase = baseAddr + off;

            // ── 一次性计算 6 种类型的 per-byte 匹配掩码 ──
            uint32_t masks[6];

            // Byte (0)
            masks[0] = _mm256_movemask_epi8(
                SimdKernel<int8_t>::compareIntegers(chunk, broadcast_int8(typeValues[0]), op));

            // Int16 (1)
            masks[1] = expandMaskByType(
                SimdKernel<int16_t>::compareIntegers(chunk, broadcast_int16(typeValues[1]), op), 2, op);

            // Int32 (2)
            masks[2] = expandMaskByType(
                SimdKernel<int32_t>::compareIntegers(chunk, broadcast_int32(typeValues[2]), op), 4, op);

            // Int64 (3)
            masks[3] = expandMaskByType(
                SimdKernel<int64_t>::compareIntegers(chunk, broadcast_int64(typeValues[3]), op), 8, op);

            // Float32 (4): _mm256_cmp_ps → 8-bit mask → expand to 32-bit
            {
                __m256 fCmp = SimdKernel<float>::compareFloats_ps(
                    _mm256_castsi256_ps(chunk), broadcast_float(typeValues[4]), op);
                masks[4] = expandF32Mask(_mm256_movemask_ps(fCmp), op);
            }

            // Float64 (5): _mm256_cmp_pd → 4-bit mask → expand to 32-bit
            {
                __m256d dCmp = SimdKernel<double>::compareDoubles_pd(
                    _mm256_castsi256_pd(chunk), broadcast_double(typeValues[5]), op);
                masks[5] = expandF64Mask(_mm256_movemask_pd(dCmp), op);
            }

            // ── 对 32 字节逐个偏移，构建完整 typeMask ──
            for (int byteOff = 0; byteOff < BLOCK; ++byteOff) {
                uint32_t bit = 1u << byteOff;
                uint16_t typeMask = 0;
                bool anyMatch = false;
                // 检查每种类型
                for (int ti = 0; ti < 6; ++ti) {
                    if (byteOff % kTypeSize[ti] != 0) continue;
                    if (masks[ti] & bit) {
                        typeMask |= (1 << ti);
                        anyMatch = true;
                    }
                }
                if (anyMatch) {
                    outPairs.emplace_back(chunkBase + byteOff, typeMask);
                }
            }
        }
    }

    /**
     * @brief All 类型 UnknownInitial 再次扫描 — Changed/Unchanged 专用
     *
     * 核心：_mm256_cmpeq_epi8(cur, old) 一次性出 32 字节相等掩码，
     * 对每个偏移，记录所有能匹配的数据类型（完整 typeMask）。
     */
    static void scanAllTypesChangedUnchanged(
        const uint8_t* curBuf, const uint8_t* oldBuf,
        size_t memSize, uint64_t baseAddr,
        bool isUnchanged,
        std::vector<std::pair<uint64_t, uint16_t>>& outPairs)
    {
        constexpr size_t BLOCK = 32;
        static constexpr uint8_t kTypeSize[] = { 1, 2, 4, 8, 4, 8 };

        for (size_t off = 0; off + BLOCK <= memSize; off += BLOCK) {
            __m256i cur = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(curBuf + off));
            __m256i old = _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(oldBuf + off));
            uint64_t chunkBase = baseAddr + off;

            // 核心：逐字节相等掩码
            uint32_t eqMask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(cur, old));

            // ── Byte (0) ──
            uint32_t m0 = isUnchanged ? eqMask : ~eqMask;

            // ── Int16 (1)：每连续 2 字节都满足条件 ──
            uint32_t m1;
            if (isUnchanged) {
                m1 = eqMask & (eqMask >> 1);
                m1 = (m1 & 0x55555555u);        // 保留偶数偏移
                m1 = (m1 << 1) | m1;            // 展开回 2 字节
            } else {
                uint32_t neq = ~eqMask;
                m1 = neq | (neq >> 1);
                m1 = (m1 & 0x55555555u);
                m1 = (m1 << 1) | m1;
            }

            // ── Int32 (2)：每连续 4 字节都满足条件 ──
            uint32_t m2;
            {
                __m256i cmp = _mm256_cmpeq_epi32(cur, old);
                m2 = expandMaskByType(cmp, 4, isUnchanged ? SimdOp::Equal : SimdOp::NotEqual);
            }

            // ── Float32 (4)：同 Int32（4字节对齐） ──
            uint32_t m4;
            {
                __m256 fCur = _mm256_castsi256_ps(cur);
                __m256 fOld = _mm256_castsi256_ps(old);
                uint32_t ps8 = _mm256_movemask_ps(
                    isUnchanged ? _mm256_cmp_ps(fCur, fOld, _CMP_EQ_OQ)
                                : _mm256_cmp_ps(fCur, fOld, _CMP_NEQ_OQ));
                m4 = expandF32Mask(ps8, SimdOp::Equal);
            }

            // ── Int64 (3)：每连续 8 字节都满足条件 ──
            uint32_t m3;
            {
                __m256i cmp = _mm256_cmpeq_epi64(cur, old);
                m3 = expandMaskByType(cmp, 8, isUnchanged ? SimdOp::Equal : SimdOp::NotEqual);
            }

            // ── Float64 (5)：同 Int64（8字节对齐） ──
            uint32_t m5;
            {
                __m256d dCur = _mm256_castsi256_pd(cur);
                __m256d dOld = _mm256_castsi256_pd(old);
                uint32_t pd4 = _mm256_movemask_pd(
                    isUnchanged ? _mm256_cmp_pd(dCur, dOld, _CMP_EQ_OQ)
                                : _mm256_cmp_pd(dCur, dOld, _CMP_NEQ_OQ));
                m5 = expandF64Mask(pd4, SimdOp::Equal);
            }

            // ── 对 32 字节逐个偏移，构建完整 typeMask ──
            uint32_t masks[] = { m0, m1, m2, m3, m4, m5 };
            for (int byteOff = 0; byteOff < BLOCK; ++byteOff) {
                uint32_t bit = 1u << byteOff;
                uint16_t typeMask = 0;
                bool anyMatch = false;
                for (int ti = 0; ti < 6; ++ti) {
                    if (byteOff % kTypeSize[ti] != 0) continue;
                    if (masks[ti] & bit) {
                        typeMask |= (1 << ti);
                        anyMatch = true;
                    }
                }
                if (anyMatch) {
                    outPairs.emplace_back(chunkBase + byteOff, typeMask);
                }
            }
        }
    }
};
