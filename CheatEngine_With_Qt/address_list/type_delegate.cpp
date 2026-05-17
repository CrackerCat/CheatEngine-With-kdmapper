#include "address_list/type_delegate.h"
#include "address_list/address_list_model.h"
#include "address_list/address_item.h"

#include <QComboBox>
#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QMouseEvent>
#include <QAbstractItemView>

TypeDelegate::TypeDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QWidget* TypeDelegate::createEditor(QWidget* parent,
                                    const QStyleOptionViewItem& /*option*/,
                                    const QModelIndex& /*index*/) const
{
    QComboBox* editor = new QComboBox(parent);

    // 添加所有可用的数据类型选项
    auto types = AddressItem::getTypeOptions();
    editor->addItems(types);
    // 保存枚举值作为 item data
    for (int i = 0; i < types.size(); ++i)
        editor->setItemData(i, static_cast<int>(static_cast<AddressItem::Type>(i)));

    // 用户选择下拉项后立即提交并关闭编辑器，无需再点击其他位置
    TypeDelegate* nonConstThis = const_cast<TypeDelegate*>(this);
    QObject::connect(editor, QOverload<int>::of(&QComboBox::activated),
                     nonConstThis, [nonConstThis, editor]() {
        emit nonConstThis->commitData(editor);
        emit nonConstThis->closeEditor(editor, QAbstractItemDelegate::NoHint);
    });

    return editor;
}

bool TypeDelegate::editorEvent(QEvent* event,
                                QAbstractItemModel* model,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index)
{
    // 单击立即进入编辑模式弹出下拉框
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            // 找到所属的 QAbstractItemView 并进入编辑模式
            QObject* p = parent();
            while (p) {
                QAbstractItemView* view = qobject_cast<QAbstractItemView*>(p);
                if (view) {
                    view->edit(index);
                    return true;
                }
                p = p->parent();
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void TypeDelegate::setEditorData(QWidget* editor,
                                 const QModelIndex& index) const
{
    QString currentText = index.data(Qt::DisplayRole).toString();
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (combo) {
        int idx = combo->findText(currentText);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
}

void TypeDelegate::setModelData(QWidget* editor,
                                QAbstractItemModel* model,
                                const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;

    // 获取用户选中的数据类型
    int selectedType = combo->currentData().toInt();
    // 通过 EditRole 写入模型，模型负责更新数据但不触发刷新（延迟刷新由 activated 连接处理）
    model->setData(index, selectedType, Qt::EditRole);
}

void TypeDelegate::updateEditorGeometry(QWidget* editor,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& /*index*/) const
{
    editor->setGeometry(option.rect);
    // 定位完成后立即展开下拉框
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (combo) {
        combo->showPopup();
    }
}

void TypeDelegate::paint(QPainter* painter,
                         const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    // 先绘制默认样式（文字、背景、选中高亮等）
    QStyledItemDelegate::paint(painter, option, index);

    // 在下拉框右侧绘制一个下拉箭头，使其看起来始终是可下拉的
    QStyleOptionComboBox comboOption;
    comboOption.rect = option.rect;
    comboOption.rect.setLeft(option.rect.right() - 20);
    comboOption.state = option.state;
    comboOption.subControls = QStyle::SC_ComboBoxArrow;
    comboOption.activeSubControls = QStyle::SC_ComboBoxArrow;

    QApplication::style()->drawComplexControl(QStyle::CC_ComboBox, &comboOption, painter);
}
