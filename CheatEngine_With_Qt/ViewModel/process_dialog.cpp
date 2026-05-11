#include "ViewModel\process_dialog.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QIcon>
#include <QFileInfo>
#include <Windows.h>
#include <QHeaderView> 
#include <QGuiApplication> 

QIcon getFileIcon(const std::string& path)
{
    SHFILEINFOA shinfo = { 0 };

    if (SHGetFileInfoA(
        path.c_str(),
        0,
        &shinfo,
        sizeof(shinfo),
        SHGFI_ICON | SHGFI_SMALLICON))
    {
        QIcon icon = QIcon(QPixmap::fromImage(
            QImage::fromHICON(shinfo.hIcon)));

        DestroyIcon(shinfo.hIcon); // ⚠️ 必须释放

        return icon;
    }

    return QIcon();
}

ProcessDialog::ProcessDialog(QWidget* parent)
    : QDialog(parent)
{
    tabs = new QTabWidget(this);
    appTable = new QTableWidget(this);
    allTable = new QTableWidget(this);
    attachBtn = new QPushButton("Attach", this);
    cancelBtn = new QPushButton("Cancel", this);
    // 默认禁用（没选中时）
    attachBtn->setEnabled(false);

    setupTable(appTable);
    setupTable(allTable);

    tabs->addTab(appTable, "Applications");
    tabs->addTab(allTable, "All Processes");

    populateTables();


    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(attachBtn);
    btnLayout->addWidget(cancelBtn);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addLayout(btnLayout);
    setLayout(layout);

    // ─── 自适应尺寸 ───
// 先让布局计算一次
    layout->activate();
    // 计算表格理想宽度（取两个表格中较宽者）
    int tableWidth = std::max(appTable->sizeHint().width(),
        allTable->sizeHint().width()) + 20;
    // 考虑标签栏和按钮高度
    int totalHeight = tabs->sizeHint().height() + btnLayout->sizeHint().height() + 40;

    int finalWidth = std::max(tableWidth, 400);
    int finalHeight = min(totalHeight, 500);
    resize(finalWidth, finalHeight);


    connect(attachBtn, &QPushButton::clicked, this, [this]()
        {
            if (selectedProcess().pid != 0)
                accept();
        });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto onDoubleClick = [this]()
        {
            if (selectedProcess().pid != 0)
                accept();
        };

    connect(appTable, &QTableWidget::doubleClicked, this, onDoubleClick);
    connect(allTable, &QTableWidget::doubleClicked, this, onDoubleClick);

    auto onSelectionChanged = [this]()
        {
            attachBtn->setEnabled(selectedProcess().pid != 0);
        };

    connect(appTable, &QTableWidget::itemSelectionChanged,
        this, onSelectionChanged);

    connect(allTable, &QTableWidget::itemSelectionChanged,
        this, onSelectionChanged);



}

ProcessInfo ProcessDialog::selectedProcess() const
{
    QTableWidget* currentTable =
        (tabs->currentIndex() == 0) ? appTable : allTable;

    int row = currentTable->currentRow();

    if (row < 0)
        return {};

    uint32_t pid = currentTable->item(row, 1)->text().toUInt();

    for (const auto& p : processes)
    {
        if (p.pid == pid)
            return p;
    }

    return {};
}

void ProcessDialog::setupTable(QTableWidget* table)
{
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({ "Process", "PID"});
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // 图标+名称
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents); // PID
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    table->resizeColumnsToContents();
    table->resizeRowsToContents();

    table->horizontalHeader()->setStretchLastSection(true);

    // 禁止行列手动调整大小，防止意外压缩
    table->horizontalHeader()->setSectionsClickable(true);
    // 允许水平滚动条（当内容超出表格宽度时）
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void ProcessDialog::populateTables()
{
    int appRow = 0;
    int allRow = 0;

    processes = ProcessManager::instance().getProcesses();

    appTable->setRowCount(0);
    allTable->setRowCount(processes.size());

    for (const auto& p : processes)
    {
        // ===== All Processes =====
        {
            QTableWidgetItem* item = new QTableWidgetItem(
                QString::fromStdString(p.name));

            if (!p.exePath.empty())
                item->setIcon(getFileIcon(p.exePath));

            allTable->setItem(allRow, 0, item);
            allTable->setItem(allRow, 1,
                new QTableWidgetItem(QString::number(p.pid)));
            allRow++;
        }

        // ===== Applications（有窗口）=====
        if (p.hasWindow)
        {
            appTable->insertRow(appRow);

            QTableWidgetItem* item = new QTableWidgetItem(
                QString::fromStdString(p.name));

            if (!p.exePath.empty())
                item->setIcon(getFileIcon(p.exePath));

            appTable->setItem(appRow, 0, item);
            appTable->setItem(appRow, 1,
                new QTableWidgetItem(QString::number(p.pid)));
            appRow++;
        }
    }

}