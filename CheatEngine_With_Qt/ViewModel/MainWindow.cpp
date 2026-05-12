//  "([^"]*[\u4e00-\u9fff][^"]*)" 这个正则表达式可以匹配包含至少一个中文字符的字符串，适用于提取中文文本。它的工作原理如下

#include "ViewModel\MainWindow.h"
#include "ui_CheatEngine_With_Qt.h"
#include "ViewModel\address_list_model.h"
#include "ViewModel\process_dialog.h"
#include "process\process_manager.h"
#include "scan\scan_service.h"
#include "scan\scan_result_view_model.h"
#include "scan\scan_data_stream_define.h"
#include "language_translations\translator_manager.h"

#include <QTimer>
#include <QTableView>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>
#include <QStringList>
#include <cstring>

// ==================== 构造与析构 ====================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::CheatEngine_With_QtClass>())
{
    setupUi();
    initServices();
    initLanguageCombobox();
    initViews();
    initTimers();
    initDataTypeComboBox();
    updateScanTypeComboBox();
    connectSignals();
    refreshUiControls();
}

MainWindow::~MainWindow() = default;

// ==================== UI 基础 ====================
void MainWindow::setupUi() { ui->setupUi(this); }

void MainWindow::initServices()
{
    m_scanService = new ScanService(this);
    m_resultModel = m_scanService->getResultViewModel();   // 获取 ViewModel 指针
    addressModel = new AddressListModel(this);
}

void MainWindow::initLanguageCombobox(int currentLangIndex)
{
    ui->comboBox_language->blockSignals(true);
    ui->comboBox_language->clear();
    ui->comboBox_language->addItem(tr("language"));
    ui->comboBox_language->addItem(tr("English"));
    ui->comboBox_language->setCurrentIndex(currentLangIndex);
    ui->comboBox_language->blockSignals(false);
    connect(ui->comboBox_language, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::onLanguageChanged);
    
}

void MainWindow::initViews()
{
    setupScanResultView();
    replaceAddressTable();
    ui->progressBar->setVisible(false);
    ui->checkBox_Only_Simple_Value->setToolTip(tr("仅扫描看起来是普通数值的地址，排除指针、代码、加密数据等复杂值"));
    ui->checkBox_percent->setToolTip(tr("选中时，后面的输入框预期输入数字为 1 到 100 之间的百分比值\n\n对比上一次变化的幅度，比如100变成 150 就是减少/增大了百分之50"));
}

void MainWindow::updateCountLabels()
{
    // 获取仓库中的总结果数
    int total = m_scanService->totalResults();

    // 获取当前视图模型显示的条目数
    int shown = m_resultModel->rowCount();

    ui->label_scan_result->setText(QString(tr("找到总地址: %1 显示地址: %2")).arg(total).arg(shown));
}


void MainWindow::setupScanResultView()
{
    QWidget* firstTab = ui->tabWidget->widget(0);
    QVBoxLayout* tabLayout = new QVBoxLayout(firstTab);
    tabLayout->setContentsMargins(0, 0, 0, 0);

    scanResultView = new QTableView(firstTab);
    scanResultView->setModel(m_resultModel);
    scanResultView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    scanResultView->verticalHeader()->setDefaultSectionSize(24);
    scanResultView->horizontalHeader()->setStretchLastSection(false);
    scanResultView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    scanResultView->setColumnWidth(0, 150);
    scanResultView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    scanResultView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    scanResultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabLayout->addWidget(scanResultView);
}

void MainWindow::replaceAddressTable()
{
    QLayout* addrLayout = ui->tableWidget_addressList->parentWidget()->layout();
    if (auto* vbox = qobject_cast<QVBoxLayout*>(addrLayout)) {
        vbox->removeWidget(ui->tableWidget_addressList);
        delete ui->tableWidget_addressList;
        addressView = new QTableView(this);
        addressView->setModel(addressModel);
        addressView->setSelectionBehavior(QAbstractItemView::SelectRows);
        addressView->setSelectionMode(QAbstractItemView::SingleSelection);
        vbox->addWidget(addressView);
    }
}

// ==================== 定时器 ====================
void MainWindow::initTimers()
{
    freezeTimer = new QTimer(this);
    connect(freezeTimer, &QTimer::timeout, this, [this]() {
        auto& items = addressModel->items();
        auto mem = ProcessManager::instance().memory();
        if (!mem) return;
        for (auto& item : items) {
            if (item.frozen)
                mem->write(item.address, &item.value, sizeof(item.value));
        }
        });
    freezeTimer->start(500);

    healthTimer = new QTimer(this);
    connect(healthTimer, &QTimer::timeout, this, [this]() {
        if (m_attachedToProcess && !ProcessManager::instance().isProcessAlive()) {
            m_attachedToProcess = false;
            QMetaObject::invokeMethod(this, [this] { onProcessTerminated(); });
        }
        });
    healthTimer->start(1000);
}



uint64_t MainWindow::resolveValue(const QString& text, ScanDataType dataType) const {
    bool ok = false;
    QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) return 0;

    // 1. 处理浮点数类型
    if (isFloatingPoint(dataType)) {
        double dVal = trimmedText.toDouble(&ok);
        if (!ok) return 0;

        uint64_t result = 0;
        if (dataType == ScanDataType::Float32) {
            float fVal = static_cast<float>(dVal);
            // 将 float 的位模式拷贝到 result 的低 32 位
            std::memcpy(&result, &fVal, sizeof(float));
        }
        else {
            // 将 double 的位模式拷贝到 result
            std::memcpy(&result, &dVal, sizeof(double));
        }
        return result;
    }

    // 2. 处理整数类型 (Bit, Int8, Int16, Int32, Int64)
    bool isHex = ui->checkBox_Hex_Value->isChecked();

    // CE 习惯：如果输入以 0x 开头，强制按十六进制解析，无视 CheckBox
    int base = 10;
    if (trimmedText.startsWith("0x", Qt::CaseInsensitive)) {
        base = 16;
    }
    else {
        base = isHex ? 16 : 10;
    }

    uint64_t iVal = trimmedText.toULongLong(&ok, base);

    // 3. 溢出与位宽截断 (模仿 CE 的数据类型溢出处理)
    if (!ok) return 0;

    switch (dataType) {
    case ScanDataType::Int8:  return static_cast<uint8_t>(iVal);
    case ScanDataType::Int16: return static_cast<uint16_t>(iVal);
    case ScanDataType::Int32: return static_cast<uint32_t>(iVal);
    case ScanDataType::Bit:   return iVal & 0x1; // 仅取最低位
    default:                  return iVal;       // Int64 或其他
    }
}

ScanParams MainWindow::parseCurrentParams(ScanDataType dataType, int scanTypeInt, bool isNextScan) const {
    // 1. 确定当前是否需要输入框，需要几个
    int neededInputs = 0;
    if (isNextScan) {
        NextScanType nt = static_cast<NextScanType>(scanTypeInt);
        if (nt == NextScanType::Equal || nt == NextScanType::IncreasedBy || nt == NextScanType::DecreasedBy) neededInputs = 1;
        else if (nt == NextScanType::Between) neededInputs = 2;
    }
    else {
        ScanType st = static_cast<ScanType>(scanTypeInt);
        if (st == ScanType::Between) neededInputs = 2;
        else if (st != ScanType::UnknownInitial) neededInputs = 1;
    }

    // 2. 根据数据类型分发解析
    if (isStringType(dataType)) return parseStringParams();
    if (dataType == ScanDataType::ByteArray) return parseAobParams();

    // 3. 统一数值解析 (处理 Hex, Float, Integer)
    ValueParams vp;
    if (neededInputs >= 1) {
        vp.value1 = resolveValue(ui->lineEdit_ValueInput->text(), dataType);
    }
    if (neededInputs == 2) {
        vp.value2 = resolveValue(ui->lineEdit_ValueInput2->text(), dataType);
    }
    return vp;
}


// ==================== 信号连接 ====================
void MainWindow::connectSignals()
{
    connect(ui->pushButton_openProcess, &QPushButton::clicked,
        this, &MainWindow::onOpenProcess);
    connect(ui->pushButton_new_find, &QPushButton::clicked,
        this, &MainWindow::onFirstScan);
    connect(ui->pushButton_next_find, &QPushButton::clicked,
        this, &MainWindow::onNextScan);

    connect(m_scanService, &ScanService::scanCompleted,
        this, &MainWindow::onScanCompleted);

    connect(m_scanService, &ScanService::progressChanged,
        this, &MainWindow::onProgressChanged);

    connect(scanResultView, &QTableView::doubleClicked,
        this, &MainWindow::onDoubleClickScanResult);

    connect(ui->pushButton_delete_all_address, &QPushButton::clicked, this, [this]() {
        addressModel->clear();
        });

    connect(ui->comboBox_atribute_For_Find, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::refreshUiControls);

    connect(ui->comboBox_Value_Data_Size, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::updateScanTypeComboBox);

    connect(ui->checkBox_use_UTF8, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) ui->checkBox_use_UTF16->setChecked(false);
        refreshUiControls(); // 状态变更后刷新 UI
        });

    connect(ui->checkBox_use_UTF16, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) ui->checkBox_use_UTF8->setChecked(false);
        refreshUiControls(); // 状态变更后刷新 UI
        });
    connect(ui->checkBox_fast_scan, &QCheckBox::toggled, this, &MainWindow::refreshUiControls);
    connect(ui->checkBox_percent, &QCheckBox::toggled, this, &MainWindow::refreshUiControls);
    connect(ui->checkBox_repeat, &QCheckBox::toggled, this, &MainWindow::refreshUiControls);
    connect(ui->checkBox_Only_Simple_Value, &QCheckBox::toggled, this, &MainWindow::refreshUiControls);

}


// ==================== 打开进程 ====================
void MainWindow::onOpenProcess()
{
    ProcessDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto p = dlg.selectedProcess();

    m_scanService->cancel();
    m_scanService->clear();       

    ProcessManager::instance().detach();

    if (ProcessManager::instance().attach(p)) {
        m_attachedToProcess = true;
        m_isFirstScan = true;



        ui->label_Process_name->setText(QString::fromStdString(p.name));
        setWindowTitle(QString("Cheat Engine - %1").arg(QString::fromStdString(p.name)));

        QComboBox* moduleBox = ui->comboBox_process_module_List;

        // UI 还原
        ui->comboBox_Value_Data_Size->setEnabled(true);
        ui->pushButton_new_find->setText(tr("首次扫描"));
        ui->pushButton_new_find->setEnabled(true);
        ui->pushButton_next_find->setEnabled(false);

        moduleBox->clear();
        moduleBox->addItem(tr("全部模块"));
        const auto& modules = ProcessManager::instance().modules();
        for (const auto& mod : modules)
            moduleBox->addItem(QString::fromStdString(mod.name));
    }
    else {
        QMessageBox::warning(this, tr("错误"), tr("附加进程失败."));
    }

    updateCountLabels();
    initDataTypeComboBox();
    updateScanTypeComboBox();
    //refreshUiControls();
}

// ==================== 构建扫描请求（保留原解析逻辑） ====================
// mainwindow.cpp

void MainWindow::initDataTypeComboBox()
{
    // 初始化数据类型下拉框...
    // 默认勾选 UTF-8 复选框（中文系统最常用）
    ui->checkBox_use_UTF8->setChecked(true);

    ui->comboBox_Value_Data_Size->clear();

    // 使用 QVariant 绑定枚举值到每一个条目
    ui->comboBox_Value_Data_Size->addItem(tr("二进制 (Bit)"), (int)ScanDataType::Bit);
    ui->comboBox_Value_Data_Size->addItem(tr("1 字节整数"), (int)ScanDataType::Int8);
    ui->comboBox_Value_Data_Size->addItem(tr("2 字节整数"), (int)ScanDataType::Int16);
    ui->comboBox_Value_Data_Size->addItem(tr("4 字节整数"), (int)ScanDataType::Int32);
    ui->comboBox_Value_Data_Size->addItem(tr("8 字节整数"), (int)ScanDataType::Int64);
    ui->comboBox_Value_Data_Size->addItem(tr("32位单浮点小数"), (int)ScanDataType::Float32);
    ui->comboBox_Value_Data_Size->addItem(tr("64位双浮点小数"), (int)ScanDataType::Float64);
    ui->comboBox_Value_Data_Size->addItem(tr("字符串"), (int)ScanDataType::AsciiString);
    ui->comboBox_Value_Data_Size->addItem(tr("字节数组"), (int)ScanDataType::ByteArray);
    ui->comboBox_Value_Data_Size->addItem(tr("所有类型 (整数和浮点小数)"), (int)ScanDataType::All);
    ui->comboBox_Value_Data_Size->addItem(tr("结构体"), (int)ScanDataType::Structure);

    // 默认选中 4 字节
    ui->comboBox_Value_Data_Size->setCurrentIndex(3);
}

ScanDataType MainWindow::parseDataTypeFromUI() const
{
    // 获取 ComboBox 当前条目绑定的枚举数据
    QVariant data = ui->comboBox_Value_Data_Size->currentData();
    if (!data.isValid()) return ScanDataType::Int32; // 默认值

    ScanDataType type = static_cast<ScanDataType>(data.toInt());

    if (type == ScanDataType::AsciiString) {
        if (ui->checkBox_use_UTF16->isChecked()) {
            return ScanDataType::Utf16String;
        }
         if (ui->checkBox_use_UTF8->isChecked()) return ScanDataType::Utf8String;

        return ScanDataType::AsciiString;
    }

    return type;
}


ValueParams MainWindow::parseValueParams(ScanType type, ScanDataType dataType) const
{
    ValueParams vp;
    if (type == ScanType::UnknownInitial) return vp;

    QString text1 = ui->lineEdit_ValueInput->text();
    bool isHex = ui->checkBox_Hex_Value->isChecked();

    if (isFloatingPoint(dataType)) {
        bool ok = false;
        double d1 = text1.toDouble(&ok);
        if (!ok && !text1.isEmpty()) return {};
        if (dataType == ScanDataType::Float32) {
            float f = static_cast<float>(d1);
            std::memcpy(&vp.value1, &f, sizeof(f));
        }
        else {
            std::memcpy(&vp.value1, &d1, sizeof(d1));
        }

        if (type == ScanType::Between) {
            QString text2 = ui->lineEdit_ValueInput2->text();
            double d2 = text2.toDouble(&ok);
            if (!ok) return {};
            if (dataType == ScanDataType::Float32) {
                float f2 = static_cast<float>(d2);
                std::memcpy(&vp.value2, &f2, sizeof(f2));
            }
            else {
                std::memcpy(&vp.value2, &d2, sizeof(d2));
            }
        }
    }
    else {
        bool ok = false;
        vp.value1 = isHex ? text1.toULongLong(&ok, 16) : text1.toULongLong(&ok, 10);
        if (!ok && !text1.isEmpty()) return {};
        if (type == ScanType::Between) {
            QString text2 = ui->lineEdit_ValueInput2->text();
            bool ok2 = false;
            vp.value2 = isHex ? text2.toULongLong(&ok2, 16) : text2.toULongLong(&ok2, 10);
            if (!ok2) return {};
        }
    }
    return vp;
}

StringParams MainWindow::parseStringParams() const
{
    StringParams sp;
    sp.caseSensitive = ui->checkBox_Caps_Check->isChecked();
    QString text = ui->lineEdit_ValueInput->text();
    ScanDataType type = parseDataTypeFromUI();

    if (type == ScanDataType::Utf16String) {
        // UTF-16: 提取原始 2 字节数据
        auto utf16Data = text.utf16();
        sp.text.assign(reinterpret_cast<const char*>(utf16Data), text.size() * 2);
    }
    else if (type == ScanDataType::Utf8String) {
        // UTF-8: 提取 UTF-8 编码的字节流
        sp.text = text.toUtf8().toStdString();
    }
    else {
        // Ascii (ANSI): 按照本地 8 位编码提取（中文系统为 GBK）
        sp.text = text.toLocal8Bit().toStdString();
    }
    return sp;
}

AobParams MainWindow::parseAobParams() const
{
    AobParams ap;
    QString text = ui->lineEdit_ValueInput->text().trimmed();
    QStringList tokens = text.split(' ', Qt::SkipEmptyParts);
    for (const QString& tok : tokens) {
        if (tok.compare("??", Qt::CaseInsensitive) == 0 || tok == "?") {
            ap.pattern.push_back(0);
            ap.mask.push_back(false);
        }
        else {
            bool ok = false;
            uint32_t val = tok.toUInt(&ok, 16);
            if (!ok || val > 0xFF) return {};
            ap.pattern.push_back(static_cast<uint8_t>(val));
            ap.mask.push_back(true);
        }
    }
    return ap;
}


// ==================== 扫描执行 ====================
void MainWindow::onFirstScan()
{
    if (m_scanService->isScanning()) {
        m_scanService->cancel();
        m_isScanning = false;
        refreshUiControls();
        return;
    }

    if (!m_isFirstScan) {
        m_isFirstScan = true;
        m_scanService->clear();
        m_scanService->stopAutoRefresh();
        updateScanTypeComboBox();
        //refreshUiControls();
        updateCountLabels();
        return; 
    }


    if (!ProcessManager::instance().memory()) {
        QMessageBox::warning(this, tr("错误"), tr("请先打开一个进程."));
        return;
    }


    // 新增：验证输入，不合法则弹窗并返回
    if (!validateScanInput(ScanMode::First)) {
        return;
    }


    ScanRequest req = buildScanRequest(ScanMode::First);
    //ScanRequest req = buildFirstScanRequest();
    // 参数校验
    if (std::holds_alternative<StringParams>(req.params)) {
        if (std::get<StringParams>(req.params).text.empty()) {
            QMessageBox::warning(this, tr("错误"), tr("请输入一个字符串."));
            return;
        }
    }
    else if (std::holds_alternative<AobParams>(req.params)) {
        if (std::get<AobParams>(req.params).pattern.empty()) {
            QMessageBox::warning(this, tr("错误"), tr("请输入一个字节数组类似这样的格式 （如：AA ?? BB）."));
            return;
        }
    }
    else if (std::holds_alternative<ValueParams>(req.params)) {
        if (req.firstType != ScanType::UnknownInitial &&
            std::get<ValueParams>(req.params).value1 == 0 &&
            ui->lineEdit_ValueInput->text().isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("无效的输入数值."));
            return;
        }
    }

    m_currentDataType = req.dataType;
    m_currentFirstScanType = req.firstType;
    m_scanService->startScan(req);

	//扫描进度条显示
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    statusBar()->showMessage(tr("正在初始化扫描..."));

    m_isScanning = true;
    refreshUiControls();
}


ScanRequest MainWindow::buildScanRequest(ScanMode mode) const
{
    ScanRequest req;
    req.mode = mode;
    req.dataType = (mode == ScanMode::First) ? parseDataTypeFromUI() : m_currentDataType;

    // 获取 ComboBox 选中的类型
    QVariant typeData = ui->comboBox_atribute_For_Find->currentData();
    if (mode == ScanMode::First) {
        req.firstType = typeData.value<ScanType>();
    }
    else {
        req.nextType = typeData.value<NextScanType>();
    }

    // 快速扫描/对齐
    if (ui->checkBox_fast_scan->isChecked()) {
        req.alignment = ui->lineEdit_fast_scan_value->text().toUInt();
    }
    else {
        req.alignment = 1;
    }

    // ===== 模块过滤 =====
    const QString selectedModule = ui->comboBox_process_module_List->currentText();
    if (selectedModule != tr("全部模块")) {
        const auto& modules = ProcessManager::instance().modules();
        for (const auto& mod : modules) {
            if (QString::fromStdString(mod.name) == selectedModule) {
                req.moduleBase = mod.base;
                req.moduleSize = mod.size;
                break;
            }
        }
    }

    // ===== 内存属性过滤 =====
    req.onlyWritable = ui->checkBox_able_to_write->isChecked();
    req.includeExecutable = ui->checkBox_able_to_execute->isChecked();


    // ===== 其他 UI 选项（已有但未赋值） =====
    req.percentMode = ui->checkBox_percent->isChecked();

    // 根据模式填充参数
    if (req.dataType == ScanDataType::Structure) {
        req.params = getStructureParamsFromUi();
    }
    else if (isStringType(req.dataType)) {
        req.params = parseStringParams();
    }
    else if (isByteArrayType(req.dataType)) {
        req.params = parseAobParams();
    }
    else {
        // 数值扫描类型 (支持 Between 和单一值)
        req.params = parseValueParams(req.firstType, req.dataType);
    }

    return req;
}



void MainWindow::onNextScan()
{
    if (!ProcessManager::instance().memory()) {
        QMessageBox::warning(this, tr("错误"), tr("没有附加进程."));
        return;
    }
    if (m_scanService->isScanning()) return;
    if (!m_scanService->hasResults()) {
        QMessageBox::warning(this, tr("错误"), tr("请进行一次初次扫描."));
        return;
    }

    if (!validateScanInput(ScanMode::Next)) {
        return;
    }
    


	ScanRequest req = buildScanRequest(ScanMode::Next);

    if (std::holds_alternative<ValueParams>(req.params)) {
        if (std::get<ValueParams>(req.params).value1 == 0 &&
            ui->lineEdit_ValueInput->text().isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("无效的输入数值."));
            return;
        }
    }

    m_scanService->startScan(req);
    m_isScanning = true;
    refreshUiControls();
    ui->pushButton_new_find->setEnabled(false);
    ui->pushButton_next_find->setEnabled(false);
}

void MainWindow::updateScanTypeComboBox()
{
    ui->comboBox_atribute_For_Find->blockSignals(true);
    ui->comboBox_atribute_For_Find->clear();

    const bool isFirst = m_isFirstScan;
    const ScanDataType dataType = isFirst ? parseDataTypeFromUI() : m_currentDataType;

    // 判断是否为单一扫描条件类型（字符串、字节数组、Bit）
    const bool uniqueCondition = isStringType(dataType) || (dataType == ScanDataType::ByteArray) || (dataType == ScanDataType::Bit);

    if (dataType == ScanDataType::Structure) {
        ui->comboBox_atribute_For_Find->addItem(tr("结构体扫描"));
    }
    else if (uniqueCondition) {

        if (isStringType(dataType)) {
            if (isFirst)
                ui->comboBox_atribute_For_Find->addItem(tr("字符串搜索"), QVariant::fromValue(ScanType::StringScan));
            else
                ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(NextScanType::Equal));
        }
        else if (dataType == ScanDataType::ByteArray) {
            if (isFirst)
                ui->comboBox_atribute_For_Find->addItem(tr("字节数组"), QVariant::fromValue(ScanType::ExactValue));
            else
                ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(NextScanType::Equal));
        }
        else if (dataType == ScanDataType::Bit) {
            if (isFirst)
                ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(ScanType::ExactValue));
            else
                ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(NextScanType::Equal));
        }
        ui->comboBox_atribute_For_Find->setCurrentIndex(0);
    }
    else {
        // 数值类型（含 All）动态添加
        if (isFirst) {
            ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(ScanType::ExactValue));
            ui->comboBox_atribute_For_Find->addItem(tr("值大于"), QVariant::fromValue(ScanType::GreaterThan));
            ui->comboBox_atribute_For_Find->addItem(tr("值小于"), QVariant::fromValue(ScanType::LessThan));
            ui->comboBox_atribute_For_Find->addItem(tr("介于两者之间"), QVariant::fromValue(ScanType::Between));
            ui->comboBox_atribute_For_Find->addItem(tr("未知初始值"), QVariant::fromValue(ScanType::UnknownInitial));
        }
        else {
            ui->comboBox_atribute_For_Find->addItem(tr("精确数值"), QVariant::fromValue(NextScanType::Equal));
            ui->comboBox_atribute_For_Find->addItem(tr("变动的值"), QVariant::fromValue(NextScanType::Changed));
            ui->comboBox_atribute_For_Find->addItem(tr("未变动的值"), QVariant::fromValue(NextScanType::Unchanged));
            ui->comboBox_atribute_For_Find->addItem(tr("增加的值"), QVariant::fromValue(NextScanType::Increased));
            ui->comboBox_atribute_For_Find->addItem(tr("减少的值"), QVariant::fromValue(NextScanType::Decreased));
            ui->comboBox_atribute_For_Find->addItem(tr("数值增加了多少"), QVariant::fromValue(NextScanType::IncreasedBy));
            ui->comboBox_atribute_For_Find->addItem(tr("数值减少了多少"), QVariant::fromValue(NextScanType::DecreasedBy));
            ui->comboBox_atribute_For_Find->addItem(tr("介于两者之间"), QVariant::fromValue(NextScanType::Between));
            ui->comboBox_atribute_For_Find->addItem(tr("以...结尾的数值"), QVariant::fromValue(NextScanType::EndsWith));
            ui->comboBox_atribute_For_Find->addItem(tr("对比首次扫描"), QVariant::fromValue(NextScanType::Compare_to_First_Scan));
        }
        ui->comboBox_atribute_For_Find->setCurrentIndex(0);
    }

    ui->comboBox_atribute_For_Find->blockSignals(false);
    refreshUiControls(); // 更新完条件列表后立即刷新其余控件
}

// ==================== 扫描完成槽 ====================
void MainWindow::onScanCompleted()
{
	m_isFirstScan = false;
    m_isScanning = false;

    refreshUiControls();
    updateScanTypeComboBox();
    updateCountLabels();


    if (isStringType(m_currentDataType) || m_currentDataType == ScanDataType::ByteArray) {
        m_scanService->stopAutoRefresh();
    }
    else {
        m_scanService->startAutoRefresh(200);
    }

    ui->progressBar->setVisible(false);
    statusBar()->showMessage(tr("扫描完成，找到 %1 个结果").arg(m_scanService->totalResults()), 3000);
}

// ==================== 进度变化槽 ====================
void MainWindow::onProgressChanged(int completed, int total)
{
    if (total <= 0) return;
    ui->progressBar->setVisible(true);
    ui->progressBar->setRange(0, total);
    ui->progressBar->setValue(completed);
    double percent = (total > 0) ? (static_cast<double>(completed) / total * 100.0) : 0.0;
    statusBar()->showMessage(QString(tr("正在扫描: %1% (区域 %2 / %3)")).arg(percent, 0, 'f', 1).arg(completed).arg(total));
}

// ==================== 双击添加地址 ====================
void MainWindow::onDoubleClickScanResult(const QModelIndex& index)
{
    if (!ProcessManager::instance().memory()) return;

    uint64_t addr = m_resultModel->getAddress(index.row());
    if (addr == 0) return;

    uint64_t val = 0;
    ProcessManager::instance().memory()->read(addr, &val, sizeof(val));
    QString desc = QString(tr("地址 0x%1")).arg(addr, 0, 16);
    addressModel->addItem(addr, desc, val);
}

// ==================== 进程退出与重置 ====================
void MainWindow::resetToNoProcess()
{
    m_attachedToProcess = false;
    m_isFirstScan = true;

    m_scanService->cancel();
    m_scanService->stopAutoRefresh();
    m_scanService->clear();                   
    updateCountLabels();

    ProcessManager::instance().detach();
    addressModel->clear();



    ui->label_Process_name->setText(tr("请选择进程"));
    setWindowTitle("Cheat Engine");
    ui->comboBox_process_module_List->clear();
    ui->comboBox_process_module_List->addItem(tr("全部模块"));

    refreshUiControls();
    ui->progressBar->setVisible(false);
    statusBar()->clearMessage();
}

void MainWindow::onProcessTerminated()
{
    m_attachedToProcess = false;
    QMessageBox::information(this, tr("进程终止"), tr("目标进程已经退出"));
    resetToNoProcess();
}


void MainWindow::onAddStructureMember()
{
    // 创建一个新的水平容器行
    QWidget* rowWidget = new QWidget(this);
    QHBoxLayout* hLayout = new QHBoxLayout(rowWidget);
    hLayout->setContentsMargins(2, 2, 2, 2);

    // 控件 1: 数据类型
    QComboBox* typeCombo = new QComboBox(rowWidget);
    typeCombo->addItem(tr("4 字节"), (int)ScanDataType::Int32);
    typeCombo->addItem(tr("32位 单浮点小数"), (int)ScanDataType::Float32);
    // ... 添加其他常用类型 ...

    // 控件 2: 期望值
    QLineEdit* valEdit = new QLineEdit(rowWidget);
    valEdit->setPlaceholderText(tr("数值"));

    // 控件 3: 偏移量 (相对于上一个成员)
    QLineEdit* offsetEdit = new QLineEdit(rowWidget);
    offsetEdit->setPlaceholderText(tr("偏移"));
    offsetEdit->setValidator(new QIntValidator(0, 1024, this)); // 限制偏移量输入

    hLayout->addWidget(new QLabel(tr("类型:"), rowWidget));
    hLayout->addWidget(typeCombo);
    hLayout->addWidget(new QLabel(tr("值:"), rowWidget));
    hLayout->addWidget(valEdit);
    hLayout->addWidget(new QLabel(tr("偏移:"), rowWidget));
    hLayout->addWidget(offsetEdit);

    ui->layout_Struct_MemberList->addWidget(rowWidget);
}

void MainWindow::onRemoveStructureMember()
{
    int count = ui->layout_Struct_MemberList->count();
    if (count > 0) {
        QLayoutItem* item = ui->layout_Struct_MemberList->takeAt(count - 1);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
}

StructureParams MainWindow::getStructureParamsFromUi() const
{
    StructureParams sp;
    for (int i = 0; i < ui->layout_Struct_MemberList->count(); ++i) {
        QWidget* row = ui->layout_Struct_MemberList->itemAt(i)->widget();
        if (!row) continue;

        // 通过 findChild 寻找行内的控件
        QComboBox* typeBox = row->findChild<QComboBox*>();
        QLineEdit* valEdit = row->findChildren<QLineEdit*>().at(0);
        QLineEdit* offEdit = row->findChildren<QLineEdit*>().at(1);

        StructureMember member;
        member.type = static_cast<ScanDataType>(typeBox->currentData().toInt());
        member.criteria.value1 = valEdit->text().toULongLong(); // 简单示例，需处理进制
        member.offsetFromPrev = offEdit->text().toULong();

        sp.members.push_back(member);
    }
    return sp;
}


UiContext MainWindow::computeUiContext() const
{
    UiContext ctx;
    ctx.hasProcess = m_attachedToProcess;
    ctx.isScanning = m_isScanning.load();
    ctx.isFirstScan = m_isFirstScan;

    // 数据类型：首次根据 UI，再次根据锁定的类型
    ctx.dataType = ctx.isFirstScan ? parseDataTypeFromUI() : m_currentDataType;

    ctx.isStringMode = isStringType(ctx.dataType);
    ctx.isByteArrayMode = (ctx.dataType == ScanDataType::ByteArray);
    ctx.isStructureMode = (ctx.dataType == ScanDataType::Structure);
    ctx.isAllMode = (ctx.dataType == ScanDataType::All);

    const bool isNumeric = (ctx.dataType == ScanDataType::Int8 ||
        ctx.dataType == ScanDataType::Int16 ||
        ctx.dataType == ScanDataType::Int32 ||
        ctx.dataType == ScanDataType::Int64 ||
        ctx.dataType == ScanDataType::Float32 ||
        ctx.dataType == ScanDataType::Float64 ||
        ctx.dataType == ScanDataType::Bit ||
        ctx.isAllMode);

    // 扫描条件枚举
    const QVariant typeVar = ui->comboBox_atribute_For_Find->currentData();
    if (ctx.isFirstScan) {
        ctx.firstScanType = typeVar.isValid() ? typeVar.value<ScanType>() : ScanType::ExactValue;
    }
    else {
        ctx.nextScanType = typeVar.isValid() ? typeVar.value<NextScanType>() : NextScanType::Equal;
    }

    const bool canConfig = !ctx.isScanning && ctx.hasProcess;

    ctx.comboDataTypeEnabled = canConfig && ctx.isFirstScan;
    ctx.comboModuleEnabled = canConfig && ctx.isFirstScan;
    // 属性下拉框禁用条件：字符串、字节数组、Bit、结构体
    ctx.comboTypeEnabled = canConfig && !ctx.isStringMode && !ctx.isByteArrayMode&& !ctx.isStructureMode;

    // ---- 输入框数量 ----
    if (ctx.isFirstScan) {
        switch (ctx.firstScanType) {
        case ScanType::UnknownInitial: ctx.inputFieldsNeeded = 0; break;
        case ScanType::ExactValue:
        case ScanType::GreaterThan:
        case ScanType::LessThan:
        case ScanType::StringScan:     ctx.inputFieldsNeeded = 1; break;
        case ScanType::Between:        ctx.inputFieldsNeeded = 2; break;
        default:                       ctx.inputFieldsNeeded = 1; break;
        }
    }
    else {
        switch (ctx.nextScanType) {
        case NextScanType::Changed:
        case NextScanType::Unchanged:
        case NextScanType::Increased:
        case NextScanType::Decreased:
        case NextScanType::NotEqual:
        case NextScanType::Compare_to_First_Scan:
            ctx.inputFieldsNeeded = 0; break;
        case NextScanType::Equal:
        case NextScanType::IncreasedBy:
        case NextScanType::DecreasedBy:
        case NextScanType::EndsWith:
            ctx.inputFieldsNeeded = 1; break;
        case NextScanType::Between:
            ctx.inputFieldsNeeded = 2; break;
        default: ctx.inputFieldsNeeded = 0; break;
        }
    }

    //百分比按钮
    ctx.showPercent = false;
    if (!ctx.isFirstScan && !ctx.isStringMode && !ctx.isByteArrayMode && !ctx.isStructureMode) {
        NextScanType nt = ctx.nextScanType;
        if (nt == NextScanType::IncreasedBy || nt == NextScanType::DecreasedBy || nt == NextScanType::Between) {
            ctx.showPercent = true;
        }
    }


    // ---- 控件可见性 ----
    ctx.showRepeat = false;
    if (!ctx.isFirstScan && !ctx.isStringMode && !ctx.isByteArrayMode && !ctx.isStructureMode) {
        // 仅对不需要输入数值的再次扫描条件显示 Repeat
        NextScanType nt = ctx.nextScanType;
        if (nt == NextScanType::Changed || nt == NextScanType::Unchanged ||
            nt == NextScanType::Increased || nt == NextScanType::Decreased) {
            ctx.showRepeat = true;
        }
    }

    ctx.showHex = !ctx.isStringMode && !ctx.isByteArrayMode && !ctx.isStructureMode
        && !isFloatingPoint(ctx.dataType) && !ctx.isAllMode; // All 内部复杂，不显示 Hex
    if (ctx.isFirstScan && ctx.firstScanType == ScanType::UnknownInitial) ctx.showHex = false;
    if (!ctx.isFirstScan && ctx.inputFieldsNeeded == 0) ctx.showHex = false;

    ctx.showStringOptions = ctx.isStringMode;

    ctx.showFastScan = ctx.isFirstScan && !ctx.isStringMode && !ctx.isByteArrayMode
        && !ctx.isStructureMode && !ctx.isAllMode;
    ctx.showFastScanOptions = ctx.showFastScan && ui->checkBox_fast_scan->isChecked();

    ctx.showNot = !ctx.isStringMode && !ctx.isByteArrayMode && !ctx.isStructureMode
        && ctx.inputFieldsNeeded > 0;

    ctx.showOnlySimpleValue = isFloatingPoint(ctx.dataType)
        && !ctx.isStringMode
        && !ctx.isByteArrayMode
        && !ctx.isStructureMode
        && !ctx.isAllMode;

    // ---- 按钮启用 ----
    if (ctx.isScanning) {
        ctx.newScanButtonEnabled = false;
        ctx.nextScanButtonEnabled = false;
        ctx.newScanText = tr("扫描中...");
        ctx.nextScanText = tr("扫描中...");
        return ctx;
    }

    // 按钮只要有进程、非扫描中就可用，不再检查输入合法性
    ctx.newScanButtonEnabled = ctx.hasProcess;
    ctx.nextScanButtonEnabled = ctx.hasProcess && !ctx.isFirstScan;

    // 动态按钮文字
    if (ctx.isFirstScan) {
        ctx.newScanText = tr("首次扫描");
        ctx.nextScanText = tr("再次扫描");
    }
    else {
        ctx.newScanText = tr("新扫描");
        ctx.nextScanText = tr("再次扫描");
    }

    return ctx;
}

void MainWindow::refreshUiControls()
{
    const UiContext ctx = computeUiContext();

    // ######## 统一隐藏 ########
    ui->lineEdit_ValueInput->hide();
    ui->lineEdit_ValueInput2->hide();
    ui->label_and->hide();
    ui->checkBox_Hex_Value->hide();
    ui->checkBox_Not->hide();
    ui->checkBox_fast_scan->hide();
    ui->lineEdit_fast_scan_value->hide();
    ui->radioButton_align_size->hide();
    ui->radioButton_end_number->hide();
    ui->widget_StructurePanel->hide();
    ui->widget_Standard_Struct_InputArea->show();
    ui->checkBox_use_UTF8->hide();
    ui->checkBox_use_UTF16->hide();
    ui->checkBox_Caps_Check->hide();
    ui->checkBox_Only_Simple_Value->hide();
    ui->checkBox_percent->hide();
    ui->checkBox_repeat->hide();


    // ######## 基本启用状态 ########
    ui->comboBox_Value_Data_Size->setEnabled(ctx.comboDataTypeEnabled);
    ui->comboBox_atribute_For_Find->setEnabled(ctx.comboTypeEnabled);
    ui->comboBox_process_module_List->setEnabled(ctx.comboModuleEnabled);
    ui->checkBox_able_to_execute->setEnabled(ctx.comboModuleEnabled);
    ui->checkBox_able_to_write->setEnabled(ctx.comboModuleEnabled);
    ui->pushButton_new_find->setEnabled(ctx.newScanButtonEnabled);
    ui->pushButton_next_find->setEnabled(ctx.nextScanButtonEnabled);
    ui->pushButton_new_find->setText(ctx.newScanText);
    ui->pushButton_next_find->setText(ctx.nextScanText);

    if (!ctx.hasProcess) return;

    // ######## 结构体模式 ########
    if (ctx.isStructureMode) {
        ui->widget_Standard_Struct_InputArea->hide();
        ui->widget_StructurePanel->show();
        return;
    }

    // ######## 输入框显示 ########
    if (ctx.inputFieldsNeeded >= 1) {
        ui->lineEdit_ValueInput->show();
        ui->lineEdit_ValueInput->setEnabled(!ctx.isScanning);
    }
    if (ctx.inputFieldsNeeded >= 2) {
        ui->lineEdit_ValueInput2->show();
        ui->label_and->show();
        ui->lineEdit_ValueInput2->setEnabled(!ctx.isScanning);
    }

    // ######## 字符串模式 ########
    if (ctx.isStringMode) {
        ui->checkBox_use_UTF8->show();
        ui->checkBox_use_UTF16->show();
        ui->checkBox_Caps_Check->show();
        ui->checkBox_use_UTF8->setEnabled(!ctx.isScanning);
        ui->checkBox_use_UTF16->setEnabled(!ctx.isScanning);
        ui->checkBox_Caps_Check->setEnabled(!ctx.isScanning);
        ui->checkBox_Only_Simple_Value->setEnabled(!ctx.isScanning);
        return; // 字符串模式下其余控件均不显示
    }

    // ######## 字节数组模式 ########
    if (ctx.isByteArrayMode) {
        // 仅保留输入框，可能的 Hex? 不需要
        return;
    }

    // ######## 常规数值扫描（含 Bit, All） ########
    if (ctx.showHex) {
        ui->checkBox_Hex_Value->show();
        ui->checkBox_Hex_Value->setEnabled(!ctx.isScanning);
    }

    if (ctx.showFastScan) {
        ui->checkBox_fast_scan->show();
        ui->checkBox_fast_scan->setEnabled(!ctx.isScanning);
        if (ctx.showFastScanOptions) {
            ui->lineEdit_fast_scan_value->show();
            ui->radioButton_align_size->show();
            ui->radioButton_end_number->show();
            ui->lineEdit_fast_scan_value->setEnabled(!ctx.isScanning);
            ui->radioButton_align_size->setEnabled(!ctx.isScanning);
            ui->radioButton_end_number->setEnabled(!ctx.isScanning);
        }
    }

    if (ctx.showNot) {
        ui->checkBox_Not->show();
        ui->checkBox_Not->setEnabled(!ctx.isScanning);
    }

    if (ctx.showPercent) {
        ui->checkBox_percent->show();
        ui->checkBox_percent->setEnabled(!ctx.isScanning);  // 扫描中不可修改
    }

    if (ctx.showRepeat) {
        ui->checkBox_repeat->show();
        ui->checkBox_repeat->setEnabled(!ctx.isScanning);
    }

    if (ctx.showOnlySimpleValue) {
        ui->checkBox_Only_Simple_Value->show();
        ui->checkBox_Only_Simple_Value->setEnabled(!ctx.isScanning);
    }


    // 代码段等常规可见性
    ui->checkBox_Include_Code_Section->setVisible(true);
    ui->checkBox_Include_Code_Section->setEnabled(!ctx.isScanning);
}



bool MainWindow::validateScanInput(ScanMode mode)
{
    const ScanDataType dataType = (mode == ScanMode::First) ? parseDataTypeFromUI() : m_currentDataType;
    const QVariant typeVar = ui->comboBox_atribute_For_Find->currentData();

    // 结构体模式：至少需要一个成员
    if (dataType == ScanDataType::Structure) {
        if (ui->layout_Struct_MemberList->count() == 0) {
            QMessageBox::warning(this, tr("错误"), tr("结构体扫描至少需要添加一个成员。"));
            return false;
        }
        return true;
    }

    // 字符串模式：输入不能为空
    if (isStringType(dataType)) {
        if (ui->lineEdit_ValueInput->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("请输入要搜索的字符串。"));
            return false;
        }
        return true;
    }

    // 字节数组模式：输入不能为空，且必须是合法十六进制字节
    if (dataType == ScanDataType::ByteArray) {
        QString text = ui->lineEdit_ValueInput->text().trimmed();
        if (text.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("请输入字节数组模式（如：AA ?? BB）。"));
            return false;
        }
        // 简单验证：至少有一个有效字节或通配符
        QStringList tokens = text.split(' ', Qt::SkipEmptyParts);
        bool hasValid = false;
        for (const QString& tok : tokens) {
            if (tok.compare("??", Qt::CaseInsensitive) == 0 || tok == "?") {
                hasValid = true;
                continue;
            }
            bool ok = false;
            uint val = tok.toUInt(&ok, 16);
            if (ok && val <= 0xFF) {
                hasValid = true;
            }
            else {
                QMessageBox::warning(this, tr("错误"), QString(tr("无效的字节：“%1”")).arg(tok));
                return false;
            }
        }
        if (!hasValid) {
            QMessageBox::warning(this, tr("错误"), tr("请输入至少一个有效字节或通配符。"));
            return false;
        }
        return true;
    }

    // 数值类型（含 Bit、All）
    if (mode == ScanMode::First) {
        ScanType st = typeVar.value<ScanType>();
        if (st == ScanType::UnknownInitial) {
            // 未知初始值允许空输入
            return true;
        }
    }
    else {
        NextScanType nt = typeVar.value<NextScanType>();
        if (nt == NextScanType::Changed || nt == NextScanType::Unchanged ||
            nt == NextScanType::Increased || nt == NextScanType::Decreased ||
            nt == NextScanType::NotEqual || nt == NextScanType::Compare_to_First_Scan) {
            // 这些条件无需用户输入
            return true;
        }
    }

    // 需要至少一个输入框
    if (ui->lineEdit_ValueInput->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入要搜索的数值。"));
        return false;
    }

    // 如果有第二个输入框（Between）
    if (ui->lineEdit_ValueInput2->isVisible() && ui->lineEdit_ValueInput2->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入第二个数值（范围上限）。"));
        return false;
    }

    // 尝试解析数值，检查格式是否正确
    bool isHex = ui->checkBox_Hex_Value->isChecked() || ui->lineEdit_ValueInput->text().trimmed().startsWith("0x", Qt::CaseInsensitive);
    if (isFloatingPoint(dataType)) {
        bool ok = false;
        ui->lineEdit_ValueInput->text().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(this, tr("错误"), tr("无效的浮点数格式。"));
            return false;
        }
        if (ui->lineEdit_ValueInput2->isVisible()) {
            ok = false;
            ui->lineEdit_ValueInput2->text().toDouble(&ok);
            if (!ok) {
                QMessageBox::warning(this, tr("错误"), tr("第二个数值不是有效的浮点数。"));
                return false;
            }
        }
    }
    else {
        bool ok = false;
        QString text1 = ui->lineEdit_ValueInput->text().trimmed();
        int base = isHex ? 16 : 10;
        text1.toULongLong(&ok, base);
        if (!ok) {
            QMessageBox::warning(this, tr("错误"), tr("无效的整数格式。"));
            return false;
        }
        if (ui->lineEdit_ValueInput2->isVisible()) {
            ok = false;
            ui->lineEdit_ValueInput2->text().toULongLong(&ok, base);
            if (!ok) {
                QMessageBox::warning(this, tr("错误"), tr("第二个数值不是有效的整数。"));
                return false;
            }
        }
    }

    return true;
}

void MainWindow::onLanguageChanged(int index)
{
    if (index == 0) {
        TranslatorManager::instance().removeTranslation();
    }
    else if (index == 1) {
        if (!TranslatorManager::instance().loadTranslation("en")) {
            QMessageBox::warning(this, tr("错误"), tr("无法加载英文翻译文件！"));
            ui->comboBox_language->blockSignals(true);
            ui->comboBox_language->setCurrentIndex(0);
            ui->comboBox_language->blockSignals(false);
            return;
        }
    }

    // 刷新 UI 文本（适用于 .ui 设计器生成的界面）
    ui->retranslateUi(this);

    // 重新设置代码中动态设置的文本（如 scan type combo 等已在代码中 setText 的部分）
    refreshDynamicTexts();
}

void MainWindow::refreshDynamicTexts()
{
    int langIdx = ui->comboBox_language->currentIndex();
    initLanguageCombobox(langIdx);

    initDataTypeComboBox();

    
    updateScanTypeComboBox(); // 该函数内部会重新添加项目，使用的是 tr()，会自动翻译
    updateCountLabels();      // 更新 "Found: ... Shown: ..."
    //refreshUiControls();      // 如果其中有 setText 也要保证使用 tr()
    // 窗口标题等
    if (m_attachedToProcess)
        setWindowTitle(tr("Cheat Engine - %1").arg(ui->label_Process_name->text()));
    else
        setWindowTitle(tr("Cheat Engine"));
}