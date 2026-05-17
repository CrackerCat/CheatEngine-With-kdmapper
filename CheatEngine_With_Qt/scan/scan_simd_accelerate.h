#pragma once
#include <immintrin.h>
#include <vector>
#include <cstdint>
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
                // 对齐步长：取 scalarSize 与 effectiveAlignment 的 LCM
                // 当 effectiveAlignment >= scalarSize 时使用 scalarSize 步进（SIMD 对齐路径）
                // 当 effectiveAlignment < scalarSize 时需逐字节检查（非对齐路径）
                const size_t innerStep = (effectiveAlignment >= scalarSize) ? scalarSize : effectiveAlignment;

                for (size_t byteIndex = 0; byteIndex + scalarSize <= simdByteWidth; byteIndex += innerStep) {
                    uint64_t candidateAddress = baseAddress + offset + byteIndex;

                    // 数据本身可能跨越 SIMD 元素边界，因此以 effectiveAlignment 对齐检查
                    if (candidateAddress % effectiveAlignment != 0) continue;

                    if (effectiveAlignment >= scalarSize) {
                        // 快速路径：利用 SIMD 掩码直接判断，避免标量重读
                        // movemask_epi8 → 32bit(1bit/byte), movemask_ps → 8bit(1bit/float), movemask_pd → 4bit(1bit/double)
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
                        // 非对齐路径：逐字节标量比较，确保不遗漏跨边界值
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
     * @param processMemory      进程当前内存数据的起始指针
     * @param memoryBlockSize    内存块字节数
     * @param baseAddress        该内存块在进程空间中的基地址
     * @param alignment          地址对齐要求 (字节)
     * @param rangeMin           区间下界（包含）
     * @param rangeMax           区间上界（包含）
     * @param out_matchedAddresses  输出：所有匹配的地址
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
                __m256 geMask = _mm256_cmp_ps(valVec, vMin, _CMP_GE_OQ);   // val >= min
                __m256 leMask = _mm256_cmp_ps(valVec, vMax, _CMP_LE_OQ);   // val <= max
                matchBitmask = _mm256_movemask_ps(_mm256_and_ps(geMask, leMask));
            }
            else if constexpr (std::is_same_v<ScalarType, double>) {
                __m256d valVec = _mm256_castsi256_pd(currentChunk);
                __m256d vMin = _mm256_set1_pd(rangeMin);
                __m256d vMax = _mm256_set1_pd(rangeMax);
                __m256d geMask = _mm256_cmp_pd(valVec, vMin, _CMP_GE_OQ);  // val >= min
                __m256d leMask = _mm256_cmp_pd(valVec, vMax, _CMP_LE_OQ);  // val <= max
                matchBitmask = _mm256_movemask_pd(_mm256_and_pd(geMask, leMask));
            }
            else {
                // 整数类型：val >= min  <=>  NOT(min > val)
                //           val <= max  <=>  NOT(val > max)
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

                // geMask = ~(vMin > val)  =>  val >= vMin
                __m256i geMask = _mm256_xor_si256(
                    SimdKernel<ScalarType>::compareIntegers(vMinVec, currentChunk, SimdOp::Greater),
                    _mm256_set1_epi8(0xFF));
                // leMask = ~(val > vMax)  =>  val <= vMax
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

        // 尾部不足 32 字节的部分，用标量方式处理
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
     *        用于 UnknownInitial 全内存再次扫描（Changed/Unchanged/Increased/Decreased）
     * @tparam ScalarType   数据类型 (int32_t, float, double …)
     * @param currentMemory      当前快照内存块
     * @param previousMemory     上一次快照内存块（大小与 currentMemory 相同）
     * @param memoryBlockSize    内存块字节数
     * @param baseAddress        该内存块在进程空间中的基地址
     * @param alignment          地址对齐要求 (字节)
     * @param operation          比较操作（Equal=Unchanged, NotEqual=Changed, Greater=Increased, Less=Decreased）
     * @param out_matchedAddresses  输出：所有匹配的地址
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
                // 整数类型比较
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

        // 尾部不足 32 字节的部分，用标量方式处理
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
     * @param memoryToSearch   待搜索的内存
     * @param memorySize       内存字节数
     * @param byteToFind       要查找的字节值
     * @param out_foundOffsets 输出：所有匹配位置的偏移量 (相对于 memoryToSearch)
     */
    static void findFirstChar(
        const uint8_t* memoryToSearch,
        size_t                  memorySize,
        uint8_t                 byteToFind,
        std::vector<size_t>& out_foundOffsets)
    {
        const size_t simdByteWidth = 32;
        // 将目标字节广播到整个256位寄存器
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
};