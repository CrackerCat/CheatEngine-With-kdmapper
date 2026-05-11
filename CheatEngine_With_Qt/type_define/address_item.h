#pragma once
#include <QString>
#include <cstdint>

enum class ValueType : uint8_t {
    Integer = 0,    // 整数（默认）
    Float,
    Double,
    // 未来可扩展：String, Byte等
};

struct AddressItem
{
    QString description;
    uint64_t address = 0;
    uint64_t value = 0;
    ValueType type = ValueType::Integer;
    bool frozen = false;

    uint64_t lastValue = 0;
    bool changed = false;
};