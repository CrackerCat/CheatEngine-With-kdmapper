#pragma once
#include <QAbstractTableModel>
#include <vector>
#include "type_define\address_item.h"

class AddressListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit AddressListModel(QObject* parent = nullptr);

    // 必须实现的接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // 地址操作
    void addItem(uint64_t address, const QString& description, uint64_t value, ValueType type = ValueType::Integer);
    void removeItem(int row);
    void clear();

    // 获取全部条目的引用（供后台冻结等使用）
    std::vector<AddressItem>& items() { return m_items; }
    const std::vector<AddressItem>& items() const { return m_items; }

private:
    std::vector<AddressItem> m_items;
};