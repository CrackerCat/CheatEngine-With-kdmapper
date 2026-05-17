#include "address_list/address_item.h"
#include "process/process_manager.h"
#include <QBrush>
#include <QColor>
#include <QObject>
#include <cstring>
#include <algorithm>

// 列索引局部定义（必须与 Model 的 Column 枚举严格对应）
enum ColumnIndex {
    ColFrozen = 0,
    ColDescription,
    ColAddress,
    ColType,
    ColDisplayMode,
    ColSigned,
    ColLength,
    ColValue
};
//=============================================================================
// 静态方法实现（聚合自 value_traits.h）
// =============================================================================

size_t AddressItem::typeSize(Type t) {
    switch (t) {
    case Type::Int8:       return 1;
    case Type::Int16:      return 2;
    case Type::Int32:      return 4;
    case Type::Int64:      return 8;
    case Type::Float:      return 4;
    case Type::Double:     return 8;
    case Type::String:     return 32;
    case Type::ByteArray:  return 256;
    }
    return 4;
}

QString AddressItem::typeName(Type t) {
    switch (t) {
    case Type::Int8:      return QStringLiteral("Byte");
    case Type::Int16:     return QStringLiteral("2 Bytes");
    case Type::Int32:     return QStringLiteral("4 Bytes");
    case Type::Int64:     return QStringLiteral("8 Bytes");
    case Type::Float:     return QStringLiteral("Float");
    case Type::Double:    return QStringLiteral("Double");
    case Type::String:    return QStringLiteral("String");
    case Type::ByteArray: return QStringLiteral("Byte Array");
    }
    return {};
}

AddressItem::Type AddressItem::fromScanDataType(ScanDataType sdt) {
    switch (sdt) {
    case ScanDataType::Bit:     return Type::Int8;
    case ScanDataType::Int8:    return Type::Int8;
    case ScanDataType::Int16:   return Type::Int16;
    case ScanDataType::Int32:   return Type::Int32;
    case ScanDataType::Int64:   return Type::Int64;
    case ScanDataType::Float32: return Type::Float;
    case ScanDataType::Float64: return Type::Double;
    case ScanDataType::AsciiString:
    case ScanDataType::Utf8String:
    case ScanDataType::Utf16String: return Type::String;
    case ScanDataType::ByteArray:   return Type::ByteArray;
    default:                    return Type::Int32;
    }
}

AddressItem::Encoding AddressItem::encodingFromScanDataType(ScanDataType sdt) {
    switch (sdt) {
    case ScanDataType::Utf16String: return Encoding::UTF16;
    case ScanDataType::Utf8String:  return Encoding::UTF8;
    default:                        return Encoding::ASCII;
    }
}

// =============================================================================
// 1. 生命周期管理与工厂创建
// =============================================================================

AddressItem AddressItem::create(const Config& cfg) {
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
    item.m_pointerChain  = cfg.pointerChain;

    // 默认缓冲区大小自适应
    if (item.m_type == Type::ByteArray && cfg.buffer.empty()) {
        item.m_buffer.resize(item.typeSize());
        item.m_hexDisplay = true; // 字节数组默认以 Hex 显示
    } else {
        item.m_buffer = cfg.buffer;
    }

    // 预刷新一次地址解析文本缓存
    item.updateResolvedAddressCache();
    return item;
}

AddressItem::Config AddressItem::toConfig() const
{
    Config cfg;
    cfg.description    = m_description;
    cfg.address        = m_address;
    cfg.rawValue       = m_rawValue;
    cfg.type           = m_type;
    cfg.frozen         = m_frozen;
    cfg.hexDisplay     = m_hexDisplay;
    cfg.signedDisplay  = m_signedDisplay;
    cfg.encoding       = m_encoding;
    cfg.stringLength   = m_stringLength;
    cfg.buffer         = m_buffer;
    cfg.pointerChain   = m_pointerChain;
    return cfg;
}

void AddressItem::applyConfig(const Config& cfg)
{
    m_description   = cfg.description;
    m_address       = cfg.address;
    m_rawValue      = cfg.rawValue;
    m_type          = cfg.type;
    m_frozen        = cfg.frozen;
    m_hexDisplay    = cfg.hexDisplay;
    m_signedDisplay = cfg.signedDisplay;
    m_encoding      = cfg.encoding;
    m_stringLength  = cfg.stringLength;
    m_buffer        = cfg.buffer;
    m_pointerChain  = cfg.pointerChain;

    // 重新刷新地址缓存
    updateResolvedAddressCache();
}

// =============================================================================
// 2. 数据展示自治 (displayData) — 彻底解放 Model
// =============================================================================

QVariant AddressItem::displayData(int column, int role) const {
    // ---- 2.1 文本显示角色 ----
    if (role == Qt::DisplayRole) {
        switch (column) {
        case 0: return {}; // Frozen 列通常只放 CheckBox
        case 1: return m_description;
        case 2: return m_cachedAddressText;
        case 3: return typeName(m_type);
        case 4: {
            if (isStringType()) {
                switch (m_encoding) {
                case Encoding::ASCII: return QStringLiteral("ASCII");
                case Encoding::UTF8:  return QStringLiteral("UTF-8");
                case Encoding::UTF16: return QStringLiteral("UTF-16");
                }
            }
            return m_hexDisplay ? QStringLiteral("Hex") : QStringLiteral("Dec");
        }
        case 5: {
            if (isIntegerType(m_type)) {
                return m_signedDisplay ? QStringLiteral("Signed") : QStringLiteral("Unsigned");
            }
            return QStringLiteral("-");
        }
        case 6: {
            int len = (isStringType() || isByteArrayType())
                      ? (m_stringLength > 0 ? m_stringLength : static_cast<int>(m_buffer.size()))
                      : static_cast<int>(typeSize());
            return QString::number(len) + QStringLiteral(" bytes");
        }
        case 7: return formattedValue();
        }
    }

    // ---- 2.2 编辑态预填角色 ----
    if (role == Qt::EditRole) {
        if (column == 1) return m_description;
        if (column == 7) return formattedValue();
        if (column == 6 && (isStringType() || isByteArrayType())) {
            return m_stringLength > 0 ? m_stringLength : static_cast<int>(m_buffer.size());
        }
    }

    // ---- 2.3 复选框勾选状态角色 ----
    if (role == Qt::CheckStateRole) {
        if (column == 0) return m_frozen ? Qt::Checked : Qt::Unchecked;
        if (column == 4 && !isStringType()) return m_hexDisplay ? Qt::Checked : Qt::Unchecked;
        if (column == 5 && isIntegerType(m_type))  return m_signedDisplay ? Qt::Checked : Qt::Unchecked;
    }

    // ---- 2.4 前景高亮颜色角色 ----
    if (role == Qt::ForegroundRole) {
        if (column == 2 && m_cachedIsBase) return QBrush(QColor(0, 128, 0)); // 基址绿
        if (column == 7 && m_changed)      return QBrush(Qt::red);          // 变动红
    }

    // ---- 2.5 文本对齐角色 ----
    if (role == Qt::TextAlignmentRole) {
        if (column == 2 || column == 7) return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (column == 5 || column == 6) return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // ---- 2.6 工具提示角色 ----
    if (role == Qt::ToolTipRole) {
        if (column == 2) {
            if (isPointer()) {
                QString tip;
                tip += QStringLiteral("指针链: ") + m_pointerChain.baseAddressText;
                for (const auto& pl : m_pointerChain.levels) {
                    tip += QString(" +0x%1").arg(static_cast<long long>(pl.offset), 0, 16);
                }
                tip += QStringLiteral("\n原始基址: 0x%1").arg(m_pointerChain.baseAddress, 16, 16, QChar('0'));
                tip += QStringLiteral("\n最终地址: 0x%1").arg(m_address, 16, 16, QChar('0'));
                return tip;
            }
            return QString("0x%1").arg(m_address, 16, 16, QChar('0'));
        }
        if (column == 4) {
            if (isNumericType() || isByteArrayType()) {
                return m_hexDisplay
                    ? QObject::tr("单击切换为十进制显示")
                    : QObject::tr("单击切换为16进制显示");
            }
            if (isStringType()) {
                return QObject::tr("单击切换字符串编码");
            }
        }
        if (column == 5) {
            if (isIntegerType(m_type)) {
                return m_signedDisplay
                    ? QObject::tr("单击切换为无符号显示")
                    : QObject::tr("单击切换为有符号显示");
            }
            return QObject::tr("仅整数类型支持有符号/无符号切换");
        }
        if (column == 6) {
            if (isNumericType()) {
                return QObject::tr("数据类型大小: %1 字节").arg(typeSize());
            }
            if (isStringType() || isByteArrayType()) {
                return QObject::tr("数据长度: %1 字节（可编辑）")
                    .arg(m_stringLength > 0 ? m_stringLength : static_cast<int>(m_buffer.size()));
            }
        }
    }

    // ---- 2.7 元数据选项抛出（解除委托层硬编码） ----
    if (role == Qt::UserRole + 99) {
        if (column == 3) return getTypeOptions();
        if (column == 4) return getDisplayOptions();
    }

    return {};
}

// =============================================================================
// 3. 交互自治：数据黑盒解析与写入 (tryEditValue)
// =============================================================================

bool AddressItem::tryEditValue(const QString& input, const std::shared_ptr<IMemoryAccessor>& mem) {
    if (!mem || input.isEmpty()) return false;
    uint64_t writeAddr = getEffectiveAddress(mem);

    // ── 3.1 字符串编码写入 ──
    if (isStringType()) {
        QByteArray encoded;
        switch (m_encoding) {
        case Encoding::UTF16: encoded = QByteArray(reinterpret_cast<const char*>(input.utf16()), input.size() * 2); break;
        case Encoding::UTF8:  encoded = input.toUtf8(); break;
        case Encoding::ASCII: encoded = input.toLatin1(); break;
        }
        if (encoded.isEmpty()) return false;

        if (mem->write(writeAddr, encoded.constData(), encoded.size())) {
            m_buffer.resize(encoded.size());
            std::memcpy(m_buffer.data(), encoded.constData(), encoded.size());
            m_changed = false;
            return true;
        }
        return false;
    }

    // ── 3.2 字节数组（AOB）写入 ──
    if (isByteArrayType()) {
        QStringList tokens = input.split(' ', Qt::SkipEmptyParts);
        std::vector<uint8_t> newBuf;
        newBuf.reserve(tokens.size());
        bool ok = true;
        for (const auto& token : tokens) {
            uint val = token.toUInt(&ok, m_hexDisplay ? 16 : 10);
            if (!ok || val > 0xFF) { ok = false; break; }
            newBuf.push_back(static_cast<uint8_t>(val));
        }
        if (!ok || newBuf.empty()) return false;

        if (mem->write(writeAddr, newBuf.data(), newBuf.size())) {
            m_buffer = std::move(newBuf);
            m_stringLength = static_cast<int>(m_buffer.size());
            m_changed = false;
            return true;
        }
        return false;
    }

    // ── 3.3 基础数值类型解析写入 ──
    bool ok = false;
    uint64_t newRaw = 0;
    int base = m_hexDisplay ? 16 : 0;

    switch (m_type) {
    case Type::Int8: {
        int v = input.toInt(&ok, base);
        if (ok) { int8_t s8 = static_cast<int8_t>(v); std::memcpy(&newRaw, &s8, 1); }
        break;
    }
    case Type::Int16: {
        int v = input.toInt(&ok, base);
        if (ok) { int16_t s16 = static_cast<int16_t>(v); std::memcpy(&newRaw, &s16, 2); }
        break;
    }
    case Type::Int32: {
        if (base == 16) {
            uint32_t v = input.toUInt(&ok, 16); if (ok) newRaw = v;
        } else {
            int32_t v = input.toInt(&ok, 10); if (ok) std::memcpy(&newRaw, &v, 4);
        }
        break;
    }
    case Type::Int64: {
        if (base == 16) {
            newRaw = input.toULongLong(&ok, 16);
        } else {
            int64_t v = input.toLongLong(&ok, 10); if (ok) std::memcpy(&newRaw, &v, 8);
        }
        break;
    }
    case Type::Float: {
        float f = input.toFloat(&ok);
        if (ok) std::memcpy(&newRaw, &f, 4);
        break;
    }
    case Type::Double: {
        double d = input.toDouble(&ok);
        if (ok) std::memcpy(&newRaw, &d, 8);
        break;
    }
    }

    if (!ok) return false;

    if (mem->write(writeAddr, &newRaw, typeSize())) {
        m_rawValue = newRaw;
        m_changed = false;
        return true;
    }
    return false;
}

// =============================================================================
// 4. 定时刷新与数据同步交互
// =============================================================================

bool AddressItem::refreshValue(const std::shared_ptr<IMemoryAccessor>& mem) {
    if (!mem) return false;
    uint64_t readAddr = getEffectiveAddress(mem);

    // 动态解引用检测：如果多级指针结果变动，刷新地址缓存文本
    if (isPointer() && readAddr != m_address) {
        m_address = readAddr;
        updateResolvedAddressCache();
    }

    bool valueChanged = false;

    if (isStringType() || isByteArrayType()) {
        int readLen = (m_stringLength > 0) ? m_stringLength : static_cast<int>(typeSize());
        std::vector<uint8_t> oldBuf = m_buffer;
        m_buffer.resize(readLen);
        
        mem->read(readAddr, m_buffer.data(), readLen);
        if (isStringType()) {
            truncateBuffer(readLen);
        }

        bool bufferChanged = (oldBuf != m_buffer);
        if (!m_frozen && bufferChanged) {
            m_changed = true; valueChanged = true;
        } else if (m_changed && !bufferChanged) {
            m_changed = false; valueChanged = true;
        }
    } else {
        uint64_t oldRaw = m_rawValue;
        size_t size = typeSize();
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

bool AddressItem::freezeWrite(const std::shared_ptr<IMemoryAccessor>& mem) const {
    if (!mem || !m_frozen) return false;
    uint64_t writeAddr = getEffectiveAddress(mem);

    if (isStringType() || isByteArrayType()) {
        return !m_buffer.empty() ? mem->write(writeAddr, m_buffer.data(), m_buffer.size()) : false;
    }
    return mem->write(writeAddr, &m_rawValue, typeSize());
}

void AddressItem::updateResolvedAddressCache() {
    if (isPointer()) {
        m_cachedAddressText = QStringLiteral("P->0x%1").arg(m_address, 16, 16, QChar('0'));
        m_cachedIsBase = false;
    } else {
        std::string display;
        ProcessManager::instance().resolveAddress(m_address, display, m_cachedIsBase);
        m_cachedAddressText = QString::fromStdString(display);
    }
}

uint64_t AddressItem::getEffectiveAddress(const std::shared_ptr<IMemoryAccessor>& mem) const {
    uint64_t finalAddr = 0;
    if (isPointer() && resolvePointerAddress(m_pointerChain, mem, finalAddr)) {
        return finalAddr;
    }
    return m_address;
}

void AddressItem::truncateBuffer(int readLen) {
    if (!isStringType()) return;
    int realLen = 0;

    if (m_encoding == Encoding::UTF16) {
        const char16_t* u16 = reinterpret_cast<const char16_t*>(m_buffer.data());
        int maxElements = readLen / 2;
        while (realLen < maxElements && u16[realLen] != 0) ++realLen;
        realLen *= 2;
    } else {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        while (realLen < readLen && data[realLen] != '\0') ++realLen;
    }

    if (realLen < static_cast<int>(m_buffer.size())) {
        m_buffer.resize(realLen);
        m_stringLength = realLen;
    }
}

// =============================================================================
// 5. 格式化输出（为 UI 喂饭）
// =============================================================================

QString AddressItem::formattedValue() const {
    if (isStringType())    return formatString();
    if (isByteArrayType()) return formatByteArray();
    return formatNumeric();
}

QString AddressItem::formatString() const {
    if (m_buffer.empty()) return {};
    switch (m_encoding) {
    case Encoding::UTF16: {
        const char16_t* u16 = reinterpret_cast<const char16_t*>(m_buffer.data());
        int u16len = static_cast<int>(m_buffer.size()) / 2, realLen = 0;
        while (realLen < u16len && u16[realLen] != 0) ++realLen;
        return QString::fromUtf16(u16, realLen);
    }
    case Encoding::UTF8: {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        int len = static_cast<int>(m_buffer.size()), realLen = 0;
        while (realLen < len && data[realLen] != '\0') ++realLen;
        return QString::fromUtf8(data, realLen);
    }
    default: {
        const char* data = reinterpret_cast<const char*>(m_buffer.data());
        int len = static_cast<int>(m_buffer.size()), realLen = 0;
        while (realLen < len && data[realLen] != '\0') ++realLen;
        return QString::fromLatin1(data, realLen);
    }
    }
}

QString AddressItem::formatByteArray() const {
    if (m_buffer.empty()) return {};
    QString result;
    for (size_t i = 0; i < m_buffer.size(); ++i) {
        if (i > 0) result += ' ';
        result += m_hexDisplay ? QString::asprintf("%02X", m_buffer[i]) : QString::number(m_buffer[i]);
    }
    return result;
}

QString AddressItem::formatNumeric() const {
    switch (m_type) {
    case Type::Int8: {
        if (m_hexDisplay) return QString("0x%1").arg(static_cast<uint8_t>(m_rawValue & 0xFF), 2, 16, QChar('0'));
        return m_signedDisplay ? QString::number(static_cast<int8_t>(m_rawValue & 0xFF)) : QString::number(static_cast<uint8_t>(m_rawValue & 0xFF));
    }
    case Type::Int16: {
        if (m_hexDisplay) return QString("0x%1").arg(static_cast<uint16_t>(m_rawValue & 0xFFFF), 4, 16, QChar('0'));
        return m_signedDisplay ? QString::number(static_cast<int16_t>(m_rawValue & 0xFFFF)) : QString::number(static_cast<uint16_t>(m_rawValue & 0xFFFF));
    }
    case Type::Int32: {
        if (m_hexDisplay) return QString("0x%1").arg(static_cast<uint32_t>(m_rawValue), 8, 16, QChar('0'));
        return m_signedDisplay ? QString::number(static_cast<int32_t>(m_rawValue)) : QString::number(static_cast<uint32_t>(m_rawValue));
    }
    case Type::Int64: {
        if (m_hexDisplay) return QString("0x%1").arg(m_rawValue, 16, 16, QChar('0'));
        return m_signedDisplay ? QString::number(static_cast<int64_t>(m_rawValue)) : QString::number(m_rawValue);
    }
    case Type::Float: {
        if (m_hexDisplay) { uint32_t bits; std::memcpy(&bits, &m_rawValue, sizeof(bits)); return QString("0x%1").arg(bits, 8, 16, QChar('0')); }
        float f; std::memcpy(&f, &m_rawValue, sizeof(f)); return QString::number(f, 'g', 7);
    }
    case Type::Double: {
        if (m_hexDisplay) return QString("0x%1").arg(m_rawValue, 16, 16, QChar('0'));
        double d; std::memcpy(&d, &m_rawValue, sizeof(d)); return QString::number(d, 'g', 15);
    }
    default: return {};
    }
}

// =============================================================================
// 6. 通用委托元数据接口
// =============================================================================

QStringList AddressItem::getTypeOptions() {
    return {
        typeName(Type::Int8), typeName(Type::Int16), typeName(Type::Int32),
        typeName(Type::Int64), typeName(Type::Float), typeName(Type::Double),
        typeName(Type::String), typeName(Type::ByteArray)
    };
}

QStringList AddressItem::getDisplayOptions() const {
    if (isStringType()) {
        return {
            QStringLiteral("ASCII"),
            QStringLiteral("UTF-8"),
            QStringLiteral("UTF-16")
        };
    }
    return {
        QStringLiteral("Hex"),
        QStringLiteral("Dec")
    };
}
