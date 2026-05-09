#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QTableView>
#include <atomic>
#include <memory>
#include "scan_data_stream_define.h"

namespace Ui {
    class CheatEngine_With_QtClass;
}


struct UiContext {
    bool hasProcess = false;
    bool isScanning = false;
    bool isFirstScan = true;
    ScanDataType dataType = ScanDataType::Int32;
    ScanType firstScanType = ScanType::ExactValue;
    NextScanType nextScanType = NextScanType::Equal;

    // 派生状态标志
    bool isStringMode = false;
    bool isByteArrayMode = false;
    bool isStructureMode = false;
    bool isAllMode = false;

    int  inputFieldsNeeded = 1;     // 0, 1, 2
    bool showHex = false;
    bool showFastScan = false;
    bool showFastScanOptions = false;
    bool showStringOptions = false; // UTF-8/16, 区分大小写
    bool showNot = false;
    bool showCodeSection = true;    // “包含代码段”始终可见（除结构体）
    bool showWritableExecutable = true;
    bool showPercent = false;
    bool showRepeat = false;
    bool showOnlySimpleValue = false;

    // 控件启用状态
    bool comboTypeEnabled = true;
    bool comboDataTypeEnabled = true;
    bool comboModuleEnabled = true;

    bool newScanButtonEnabled = false;
    bool nextScanButtonEnabled = false;
    QString newScanText;
    QString nextScanText;

};



class ScanService;
class ScanResultViewModel;
class AddressListModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT


public:
   
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:



    void setupUi();
    void initServices();

    void initLanguageCombobox(int currentLangIndex = 0);
    
    void initViews();
    void updateCountLabels();
    void setupScanResultView();
    void replaceAddressTable();
    void initTimers();

    void connectSignals();
    void initDataTypeComboBox();

    ScanRequest buildScanRequest(ScanMode mode) const;
    

	// 从 UI 获取扫描参数

    uint64_t resolveValue(const QString& text, ScanDataType dataType) const;
    ScanDataType parseDataTypeFromUI() const;
    ValueParams parseValueParams(ScanType type, ScanDataType dataType) const;
    StringParams parseStringParams() const;
    AobParams parseAobParams() const;
    ScanParams parseCurrentParams(ScanDataType dataType, int scanTypeInt, bool isNextScan) const;


    void resetToNoProcess();
    void onProcessTerminated();

	//UI 状态控制
    void refreshUiControls();
    UiContext computeUiContext() const;

	//结构体扫描相关
    void onAddStructureMember();
    void onRemoveStructureMember();
    StructureParams getStructureParamsFromUi() const;

	// 扫描参数输入验证
    bool validateScanInput(ScanMode mode);


    // 状态
    std::unique_ptr<Ui::CheatEngine_With_QtClass> ui;
    ScanService* m_scanService = nullptr;
    ScanResultViewModel* m_resultModel = nullptr;
    AddressListModel* addressModel = nullptr;

    QTableView* scanResultView = nullptr;
    QTableView* addressView = nullptr;

    QTimer* freezeTimer = nullptr;
    QTimer* healthTimer = nullptr;

    std::atomic<bool> m_isScanning = false;
    bool m_attachedToProcess = false;
    bool m_isFirstScan = true;

    ScanDataType m_currentDataType = ScanDataType::Int32; // 记录当前数据类型
    ScanType     m_currentFirstScanType = ScanType::ExactValue;

private slots:
    void onOpenProcess();
    void onFirstScan();
    
    void onNextScan();
    void updateScanTypeComboBox();
    void onDoubleClickScanResult(const QModelIndex& index);
    void onScanCompleted();
    void onProgressChanged(int completed, int total);

    void onLanguageChanged(int index);
    void refreshDynamicTexts();
};