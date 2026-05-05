#pragma once

#include "scan_result_formatter.h"
#include "process_manager.h"
#include <string>
#include <cstdio>
#include <cctype>
#include <fstream>

size_t getDataTypeSize(ScanDataType type) {
    switch (type) {
    case ScanDataType::Int8: return 1;
    case ScanDataType::Int16: return 2;
    case ScanDataType::Int32: return 4;
    case ScanDataType::Int64: return 8;
    case ScanDataType::Float32: return 4;
    case ScanDataType::Float64: return 8;
    default: return 0; // 字符串和字节数组不适用
    }
}


std::string ScanResultFormatter::formatValueAt(uint64_t addr, ScanDataType type) {
    auto mem = ProcessManager::instance().memory();
    if (!mem) return "???";

    uint64_t raw = 0;
    // 根据类型长度读取
    size_t size = getDataTypeSize(type); // 假设你有一个获取大小的辅助函数
    if (!mem->read(addr, &raw, size)) return "???";

    return formatValue(raw, type); // 复用原有的格式化逻辑
}

// 增加从指定快照文件读取并格式化的逻辑
std::string ScanResultFormatter::formatValueFromSnapshot(
    uint64_t addr,
    const std::string& snapshotPath,
    const std::map<uint64_t, size_t>& index,
    ScanDataType type)
{
    std::ifstream file(snapshotPath, std::ios::binary);
    if (!file) return "???";

    // 1. 查找地址在快照中的偏移 (使用你已有的逻辑)
    auto it = index.upper_bound(addr);
    if (it == index.begin()) return "???";
    --it;

    size_t fileOffset = it->second + (addr - it->first);
    file.seekg(fileOffset);

    uint64_t raw = 0;
    size_t size = getDataTypeSize(type);
    file.read(reinterpret_cast<char*>(&raw), size);

    return formatValue(raw, type);
}


std::string ScanResultFormatter::formatValue(uint64_t raw, ScanDataType type) {
    char buf[64] = {};
    switch (type) {
    case ScanDataType::Int8:
        snprintf(buf, sizeof(buf), "%d", static_cast<int8_t>(raw));
        break;
    case ScanDataType::Int16:
        snprintf(buf, sizeof(buf), "%d", static_cast<int16_t>(raw));
        break;
    case ScanDataType::Int32:
        snprintf(buf, sizeof(buf), "%d", static_cast<int32_t>(raw));
        break;
    case ScanDataType::Int64:
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(raw));
        break;
    case ScanDataType::Float32: {
        float f;
        std::memcpy(&f, &raw, sizeof(f));
        snprintf(buf, sizeof(buf), "%g", f);
        break;
    }
    case ScanDataType::Float64: {
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        snprintf(buf, sizeof(buf), "%g", d);
        break;
    }
    default:
        return std::to_string(raw);
    }
    return std::string(buf);
}

std::string ScanResultFormatter::formatStringAt(uint64_t addr, ScanDataType type) {
    auto mem = ProcessManager::instance().memory();
    if (!mem) return "???";

    if (type == ScanDataType::AsciiString) {
        char buf[64] = {};
        if (!mem->read(addr, buf, sizeof(buf)))
            return "???";
        size_t len = 0;
        while (len < sizeof(buf) && buf[len]) ++len;
        return std::string(buf, len);
    }
    else { // Utf16String
        char16_t buf[32] = {};
        if (!mem->read(addr, buf, sizeof(buf)))
            return "???";
        int len = 0;
        while (len < 32 && buf[len]) ++len;
        // 简单的 UTF‑16 到 UTF‑8 转换（仅支持 BMP）
        std::string utf8;
        for (int i = 0; i < len; ++i) {
            char16_t c = buf[i];
            if (c < 0x80) {
                utf8 += static_cast<char>(c);
            }
            else if (c < 0x800) {
                utf8 += static_cast<char>(0xC0 | (c >> 6));
                utf8 += static_cast<char>(0x80 | (c & 0x3F));
            }
            else {
                utf8 += static_cast<char>(0xE0 | (c >> 12));
                utf8 += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                utf8 += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return utf8;
    }
}

std::string ScanResultFormatter::formatByteArrayAt(uint64_t addr) {
    auto mem = ProcessManager::instance().memory();
    if (!mem) return "???";

    uint8_t buf[32];
    if (!mem->read(addr, buf, sizeof(buf)))
        return "???";

    std::string hex;
    char tmp[4];
    for (size_t i = 0; i < sizeof(buf); ++i) {
        snprintf(tmp, sizeof(tmp), "%02X ", buf[i]);
        hex += tmp;
    }
    return hex;
}