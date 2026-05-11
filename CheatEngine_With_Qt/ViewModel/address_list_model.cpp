#include "ViewModel\address_list_model.h"
#include "process\process_manager.h"
#include <QBrush>
#include <QColor>

AddressListModel::AddressListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int AddressListModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(m_items.size());
}

int AddressListModel::columnCount(const QModelIndex&) const
{
    return 5; // Description, Address, Value, Type, Frozen
}

QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.row() < 0 || index.row() >= static_cast<int>(m_items.size()))
        return {};

    const auto& item = m_items[index.row()];

    // 显示角色
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return item.description;
        case 1: { // 地址列：使用模块解析
            std::string display;
            bool isBase = false;
            ProcessManager::instance().resolveAddress(item.address, display, isBase);
            return QString::fromStdString(display);
        }
        case 2: return item.value; // 数值列（十进制）
        case 3: { // 类型列
            switch (item.type) {
            case ValueType::Integer: return "Int";
            case ValueType::Float:   return "Float";
            case ValueType::Double:  return "Double";
            default: return "Unknown";
            }
        }
        case 4: return QVariant(); // 冻结列不显示文本，由复选框处理
        }
    }
    // 前景颜色：地址基址绿色
    if (role == Qt::ForegroundRole && index.column() == 1) {
        std::string display;
        bool isBase = false;
        ProcessManager::instance().resolveAddress(item.address, display, isBase);
        if (isBase)
            return QBrush(QColor(0, 128, 0)); // 绿色
        return {};
    }
    // 冻结列：复选框状态
    if (role == Qt::CheckStateRole && index.column() == 4) {
        return item.frozen ? Qt::Checked : Qt::Unchecked;
    }
    // 数值变化时标红（可选）
    if (role == Qt::ForegroundRole && index.column() == 2 && item.changed) {
        return QBrush(Qt::red);
    }
    return {};
}

QVariant AddressListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case 0: return "Description";
    case 1: return "Address";
    case 2: return "Value";
    case 3: return "Type";
    case 4: return "Frozen";
    }
    return {};
}

bool AddressListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_items.size()))
        return false;

    if (role == Qt::CheckStateRole && index.column() == 4) {
        auto& item = m_items[index.row()];
        item.frozen = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, { Qt::CheckStateRole });
        return true;
    }
    // 允许修改描述 ?
    if (role == Qt::EditRole && index.column() == 0) {
        m_items[index.row()].description = value.toString();
        emit dataChanged(index, index, { Qt::DisplayRole });
        return true;
    }
    return false;
}

Qt::ItemFlags AddressListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = QAbstractTableModel::flags(index);
    if (index.column() == 4)
        flags |= Qt::ItemIsUserCheckable; // 冻结列可勾选
    if (index.column() == 0)
        flags |= Qt::ItemIsEditable;       // 描述列可编辑
    return flags;
}

void AddressListModel::addItem(uint64_t address, const QString& description, uint64_t value, ValueType type)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.push_back({ description, address, value, type, false, 0, false });
    endInsertRows();
}

void AddressListModel::removeItem(int row)
{
    if (row < 0 || row >= static_cast<int>(m_items.size()))
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
}

void AddressListModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}