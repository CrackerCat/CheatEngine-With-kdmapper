#include "Add_Or_Change_Address.h"
#include "process/process_manager.h"
#include "interface/imemory_accessor.h"
#include "address_list/address_item.h"

#include <QMessageBox>
#include <QIntValidator>
#include <QRegularExpression>
#include <cstring>

// ==================== 构造 ====================

Add_Or_Change_Address_Dialog::Add_Or_Change_Address_Dialog(
    QWidget* parent, const AddressItem::Config& cfg, bool editMode)
    : QDialog(parent)
    , m_ui(std::make_unique<Ui::Dialog_Add_Or_Change_Address>())
    , m_initialConfig(cfg)
    , m_editMode(editMode)
{
    setupUi();

    // ── 逐一预填 UI ──

    // 地址
    if (cfg.address != 0)
        m_ui->lineEdit_address->setText(QString("0x%1").arg(cfg.address, 16, 16, QChar('0')));
    else
        m_ui->lineEdit_address->clear();
    m_originalAddressText = m_ui->lineEdit_address->text();

    // 基础指针地址（与 CE 行为一致，开启指针时从地址栏复制）
    m_ui->lineEdit_base_pointer->setText(m_originalAddressText);

    // 描述
    m_ui->lineEdit_description->setText(cfg.description);

    // Hex/Signed 复选框
    m_ui->checkBox_Hex_Display->setChecked(cfg.hexDisplay);
    m_ui->checkBox_signed_Value->setChecked(cfg.signedDisplay);

    // 编码
    for (int i = 0; i < m_ui->comboBox_encoding->count(); ++i) {
        if (m_ui->comboBox_encoding->itemData(i).toInt() == static_cast<int>(cfg.encoding)) {
            m_ui->comboBox_encoding->setCurrentIndex(i);
            break;
        }
    }

    // 长度
    int len = cfg.stringLength > 0 ? cfg.stringLength : static_cast<int>(cfg.buffer.size());
    if (len > 0) m_ui->lineEdit_length->setText(QString::number(len));

    // 数据类型（comboBox_Data_Type 已由 populateDataTypeCombo 填充）
    for (int i = 0; i < m_ui->comboBox_Data_Type->count(); ++i) {
        if (m_ui->comboBox_Data_Type->itemData(i).toInt() == static_cast<int>(cfg.type)) {
            m_ui->comboBox_Data_Type->setCurrentIndex(i);
            break;
        }
    }

    // ── 指针链 ──
    if (cfg.pointerChain.isValid()) {
        m_ui->checkBox_pointer->setChecked(true);

        m_ui->lineEdit_base_pointer->blockSignals(true);
        m_ui->lineEdit_base_pointer->setText(cfg.pointerChain.baseAddressText);
        m_ui->lineEdit_base_pointer->blockSignals(false);

        // 清理多余的层级（保留第1级）
        while (m_pointerLevels.size() > 1)
            removeLastPointerLevel();

        // 设置第1级偏移
        if (!cfg.pointerChain.levels.empty()) {
            m_pointerLevels[0].offsetEdit->blockSignals(true);
            m_pointerLevels[0].offsetEdit->setText(QString::number(cfg.pointerChain.levels[0].offset));
            m_pointerLevels[0].offsetEdit->blockSignals(false);
        }

        // 添加更多层级
        for (size_t i = 1; i < cfg.pointerChain.levels.size(); ++i) {
            int levelIdx = static_cast<int>(i);
            createPointerLevelWidget(levelIdx);
            m_pointerLevels[levelIdx].offsetEdit->blockSignals(true);
            m_pointerLevels[levelIdx].offsetEdit->setText(QString::number(cfg.pointerChain.levels[i].offset));
            m_pointerLevels[levelIdx].offsetEdit->blockSignals(false);
        }

        // 刷新指针计算值
        refreshPointerValue();
    }

    onAddressTextChanged(m_ui->lineEdit_address->text());
}

// ==================== UI 初始化 ====================

void Add_Or_Change_Address_Dialog::setupUi()
{
    m_ui->setupUi(this);

    if (m_editMode)
        setWindowTitle(tr("修改地址"));
    else
        setWindowTitle(tr("添加地址"));

    m_ui->lineEdit_address->setPlaceholderText("0x12345678");
    m_ui->lineEdit_base_pointer->setPlaceholderText("模块名+0x1234 或 0x12345678");

    m_ui->comboBox_encoding->clear();
    m_ui->comboBox_encoding->addItem("ASCII",  static_cast<int>(AddressItem::Encoding::ASCII));
    m_ui->comboBox_encoding->addItem("UTF-8",  static_cast<int>(AddressItem::Encoding::UTF8));
    m_ui->comboBox_encoding->addItem("UTF-16", static_cast<int>(AddressItem::Encoding::UTF16));

    m_ui->lineEdit_length->setValidator(new QIntValidator(1, 4096, this));
    m_ui->lineEdit_length->setText("32");

    m_ui->widget_pointer_container->setVisible(false);

    populateDataTypeCombo();
    updateStringControlsVisibility();

    connect(m_ui->lineEdit_address, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onAddressTextChanged);
    connect(m_ui->comboBox_Data_Type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Add_Or_Change_Address_Dialog::onDataTypeChanged);

    connect(m_ui->checkBox_Hex_Display, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);
    connect(m_ui->checkBox_signed_Value, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    connect(m_ui->comboBox_encoding, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    connect(m_ui->lineEdit_length, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::refreshComputedValue);

    // 指针相关
    connect(m_ui->checkBox_pointer, &QCheckBox::toggled,
            this, &Add_Or_Change_Address_Dialog::onPointerCheckChanged);
    connect(m_ui->lineEdit_base_pointer, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onPointerAddressChanged);

    // 内建第1级指针控件
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

    connect(m_ui->pushButton_add_offset, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onAddOffset);
    connect(m_ui->pushButton_delete_offset, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onDeleteOffset);

    disconnect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, nullptr);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted,
            this, &Add_Or_Change_Address_Dialog::validateInput);
}

void Add_Or_Change_Address_Dialog::populateDataTypeCombo()
{
    m_ui->comboBox_Data_Type->blockSignals(true);
    m_ui->comboBox_Data_Type->clear();
    m_ui->comboBox_Data_Type->addItem(tr("1字节"),   static_cast<int>(AddressItem::Type::Int8));
    m_ui->comboBox_Data_Type->addItem(tr("2字节"),   static_cast<int>(AddressItem::Type::Int16));
    m_ui->comboBox_Data_Type->addItem(tr("4字节"),   static_cast<int>(AddressItem::Type::Int32));
    m_ui->comboBox_Data_Type->addItem(tr("8字节"),   static_cast<int>(AddressItem::Type::Int64));
    m_ui->comboBox_Data_Type->addItem(tr("单浮点"),   static_cast<int>(AddressItem::Type::Float));
    m_ui->comboBox_Data_Type->addItem(tr("双浮点"),   static_cast<int>(AddressItem::Type::Double));
    m_ui->comboBox_Data_Type->addItem(tr("字符串"),   static_cast<int>(AddressItem::Type::String));
    m_ui->comboBox_Data_Type->addItem(tr("字节数组"), static_cast<int>(AddressItem::Type::ByteArray));

    for (int i = 0; i < m_ui->comboBox_Data_Type->count(); ++i) {
        if (m_ui->comboBox_Data_Type->itemData(i).toInt() == static_cast<int>(m_initialConfig.type)) {
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
        QString currentAddress = m_ui->lineEdit_address->text();
        m_ui->lineEdit_address->blockSignals(true);
        m_ui->lineEdit_address->setEnabled(false);

        m_ui->lineEdit_base_pointer->blockSignals(true);
        m_ui->lineEdit_base_pointer->setText(currentAddress);
        m_ui->lineEdit_base_pointer->blockSignals(false);

        m_ui->lineEdit_address->blockSignals(false);
        m_originalAddressText = currentAddress;
        refreshPointerValue();
    } else {
        m_ui->lineEdit_address->setEnabled(true);
        m_ui->lineEdit_address->blockSignals(true);
        // 取消指针时，把 base 指针的解析地址（如模块基址+偏移）同步到地址栏
        if (m_pointerBaseAddr != 0)
            m_ui->lineEdit_address->setText(QString("0x%1").arg(m_pointerBaseAddr, 16, 16, QChar('0')));
        else
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
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int levelIdx = -1;
    for (int i = 0; i < static_cast<int>(m_pointerLevels.size()); ++i) {
        if (m_pointerLevels[i].incBtn == btn) { levelIdx = i; break; }
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
        if (m_pointerLevels[i].decBtn == btn) { levelIdx = i; break; }
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
    int newLevel = static_cast<int>(m_pointerLevels.size());
    if (newLevel >= 8) return;
    createPointerLevelWidget(newLevel);
    refreshPointerValue();
}

void Add_Or_Change_Address_Dialog::onDeleteOffset()
{
    if (m_pointerLevels.size() <= 1) return;
    removeLastPointerLevel();
    refreshPointerValue();
}

void Add_Or_Change_Address_Dialog::createPointerLevelWidget(int levelIndex)
{
    QWidget* newContainer = new QWidget(m_ui->widget_pointer_container);
    newContainer->setSizePolicy(m_ui->widget_pointer_level_1->sizePolicy());

    QHBoxLayout* hLayout = new QHBoxLayout(newContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);

    QLabel* levelLabel = new QLabel(
        QStringLiteral(" %1 级指针").arg(levelIndex + 1), newContainer);
    levelLabel->setSizePolicy(m_ui->label_pointer_level1->sizePolicy());

    QPushButton* decBtn = new QPushButton(QStringLiteral("<"), newContainer);
    decBtn->setMaximumSize(30, 16777215);
    decBtn->setSizePolicy(m_ui->pushButton_decrease_offset->sizePolicy());

    QLineEdit* offsetEdit = new QLineEdit(newContainer);
    offsetEdit->setMaximumSize(80, 16777215);
    offsetEdit->setSizePolicy(m_ui->lineEdit_level_pointer->sizePolicy());
    offsetEdit->setValidator(new QIntValidator(this));
    offsetEdit->setText("0");

    QPushButton* incBtn = new QPushButton(QStringLiteral(">"), newContainer);
    incBtn->setMaximumSize(30, 16777215);
    incBtn->setSizePolicy(m_ui->pushButton_increase_offset->sizePolicy());

    QLabel* resultLabel = new QLabel(QStringLiteral("???+?=???"), newContainer);
    resultLabel->setSizePolicy(m_ui->label_level_pointer_offset_compute_value->sizePolicy());

    QSpacerItem* spacer = new QSpacerItem(10, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    hLayout->addWidget(levelLabel);
    hLayout->addWidget(decBtn);
    hLayout->addWidget(offsetEdit);
    hLayout->addWidget(incBtn);
    hLayout->addWidget(resultLabel);
    hLayout->addItem(spacer);

    QVBoxLayout* parentLayout = m_ui->verticalLayout_add_pointer;
    parentLayout->insertWidget(levelIndex, newContainer);

    PointerLevelWidgets lvl;
    lvl.container   = newContainer;
    lvl.levelLabel  = levelLabel;
    lvl.decBtn      = decBtn;
    lvl.offsetEdit  = offsetEdit;
    lvl.incBtn      = incBtn;
    lvl.resultLabel = resultLabel;
    m_pointerLevels.push_back(lvl);

    rebuildPointerLevelSignals(levelIndex);
}

void Add_Or_Change_Address_Dialog::removeLastPointerLevel()
{
    if (m_pointerLevels.size() <= 1) return;
    int lastIdx = static_cast<int>(m_pointerLevels.size()) - 1;
    auto& lvl = m_pointerLevels[lastIdx];
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
    connect(lvl.offsetEdit, &QLineEdit::textChanged,
            this, &Add_Or_Change_Address_Dialog::onOffsetValueChanged);
    connect(lvl.incBtn, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onIncreaseOffset);
    connect(lvl.decBtn, &QPushButton::clicked,
            this, &Add_Or_Change_Address_Dialog::onDecreaseOffset);
}

uint64_t Add_Or_Change_Address_Dialog::parseAddressOrModule(const QString& text, bool* ok) const
{
    if (ok) *ok = false;
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return 0;

    if (trimmed.startsWith("0x", Qt::CaseInsensitive) ||
        trimmed.startsWith("0X")) {
        bool convOk = false;
        uint64_t addr = trimmed.toULongLong(&convOk, 16);
        if (convOk) { if (ok) *ok = true; return addr; }
    }

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
            if (convOk) { if (ok) *ok = true; return addr; }
        }
    }

    static const QRegularExpression re(
        R"(^([a-zA-Z0-9_\.\-]+)\s*([\+\-])\s*(0x[0-9a-fA-F]+|\d+)$)");
    auto match = re.match(trimmed);
    if (!match.hasMatch()) return 0;

    QString moduleName = match.captured(1);
    QString op         = match.captured(2);
    QString offsetStr  = match.captured(3);

    const auto& mods = ProcessManager::instance().modules();
    uint64_t baseAddr = 0;
    for (const auto& mod : mods) {
#ifdef _WIN32
        if (QString::compare(QString::fromStdString(mod.name), moduleName, Qt::CaseInsensitive) == 0)
#else
        if (QString::fromStdString(mod.name) == moduleName)
#endif
        { baseAddr = mod.base; break; }
    }
    if (baseAddr == 0) return 0;

    bool offOk = false;
    int64_t offset = 0;
    if (offsetStr.startsWith("0x", Qt::CaseInsensitive))
        offset = static_cast<int64_t>(offsetStr.toULongLong(&offOk, 16));
    else
        offset = offsetStr.toLongLong(&offOk, 10);
    if (!offOk) return 0;
    if (op == QStringLiteral("-")) offset = -offset;
    if (ok) *ok = true;
    return baseAddr + static_cast<uint64_t>(offset);
}

void Add_Or_Change_Address_Dialog::refreshPointerValue()
{
    QString ptrText = m_ui->lineEdit_base_pointer->text().trimmed();
    bool baseOk = false;
    m_pointerBaseAddr = parseAddressOrModule(ptrText, &baseOk);

    if (!baseOk || m_pointerBaseAddr == 0) {
        for (auto& lvl : m_pointerLevels) {
            lvl.resultLabel->setText("???+?=???");
            lvl.offset = 0; lvl.pointerValue = 0; lvl.computedAddr = 0;
        }
        m_ui->label_base_pointer_compute_value->setText("=?????");
        return;
    }

    auto mem = ProcessManager::instance().memory();
    if (!mem) {
        for (auto& lvl : m_pointerLevels)
            lvl.resultLabel->setText(tr("???+?=???"));
        m_ui->label_base_pointer_compute_value->setText(tr("->无进程"));
        return;
    }

    // 指针链解引用必须始终按指针宽度（64位系统为8字节）来读取地址值，
    // 不能使用当前数据类型的字节大小（如字符串类型 sizeof(Type) 返回32，Byte类型返回1）
    size_t ptrSize = sizeof(uint64_t);

    uint64_t basePtrValue = 0;
    if (!mem->read(m_pointerBaseAddr, &basePtrValue, ptrSize)) {
        for (auto& lvl : m_pointerLevels) {
            lvl.pointerValue = 0; lvl.computedAddr = 0;
            lvl.resultLabel->setText(tr("读取失败"));
        }
        m_ui->label_base_pointer_compute_value->setText("->?????");
        return;
    }

    m_ui->label_base_pointer_compute_value->setText(
        QString("->0x%1").arg(basePtrValue, 16, 16, QChar('0')));

    uint64_t currentAddr = basePtrValue;
    bool allLevelsOk = true;

    for (int i = 0; i < static_cast<int>(m_pointerLevels.size()); ++i) {
        auto& lvl = m_pointerLevels[i];

        QString offsetText = lvl.offsetEdit->text().trimmed();
        bool offOk = false;
        lvl.offset = offsetText.isEmpty() ? 0 : offsetText.toLongLong(&offOk);

        uint64_t readAddr = currentAddr + static_cast<uint64_t>(static_cast<int64_t>(lvl.offset));

        uint64_t pointerValue = 0;
        if (!mem->read(readAddr, &pointerValue, ptrSize)) {
            lvl.pointerValue = 0; lvl.computedAddr = 0;
            lvl.resultLabel->setText(tr("读取失败"));
            // 当前级失败后停止继续；不清除 base label
            allLevelsOk = false;
            break;
        }

        lvl.pointerValue = pointerValue;
        lvl.computedAddr = pointerValue;

        lvl.levelLabel->setText(
            QStringLiteral(" %1 级指针")
                .arg(i + 1));

        QString offsetHex = (lvl.offset >= 0)
            ? QString("0x%1").arg(lvl.offset, 0, 16)
            : QString("-0x%1").arg(-lvl.offset, 0, 16);

        lvl.resultLabel->setText(
            QString("0x%1+%2=0x%3")
                .arg(currentAddr, 0, 16).arg(offsetHex).arg(pointerValue, 16, 16, QChar('0')));

        currentAddr = lvl.computedAddr;
    }

    // 用最后成功解引用的地址更新地址栏（即使部分级失败）
    m_computedFinalAddr = currentAddr;
    m_ui->lineEdit_address->setText(
        QString("0x%1").arg(m_computedFinalAddr, 16, 16, QChar('0')));

    refreshComputedValue();
}

void Add_Or_Change_Address_Dialog::refreshComputedValue()
{
    QString addrText = m_ui->lineEdit_address->text().trimmed();
    if (addrText.isEmpty()) { m_ui->label_computed_Value->setText("=???"); return; }

    bool ok = false;
    uint64_t addr = 0;
    if (addrText.startsWith("0x", Qt::CaseInsensitive))
        addr = addrText.toULongLong(&ok, 16);
    else
        addr = addrText.toULongLong(&ok, 10);

    if (!ok || addr == 0) { m_ui->label_computed_Value->setText("=???"); return; }

    auto mem = ProcessManager::instance().memory();
    if (!mem) { m_ui->label_computed_Value->setText(tr("=无进程")); return; }

    int dtIndex = m_ui->comboBox_Data_Type->currentIndex();
    auto vt = static_cast<AddressItem::Type>(m_ui->comboBox_Data_Type->itemData(dtIndex).toInt());

    QString valueStr;

    if (AddressItem::isStringType(vt)) {
        int len = m_ui->lineEdit_length->text().toInt(&ok);
        if (!ok || len <= 0) len = 32;
        if (len > 4096) len = 4096;

        std::vector<uint8_t> buf(len);
        if (!mem->read(addr, buf.data(), len)) {
            m_ui->label_computed_Value->setText(tr("=读取失败")); return;
        }

        int encIdx = m_ui->comboBox_encoding->currentIndex();
        auto enc = static_cast<AddressItem::Encoding>(m_ui->comboBox_encoding->itemData(encIdx).toInt());

        int realLen = 0;
        if (enc == AddressItem::Encoding::UTF16) {
            const char16_t* u16 = reinterpret_cast<const char16_t*>(buf.data());
            int u16len = len / 2;
            while (realLen < u16len && u16[realLen] != 0) ++realLen;
            if (realLen > 0) valueStr = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.data()), realLen);
        } else if (enc == AddressItem::Encoding::UTF8) {
            const char* data = reinterpret_cast<const char*>(buf.data());
            while (realLen < len && data[realLen] != '\0') ++realLen;
            valueStr = QString::fromUtf8(data, realLen);
        } else {
            const char* data = reinterpret_cast<const char*>(buf.data());
            while (realLen < len && data[realLen] != '\0') ++realLen;
            valueStr = QString::fromLatin1(data, realLen);
        }
        if (realLen > 64) valueStr = valueStr.left(64) + "...";
        valueStr = valueStr.toHtmlEscaped();

    } else if (AddressItem::isByteArrayType(vt)) {
        int len = m_ui->lineEdit_length->text().toInt(&ok);
        if (!ok || len <= 0) len = 32;
        std::vector<uint8_t> buf(len);
        if (!mem->read(addr, buf.data(), len)) {
            m_ui->label_computed_Value->setText(tr("=读取失败")); return;
        }
        QStringList hexParts;
        int showCount = qMin(len, 16);
        for (int i = 0; i < showCount; ++i) hexParts += QString::asprintf("%02X", buf[i]);
        valueStr = hexParts.join(' ');
        if (len > 16) valueStr += "...";

    } else {
        size_t size = AddressItem::typeSize(vt);
        uint64_t raw = 0;
        if (!mem->read(addr, &raw, size)) {
            m_ui->label_computed_Value->setText(tr("=读取失败")); return;
        }

        bool hex = m_ui->checkBox_Hex_Display->isChecked();
        bool signedDisplay = m_ui->checkBox_signed_Value->isChecked();

        switch (vt) {
        case AddressItem::Type::Int8: {
            if (hex) valueStr = QString("0x%1").arg(static_cast<uint8_t>(raw & 0xFF), 2, 16, QChar('0'));
            else if (signedDisplay) valueStr = QString::number(static_cast<int8_t>(raw & 0xFF));
            else valueStr = QString::number(static_cast<uint8_t>(raw & 0xFF));
            break;
        }
        case AddressItem::Type::Int16: {
            if (hex) valueStr = QString("0x%1").arg(static_cast<uint16_t>(raw & 0xFFFF), 4, 16, QChar('0'));
            else if (signedDisplay) valueStr = QString::number(static_cast<int16_t>(raw & 0xFFFF));
            else valueStr = QString::number(static_cast<uint16_t>(raw & 0xFFFF));
            break;
        }
        case AddressItem::Type::Int32: {
            if (hex) valueStr = QString("0x%1").arg(static_cast<uint32_t>(raw), 8, 16, QChar('0'));
            else if (signedDisplay) valueStr = QString::number(static_cast<int32_t>(raw));
            else valueStr = QString::number(static_cast<uint32_t>(raw));
            break;
        }
        case AddressItem::Type::Int64: {
            if (hex) valueStr = QString("0x%1").arg(raw, 16, 16, QChar('0'));
            else if (signedDisplay) valueStr = QString::number(static_cast<int64_t>(raw));
            else valueStr = QString::number(raw);
            break;
        }
        case AddressItem::Type::Float: {
            if (hex) { uint32_t bits; std::memcpy(&bits, &raw, sizeof(bits));
                valueStr = QString("0x%1").arg(bits, 8, 16, QChar('0'));
            } else { float f; std::memcpy(&f, &raw, sizeof(f));
                valueStr = QString::number(f, 'g', 7); }
            break;
        }
        case AddressItem::Type::Double: {
            if (hex) valueStr = QString("0x%1").arg(raw, 16, 16, QChar('0'));
            else { double d; std::memcpy(&d, &raw, sizeof(d));
                valueStr = QString::number(d, 'g', 15); }
            break;
        }
        default: valueStr = "=???"; break;
        }
    }

    m_ui->label_computed_Value->setText("= " + valueStr);
}

void Add_Or_Change_Address_Dialog::updateStringControlsVisibility()
{
    AddressItem::Type vt = static_cast<AddressItem::Type>(m_ui->comboBox_Data_Type->currentData().toInt());

    bool isString = AddressItem::isStringType(vt);
    m_ui->label_length->setVisible(isString);
    m_ui->lineEdit_length->setVisible(isString);
    m_ui->comboBox_encoding->setVisible(isString);
    m_ui->checkBox_code_page->setVisible(false);

    bool showHex = !AddressItem::isStringType(vt);
    m_ui->checkBox_Hex_Display->setVisible(showHex);

    bool showSigned = AddressItem::isIntegerType(vt);
    m_ui->checkBox_signed_Value->setVisible(showSigned);

    if (AddressItem::isByteArrayType(vt)) {
        m_ui->label_length->setVisible(true);
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setVisible(true);
        m_ui->lineEdit_length->setText("32");
        m_ui->comboBox_encoding->setVisible(false);
    }

    if (AddressItem::isNumericType(vt)) {
        m_ui->label_length->setVisible(true);
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setVisible(true);
        m_ui->lineEdit_length->setEnabled(false);
        m_ui->lineEdit_length->setText(QString::number(AddressItem::typeSize(vt)));
        m_ui->comboBox_encoding->setVisible(false);
    }

    if (AddressItem::isStringType(vt)) {
        m_ui->label_length->setText(tr("长度"));
        m_ui->lineEdit_length->setEnabled(true);
        m_ui->lineEdit_length->setText("32");
    }
}

bool Add_Or_Change_Address_Dialog::validateInput()
{
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

    if (m_ui->checkBox_pointer->isChecked()) {
        QString ptrText = m_ui->lineEdit_base_pointer->text().trimmed();
        if (ptrText.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("指针模式：基础指针地址不能为空。"));
            m_ui->lineEdit_base_pointer->setFocus();
            return false;
        }
        bool parseOk = false;
        parseAddressOrModule(ptrText, &parseOk);
        if (!parseOk) {
            QMessageBox::warning(this, tr("错误"), tr("无效的基址格式。支持：0x地址、十进制数字、或\"模块名+0x偏移\"。"));
            m_ui->lineEdit_base_pointer->setFocus();
            return false;
        }
    }

    accept();
    return true;
}

// ==================== 结果 ====================

AddressItem::Config Add_Or_Change_Address_Dialog::resultConfig() const
{
    AddressItem::Config cfg;

    // 从 m_initialConfig 继承原始 buffer 等不变属性，
    // 再覆盖用户修改过的属性

    // 地址
    if (m_ui->checkBox_pointer->isChecked())
        cfg.address = m_computedFinalAddr;
    else {
        QString text = m_ui->lineEdit_address->text().trimmed();
        bool ok = false;
        if (text.startsWith("0x", Qt::CaseInsensitive))
            cfg.address = text.toULongLong(&ok, 16);
        else
            cfg.address = text.toULongLong(&ok, 10);
        if (!ok) cfg.address = 0;
    }

    // 描述
    cfg.description = m_ui->lineEdit_description->text().trimmed();

    // 类型
    int dtIdx = m_ui->comboBox_Data_Type->currentIndex();
    cfg.type = static_cast<AddressItem::Type>(m_ui->comboBox_Data_Type->itemData(dtIdx).toInt());

    // Hex / Signed 显示
    cfg.hexDisplay = m_ui->checkBox_Hex_Display->isChecked();
    cfg.signedDisplay = m_ui->checkBox_signed_Value->isChecked();

    // 编码
    int encIdx = m_ui->comboBox_encoding->currentIndex();
    cfg.encoding = static_cast<AddressItem::Encoding>(m_ui->comboBox_encoding->itemData(encIdx).toInt());

    // 长度
    bool lenOk = false;
    int len = m_ui->lineEdit_length->text().toInt(&lenOk);
    cfg.stringLength = (lenOk && len > 0) ? len : 0;

    // 指针链
    if (m_ui->checkBox_pointer->isChecked()) {
        cfg.pointerChain.baseAddressText = m_ui->lineEdit_base_pointer->text().trimmed();
        cfg.pointerChain.baseAddress = m_pointerBaseAddr;

        cfg.pointerChain.levels.clear();
        cfg.pointerChain.levels.reserve(m_pointerLevels.size());
        for (const auto& lvl : m_pointerLevels) {
            PointerLevel pl;
            pl.offset = lvl.offset;
            cfg.pointerChain.levels.push_back(pl);
        }
    }

    // buffer：对于字符串/字节数组类型，从内存重新读取
    if (AddressItem::isStringType(cfg.type) || AddressItem::isByteArrayType(cfg.type)) {
        auto mem = ProcessManager::instance().memory();
        if (mem && cfg.address != 0) {
            int readLen = cfg.stringLength > 0 ? cfg.stringLength : static_cast<int>(AddressItem::typeSize(cfg.type));
            if (readLen <= 0) readLen = 32;
            cfg.buffer.resize(readLen);
            mem->read(cfg.address, cfg.buffer.data(), readLen);
        }
    }

    // 保留其他不通过 UI 修改的属性（如 rawValue 等，将在模型里重新读取时覆盖）
    return cfg;
}

size_t Add_Or_Change_Address_Dialog::currentDataTypeSize() const
{
    AddressItem::Type vt = static_cast<AddressItem::Type>(m_ui->comboBox_Data_Type->currentData().toInt());
    if (AddressItem::isNumericType(vt) || AddressItem::isByteArrayType(vt))
        return AddressItem::typeSize(vt);
    return 1;
}
