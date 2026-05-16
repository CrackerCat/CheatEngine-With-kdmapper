#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QTableView>
#include <QComboBox>
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QGraphicsScene>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <atomic>
#include <memory>
#include "scan\scan_data_stream_define.h"

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

    int  inputFieldsNeeded = 1;     // 0, 1, 2 扫描值输入
    bool showHex = false;
    bool showFastScan = false;
    bool showFastScanOptions = false;
    bool showStringOptions = false; // UTF-8/16, 区分大小写
    bool showNot = false;
    bool showCodeSection = true;    // "包含代码段"始终可见（除结构体）
    bool showWritableExecutable = true;
    bool showPercent = false;
    bool showRepeat = false;
    bool showContainApproximateValue = false;

    // 控件启用状态
    bool comboTypeEnabled = true;
    bool comboDataTypeEnabled = true;
    bool comboModuleEnabled = true;

    bool newScanButtonEnabled = false;
    bool nextScanButtonEnabled = false;
    bool preFindButtonEnabled = false;
    QString newScanText;
    QString nextScanText;

};



class ScanService;
class ScanResultViewModel;
class AddressListModel;
class TypeDelegate;

class MainWindow : public QMainWindow
{
    Q_OBJECT


public:
   
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:



    void setupUi();
    void initServices();
    void initMenuBar();

    void initViews();
    void updateCountLabels();
    void setupScanResultView();
    void replaceAddressTable();
    void initTimers();

    void connectSignals();
    void initDataTypeComboBox();

    ScanRequest buildScanRequest(ScanMode mode) const;

    // ★ 根据数据类型自动填充快速扫描的对齐值
    void autoFillFastScanAlignment();

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

    QTableView* addressView = nullptr;

    QTimer* freezeTimer = nullptr;
    QTimer* addressListRefreshTimer = nullptr;
    QTimer* healthTimer = nullptr;

    QActionGroup* m_langActionGroup = nullptr;
    int m_currentLanguageIndex = 0;

    std::atomic<bool> m_isScanning = false;
    bool m_updatingAobInput = false;   // 防止字节数组自动空格递归
    bool m_attachedToProcess = false;
    bool m_isFirstScan = true;

    ScanDataType m_currentDataType = ScanDataType::Int32; // 记录当前数据类型
    ScanType     m_currentFirstScanType = ScanType::ExactValue;

private slots:
    void onOpenProcess();
    void onFirstScan();
    void onPreFind();
    void onNextScan();
    void updateScanTypeComboBox();
    void onDoubleClickScanResult(const QModelIndex& index);
    void onDoubleClickAddressList(const QModelIndex& index);
    void onCopySelectedToAddressList();
    void onScanCompleted();
    void onProgressChanged(int completed, int total);

    void onLanguageChanged(int index);
    void refreshDynamicTexts();

    /// 批量将选中的扫描行添加到地址列表（内部方法）
    void addSelectedScanRowsToAddressList();

    /// 打开软件捐赠/关于对话框
    void onAboutDonate();

    /// 打开"手动添加地址"对话框
    void onAddAddressManually();

    /// 删除地址列表中选中的条目（右键菜单/Delete键）
    void onDeleteSelectedAddresses();
};
