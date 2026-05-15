#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <memory>
#include <vector>
#include "type_define/address_item.h"
#include "ui_Add_Or_Change_Address.h"

/// @brief "添加/修改地址"对话框
///
/// 支持两种模式：
///   - 添加模式：用户手动输入地址、描述等，点击 OK 后添加到地址列表
///   - 修改模式：双击地址列表中某行的 Address 列打开，预填已有信息
class Add_Or_Change_Address_Dialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode {
        Add,    // 手动添加新地址
        Edit    // 编辑已有地址
    };

    /// @param parent 父窗口
    /// @param mode   对话框模式
    /// @param addr   预填地址（0 = 不预填）
    /// @param desc   预填描述
    /// @param vt     预填值类型
    explicit Add_Or_Change_Address_Dialog(QWidget* parent,
                                          Mode mode = Mode::Add,
                                          uint64_t addr = 0,
                                          const QString& desc = {},
                                          ValueType vt = ValueType::Int32);
    ~Add_Or_Change_Address_Dialog() override = default;

    // ---- 获取用户输入的结果 ----
    uint64_t    address() const;
    QString     description() const;
    ValueType   valueType() const;
    bool        hexDisplay() const;
    bool        signedDisplay() const;

    // 字符串相关（仅当类型为 String 时有效）
    StringEncoding encoding() const;
    int             stringLength() const;

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
        QWidget*    container   = nullptr;   // widget_pointer_level_N
        QLabel*     levelLabel  = nullptr;   // label_pointer_levelX (" N 级指针")
        QLabel*     resultLabel = nullptr;   // label_offset_compute_value (显示 ???+?=???)
        QLineEdit*  offsetEdit  = nullptr;   // lineEdit_offset_Value
        QPushButton* decBtn     = nullptr;   // pushButton_decrease_offset
        QPushButton* incBtn     = nullptr;   // pushButton_increase_offset

        int64_t  offset          = 0;   // 当前解析的偏移值
        uint64_t pointerValue    = 0;   // 从内存 [currentAddr] 读取到的指针值
        uint64_t computedAddr    = 0;   // 该级最终地址 = pointerValue + offset（即下一级的 currentAddr）
    };


    void setupUi();
    void populateDataTypeCombo();
    void refreshComputedValue();
    void updateStringControlsVisibility();

    /// 指针相关
    void refreshPointerValue();                    // 完整遍历所有级计算最终地址
    uint64_t parseAddressOrModule(const QString& text, bool* ok = nullptr) const; // 解析 "module.dll+0x1234"
    void createPointerLevelWidget(int levelIndex); // 动态创建第 N 级指针控件（N>=1, 0 级为内置 widget_pointer_level_1）
    void removeLastPointerLevel();                 // 移除最后一级指针控件
    void rebuildPointerLevelSignals(int levelIndex); // 重新连接该级控件的信号

    bool validateInput();

    // 获取当前数据类型对应的字节大小（指针偏移步进用）
    size_t currentDataTypeSize() const;

    std::unique_ptr<Ui::Dialog_Add_Or_Change_Address> m_ui;

    // 预填时的 ValueType — 用于 comboBox 索引映射
    ValueType m_initialValueType = ValueType::Int32;
    Mode      m_mode = Mode::Add;

    // ── 指针相关 ──
    uint64_t m_pointerBaseAddr = 0;     // 基础指针地址（lineEdit_address_pointer 解析值）
    uint64_t m_computedFinalAddr = 0;   // 最终计算地址
    QString  m_originalAddressText;     // 初始 lineEdit_address 文本（取消指针时恢复）

    /// 动态指针层级列表（下标 0 = 第1级, 1 = 第2级 …）
    std::vector<PointerLevelWidgets> m_pointerLevels;

    /// 取消指针前暂存的上一次最终地址（用于取消时恢复）
    uint64_t m_lastPointerFinalAddr = 0;
};
