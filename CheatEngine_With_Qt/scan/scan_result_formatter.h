#pragma once
#include <string>
#include <cstdint>
#include <map>

#include "scan_types.h"

class ScanResultFormatter {

public:
    static std::string formatValueAt(uint64_t addr, ScanDataType type);
    static std::string formatValueFromSnapshot(uint64_t addr, const std::string& snapshotPath, const std::map<uint64_t, size_t>& index, ScanDataType type);
    // 按数据类型格式化数值
    static std::string formatValue(uint64_t raw, ScanDataType type);

    // 从内存读取字符串并格式化（ASCII / UTF‑16）
    static std::string formatStringAt(uint64_t addr, ScanDataType type);

    // 从内存读取字节并格式化为 “42 8B 03 …” 形式
    static std::string formatByteArrayAt(uint64_t addr);

    ScanResultFormatter() = delete;
};