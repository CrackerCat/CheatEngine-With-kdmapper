#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <memory>
#include <vector>
#include "address_list/address_item.h"
#include "ui_Add_Or_Change_Address.h"

/// @brief "添加/修改地址"对话框
///
/// 以 AddressItem::Config 作为输入/输出载体，确保所有属性（包括指针链、编码等）
/// 始终完整传递，不会丢失。
///
/// 使用方式：
///   // 编辑已有地址
///   AddressItem::Config config = item.toConfig();
///   Add_Or_Change_Address_Dialog dlg(this, config, true); // true = 编辑模式
///   if (dlg.exec() == QDialog::Accepted) {
///       config = dlg.resultConfig();   // 取回完整配置
///       model->updateItem(row, config);
///   }
class Add_Or_Change_Address_Dialog : public QDialog
{
    Q_OBJECT

public:
    /// @param parent 父窗口
    /// @param cfg    预填配置（地址、描述、类型等所有属性）
    /// @param editMode true=编辑模式, false=添加模式
    explicit Add_Or_Change_Address_Dialog(QWidget* parent,
                                          const AddressItem::Config& cfg,
                                          bool editMode = false);
    ~Add_Or_Change_Address_Dialog() override = default;

    /// 获取用户编辑后的完整配置
    AddressItem::Config resultConfig() const;

private slots:
    void onAddressTextChanged(const QString& text);
    void onDataTypeChanged(int index);
    void onPointerCheckChanged(bool checked);
    void onPointerAddressChanged(const QString& text);
    void onOffsetValueChanged();
    void onIncreaseOffset();
    void onDecreaseOffset();
    void onAddOffset();
    void onDeleteOffset();

private:
    /// 单个指针层级的数据 + UI 控件
    struct PointerLevelWidgets {
        QWidget*    container   = nullptr;
        QLabel*     levelLabel  = nullptr;
        QLabel*     resultLabel = nullptr;
        QLineEdit*  offsetEdit  = nullptr;
        QPushButton* decBtn     = nullptr;
        QPushButton* incBtn     = nullptr;

        int64_t  offset          = 0;
        uint64_t pointerValue    = 0;
        uint64_t computedAddr    = 0;
    };

    void setupUi();
    void populateDataTypeCombo();
    void refreshComputedValue();
    void updateStringControlsVisibility();

    // 指针相关
    void refreshPointerValue();
    uint64_t parseAddressOrModule(const QString& text, bool* ok = nullptr) const;
    void createPointerLevelWidget(int levelIndex);
    void removeLastPointerLevel();
    void rebuildPointerLevelSignals(int levelIndex);

    bool validateInput();

    size_t currentDataTypeSize() const;

    std::unique_ptr<Ui::Dialog_Add_Or_Change_Address> m_ui;

    // 预填配置快照（用于取消指针时恢复原始地址等）
    AddressItem::Config m_initialConfig;
    bool      m_editMode = false;

    // 指针相关
    uint64_t m_pointerBaseAddr = 0;
    uint64_t m_computedFinalAddr = 0;
    QString  m_originalAddressText;

    std::vector<PointerLevelWidgets> m_pointerLevels;
    uint64_t m_lastPointerFinalAddr = 0;

    // ── 实时值自动刷新定时器 ──
    QTimer* m_refreshTimer = nullptr;
};
