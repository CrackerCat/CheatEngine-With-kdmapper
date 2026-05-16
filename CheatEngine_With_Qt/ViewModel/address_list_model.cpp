#include "ViewModel/address_list_model.h"
#include "process/process_manager.h"
#include "interface/imemory_accessor.h"

#include <QBrush>
#include <QColor>
#include <algorithm>
#include <cstring>

// ==================== 静态辅助 ====================

static void emitRowChanged(QAbstractItemModel* model, int row)
{
    QModelIndex topLeft = model->index(row, 0);
    QModelIndex bottomRight = model->index(row, AddressListModel::ColumnCount_ - 1);
    emit model->dataChanged(topLeft, bottomRight);
}

// ==================== 构造 ====================

AddressListModel::AddressListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

// ==================== QAbstractTableModel 接口 ====================

int AddressListModel::rowCount(const QModelIndex&) const
{
    return m_items.size();
}

int AddressListModel::columnCount(const QModelIndex&) const
{
    return ColumnCount_;
}

QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const auto& item = m_items[index.row()];

    // ---- 显示文本 ----
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColFrozen:
            return {};
        case ColDescription:
            return item.description();
        case ColAddress: {
            if (item.isPointer()) {
                return item.formattedAddress();
            }
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address(), display, isBase);
            return QString::fromStdString(display);
        }

        case ColValue:
            return item.formattedValue();
        case ColType: {
            switch (item.type()) {
            case ValueType::Int8:      return "Byte";
            case ValueType::Int16:     return "2 Bytes";
            case ValueType::Int32:     return "4 Bytes";
            case ValueType::Int64:     return "8 Bytes";
            case ValueType::Float:     return "Float";
            case ValueType::Double:    return "Double";
            case ValueType::String:    return "String";
            case ValueType::ByteArray: return "Byte Array";
            default:                   return "Int";
            }
        }
        case ColDisplayMode: {
            if (isNumericType(item.type())) {
                return item.hexDisplay() ? QString("Hex") : QString("Dec");
            }
            if (isStringValueType(item.type())) {
                switch (item.encoding()) {
                case StringEncoding::ASCII: return "ASCII";
                case StringEncoding::UTF8:  return "UTF-8";
                case StringEncoding::UTF16: return "UTF-16";
                }
            }
            if (isByteArrayValueType(item.type())) {
                return item.hexDisplay() ? QString("Hex") : QString("Dec");
            }
            return {};
        }
        case ColSigned: {
            if (isIntegerType(item.type())) {
                return item.signedDisplay() ? QString("Signed") : QString("Unsigned");
            }
            return QString("-");
        }
        case ColLength: {
            if (isNumericType(item.type())) {
                return QString::number(valueTypeSize(item.type())) + " bytes";
            }
            if (isStringValueType(item.type()) || isByteArrayValueType(item.type())) {
                if (item.stringLength() > 0)
                    return QString::number(item.stringLength()) + " bytes";
                return QString::number(static_cast<int>(item.buffer().size())) + " bytes";
            }
            return {};
        }
        }
    }

    // ---- 编辑时的预填内容 ----
    if (role == Qt::EditRole) {
        switch (index.column()) {
        case ColDescription:
            return item.description();
        case ColValue:
            return item.formattedValue();
        case ColLength:
            if (isStringValueType(item.type()) || isByteArrayValueType(item.type()))
                return QString::number(item.stringLength() > 0 ? item.stringLength() : static_cast<int>(item.buffer().size()));
            return {};
        default:
            return {};
        }
    }

    // ---- 工具提示 ----
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColAddress) {
            if (item.isPointer()) {
                QString tip;
                tip += QStringLiteral("指针链: ") + item.pointerChain().baseAddressText;
                for (const auto& pl : item.pointerChain().levels) {
                    tip += QString(" +0x%1").arg(static_cast<long long>(pl.offset), 0, 16);
                }
                tip += QStringLiteral("\n原始基址: 0x%1").arg(item.pointerChain().baseAddress, 16, 16, QChar('0'));
                tip += QStringLiteral("\n最终地址: 0x%1").arg(item.address(), 16, 16, QChar('0'));
                return tip;
            }
            return QString("0x%1").arg(item.address(), 16, 16, QChar('0'));
        }

        if (index.column() == ColDisplayMode) {
            if (isNumericType(item.type())) {
                return item.hexDisplay() ? tr("单击切换为十进制显示") : tr("单击切换为16进制显示");
            }
            if (isStringValueType(item.type())) {
                return tr("单击切换字符串编码");
            }
            if (isByteArrayValueType(item.type())) {
                return item.hexDisplay() ? tr("单击切换为十进制显示") : tr("单击切换为16进制显示");
            }
        }
        if (index.column() == ColSigned) {
            if (isIntegerType(item.type())) {
                return item.signedDisplay() ? tr("单击切换为无符号显示") : tr("单击切换为有符号显示");
            }
            return tr("仅整数类型支持有符号/无符号切换");
        }
        if (index.column() == ColLength) {
            if (isNumericType(item.type())) {
                return tr("数据类型大小: %1 字节").arg(valueTypeSize(item.type()));
            }
            if (isStringValueType(item.type()) || isByteArrayValueType(item.type())) {
                return tr("数据长度: %1 字节（可编辑）").arg(
                    item.stringLength() > 0 ? item.stringLength() : static_cast<int>(item.buffer().size()));
            }
        }
    }

    // ---- 对齐 ----
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColValue || index.column() == ColAddress)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (index.column() == ColLength)
            return QVariant(Qt::AlignCenter);
        if (index.column() == ColSigned)
            return QVariant(Qt::AlignCenter);
        if (index.column() == ColDisplayMode)
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // ---- 前景色 ----
    if (role == Qt::ForegroundRole) {
        if (index.column() == ColAddress) {
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address(), display, isBase);
            if (isBase)
                return QBrush(QColor(0, 128, 0));
        }
        if (index.column() == ColValue && item.isChanged())
            return QBrush(Qt::red);
    }

    // ---- 复选框状态 ----
    if (role == Qt::CheckStateRole) {
        if (index.column() == ColFrozen)
            return item.isFrozen() ? Qt::Checked : Qt::Unchecked;

        if (index.column() == ColDisplayMode) {
            if (isNumericType(item.type()) || isByteArrayValueType(item.type()))
                return item.hexDisplay() ? Qt::Checked : Qt::Unchecked;
            return {};
        }

        if (index.column() == ColSigned) {
            if (isIntegerType(item.type()))
                return item.signedDisplay() ? Qt::Checked : Qt::Unchecked;
            return {};
        }
    }

    return {};
}

QVariant AddressListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColFrozen:      return "Frozen";
    case ColDescription: return "Description";
    case ColAddress:     return "Address";
    case ColValue:       return "Value";
    case ColType:        return "Type";
    case ColDisplayMode: return "Display";
    case ColSigned:      return "Signed";
    case ColLength:      return "Length";
    }
    return {};
}

bool AddressListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= m_items.size())
        return false;

    auto& item = m_items[index.row()];
    int row = index.row();

    // ---- CheckBox 切换 ----
    if (role == Qt::CheckStateRole) {
        bool checked = (value.toInt() == Qt::Checked);

        if (index.column() == ColFrozen) {
            item.m_frozen = checked;
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColDisplayMode) {
            if (isNumericType(item.m_type) || isByteArrayValueType(item.m_type)) {
                item.m_hexDisplay = checked;
                auto mem = ProcessManager::instance().memory();
                if (mem) item.refreshValue(mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColSigned) {
            if (isIntegerType(item.m_type)) {
                item.m_signedDisplay = checked;
                auto mem = ProcessManager::instance().memory();
                if (mem) item.refreshValue(mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        return false;
    }

    // ---- 文本编辑 ----
    if (role == Qt::EditRole) {
        if (index.column() == ColDescription) {
            QString newDesc = value.toString().trimmed();
            if (!newDesc.isEmpty())
                item.m_description = newDesc;
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColType) {
            ValueType newType = static_cast<ValueType>(value.toInt());
            if (newType != item.m_type) {
                item.m_type = newType;
                auto mem = ProcessManager::instance().memory();
                if (mem) {
                    item.refreshValue(mem);
                } else {
                    if (isStringValueType(item.m_type) || isByteArrayValueType(item.m_type)) {
                        item.m_buffer.clear();
                        item.m_stringLength = 0;
                    }
                }
                QMetaObject::invokeMethod(this, [this, row]() {
                    emitRowChanged(this, row);
                }, Qt::QueuedConnection);
            }
            return true;
        }

        if (index.column() == ColValue) {
            QString text = value.toString().trimmed();
            if (text.isEmpty()) return false;

            auto mem = ProcessManager::instance().memory();
            if (!mem) return false;

            uint64_t writeAddr = item.getEffectiveAddress(mem);

            if (isStringValueType(item.m_type)) {
                QByteArray encoded;
                switch (item.m_encoding) {
                case StringEncoding::UTF16:
                    encoded = QByteArray(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
                    break;
                case StringEncoding::UTF8:
                    encoded = text.toUtf8();
                    break;
                case StringEncoding::ASCII:
                default:
                    encoded = text.toLatin1();
                    break;
                }
                if (!encoded.isEmpty()) {
                    mem->write(writeAddr, encoded.constData(), encoded.size());
                    item.m_buffer.resize(encoded.size());
                    mem->read(writeAddr, item.m_buffer.data(), encoded.size());
                }
                item.m_changed = false;
                emitRowChanged(this, row);
                return true;
            }

            if (isByteArrayValueType(item.m_type)) {
                QStringList byteTokens = text.split(' ', Qt::SkipEmptyParts);
                std::vector<uint8_t> newBuffer;
                newBuffer.reserve(byteTokens.size());
                bool ok = true;
                for (const QString& byteStr : byteTokens) {
                    uint val = byteStr.toUInt(&ok, item.m_hexDisplay ? 16 : 10);
                    if (!ok || val > 0xFF) { ok = false; break; }
                    newBuffer.push_back(static_cast<uint8_t>(val));
                }
                if (!ok || newBuffer.empty()) return false;
                mem->write(writeAddr, newBuffer.data(), newBuffer.size());
                item.m_buffer.resize(newBuffer.size());
                mem->read(writeAddr, item.m_buffer.data(), newBuffer.size());
                item.m_changed = false;
                emitRowChanged(this, row);
                return true;
            }

            // 数值类型编辑
            bool ok = false;
            uint64_t newRaw = 0;
            int base = item.m_hexDisplay ? 16 : 0;

            switch (item.m_type) {
            case ValueType::Int8: {
                int v = text.toInt(&ok, base);
                if (ok) { newRaw = static_cast<uint8_t>(static_cast<int8_t>(v)); mem->write(writeAddr, &newRaw, 1); }
                break;
            }
            case ValueType::Int16: {
                int v = text.toInt(&ok, base);
                if (ok) { newRaw = static_cast<uint16_t>(static_cast<int16_t>(v)); mem->write(writeAddr, &newRaw, 2); }
                break;
            }
            case ValueType::Int32: {
                if (base == 16) {
                    uint v = text.toUInt(&ok, 16);
                    if (ok) { newRaw = v; mem->write(writeAddr, &newRaw, 4); }
                } else {
                    int v = text.toInt(&ok, 10);
                    if (ok) { std::memcpy(&newRaw, &v, sizeof(v)); mem->write(writeAddr, &newRaw, 4); }
                }
                break;
            }
            case ValueType::Int64: {
                if (base == 16) {
                    uint64_t v = text.toULongLong(&ok, 16);
                    if (ok) { newRaw = v; mem->write(writeAddr, &newRaw, 8); }
                } else {
                    int64_t v = text.toLongLong(&ok, 10);
                    if (ok) { std::memcpy(&newRaw, &v, sizeof(v)); mem->write(writeAddr, &newRaw, 8); }
                }
                break;
            }
            case ValueType::Float: {
                float f = text.toFloat(&ok);
                if (ok) { std::memcpy(&newRaw, &f, sizeof(f)); mem->write(writeAddr, &newRaw, 4); }
                break;
            }
            case ValueType::Double: {
                double d = text.toDouble(&ok);
                if (ok) { std::memcpy(&newRaw, &d, sizeof(d)); mem->write(writeAddr, &newRaw, 8); }
                break;
            }
            default: {
                uint v = text.toUInt(&ok, base);
                if (ok) { newRaw = v; mem->write(writeAddr, &newRaw, 4); }
                break;
            }
            }
            if (!ok) return false;

            size_t size = valueTypeSize(item.m_type);
            mem->read(writeAddr, &item.m_rawValue, size);
            item.m_changed = false;
            emitRowChanged(this, row);
            return true;
        }

        if (index.column() == ColDisplayMode) {
            if (isStringValueType(item.m_type)) {
                QString data = value.toString();
                QStringList parts = data.split(',');
                for (const QString& part : parts) {
                    QStringList kv = part.split(':');
                    if (kv.size() == 2 && kv[0] == "encoding") {
                        item.m_encoding = static_cast<StringEncoding>(kv[1].toInt());
                    }
                }
                auto mem = ProcessManager::instance().memory();
                if (mem) item.refreshValue(mem);
                QMetaObject::invokeMethod(this, [this, row]() {
                    emitRowChanged(this, row);
                }, Qt::QueuedConnection);
                return true;
            }
            return false;
        }

        if (index.column() == ColLength) {
            if (!isStringValueType(item.m_type) && !isByteArrayValueType(item.m_type))
                return false;
            bool ok = false;
            int newLen = value.toInt(&ok);
            if (!ok || newLen <= 0 || newLen > 4096) return false;
            item.m_stringLength = newLen;

            auto mem = ProcessManager::instance().memory();
            if (mem) {
                item.m_buffer.resize(newLen);
                uint64_t readAddr = item.getEffectiveAddress(mem);
                mem->read(readAddr, item.m_buffer.data(), newLen);
            }
            emitRowChanged(this, row);
            return true;
        }
    }

    return false;
}

Qt::ItemFlags AddressListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);

    switch (index.column()) {
    case ColFrozen:
        flags |= Qt::ItemIsUserCheckable;
        break;
    case ColDescription:
        flags |= Qt::ItemIsEditable;
        break;
    case ColType:
        flags |= Qt::ItemIsEditable;
        break;
    case ColValue:
        flags |= Qt::ItemIsEditable;
        break;
    case ColDisplayMode:
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isNumericType(item.type()) || isByteArrayValueType(item.type()))
                flags |= Qt::ItemIsUserCheckable;
            else if (isStringValueType(item.type()))
                flags |= Qt::ItemIsEditable;
        }
        break;
    case ColSigned:
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isIntegerType(item.type()))
                flags |= Qt::ItemIsUserCheckable;
        }
        break;
    case ColLength:
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (isStringValueType(item.type()) || isByteArrayValueType(item.type()))
                flags |= Qt::ItemIsEditable;
        }
        break;
    }

    return flags;
}

// ==================== 辅助 ====================

QString AddressListModel::pointerChainKey(const PointerChain& chain)
{
    // 基址 + 各级偏移量组成唯一 key
    QString key = QString("0x%1").arg(chain.baseAddress, 16, 16, QChar('0'));
    for (const auto& lv : chain.levels)
        key += QString("+0x%1").arg(static_cast<long long>(lv.offset), 0, 16);
    return key;
}

// ==================== 地址操作 ====================

int AddressListModel::addItem(uint64_t address, const QString& description,
                              uint64_t rawValue, ValueType type,
                              const PointerChain& pointerChain)
{
    // O(1) 去重检查
    if (pointerChain.isValid()) {
        QString key = pointerChainKey(pointerChain);
        if (m_pointerChainKeySet.find(key) != m_pointerChainKeySet.end())
            return -1;
    } else {
        if (m_normalAddressSet.find(address) != m_normalAddressSet.end())
            return -1;
    }

    int row = static_cast<int>(m_items.size());
    beginInsertRows(QModelIndex(), row, row);

    AddressItem::Config cfg;
    cfg.description   = description;
    cfg.address       = address;
    cfg.rawValue      = rawValue;
    cfg.type          = type;
    cfg.pointerChain  = pointerChain;

    // 先记录 set，再创建 item
    if (pointerChain.isValid())
        m_pointerChainKeySet.insert(pointerChainKey(pointerChain));
    else
        m_normalAddressSet.insert(address);

    m_items.push_back(AddressItem::create(cfg));

    endInsertRows();
    return row;
}

void AddressListModel::addItems(const std::vector<AddressItem>& newItems)
{
    if (newItems.empty()) return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + newItems.size() - 1);
    for (const auto& item : newItems) {
        // 同步更新 set
        if (item.isPointer())
            m_pointerChainKeySet.insert(pointerChainKey(item.pointerChain()));
        else
            m_normalAddressSet.insert(item.address());
    }
    m_items.insert(m_items.end(), newItems.begin(), newItems.end());
    endInsertRows();
}

void AddressListModel::addItemsFromScanResults(
    const std::vector<uint64_t>& addresses,
    const std::vector<std::string>& addressTexts,
    ScanDataType scanDataType)
{
    if (addresses.empty()) return;

    std::vector<AddressItem> newItems;
    newItems.reserve(addresses.size());

    // 批量内 O(1) 去重辅助（防止同一批内重复）
    std::unordered_set<uint64_t> batchNormalSet;
    std::unordered_set<QString> batchPointerSet;

    auto mem = ProcessManager::instance().memory();
    ValueType vt = scanDataTypeToValueType(scanDataType);
    size_t readSize = valueTypeSize(vt);

    for (int i = 0; i < addresses.size(); ++i) {
        uint64_t addr = addresses[i];
        AddressItem::Config cfg;
        cfg.address = addr;
        cfg.type = vt;

        if (isStringValueType(vt) || isByteArrayValueType(vt)) {
            if (isStringValueType(vt)) {
                switch (scanDataType) {
                case ScanDataType::Utf16String:
                    cfg.encoding = StringEncoding::UTF16;
                    break;
                case ScanDataType::Utf8String:
                    cfg.encoding = StringEncoding::UTF8;
                    break;
                default:
                    cfg.encoding = StringEncoding::ASCII;
                    break;
                }
            }

            int probeLen = isStringValueType(vt) ? 256 : static_cast<int>(readSize);
            cfg.buffer.resize(probeLen);
            if (mem) {
                mem->read(addr, cfg.buffer.data(), probeLen);
            }
            if (isStringValueType(vt)) {
                int realLen = 0;
                if (cfg.encoding == StringEncoding::UTF16) {
                    const char16_t* u16 = reinterpret_cast<const char16_t*>(cfg.buffer.data());
                    int u16len = probeLen / 2;
                    while (realLen < u16len && u16[realLen] != 0)
                        ++realLen;
                    realLen = std::min(realLen * 2, 32);
                } else {
                    const char* data = reinterpret_cast<const char*>(cfg.buffer.data());
                    while (realLen < probeLen && data[realLen] != '\0')
                        ++realLen;
                    realLen = std::min(realLen, 32);
                }
                if (realLen <= 0) realLen = 1;
                cfg.buffer.resize(realLen);
                cfg.stringLength = realLen;
            } else {
                cfg.buffer.resize(readSize);
                cfg.stringLength = static_cast<int>(readSize);
                cfg.hexDisplay = true;
            }
        } else {
            if (mem)
                mem->read(addr, &cfg.rawValue, readSize);
        }

        if (i < static_cast<int>(addressTexts.size()) && !addressTexts[i].empty())
            cfg.description = QString::fromStdString(addressTexts[i]);
        else
            cfg.description = QString("0x%1").arg(addr, 0, 16);

        // O(1) 去重检查：全局 + 组内
        if (cfg.pointerChain.isValid()) {
            QString key = pointerChainKey(cfg.pointerChain);
            if (m_pointerChainKeySet.find(key) != m_pointerChainKeySet.end())
                continue;
            if (batchPointerSet.find(key) != batchPointerSet.end())
                continue;
            batchPointerSet.insert(std::move(key));
        } else {
            if (m_normalAddressSet.find(cfg.address) != m_normalAddressSet.end())
                continue;
            if (batchNormalSet.find(cfg.address) != batchNormalSet.end())
                continue;
            batchNormalSet.insert(cfg.address);
        }

        newItems.push_back(AddressItem::create(cfg));
    }

    addItems(newItems);
}

void AddressListModel::updateItem(int row, const ItemUpdate& update)
{
    if (row < 0 || row >= static_cast<int>(m_items.size())) return;

    auto& item = m_items[row];

    // 如果地址或指针链发生变化，需要更新 set
    bool needRehashNormal = !item.isPointer() && !update.pointerChain.isValid()
                            && item.address() != update.address;
    bool needRehashPointer = item.isPointer() && update.pointerChain.isValid()
                             && !(item.pointerChain() == update.pointerChain);

    // 清除旧的 key
    if (needRehashNormal)
        m_normalAddressSet.erase(item.address());
    if (needRehashPointer)
        m_pointerChainKeySet.erase(pointerChainKey(item.pointerChain()));

    item.m_address      = update.address;
    item.m_description  = update.description;
    item.m_type         = update.type;
    item.m_hexDisplay   = update.hexDisplay;
    item.m_signedDisplay = update.signedDisplay;
    item.m_encoding     = update.encoding;
    item.m_stringLength = update.stringLength;
    item.m_buffer       = update.buffer;
    item.m_pointerChain = update.pointerChain;

    // 插入新的 key
    if (needRehashNormal)
        m_normalAddressSet.insert(update.address);
    if (needRehashPointer)
        m_pointerChainKeySet.insert(pointerChainKey(update.pointerChain));

    emitRowChanged(this, row);
}

void AddressListModel::removeItem(int row)
{
    if (row < 0 || row >= m_items.size()) return;

    // 清理 set
    const auto& item = m_items[row];
    if (item.isPointer())
        m_pointerChainKeySet.erase(pointerChainKey(item.pointerChain()));
    else
        m_normalAddressSet.erase(item.address());

    beginRemoveRows(QModelIndex(), row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
}

void AddressListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_normalAddressSet.clear();
    m_pointerChainKeySet.clear();
    endResetModel();
}

// ==================== 批量操作 ====================

void AddressListModel::refreshValues(std::shared_ptr<IMemoryAccessor> mem)
{
    if (!mem || m_items.empty()) return;

    int firstChanged = -1;
    int lastChanged = -1;

    for (int i = 0; i < m_items.size(); ++i) {
        auto& item = m_items[i];
        bool oldChanged = item.m_changed;
        item.refreshValue(mem);

        if (item.m_changed != oldChanged) {
            if (firstChanged == -1) firstChanged = i;
            lastChanged = i;
        }
    }

    if (firstChanged != -1) {
        QModelIndex top = index(firstChanged, ColValue);
        QModelIndex bottom = index(lastChanged, ColValue);
        emit dataChanged(top, bottom, { Qt::DisplayRole, Qt::ForegroundRole });
    }
}

void AddressListModel::freezeAll(std::shared_ptr<IMemoryAccessor> mem)
{
    if (!mem) return;

    for (auto& item : m_items) {
        if (item.m_frozen) {
            item.freezeWrite(mem);
        }
    }
}

int AddressListModel::findRow(const AddressItem* ptr) const
{
    if (!ptr) return -1;
    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [ptr](const AddressItem& item) { return &item == ptr; });
    if (it == m_items.end()) return -1;
    return static_cast<int>(std::distance(m_items.begin(), it));
}
