#pragma once
#include <cstdint>
#include <cstddef>
#include "scan/scan_data_stream_define.h"

/// @brief 地址列表中值的显示类型（映射 ScanDataType 到地址列表可用的类型子集）
enum class ValueType : uint8_t {
    Integer = 0,
    Int8,
    Int16,
    Int32,
    Int64,
    Float,
    Double,
    String,      // 字符串类型
    ByteArray,   // 字节数组类型
};

/// @brief 字符串编码类型
enum class StringEncoding : uint8_t {
    ASCII = 0,
    UTF8,
    UTF16,
};

// ==================== 内联工具函数 ====================

/// @brief 将 ScanDataType 转换为 ValueType
inline ValueType scanDataTypeToValueType(ScanDataType sdt)
{
    switch (sdt) {
    case ScanDataType::Bit:     return ValueType::Int8;
    case ScanDataType::Int8:    return ValueType::Int8;
    case ScanDataType::Int16:   return ValueType::Int16;
    case ScanDataType::Int32:   return ValueType::Int32;
    case ScanDataType::Int64:   return ValueType::Int64;
    case ScanDataType::Float32: return ValueType::Float;
    case ScanDataType::Float64: return ValueType::Double;
    case ScanDataType::AsciiString:
    case ScanDataType::Utf8String:
    case ScanDataType::Utf16String: return ValueType::String;
    case ScanDataType::ByteArray:   return ValueType::ByteArray;
    default:                    return ValueType::Int32;
    }
}

/// @brief 获取 ValueType 对应的字节数（字符串/字节数组返回默认读取大小）
inline size_t valueTypeSize(ValueType vt)
{
    switch (vt) {
    case ValueType::Int8:    return 1;
    case ValueType::Int16:   return 2;
    case ValueType::Int32:   return 4;
    case ValueType::Int64:   return 8;
    case ValueType::Float:   return 4;
    case ValueType::Double:  return 8;
    case ValueType::String:  return 32;
    case ValueType::ByteArray: return 256;
    default:                 return 4;
    }
}

/// @brief 判断是否为整数类型
inline bool isIntegerType(ValueType vt)
{
    return vt == ValueType::Int8 || vt == ValueType::Int16 ||
           vt == ValueType::Int32 || vt == ValueType::Int64 ||
           vt == ValueType::Integer;
}

/// @brief 判断是否为浮点类型
inline bool isFloatType(ValueType vt)
{
    return vt == ValueType::Float || vt == ValueType::Double;
}

/// @brief 判断是否为数值类型（整数或浮点）
inline bool isNumericType(ValueType vt)
{
    return isIntegerType(vt) || isFloatType(vt);
}

/// @brief 判断是否为字符串类型
inline bool isStringValueType(ValueType vt)
{
    return vt == ValueType::String;
}

/// @brief 判断是否为字节数组类型
inline bool isByteArrayValueType(ValueType vt)
{
    return vt == ValueType::ByteArray;
}
