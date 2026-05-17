#pragma once
#include <QAbstractTableModel>
#include <vector>
#include <unordered_set>
#include <QString>
#include "type_define/address_item.h"

class AddressListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /// 列索引
    enum Column {
        ColFrozen = 0,      // 冻结列（CheckBox）
        ColDescription,
        ColAddress,
        ColType,
        ColDisplayMode,     // 数据呈现方式列（CheckBox 形式，单击切换）
        ColSigned,          // 有符号/无符号开关列（CheckBox 形式，单击切换，仅整数类型）
        ColLength,          // 长度列（字符串/字节数组的长度）
        ColValue,           // 值列（放到最后，默认 Stretch 拉伸）
        ColumnCount_
    };

    /// 更新单条地址项时直接使用 AddressItem::Config
    using ItemUpdate = AddressItem::Config;

    explicit AddressListModel(QObject* parent = nullptr);

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // ---- 只读访问 ----

    /// 获取条目数量
    int itemCount() const { return static_cast<int>(m_items.size()); }

    /// 获取第 row 个条目的只读引用
    const AddressItem& itemAt(int row) const { return m_items.at(row); }

    // ---- 地址操作 ----

    /// 添加单个地址（含指针链），自动去重，返回新条目所在行号（重复则返回 -1）
    int addItem(uint64_t address, const QString& description,
                uint64_t rawValue, ValueType type = ValueType::Int32,
                const PointerChain& pointerChain = {});

    /// 批量从扫描结果添加，自动去重（自动读取当前值）
    void addItemsFromScanResults(const std::vector<uint64_t>& addresses,
                                 const std::vector<std::string>& addressTexts,
                                 ScanDataType scanDataType);

    /// 用 ItemUpdate 更新指定行
    void updateItem(int row, const ItemUpdate& update);

    void removeItem(int row);
    void clear();

    /// 更新所有条目的当前值（由定时器调用）
    void refreshValues(std::shared_ptr<class IMemoryAccessor> mem);

    /// 冻结定时器：将所有冻结项的当前值写回内存
    void freezeAll(std::shared_ptr<class IMemoryAccessor> mem);

    /// 查找某个条目的行号（按指针相等比较）
    int findRow(const AddressItem* ptr) const;

signals:
    /// 当数据被外部更新（如 freezeAll）后发出，视图需要刷新
    void dataRefreshed();

private:
    /// 内部批量追加（由 addItemsFromScanResults 使用，不含去重）
    void addItems(const std::vector<AddressItem>& newItems);

    /// 构建指针链的去重 key
    static QString pointerChainKey(const PointerChain& chain);

    std::vector<AddressItem> m_items;
    std::unordered_set<uint64_t> m_normalAddressSet;   // O(1) 普通地址去重
    std::unordered_set<QString> m_pointerChainKeySet;  // O(1) 指针链去重
};
