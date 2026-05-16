#pragma once
//
// address_item.h — 地址列表核心数据模型（类封装重构版）
//
// AddressItem 从公有 struct 重构为 class，所有数据成员私有，
// 通过 AddressListModel（友元）完成数据变更，
// 对外仅暴露只读 getter 和业务方法。
//
// 包含关系：address_item.h → value_traits.h → pointer_chain.h

#include "value_traits.h"
#include "pointer_chain.h"
#include "interface/imemory_accessor.h"
#include <memory>

// ==================== AddressItem 类 ====================

class AddressItem
{
    friend class AddressListModel;  // 仅模型有权修改内部状态

public:
    /// 创建/更新 AddressItem 时使用的配置结构
    struct Config
    {
        QString     description;
        uint64_t    address = 0;
        uint64_t    rawValue = 0;
        ValueType   type = ValueType::Int32;
        bool        frozen = false;
        bool        hexDisplay = false;
        bool        signedDisplay = true;
        StringEncoding encoding = StringEncoding::UTF8;
        int         stringLength = 0;
        std::vector<uint8_t> buffer;
        PointerChain pointerChain;
    };

    /// 工厂方法：从 Config 创建 AddressItem
    static AddressItem create(const Config& cfg);

    /// 导出当前完整状态为 Config
    Config toConfig() const;

    AddressItem() = default;

    // ==================== 只读 Getter ====================

    const QString&      description() const noexcept { return m_description; }
    uint64_t            address() const noexcept { return m_address; }
    uint64_t            rawValue() const noexcept { return m_rawValue; }
    ValueType           type() const noexcept { return m_type; }
    bool                isFrozen() const noexcept { return m_frozen; }
    bool                isChanged() const noexcept { return m_changed; }
    uint64_t            lastRawValue() const noexcept { return m_lastRawValue; }
    bool                hexDisplay() const noexcept { return m_hexDisplay; }
    bool                signedDisplay() const noexcept { return m_signedDisplay; }
    StringEncoding      encoding() const noexcept { return m_encoding; }
    int                 stringLength() const noexcept { return m_stringLength; }
    const std::vector<uint8_t>& buffer() const noexcept { return m_buffer; }
    const PointerChain& pointerChain() const noexcept { return m_pointerChain; }

    /// 是（多级）指针项
    bool isPointer() const noexcept { return m_pointerChain.isValid(); }

    // ==================== 格式化输出 ====================

    QString formattedValue() const;
    QString formattedNumericValue() const;
    QString formattedStringValue() const;
    QString formattedByteArrayValue() const;
    QString formattedAddress() const;

    // ==================== I/O 操作 ====================

    /// 从内存读取当前值，更新内部状态，返回 true 表示数值有变化
    bool refreshValue(const std::shared_ptr<IMemoryAccessor>& mem);

    /// 将冻结值写回内存（冻结定时器使用）
    bool freezeWrite(const std::shared_ptr<IMemoryAccessor>& mem) const;

    /// 解析可读/可写地址：
    ///   指针项 → 沿指针链解引用
    ///   普通项 → 直接返回 m_address
    uint64_t getEffectiveAddress(const std::shared_ptr<IMemoryAccessor>& mem) const;

private:
    // ==================== 私有数据 ====================

    QString     m_description;
    uint64_t    m_address = 0;
    uint64_t    m_rawValue = 0;
    ValueType   m_type = ValueType::Int32;
    bool        m_frozen = false;

    // 变化追踪
    uint64_t    m_lastRawValue = 0;
    bool        m_changed = false;

    // 显示模式选项
    bool        m_hexDisplay = false;
    bool        m_signedDisplay = true;
    StringEncoding m_encoding = StringEncoding::UTF8;

    // 字符串/字节数组数据缓冲区
    std::vector<uint8_t> m_buffer;
    int         m_stringLength = 0;

    // 多级指针信息
    PointerChain m_pointerChain;

    // ==================== 内部辅助 ====================

    /// 在空终止符处截断字符串缓冲区
    void truncateBuffer(int readLen);
};


// ==================== 全局辅助函数（兼容旧调用方） ====================

/// @brief 获取可读地址：指针项解析指针链，普通项直接返回 address
inline uint64_t getEffectiveAddress(const AddressItem& item,
                                    const std::shared_ptr<IMemoryAccessor>& mem)
{
    return item.getEffectiveAddress(mem);
}


// ==================== 内联实现 ====================

inline AddressItem AddressItem::create(const Config& cfg)
{
    AddressItem item;
    item.m_description   = cfg.description;
    item.m_address       = cfg.address;
    item.m_rawValue      = cfg.rawValue;
    item.m_type          = cfg.type;
    item.m_frozen        = cfg.frozen;
    item.m_hexDisplay    = cfg.hexDisplay;
    item.m_signedDisplay = cfg.signedDisplay;
    item.m_encoding      = cfg.encoding;
    item.m_stringLength  = cfg.stringLength;
    item.m_buffer        = cfg.buffer;
    item.m_pointerChain  = cfg.pointerChain;
    return item;
}

inline AddressItem::Config AddressItem::toConfig() const
{
    Config cfg;
    cfg.description   = m_description;
    cfg.address       = m_address;
    cfg.rawValue      = m_rawValue;
    cfg.type          = m_type;
    cfg.frozen        = m_frozen;
    cfg.hexDisplay    = m_hexDisplay;
    cfg.signedDisplay = m_signedDisplay;
    cfg.encoding      = m_encoding;
    cfg.stringLength  = m_stringLength;
    cfg.buffer        = m_buffer;
    cfg.pointerChain  = m_pointerChain;
    return cfg;
}

inline QString AddressItem::formattedValue() const
{
    if (isStringValueType(m_type))
        return formattedStringValue();
    if (isByteArrayValueType(m_type))
        return formattedByteArrayValue();
    return formattedNumericValue();
}

inline QString AddressItem::formattedStringValue() const
{
    if (m_buffer.empty()) return {};

    switch (m_encoding) {
    case StringEncoding::UTF16: {
        const char16_t* u16 = reinterpret_cast<const char16_t*>(m_buffer.data());
        int u16len = static_cast<int>(m_buffer.size()) / 2;
        int realLen = 0;
        while (realLen < u16len && u16[realLen] != 0)
            ++realLen;
        return QString::fromUtf16(u16, realLen);
    }
    case StringEncoding::UTF8: {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        int len = static_cast<int>(m_buffer.size());
        int realLen = 0;
        while (realLen < len && data[realLen] != '\0')
            ++realLen;
        return QString::fromUtf8(data, realLen);
    }
    case StringEncoding::ASCII:
    default: {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        int len = static_cast<int>(m_buffer.size());
        int realLen = 0;
        while (realLen < len && data[realLen] != '\0')
            ++realLen;
        return QString::fromLatin1(data, realLen);
    }
    }
}

inline QString AddressItem::formattedByteArrayValue() const
{
    if (m_buffer.empty()) return {};

    QString result;
    for (size_t i = 0; i < m_buffer.size(); ++i) {
        if (i > 0) result += ' ';
        if (m_hexDisplay)
            result += QString::asprintf("%02X", m_buffer[i]);
        else
            result += QString::number(m_buffer[i]);
    }
    return result;
}

inline QString AddressItem::formattedNumericValue() const
{
    switch (m_type) {
    case ValueType::Int8: {
        if (m_hexDisplay)
            return QString("0x%1").arg(static_cast<uint8_t>(m_rawValue & 0xFF), 2, 16, QChar('0'));
        auto v = static_cast<int8_t>(m_rawValue & 0xFF);
        if (m_signedDisplay)
            return QString::number(v);
        return QString::number(static_cast<uint8_t>(m_rawValue & 0xFF));
    }
    case ValueType::Int16: {
        if (m_hexDisplay)
            return QString("0x%1").arg(static_cast<uint16_t>(m_rawValue & 0xFFFF), 4, 16, QChar('0'));
        auto v = static_cast<int16_t>(m_rawValue & 0xFFFF);
        if (m_signedDisplay)
            return QString::number(v);
        return QString::number(static_cast<uint16_t>(m_rawValue & 0xFFFF));
    }
    case ValueType::Int32: {
        if (m_hexDisplay)
            return QString("0x%1").arg(static_cast<uint32_t>(m_rawValue), 8, 16, QChar('0'));
        if (m_signedDisplay)
            return QString::number(static_cast<int32_t>(m_rawValue));
        return QString::number(static_cast<uint32_t>(m_rawValue));
    }
    case ValueType::Int64: {
        if (m_hexDisplay)
            return QString("0x%1").arg(m_rawValue, 16, 16, QChar('0'));
        if (m_signedDisplay)
            return QString::number(static_cast<int64_t>(m_rawValue));
        return QString::number(m_rawValue);
    }
    case ValueType::Float: {
        if (m_hexDisplay) {
            uint32_t bits;
            std::memcpy(&bits, &m_rawValue, sizeof(bits));
            return QString("0x%1").arg(bits, 8, 16, QChar('0'));
        }
        float f;
        std::memcpy(&f, &m_rawValue, sizeof(f));
        return QString::number(f, 'g', 7);
    }
    case ValueType::Double: {
        if (m_hexDisplay)
            return QString("0x%1").arg(m_rawValue, 16, 16, QChar('0'));
        double d;
        std::memcpy(&d, &m_rawValue, sizeof(d));
        return QString::number(d, 'g', 15);
    }
    default:
        if (m_hexDisplay)
            return QString("0x%1").arg(static_cast<uint32_t>(m_rawValue), 8, 16, QChar('0'));
        return QString::number(static_cast<uint32_t>(m_rawValue));
    }
}

/// @brief 格式化地址字符串
inline QString AddressItem::formattedAddress() const
{
    if (isPointer()) {
        // 指针项：简洁显示
        // 格式: "P->0xFFFFFFFF"
        return QStringLiteral("P->0x%1").arg(m_address, 16, 16, QChar('0'));
    }

    // 普通地址（由调用者用 ProcessManager::resolveAddress 填充）
    return {};
}

// ==================== 非内联方法实现（在 cpp 中也有定义，但为了保持头文件兼容性这里放内联版本） ====================

inline void AddressItem::truncateBuffer(int readLen)
{
    if (m_type != ValueType::String) return;

    int realLen = 0;
    if (m_encoding == StringEncoding::UTF16) {
        const char16_t* u16 = reinterpret_cast<const char16_t*>(m_buffer.data());
        int u16len = readLen / 2;
        while (realLen < u16len && u16[realLen] != 0)
            ++realLen;
        realLen *= 2;
    } else {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        while (realLen < readLen && data[realLen] != '\0')
            ++realLen;
    }

    if (realLen < static_cast<int>(m_buffer.size())) {
        m_buffer.resize(realLen);
        m_stringLength = realLen;
    }
}

inline uint64_t AddressItem::getEffectiveAddress(const std::shared_ptr<IMemoryAccessor>& mem) const
{
    if (isPointer()) {
        uint64_t finalAddr = 0;
        if (resolvePointerAddress(m_pointerChain, mem, finalAddr))
            return finalAddr;
    }
    return m_address;
}

inline bool AddressItem::refreshValue(const std::shared_ptr<IMemoryAccessor>& mem)
{
    if (!mem) return false;

    uint64_t readAddr = getEffectiveAddress(mem);

    bool valueChanged = false;

    if (isStringValueType(m_type) || isByteArrayValueType(m_type)) {
        int readLen = (m_stringLength > 0) ? m_stringLength
                                           : static_cast<int>(valueTypeSize(m_type));
        std::vector<uint8_t> oldBuffer = m_buffer;
        m_buffer.resize(readLen);
        mem->read(readAddr, m_buffer.data(), readLen);

        // 字符串类型：在空终止符处截断
        if (isStringValueType(m_type))
            truncateBuffer(readLen);

        bool bufferChanged = (oldBuffer != m_buffer);

        if (!m_frozen && bufferChanged) {
            if (!m_changed) {
                m_changed = true;
            }
            valueChanged = true;
        } else if (m_changed && !bufferChanged) {
            m_changed = false;
            valueChanged = true;
        }
    } else {
        uint64_t oldRaw = m_rawValue;
        size_t size = valueTypeSize(m_type);
        m_rawValue = 0;
        mem->read(readAddr, &m_rawValue, size);

        if (!m_frozen && oldRaw != m_rawValue) {
            m_lastRawValue = m_changed ? m_lastRawValue : oldRaw;
            m_changed = true;
            valueChanged = true;
        } else if (m_changed && oldRaw == m_rawValue) {
            m_changed = false;
            valueChanged = true;
        }
    }

    return valueChanged;
}

inline bool AddressItem::freezeWrite(const std::shared_ptr<IMemoryAccessor>& mem) const
{
    if (!mem || !m_frozen) return false;

    uint64_t writeAddr = getEffectiveAddress(mem);

    if (isStringValueType(m_type) || isByteArrayValueType(m_type)) {
        if (!m_buffer.empty())
            return mem->write(writeAddr, m_buffer.data(), m_buffer.size());
    } else {
        return mem->write(writeAddr, &m_rawValue, valueTypeSize(m_type));
    }
    return false;
}
