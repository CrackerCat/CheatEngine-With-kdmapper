#pragma once

#include <QStyledItemDelegate>

/// @brief Type 列专用委托：显示为下拉框 ComboBox
class TypeDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit TypeDelegate(QObject* parent = nullptr);

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

    /// @brief 绘制下拉框样式（始终显示下拉箭头）
    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

protected:
    /// @brief 处理鼠标事件：单击直接弹出下拉框
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};
