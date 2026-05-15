#include "Add_Or_Change_Address.h"
#include "process/process_manager.h"
#include "interface/imemory_accessor.h"
#include "type_define/address_item.h"

#include <QMessageBox>
#include <QIntValidator>
#include <QRegularExpression>
#include <cstring>
#include <cctype>
#include <charconv>

// ==================== 构造 ====================

Add_Or_Change_Address_Dialog::Add_Or_Change_Address_Dialog(
    QWidget* parent, Mode mode, uint64_t addr,
    const QString& desc, ValueType vt)
    : QDialog(parent)
    , m_ui(std::make_unique<Ui::Dialog_Add_Or_Change_Address>())
    , m_initialValueType(vt)
    , m_mode(mode)
{
    setupUi();

    // 预填地址
    if (addr != 0)
        m_ui->lineEdit_address->setText(QString("0x%1").arg(addr, 16, 16, QChar('0')));
    else
        m_ui->lineEdit_address->clear();

    // 保存初始地址文本，取消指针时恢复
    m_originalAddressText = m_ui->lineEdit_address->text();
    // 默认 lineEdit_base_pointer 也显示原始地址（与 CE 一致）
    m_ui->lineEdit_base_pointer->setText(m_originalAddressText);

    m_ui->lineEdit_description->setText(desc);
    onAddressTextChanged(m_ui->lineEdit_address->text());
}

// ==================== UI 初始化 ====================

void Add_Or_Change_Address_Dialog::setupUi()
{
    m_ui->setupUi(this);

    if (m_mode == Mode::Edit)
        setWindowTitle(tr("修改地址"));
    else
        setWindowTitle(tr("添加地址"));

    // ── 限定地址输入为Hex/Decimal ──
    m_ui->lineEdit_address->setPlaceholderText("0x12345678");
    m_ui->lineEdit_base_pointer->setPlaceholderText("模块名+0x1234 或 0x12345678");

    // ── comboBox_encoding ──
    m_ui->comboBox_encoding->clear();
    m_ui->comboBox_encoding->addItem("ASCII",  static_cast<int>(StringEncoding::ASCII));
    m_ui->comboBox_encoding->addItem("UTF-8",  static_cast<int>(StringEncoding::UTF8));
    m_ui->comboBox_encoding->addItem("UTF-16", static_cast<int>(StringEncoding::UTF16));

    // ── 长度输入框数字校验 ──
    m_ui->lineEdit_length->setValidator(new QIntValidator(1, 4096, this));
    m_ui->lineEdit_length->setText("32");

    // ── 指针区域默认隐藏 ──
    m_ui->widget_pointer_container->setVisible(false);

    populateDataTypeCombo();
    updateStringControlsVisibility();

    // ── 信号连接 ──
    connect(m_ui->lineEdit_address, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onAddressTextChanged);
    connect(m_ui->comboBox_Data_Type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Add_Or_Change_Address_Dialog::onDataTypeChanged);

    // ── Hex 显示 / 有符号 复选框 → 实时刷新计算值 ──
    connect(m_ui->checkBox_Hex_Display, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);
    connect(m_ui->checkBox_signed_Value, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    // ── 编码切换 → 实时刷新字符串解码结果 ──
    connect(m_ui->comboBox_encoding, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    // ── 长度变化 → 实时刷新（字符串/字节数组读取长度改变） ──
    connect(m_ui->lineEdit_length, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    // ======== 指针相关信号 ========

    // ── 指针复选框 → 显示/隐藏指针区域 ──
    connect(m_ui->checkBox_pointer, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::onPointerCheckChanged);

    // ── 基础指针地址变化 → 刷新指针计算值 ──
    connect(m_ui->lineEdit_base_pointer, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onPointerAddressChanged);

    // ── 第 1 级指针的控件信号（内置在 UI 中） ──
    // 先将内建的第 1 级控件注册到 m_pointerLevels[0]
    {
        PointerLevelWidgets lvl;
        lvl.container   = m_ui->widget_pointer_level_1;
        lvl.levelLabel  = m_ui->label_pointer_level1;
        lvl.offsetEdit  = m_ui->lineEdit_level_pointer;
        lvl.decBtn      = m_ui->pushButton_decrease_offset;
        lvl.incBtn      = m_ui->pushButton_increase_offset;
        lvl.resultLabel = m_ui->label_level_pointer_offset_compute_value;
        m_pointerLevels.push_back(lvl);
    }

    rebuildPointerLevelSignals(0);

    // ── 添加/移除偏移按钮 ──
    connect(m_ui->pushButton_add_offset, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onAddOffset);
    connect(m_ui->pushButton_delete_offset, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onDeleteOffset);

    // ── 确定前先校验输入（替换默认的 accept()） ──
    disconnect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, nullptr);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted,
            this, &Add_Or_Change_Address_Dialog::validateInput);
}

void Add_Or_Change_Address_Dialog::populateDataTypeCombo()
{
    m_ui->comboBox_Data_Type->blockSignals(true);
    m_ui->comboBox_Data_Type->clear();

    // 下标与 ValueType 枚举一致（0=Int8, 1=Int16, ...）
    m_ui->comboBox_Data_Type->addItem(tr("1字节"),   static_cast<int>(ValueType::Int8));
    m_ui->comboBox_Data_Type->addItem(tr("2字节"),   static_cast<int>(ValueType::Int16));
    m_ui->comboBox_Data_Type->addItem(tr("4字节"),   static_cast<int>(ValueType::Int32));
    m_ui->comboBox_Data_Type->addItem(tr("8字节"),   static_cast<int>(ValueType::Int64));
    m_ui->comboBox_Data_Type->addItem(tr("单浮点"),   static_cast<int>(ValueType::Float));
    m_ui->comboBox_Data_Type->addItem(tr("双浮点"),   static_cast<int>(ValueType::Double));
    m_ui->comboBox_Data_Type->addItem(tr("字符串"),   static_cast<int>(ValueType::String));
    m_ui->comboBox_Data_Type->addItem(tr("字节数组"), static_cast<int>(ValueType::ByteArray));

    // 选中与 initialValueType 匹配的项
    for (int i = 0; i < m_ui->comboBox_Data_Type->count(); ++i) {
        if (m_ui->comboBox_Data_Type->itemData(i).toInt() == static_cast<int>(m_initialValueType)) {
            m_ui->comboBox_Data_Type->setCurrentIndex(i);
            break;
        }
    }

    m_ui->comboBox_Data_Type->blockSignals(false);
}

// ==================== 信号槽 ====================

void Add_Or_Change_Address_Dialog::onAddressTextChanged(const QString& text)
{
    Q_UNUSED(text);
    refreshComputedValue();

    // 注意：不向 lineEdit_base_pointer 同步。
    // 指针模式开启时 lineEdit_base_pointer 固定为原始地址（m_originalAddressText），
    // 与官方 CE 行为一致。
}

void Add_Or_Change_Address_Dialog::onDataTypeChanged(int index)
{
    Q_UNUSED(index);
    updateStringControlsVisibility();
    refreshComputedValue();
}

// ==================== 指针相关 ====================

void Add_Or_Change_Address_Dialog::onPointerCheckChanged(bool checked)
{
    m_ui->widget_pointer_container->setVisible(checked);

    if (checked) {
        // ★ 勾选指针：将当前地址复制到基础指针输入框，然后禁用地址输入框
        //   沿用 CE 官方行为：开启指针时，地址栏的值填入基础指针框
        QString currentAddress = m_ui->lineEdit_address->text();
        m_ui->lineEdit_address->blockSignals(true);
        m_ui->lineEdit_address->setEnabled(false);

        m_ui->lineEdit_base_pointer->blockSignals(true);
        m_ui->lineEdit_base_pointer->setText(currentAddress);
        m_ui->lineEdit_base_pointer->blockSignals(false);

        m_ui->lineEdit_address->blockSignals(false);

        // 保存当前地址文本，用于取消指针时恢复
        m_originalAddressText = currentAddress;

        refreshPointerValue();
    } else {
        // 取消指针：恢复 lineEdit_address 可编辑，并恢复原始地址
        m_ui->lineEdit_address->setEnabled(true);
        m_ui->lineEdit_address->blockSignals(true);
        m_ui->lineEdit_address->setText(m_originalAddressText);
        m_ui->lineEdit_address->blockSignals(false);
        refreshComputedValue();
    }
}


void Add_Or_Change_Address_Dialog::onPointerAddressChanged(const QString& text)
{
    Q_UNUSED(text);
    if (m_ui->checkBox_pointer->isChecked()) {
        m_ui->lineEdit_address->blockSignals(true);
        refreshPointerValue();
        m_ui->lineEdit_address->blockSignals(false);
    }
}

void Add_Or_Change_Address_Dialog::onOffsetValueChanged()
{
    if (m_ui->checkBox_pointer->isChecked()) {
        m_ui->lineEdit_address->blockSignals(true);
        refreshPointerValue();
        m_ui->lineEdit_address->blockSignals(false);
    }
}

void Add_Or_Change_Address_Dialog::onIncreaseOffset()
{
    // 获取发出信号的按钮对应的层级
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    // 查找该按钮属于哪个层级
    int levelIdx = -1;
    for (int i = 0; i < static_cast<int>(m_pointerLevels.size()); ++i) {
        if (m_pointerLevels[i].incBtn == btn) {
            levelIdx = i;
            break;
        }
    }
    if (levelIdx < 0) return;

    size_t step = currentDataTypeSize();
    if (step == 0) return;

    bool ok = false;
    int64_t curOffset = m_pointerLevels[levelIdx].offsetEdit->text().toLongLong(&ok);
    if (!ok) curOffset = 0;

    curOffset += static_cast<int64_t>(step);
    m_pointerLevels[levelIdx].offsetEdit->setText(QString::number(curOffset));
}

void Add_Or_Change_Address_Dialog::onDecreaseOffset()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    int levelIdx = -1;
    for (int i = 0; i < static_cast<int>(m_pointerLevels.size()); ++i) {
        if (m_pointerLevels[i].decBtn == btn) {
            levelIdx = i;
            break;
        }
    }
    if (levelIdx < 0) return;

    size_t step = currentDataTypeSize();
    if (step == 0) return;

    bool ok = false;
    int64_t curOffset = m_pointerLevels[levelIdx].offsetEdit->text().toLongLong(&ok);
    if (!ok) curOffset = 0;

    curOffset -= static_cast<int64_t>(step);
    m_pointerLevels[levelIdx].offsetEdit->setText(QString::number(curOffset));
}

void Add_Or_Change_Address_Dialog::onAddOffset()
{
    // 创建新的指针层级（索引 = 当前层级数）
    int newLevel = static_cast<int>(m_pointerLevels.size());
    // 限制最大层级数（防止界面溢出）
    if (newLevel >= 8) return;

    createPointerLevelWidget(newLevel);
    refreshPointerValue();
}

void Add_Or_Change_Address_Dialog::onDeleteOffset()
{
    // 至少保留第 1 级
    if (m_pointerLevels.size() <= 1) return;

    removeLastPointerLevel();
    refreshPointerValue();
}

// ==================== 动态创建/移除指针层级 ====================

void Add_Or_Change_Address_Dialog::createPointerLevelWidget(int levelIndex)
{
    // 复制第 1 级容器的样式创建新的一级
    // 新的 widget 插入到 horizontalLayout_2（lineEdit_base_pointer 所在行）之前

    // 创建新的容器 widget
    QWidget* newContainer = new QWidget(m_ui->widget_pointer_container);
    newContainer->setSizePolicy(m_ui->widget_pointer_level_1->sizePolicy());

    // 创建水平布局
    QHBoxLayout* hLayout = new QHBoxLayout(newContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);

    // ★ 创建级指针标签: " N 级指针"（levelIndex 从 0 开始，显示 +1）
    QLabel* levelLabel = new QLabel(
        QStringLiteral(" %1 级指针").arg(levelIndex + 1), newContainer);
    levelLabel->setSizePolicy(m_ui->label_pointer_level1->sizePolicy());

    // 创建减少按钮
    QPushButton* decBtn = new QPushButton(QStringLiteral("<"), newContainer);
    decBtn->setMaximumSize(30, 16777215);

    decBtn->setSizePolicy(m_ui->pushButton_decrease_offset->sizePolicy());
    decBtn->setEnabled(true);
    decBtn->setCheckable(false);

    // 创建偏移输入
    QLineEdit* offsetEdit = new QLineEdit(newContainer);
    offsetEdit->setMaximumSize(80, 16777215);
    offsetEdit->setSizePolicy(m_ui->lineEdit_level_pointer->sizePolicy());
    offsetEdit->setValidator(new QIntValidator(this));
    offsetEdit->setText("0");

    // 创建增加按钮
    QPushButton* incBtn = new QPushButton(QStringLiteral(">"), newContainer);
    incBtn->setMaximumSize(30, 16777215);
    incBtn->setSizePolicy(m_ui->pushButton_increase_offset->sizePolicy());

    // 创建结果标签
    QLabel* resultLabel = new QLabel(QStringLiteral("???+?=???"), newContainer);
    resultLabel->setSizePolicy(m_ui->label_level_pointer_offset_compute_value->sizePolicy());

    // 创建水平弹簧
    QSpacerItem* spacer = new QSpacerItem(10, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    // 组装布局
    hLayout->addWidget(levelLabel);
    hLayout->addWidget(decBtn);
    hLayout->addWidget(offsetEdit);
    hLayout->addWidget(incBtn);
    hLayout->addWidget(resultLabel);
    hLayout->addItem(spacer);

    // 插入到 verticalLayout_add_pointer 中，在 horizontalLayout_2 之前
    // (第 levelIndex 个 widget 之后, 即内置的第1级+已创建的层级之后)
    // verticalLayout_add_pointer 中的顺序:
    //   [0]: widget_pointer_level_1 (内置)
    //   [1...n]: 动态添加的层级
    //   [n+1]: horizontalLayout_2 (lineEdit_base_pointer)
    //   [n+2]: horizontalLayout_3 (按钮)
    QVBoxLayout* parentLayout = m_ui->verticalLayout_add_pointer;
    // 插入位置: 在所有动态层级之后, horizontalLayout_2 之前
    // 即插入到 parentLayout 的第 levelIndex 个位置
    parentLayout->insertWidget(levelIndex, newContainer);

    // 保存到层级列表
    PointerLevelWidgets lvl;
    lvl.container   = newContainer;
    lvl.levelLabel  = levelLabel;
    lvl.decBtn      = decBtn;
    lvl.offsetEdit  = offsetEdit;
    lvl.incBtn      = incBtn;
    lvl.resultLabel = resultLabel;
    m_pointerLevels.push_back(lvl);

    // 连接信号
    rebuildPointerLevelSignals(levelIndex);
}

void Add_Or_Change_Address_Dialog::removeLastPointerLevel()
{
    if (m_pointerLevels.size() <= 1) return;

    int lastIdx = static_cast<int>(m_pointerLevels.size()) - 1;
    auto& lvl = m_pointerLevels[lastIdx];

    // 从父布局中移除并删除
    QVBoxLayout* parentLayout = m_ui->verticalLayout_add_pointer;
    parentLayout->removeWidget(lvl.container);
    delete lvl.container;
    lvl.container = nullptr;

    m_pointerLevels.pop_back();
}

void Add_Or_Change_Address_Dialog::rebuildPointerLevelSignals(int levelIndex)
{
    if (levelIndex < 0 || levelIndex >= static_cast<int>(m_pointerLevels.size()))
        return;

    auto& lvl = m_pointerLevels[levelIndex];

    // 断开旧连接（通过 disconnecting 旧 lambda 比较麻烦，Qt 6 可以用 QMetaObject::Connection 管理
    // 这里简单粗暴，每次都断开所有相关信号再重新连接

    // 偏移值变化
    connect(lvl.offsetEdit, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onOffsetValueChanged);

    // 增加/减少按钮
    connect(lvl.incBtn, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onIncreaseOffset);
    connect(lvl.decBtn, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onDecreaseOffset);
}

// ==================== 解析"模块名+偏移"格式 ====================

uint64_t Add_Or_Change_Address_Dialog::parseAddressOrModule(const QString& text, bool* ok) const
{
    if (ok) *ok = false;

    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return 0;

    // 尝试直接解析为地址（0x 十六进制或纯十进制）
    if (trimmed.startsWith("0x", Qt::CaseInsensitive) ||
        trimmed.startsWith("0X")) {
        bool convOk = false;
        uint64_t addr = trimmed.toULongLong(&convOk, 16);
        if (convOk) {
            if (ok) *ok = true;
            return addr;
        }
    }

    // 尝试纯十进制（没有 0x 前缀，并且全数字）
    {
        bool allDigit = true;
        for (const QChar& ch : trimmed) {
            if (!ch.isDigit() && ch != QLatin1Char(' ')) {
                allDigit = false;
                break;
            }
        }
        if (allDigit) {
            bool convOk = false;
            uint64_t addr = trimmed.toULongLong(&convOk, 10);
            if (convOk) {
                if (ok) *ok = true;
                return addr;
            }
        }
    }

    // 尝试 "模块名+偏移" 格式: e.g. "module.dll+0x1234" 或 "module.dll-0x10"
    // 用正则匹配: (模块名)[+-](偏移值)
    static const QRegularExpression re(
        R"(^([a-zA-Z0-9_\.\-]+)\s*([\+\-])\s*(0x[0-9a-fA-F]+|\d+)$)");
    auto match = re.match(trimmed);
    if (!match.hasMatch()) {
        return 0;
    }

    QString moduleName = match.captured(1);
    QString op         = match.captured(2);
    QString offsetStr  = match.captured(3);

    // 查模块基址
    const auto& mods = ProcessManager::instance().modules();
    uint64_t baseAddr = 0;
    for (const auto& mod : mods) {
#ifdef _WIN32
        if (QString::compare(QString::fromStdString(mod.name), moduleName, Qt::CaseInsensitive) == 0)
#else
        if (QString::fromStdString(mod.name) == moduleName)
#endif
        {
            baseAddr = mod.base;
            break;
        }
    }
    if (baseAddr == 0) return 0;

    // 解析偏移值
    bool offOk = false;
    int64_t offset = 0;
    if (offsetStr.startsWith("0x", Qt::CaseInsensitive))
        offset = static_cast<int64_t>(offsetStr.toULongLong(&offOk, 16));
    else
        offset = offsetStr.toLongLong(&offOk, 10);

    if (!offOk) return 0;

    if (op == QStringLiteral("-"))
        offset = -offset;

    if (ok) *ok = true;
    return baseAddr + static_cast<uint64_t>(offset);
}

// ==================== 指针核心逻辑 ====================

void Add_Or_Change_Address_Dialog::refreshPointerValue()
{
    // 解析基础指针地址（支持 "模块名+偏移" 和直接地址）
    QString ptrText = m_ui->lineEdit_base_pointer->text().trimmed();
    bool baseOk = false;
    m_pointerBaseAddr = parseAddressOrModule(ptrText, &baseOk);

    if (!baseOk || m_pointerBaseAddr == 0) {
        // 更新所有层级的显示为未知
        for (auto& lvl : m_pointerLevels) {
            lvl.resultLabel->setText("???+?=???");
            lvl.offset = 0;
            lvl.pointerValue = 0;
            lvl.computedAddr = 0;
        }

        m_ui->label_base_pointer_compute_value->setText("->?????");
        return;
    }

    auto mem = ProcessManager::instance().memory();
    if (!mem) {
        for (auto& lvl : m_pointerLevels)
            lvl.resultLabel->setText(tr("???+?=???"));
        m_ui->label_base_pointer_compute_value->setText(tr("->无进程"));
        return;
    }

    // ★ 第一步：根据当前数据类型长度读取基础指针值
    size_t ptrSize = currentDataTypeSize();
    // 对于指针读取，最小 1 字节，最大不超过 8 字节
    if (ptrSize > sizeof(uint64_t)) ptrSize = sizeof(uint64_t);
    if (ptrSize == 0) ptrSize = 8;

    uint64_t basePtrValue = 0;
    if (!mem->read(m_pointerBaseAddr, &basePtrValue, ptrSize)) {
        for (auto& lvl : m_pointerLevels) {
            lvl.pointerValue = 0;
            lvl.computedAddr = 0;
            lvl.resultLabel->setText(tr("读取失败"));
        }
        m_ui->label_base_pointer_compute_value->setText("->?????");
        return;
    }

    // label_base_pointer_compute_value 显示基础指针解引用后的值，用作下一级指针的指针
    m_ui->label_base_pointer_compute_value->setText(
        QString("->0x%1").arg(basePtrValue, 16, 16, QChar('0')));

    // 第1级指针以 basePtrValue 为基址
    uint64_t currentAddr = basePtrValue;

    // ---- 逐级解引用 ----
    for (int i = 0; i < static_cast<int>(m_pointerLevels.size()); ++i) {
        auto& lvl = m_pointerLevels[i];

        // 解析偏移值
        QString offsetText = lvl.offsetEdit->text().trimmed();
        bool offOk = false;
        if (offsetText.isEmpty())
            lvl.offset = 0;
        else
            lvl.offset = offsetText.toLongLong(&offOk);

        // 该级要读取的地址 = currentAddr（来自上一级的 computedAddr）+ 偏移
        uint64_t readAddr = currentAddr + static_cast<uint64_t>(static_cast<int64_t>(lvl.offset));

        // 根据当前数据类型长度读取指针值
        uint64_t pointerValue = 0;
        if (!mem->read(readAddr, &pointerValue, ptrSize)) {

            lvl.pointerValue = 0;
            lvl.computedAddr = 0;
            lvl.resultLabel->setText(tr("读取失败"));
            m_ui->label_base_pointer_compute_value->setText("->?????");
            return;
        }

        lvl.pointerValue = pointerValue;

        // 当前级计算结果 = 读取到的指针值（下一级将以它为基址继续解引用）
        lvl.computedAddr = pointerValue;

        // ★ 更新 levelLabel: " N 级指针 = 0x[pointerValue]"
        lvl.levelLabel->setText(
            QStringLiteral(" %1 级指针 = 0x%2")
                .arg(i + 1)
                .arg(pointerValue, 16, 16, QChar('0')));

        // 更新显示: "0x[currentAddr]+0x[offset]=0x[pointerValue]"
        // 含义：从 [currentAddr + offset] 地址读取到 pointerValue
        //       下一级以 pointerValue 为基址继续解引用
        QString offsetHex = (lvl.offset >= 0)
            ? QString("0x%1").arg(lvl.offset, 0, 16)
            : QString("-0x%1").arg(-lvl.offset, 0, 16);

        lvl.resultLabel->setText(
            QString("0x%1+%2=0x%3")
                .arg(currentAddr, 0, 16)
                .arg(offsetHex)
                .arg(pointerValue, 16, 16, QChar('0')));

        // ★ 下一级以当前级的 pointerValue 为基址（不再加偏移，偏移已在本级用掉）
        currentAddr = lvl.computedAddr;

    }

    // 最后一级的 pointerValue = 最终地址（不含额外的偏移）
    // 最后一级的结果已经被 offset 修正过（因为读取的是 [previousAddr + offset]）
    m_computedFinalAddr = currentAddr;

    // 更新 lineEdit_address 显示最终计算地址
    m_ui->lineEdit_address->setText(
        QString("0x%1").arg(m_computedFinalAddr, 16, 16, QChar('0')));

    // 刷新值的显示
    refreshComputedValue();
}

// ==================== 核心逻辑 ====================

void Add_Or_Change_Address_Dialog::refreshComputedValue()
{
    QString addrText = m_ui->lineEdit_address->text().trimmed();
    if (addrText.isEmpty()) {
        m_ui->label_computed_Value->setText("=???");
        return;
    }

    // 解析地址（支持 0x 十六进制和纯十进制）
    bool ok = false;
    uint64_t addr = 0;
    if (addrText.startsWith("0x", Qt::CaseInsensitive))
        addr = addrText.toULongLong(&ok, 16);
    else
        addr = addrText.toULongLong(&ok, 10);

    if (!ok || addr == 0) {
        m_ui->label_computed_Value->setText("=???");
        return;
    }

    // 读取内存
    auto mem = ProcessManager::instance().memory();
    if (!mem) {
        m_ui->label_computed_Value->setText(tr("=无进程"));
        return;
    }

    int dtIndex = m_ui->comboBox_Data_Type->currentIndex();
    auto vt = static_cast<ValueType>(m_ui->comboBox_Data_Type->itemData(dtIndex).toInt());

    QString valueStr;

    if (isStringValueType(vt)) {
        // ── 字符串类型：读取指定长度的原始数据 ──
        int len = m_ui->lineEdit_length->text().toInt(&ok);
        if (!ok || len <= 0) len = 32;
        if (len > 4096) len = 4096;

        std::vector<uint8_t> buf(len);
        if (!mem->read(addr, buf.data(), len)) {
            m_ui->label_computed_Value->setText(tr("=读取失败"));
            return;
        }

        StringEncoding enc = encoding();

        // 确定实际有效长度（空字节截断）
        int realLen = 0;
        if (enc == StringEncoding::UTF16) {
            const char16_t* u16 = reinterpret_cast<const char16_t*>(buf.data());
            int u16len = len / 2;
            while (realLen < u16len && u16[realLen] != 0)
                ++realLen;
            if (realLen > 0) {
                valueStr = QString::fromUtf16(
                    reinterpret_cast<const char16_t*>(buf.data()),
                    realLen);
            }
        } else if (enc == StringEncoding::UTF8) {
            const char* data = reinterpret_cast<const char*>(buf.data());
            while (realLen < len && data[realLen] != '\0')
                ++realLen;
            valueStr = QString::fromUtf8(data, realLen);
        } else {
            // ASCII / Latin1
            const char* data = reinterpret_cast<const char*>(buf.data());
            while (realLen < len && data[realLen] != '\0')
                ++realLen;
            valueStr = QString::fromLatin1(data, realLen);
        }

        if (realLen > 64) {
            valueStr = valueStr.left(64) + "...";
        }
        valueStr = valueStr.toHtmlEscaped();

    } else if (isByteArrayValueType(vt)) {
        // ── 字节数组类型 ──
        int len = m_ui->lineEdit_length->text().toInt(&ok);
        if (!ok || len <= 0) len = 32;

        std::vector<uint8_t> buf(len);
        if (!mem->read(addr, buf.data(), len)) {
            m_ui->label_computed_Value->setText(tr("=读取失败"));
            return;
        }

        QStringList hexParts;
        int showCount = qMin(len, 16);
        for (int i = 0; i < showCount; ++i)
            hexParts += QString::asprintf("%02X", buf[i]);
        valueStr = hexParts.join(' ');
        if (len > 16)
            valueStr += "...";

    } else {
        // ── 数值类型 ──
        size_t size = valueTypeSize(vt);
        uint64_t raw = 0;
        if (!mem->read(addr, &raw, size)) {
            m_ui->label_computed_Value->setText(tr("=读取失败"));
            return;
        }

        bool hex = m_ui->checkBox_Hex_Display->isChecked();
        bool signedDisplay = m_ui->checkBox_signed_Value->isChecked();

        switch (vt) {
        case ValueType::Int8: {
            if (hex)
                valueStr = QString("0x%1").arg(static_cast<uint8_t>(raw & 0xFF), 2, 16, QChar('0'));
            else if (signedDisplay)
                valueStr = QString::number(static_cast<int8_t>(raw & 0xFF));
            else
                valueStr = QString::number(static_cast<uint8_t>(raw & 0xFF));
            break;
        }
        case ValueType::Int16: {
            if (hex)
                valueStr = QString("0x%1").arg(static_cast<uint16_t>(raw & 0xFFFF), 4, 16, QChar('0'));
            else if (signedDisplay)
                valueStr = QString::number(static_cast<int16_t>(raw & 0xFFFF));
            else
                valueStr = QString::number(static_cast<uint16_t>(raw & 0xFFFF));
            break;
        }
        case ValueType::Int32: {
            if (hex)
                valueStr = QString("0x%1").arg(static_cast<uint32_t>(raw), 8, 16, QChar('0'));
            else if (signedDisplay)
                valueStr = QString::number(static_cast<int32_t>(raw));
            else
                valueStr = QString::number(static_cast<uint32_t>(raw));
            break;
        }
        case ValueType::Int64: {
            if (hex)
                valueStr = QString("0x%1").arg(raw, 16, 16, QChar('0'));
            else if (signedDisplay)
                valueStr = QString::number(static_cast<int64_t>(raw));
            else
                valueStr = QString::number(raw);
            break;
        }
        case ValueType::Float: {
            if (hex) {
                uint32_t bits;
                std::memcpy(&bits, &raw, sizeof(bits));
                valueStr = QString("0x%1").arg(bits, 8, 16, QChar('0'));
            } else {
                float f;
                std::memcpy(&f, &raw, sizeof(f));
                valueStr = QString::number(f, 'g', 7);
            }
            break;
        }
        case ValueType::Double: {
            if (hex)
                valueStr = QString("0x%1").arg(raw, 16, 16, QChar('0'));
            else {
                double d;
                std::memcpy(&d, &raw, sizeof(d));
                valueStr = QString::number(d, 'g', 15);
            }
            break;
        }
        default:
            valueStr = "=???";
            break;
        }
    }

    m_ui->label_computed_Value->setText("= " + valueStr);
}

void Add_Or_Change_Address_Dialog::updateStringControlsVisibility()
{
    ValueType vt = valueType();

    // 字符串控件：仅当选择"字符串"时显示
    bool isString = isStringValueType(vt);
    m_ui->label_length->setVisible(isString);
    m_ui->lineEdit_length->setVisible(isString);
    m_ui->comboBox_encoding->setVisible(isString);
    m_ui->checkBox_code_page->setVisible(false);

    // Hex Display: 仅对数值类型和字节数组显示
    bool showHex = !isStringValueType(vt);
    m_ui->checkBox_Hex_Display->setVisible(showHex);

    // Signed: 仅对整数类型显示
    bool showSigned = isIntegerType(vt);
    m_ui->checkBox_signed_Value->setVisible(showSigned);

    if (isByteArrayValueType(vt)) {
        m_ui->label_length->setVisible(true);
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setVisible(true);
        m_ui->lineEdit_length->setText("32");
        m_ui->comboBox_encoding->setVisible(false);
    }

    if (isNumericType(vt)) {
        m_ui->label_length->setVisible(true);
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setVisible(true);
        m_ui->lineEdit_length->setEnabled(false);
        m_ui->lineEdit_length->setText(QString::number(valueTypeSize(vt)));
        m_ui->comboBox_encoding->setVisible(false);
    }

    if (isStringValueType(vt)) {
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setEnabled(true);
        m_ui->lineEdit_length->setText("32");
    }
}

bool Add_Or_Change_Address_Dialog::validateInput()
{
    // 地址不能为空
    QString addrText = m_ui->lineEdit_address->text().trimmed();
    if (addrText.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入地址。"));
        m_ui->lineEdit_address->setFocus();
        return false;
    }

    bool ok = false;
    if (addrText.startsWith("0x", Qt::CaseInsensitive))
        addrText.toULongLong(&ok, 16);
    else
        addrText.toULongLong(&ok, 10);

    if (!ok) {
        QMessageBox::warning(this, tr("错误"), tr("无效的地址格式。"));
        m_ui->lineEdit_address->setFocus();
        return false;
    }

    // 勾选了指针时，基础指针地址不能为空
    if (m_ui->checkBox_pointer->isChecked()) {
        QString ptrText = m_ui->lineEdit_base_pointer->text().trimmed();
        if (ptrText.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("指针模式：基础指针地址不能为空。"));
            m_ui->lineEdit_base_pointer->setFocus();
            return false;
        }
        // 验证地址/模块解析
        bool parseOk = false;
        parseAddressOrModule(ptrText, &parseOk);
        if (!parseOk) {
            QMessageBox::warning(this, tr("错误"), tr("无效的基址格式。支持：0x地址、十进制数字、或\"模块名+0x偏移\"。"));
            m_ui->lineEdit_base_pointer->setFocus();
            return false;
        }
    }

    // 校验通过
    accept();
    return true;
}

// ==================== 获取结果 ====================

uint64_t Add_Or_Change_Address_Dialog::address() const
{
    // 如果勾选了指针，返回最终计算地址
    if (m_ui->checkBox_pointer->isChecked()) {
        return m_computedFinalAddr;
    }

    QString text = m_ui->lineEdit_address->text().trimmed();
    bool ok = false;
    uint64_t addr = 0;
    if (text.startsWith("0x", Qt::CaseInsensitive))
        addr = text.toULongLong(&ok, 16);
    else
        addr = text.toULongLong(&ok, 10);
    return ok ? addr : 0;
}

QString Add_Or_Change_Address_Dialog::description() const
{
    return m_ui->lineEdit_description->text().trimmed();
}

ValueType Add_Or_Change_Address_Dialog::valueType() const
{
    int idx = m_ui->comboBox_Data_Type->currentIndex();
    return static_cast<ValueType>(m_ui->comboBox_Data_Type->itemData(idx).toInt());
}

bool Add_Or_Change_Address_Dialog::hexDisplay() const
{
    return m_ui->checkBox_Hex_Display->isChecked();
}

bool Add_Or_Change_Address_Dialog::signedDisplay() const
{
    return m_ui->checkBox_signed_Value->isChecked();
}

StringEncoding Add_Or_Change_Address_Dialog::encoding() const
{
    int idx = m_ui->comboBox_encoding->currentIndex();
    return static_cast<StringEncoding>(m_ui->comboBox_encoding->itemData(idx).toInt());
}

int Add_Or_Change_Address_Dialog::stringLength() const
{
    bool ok = false;
    int len = m_ui->lineEdit_length->text().toInt(&ok);
    return (ok && len > 0) ? len : 32;
}

size_t Add_Or_Change_Address_Dialog::currentDataTypeSize() const
{
    ValueType vt = valueType();
    if (isNumericType(vt) || isByteArrayValueType(vt)) {
        return valueTypeSize(vt);
    }
    return 1;
}
