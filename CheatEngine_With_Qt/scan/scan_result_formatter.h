#pragma once
#include <string>
#include <cstdint>
#include "scan_data_stream_define.h"

class ScanResultFormatter {

public:

    static std::string formatValue(uint64_t raw, ScanDataType type);

    // 对于 ASCII 和 UTF-8，直接返回原字符串
    static std::string formatString(const std::string& str, ScanDataType type);

    //格式化（ASCII / UTF‑16）
    static std::string formatUtf16String(const uint16_t* data, size_t length);

    //格式化为 “42 8B 03 …” 形式
    static std::string formatByteArray(const uint8_t* data, size_t length);

    ScanResultFormatter() = delete;
};
