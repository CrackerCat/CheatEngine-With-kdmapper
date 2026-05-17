#include "address_list/display_mode_delegate.h"
#include "address_list/address_list_model.h"
#include "address_list/address_item.h"

#include <QComboBox>
#include <QMouseEvent>
#include <QAbstractItemView>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QApplication>
#include <QStyle>

// ==================== DisplayModeDelegate ====================

DisplayModeDelegate::DisplayModeDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

bool DisplayModeDelegate::editorEvent(QEvent* event,
                                      QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
    // 仅字符串类型的 DisplayMode 列需要单击弹出编辑器
    QModelIndex typeIdx = index.sibling(index.row(), AddressListModel::ColType);
    QString typeText = typeIdx.data(Qt::DisplayRole).toString();
    if (typeText == AddressItem::typeName(AddressItem::Type::String) && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
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

QWidget* DisplayModeDelegate::createEditor(QWidget* parent,
                                           const QStyleOptionViewItem& /*option*/,
                                           const QModelIndex& index) const
{
    // 获取当前行的数据类型
    QModelIndex typeIdx = index.sibling(index.row(), AddressListModel::ColType);
    QString typeText = typeIdx.data(Qt::DisplayRole).toString();

    // 仅字符串类型需要编辑器（点击切换编码）
    if (typeText == AddressItem::typeName(AddressItem::Type::String)) {
        QComboBox* combo = new QComboBox(parent);
        combo->addItem("ASCII", static_cast<int>(AddressItem::Encoding::ASCII));
        combo->addItem("UTF-8", static_cast<int>(AddressItem::Encoding::UTF8));
        combo->addItem("UTF-16", static_cast<int>(AddressItem::Encoding::UTF16));

        // 用户选择下拉项后立即提交并关闭编辑器
        DisplayModeDelegate* nonConstThis = const_cast<DisplayModeDelegate*>(this);
        QObject::connect(combo, QOverload<int>::of(&QComboBox::activated),
                         nonConstThis, [nonConstThis, combo]() {
            emit nonConstThis->commitData(combo);
            emit nonConstThis->closeEditor(combo, QAbstractItemDelegate::NoHint);
        });

        return combo;
    }

    // 数值类型和字节数组类型使用 CheckBox，无需编辑器
    return nullptr;
}

void DisplayModeDelegate::setEditorData(QWidget* editor,
                                        const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;

    QString displayText = index.data(Qt::DisplayRole).toString();
    int idx = combo->findText(displayText);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
}

void DisplayModeDelegate::setModelData(QWidget* editor,
                                       QAbstractItemModel* model,
                                       const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;

    QString selected = combo->currentText();
    int encoding = combo->currentData().toInt();

    QStringList parts;
    parts << QString("encoding:%1").arg(encoding);
    model->setData(index, parts.join(','), Qt::EditRole);
}

void DisplayModeDelegate::updateEditorGeometry(QWidget* editor,
                                               const QStyleOptionViewItem& option,
                                               const QModelIndex& index) const
{
    editor->setGeometry(option.rect);
    // 字符串编码列：定位完成后立即展开下拉框
    QModelIndex typeIdx = index.sibling(index.row(), AddressListModel::ColType);
    if (typeIdx.data(Qt::DisplayRole).toString() == AddressItem::typeName(AddressItem::Type::String)) {
        QComboBox* combo = qobject_cast<QComboBox*>(editor);
        if (combo)
            combo->showPopup();
    }
}

void DisplayModeDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    // 先绘制默认样式（文字、背景等）
    QStyledItemDelegate::paint(painter, option, index);

    // 检查是否是字符串类型，是则绘制下拉箭头
    QModelIndex typeIdx = index.sibling(index.row(), AddressListModel::ColType);
    QString typeText = typeIdx.data(Qt::DisplayRole).toString();
    if (typeText == AddressItem::typeName(AddressItem::Type::String)) {
        QStyleOptionComboBox comboOption;
        comboOption.rect = option.rect;
        comboOption.rect.setLeft(option.rect.right() - 20);
        comboOption.state = option.state;
        comboOption.subControls = QStyle::SC_ComboBoxArrow;
        comboOption.activeSubControls = QStyle::SC_ComboBoxArrow;

        QApplication::style()->drawComplexControl(QStyle::CC_ComboBox, &comboOption, painter);
    }
}

// ==================== SignedDelegate ====================

SignedDelegate::SignedDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}
