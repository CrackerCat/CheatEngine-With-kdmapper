#include "address_list_model.h"
#include "process/process_manager.h"
#include "interface/imemory_accessor.h"

#include <algorithm>

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

    // ★ 核心策略：对于所有标准 UI 角色，直接委托给 AddressItem::displayData
    //    让 AddressItem 自治地决定每个单元格显示什么
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case Qt::CheckStateRole:
    case Qt::ForegroundRole:
    case Qt::TextAlignmentRole:
    case Qt::ToolTipRole:
        return item.displayData(index.column(), role);
    default:
        break;
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
    case ColType:        return "Type";
    case ColDisplayMode: return "Display";
    case ColSigned:      return "Signed";
    case ColLength:      return "Length";
    case ColValue:       return "Value";
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
            if (AddressItem::isNumericType(item.m_type) || AddressItem::isByteArrayType(item.m_type)) {
                item.m_hexDisplay = checked;
                auto mem = ProcessManager::instance().memory();
                if (mem) item.refreshValue(mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColSigned) {
            if (AddressItem::isIntegerType(item.m_type)) {
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
            AddressItem::Type newType = static_cast<AddressItem::Type>(value.toInt());
            if (newType != item.m_type) {
                item.m_type = newType;
                auto mem = ProcessManager::instance().memory();
                if (mem) {
                    item.refreshValue(mem);
                } else {
                    if (AddressItem::isStringType(item.m_type) || AddressItem::isByteArrayType(item.m_type)) {
                        item.m_buffer.clear();
                        item.m_stringLength = 0;
                    }
                }
                // ★ 修复：去掉 Qt::QueuedConnection，立即刷新
                emitRowChanged(this, row);
            }
            return true;
        }

        if (index.column() == ColValue) {
            QString text = value.toString().trimmed();
            if (text.isEmpty()) return false;

            auto mem = ProcessManager::instance().memory();
            if (!mem) return false;

            // ★ 直接委托给 AddressItem::tryEditValue
            if (item.tryEditValue(text, mem)) {
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColDisplayMode) {
            if (AddressItem::isStringType(item.m_type)) {
                QString data = value.toString();
                // 解析 "encoding:N" 格式
                if (data.startsWith("encoding:")) {
                    int enc = data.mid(9).toInt();
                    item.m_encoding = static_cast<AddressItem::Encoding>(enc);
                }
                auto mem = ProcessManager::instance().memory();
                if (mem) item.refreshValue(mem);
                emitRowChanged(this, row);
                return true;
            }
            return false;
        }

        if (index.column() == ColLength) {
            if (!AddressItem::isStringType(item.m_type) && !AddressItem::isByteArrayType(item.m_type))
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
            if (AddressItem::isNumericType(item.type()) || AddressItem::isByteArrayType(item.type()))
                flags |= Qt::ItemIsUserCheckable;
            else if (AddressItem::isStringType(item.type()))
                flags |= Qt::ItemIsEditable;
        }
        break;
    case ColSigned:
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (AddressItem::isIntegerType(item.type()))
                flags |= Qt::ItemIsUserCheckable;
        }
        break;
    case ColLength:
        if (index.row() < m_items.size()) {
            const auto& item = m_items[index.row()];
            if (AddressItem::isStringType(item.type()) || AddressItem::isByteArrayType(item.type()))
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
                              uint64_t rawValue, AddressItem::Type type,
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
    ScanDataType scanDataType,
    const std::vector<ScanDataType>* perAddressTypes)
{
    if (addresses.empty()) return;

    std::vector<AddressItem> newItems;
    newItems.reserve(addresses.size());

    // 批量内 O(1) 去重辅助（防止同一批内重复）
    std::unordered_set<uint64_t> batchNormalSet;
    std::unordered_set<QString> batchPointerSet;

    auto mem = ProcessManager::instance().memory();
    bool isAllMode = (perAddressTypes != nullptr);

    for (int i = 0; i < static_cast<int>(addresses.size()); ++i) {
        uint64_t addr = addresses[i];

        // ★ All 模式下获取此地址的实际匹配类型
        ScanDataType effectiveScanType = scanDataType;
        if (isAllMode && i < static_cast<int>(perAddressTypes->size())) {
            effectiveScanType = (*perAddressTypes)[i];
        }

        AddressItem::Type vt = AddressItem::fromScanDataType(effectiveScanType);
        size_t readSize = AddressItem::typeSize(vt);

        AddressItem::Config cfg;
        cfg.address = addr;
        cfg.type = vt;

        if (AddressItem::isStringType(vt) || AddressItem::isByteArrayType(vt)) {
            if (AddressItem::isStringType(vt)) {
                cfg.encoding = AddressItem::encodingFromScanDataType(effectiveScanType);
            }

            int probeLen = AddressItem::isStringType(vt) ? 256 : static_cast<int>(readSize);
            cfg.buffer.resize(probeLen);
            if (mem) {
                mem->read(addr, cfg.buffer.data(), probeLen);
            }
            if (AddressItem::isStringType(vt)) {
                int realLen = 0;
                if (cfg.encoding == AddressItem::Encoding::UTF16) {
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
            // ★ 按实际类型对应字节数读取并存储为 rawValue
            if (mem && readSize > 0) {
                if (readSize <= 8) {
                    mem->read(addr, &cfg.rawValue, readSize);
                }
            }
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
    bool wasPointer = item.isPointer();
    bool nowPointer = update.pointerChain.isValid();
    uint64_t oldAddr = item.address();
    uint64_t newAddr = update.address;

    // 清除旧的 key
    if (wasPointer)
        m_pointerChainKeySet.erase(pointerChainKey(item.pointerChain()));
    else
        m_normalAddressSet.erase(oldAddr);

    // ★ 用 applyConfig 替代逐个写成员
    item.applyConfig(update);

    // 插入新的 key
    if (nowPointer)
        m_pointerChainKeySet.insert(pointerChainKey(update.pointerChain));
    else
        m_normalAddressSet.insert(newAddr);

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

    // ★ 强制刷新所有条目：无条件对所有条目调用 refreshValue，
    //    然后发送不限定 roles 的 dataChanged（所有角色缓存全部失效），
    //    彻底解决"改变列宽才更新"的问题。
    QModelIndex top = index(0, 0);
    QModelIndex bottom = index(m_items.size() - 1, ColumnCount_ - 1);

    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i].refreshValue(mem);
    }

    // ★ 不指定 roles 列表 = 所有角色全部失效
    //    这比只传 {DisplayRole, ForegroundRole}
    //    更能强制 Qt 视图完全重绘（包括已缓存的 EditRole 等）
    emit dataChanged(top, bottom);
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
