#pragma once
#include <string>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include "scan_data_stream_define.h"


class EncodingFormatConversion
{
public:
    EncodingFormatConversion() = default;
    ~EncodingFormatConversion()= default ;

    //常规整数、浮点转化
    inline std::string formatValue(uint64_t raw, ScanDataType type) {
        char buf[64] = {};
        switch (type) {
        case ScanDataType::Int8:  snprintf(buf, sizeof(buf), "%d", static_cast<int8_t>(raw)); break;
        case ScanDataType::Int16: snprintf(buf, sizeof(buf), "%d", static_cast<int16_t>(raw)); break;
        case ScanDataType::Int32: snprintf(buf, sizeof(buf), "%d", static_cast<int32_t>(raw)); break;
        case ScanDataType::Int64: snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(raw)); break;
        case ScanDataType::Float32: {
            float f; std::memcpy(&f, &raw, sizeof(f));
            snprintf(buf, sizeof(buf), "%g", f); break;
        }
        case ScanDataType::Float64: {
            double d; std::memcpy(&d, &raw, sizeof(d));
            snprintf(buf, sizeof(buf), "%g", d); break;
        }
        default: return std::to_string(raw);
        }
        return buf;
    }

    // 对于 ASCII 和 UTF-8，直接返回原字符串
    inline std::string formatString(const std::string& str, ScanDataType type) {
        return str;
    }


    //格式化（ASCII / UTF‑16）
    inline std::string formatUtf16String(const uint16_t* data, size_t length) {
        std::string utf8;
        for (size_t i = 0; i < length; ++i) {
            uint16_t c = data[i];
            if (c == 0) break;
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

    //格式化为 “42 8B 03 …” 形式
    inline std::string formatByteArray(const uint8_t* data, size_t length) {
        std::string hex;
        char tmp[4];
        for (size_t i = 0; i < length; ++i) {
            snprintf(tmp, sizeof(tmp), "%02X ", data[i]);
            hex += tmp;
        }
        return hex;
    }

private:

};



