#pragma once

#include <QStyledItemDelegate>

/// @brief 数据呈现方式列专用委托
/// - 数值/字节数组类型：CheckBox 形式（Qt 自动绘制）
/// - 字符串类型：弹出编码选择下拉框 (ASCII/UTF-8/UTF-16)
class DisplayModeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit DisplayModeDelegate(QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;

    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override;

    void updateEditorGeometry(QWidget* editor,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

    /// @brief 为字符串类型绘制下拉箭头
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

protected:
    /// @brief 单击直接弹出编码下拉框
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};

/// @brief Signed 列专用委托
/// - 整数类型：CheckBox 形式（Qt 自动绘制）
/// - 非整数类型：无操作
class SignedDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit SignedDelegate(QObject* parent = nullptr);
};
