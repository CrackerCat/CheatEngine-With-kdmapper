#pragma once

#include <QDialog>
#include <QPushButton>
#include <QTableWidget>
#include "process\process_manager.h"
#include "type_define\process_info.h"

class ProcessDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProcessDialog(QWidget* parent = nullptr);

    ProcessInfo selectedProcess() const;

private:
    QTabWidget* tabs;

    QTableWidget* appTable;   // 有窗口
    QTableWidget* allTable;   // 全部进程
    QPushButton* attachBtn;
    QPushButton* cancelBtn;


    std::vector<ProcessInfo> processes;
    void setupTable(QTableWidget* table);
    void populateTables();
};