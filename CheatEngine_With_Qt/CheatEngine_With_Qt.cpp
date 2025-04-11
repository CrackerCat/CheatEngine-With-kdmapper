#include "CheatEngine_With_Qt.h"
#include <QTableWidget>
#include <QStandardItemModel>
#include <QListView>
#include <QStringListModel>

CheatEngine_With_Qt::CheatEngine_With_Qt(QWidget* parent)
	: QMainWindow(parent)
	, ui(new Ui::CheatEngine_With_QtClass())
{
	ui->setupUi(this);

	initCheckBoxVisibility(false);
	initoriginCombox_Value_Type();
	initTabWidgetButton();
	initFindWidget(ui->tabWidget->widget(0));
	initAddressTable();
	initOpenPorcessButton();
	initAdd_Or_Change_Address_Dialog();


	//ui->tabWidget->layout()


	ValueInput2_enable(false);

	connect(ui->checkBox_5_percent, &QCheckBox::toggled, [&](bool checked) {ValueInput2_enable(true);});

	connect(ui->comboBox_atribute_For_Find, &QComboBox::currentTextChanged, [&](const QString& text) {

		initCheckBoxVisibility(false);
		if (text == "介于两者之间的值")
		{
			SetSplitterVisibility(ui->splitter_13_FindText, true);
		}
		else if (text == "未知的初始值")
		{
			SetSplitterVisibility(ui->splitter_13_FindText, false);
			EnableCheckBox(ui->checkBox_Not, false);
			EnableCheckBox(ui->checkBox_6_Only_Simple_Value, false);

		}
		else if (text == "对比刚刚扫的增加的值" || text == "对比刚扫的减小的值" || text == "变了的值" || text == "没变的值")
		{
			SetSplitterVisibility(ui->splitter_13_FindText, false);
			//initCheckBoxVisibility(false);
			EnableCheckBox(ui->checkBox_Not, false);
			if (text == "没变的值") EnableCheckBox(ui->checkBox_repeat, true);
		}


		else {
			SetSplitterVisibility(ui->splitter_13_FindText, true);
			ValueInput2_enable(false);

			//initCheckBoxVisibility(false);

		}});


		//设置 comboBox_Value_Type 信号槽
		connect(ui->comboBox_Value_Data_Size, &QComboBox::currentTextChanged, [&](const QString& text) {
			//resetComboBox(ui->comboBox_atribute_For_Find);
			//initoriginCombox(ui->comboBox_atribute_For_Find);

			initCheckBoxVisibility(false);
			resetComboBox(ui->comboBox_atribute_For_Find);

			if (text == QString("字符串"))
			{
				ui->comboBox_atribute_For_Find->clear();

				QStringList filteredOptions = { "搜索字符串" };
				ui->comboBox_atribute_For_Find->addItems(filteredOptions);



				EnableCheckBox(ui->checkBox_Not, false);
				EnableCheckBox(ui->checkBox_3_Caps_Check, true);
				EnableCheckBox(ui->checkBox_4_use_UTF16, true);
				EnableCheckBox(ui->checkBox_2_Include_Code_Section, true);

			}
			else if (text == "4字节")
			{
				ui->comboBox_atribute_For_Find->addItems(next_Find_Value_For_Find);
			}
			else if (text == "单浮点" || text == "双浮点") EnableCheckBox(ui->checkBox_6_Only_Simple_Value, true);
			});
}







void CheatEngine_With_Qt::initoriginCombox(QComboBox* changedComboBox)
{
	for (int i = 0;i < changedComboBox->count();i++)
	{
		originalOptions << changedComboBox->itemText(i);
	}

}

void CheatEngine_With_Qt::initoriginCombox_Value_Type()
{
	initoriginCombox(ui->comboBox_atribute_For_Find);
}

void CheatEngine_With_Qt::initTabWidgetButton()
{

	connect(ui->pushButton_5_add_TabWidget, &QPushButton::clicked,
		[&]()
		{
			QWidget* newWidget = new QWidget();
			initFindWidget(newWidget);


			int addNumber = ui->tabWidget->count();
			ui->tabWidget->addTab(newWidget, QString("查找 ") + QString::number(addNumber + 1));
			ui->tabWidget->setCurrentIndex(addNumber);
		});

	connect(ui->pushButton_5_remove_TabWidget, &QPushButton::clicked,
		[&]()
		{

			int index = ui->tabWidget->count() - 1;
			if (index > 0)
			{
				if (ProcessWidget != ui->tabWidget->widget(index))
				{
					ui->tabWidget->setCurrentIndex(index - 1);
					ui->tabWidget->removeTab(index);
				}
				else if (index > 1)
				{
					//ui->tabWidget->setCurrentIndex(index - 1);
					ui->tabWidget->removeTab(index - 1);
				}
			}

		});
}

void CheatEngine_With_Qt::initFindWidget(QWidget* widget)
{
	//这里初始化新查找表
	QVBoxLayout* layout = new QVBoxLayout(widget);
	QTableView* newTableView = new QTableView(widget);

	QStandardItemModel* model = new QStandardItemModel(0, 4, this);
	QStringList headers;
	headers << "地址" << "当前值" << "先前值" << "第一次查找的值";
	model->setHorizontalHeaderLabels(headers);
	newTableView->setModel(model);

	// 禁止编辑单元格 
	newTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

	// 选择整行 
	newTableView->setSelectionBehavior(QAbstractItemView::SelectRows);

	// 设置列宽自适应 
	newTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

	QLabel* newLabel = new QLabel("结果数 0", widget);
	layout->addWidget(newLabel);

	newTableView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(newTableView, 1);

	//test add dummy data
	/*QStandardItem* addrItem1 = new QStandardItem("0x12343252636262325");
	QStandardItem* currentValueItem1 = new QStandardItem("50");
	QStandardItem* previousValueItem1 = new QStandardItem("45");
	QStandardItem* firstLookupItem1 = new QStandardItem("40");
	model->setItem(0, 0, addrItem1);
	model->setItem(0, 1, currentValueItem1);
	model->setItem(0, 2, previousValueItem1);
	model->setItem(0, 3, firstLookupItem1);



	QStandardItem* addrItem2 = new QStandardItem("0xzuoyuan425522");
	QStandardItem* currentValueItem2 = new QStandardItem("50");
	QStandardItem* previousValueItem2 = new QStandardItem("45");
	QStandardItem* firstLookupItem2 = new QStandardItem("40");
	model->setItem(2, 0, addrItem2);
	model->setItem(2, 1, currentValueItem2);
	model->setItem(2, 2, previousValueItem2);
	model->setItem(2, 3, firstLookupItem2);*/
}

void CheatEngine_With_Qt::initAddressTable()
{
	
	ui->tableWidget_addressList->setColumnCount(5);
	ui->tableWidget_addressList->setHorizontalHeaderLabels({ "激活","描述","地址","类型","数值"});

	ui->tableWidget_addressList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // 开启关闭列自适应 
	ui->tableWidget_addressList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	ui->tableWidget_addressList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch); // 地址列自动拉伸 
	ui->tableWidget_addressList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents); // 数据类型列自适应 
	ui->tableWidget_addressList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch); // 数值列拉伸 


	//Test insert Data
	ui->tableWidget_addressList->setRowCount(5);
	for (int row = 0;row < 5;row++)
	{
		QTableWidgetItem* checkItem = new QTableWidgetItem();
		checkItem->setCheckState(Qt::Unchecked);
		checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
		ui->tableWidget_addressList->setItem(row, 0, checkItem);

		QTableWidgetItem* textItem = new QTableWidgetItem("设备_" + QString::number(row + 1));
		textItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		ui->tableWidget_addressList->setItem(row, 1, textItem);

		int address = 0x2000 + row;
		QTableWidgetItem* addrItem = new QTableWidgetItem();
		addrItem->setText("0x" + QString::number(address, 16).toUpper()); // 转大写HEX[历史对话]()
		addrItem->setTextAlignment(Qt::AlignCenter);
		ui->tableWidget_addressList->setItem(row, 2, addrItem);

		QTableWidgetItem* DataType = new QTableWidgetItem("4字节");
		DataType->setTextAlignment(Qt::AlignCenter);
		ui->tableWidget_addressList->setItem(row, 3, DataType);

		QTableWidgetItem* valueItem = new QTableWidgetItem();
		valueItem->setText(QString::number(25.3 + row, 'f', 2)); // 保留两位小数 
		valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		ui->tableWidget_addressList->setItem(row, 4, valueItem);
	}

	//test data


}

void CheatEngine_With_Qt::initOpenPorcessButton()
{
	connect(ui->pushButton_openProcess, &QPushButton::clicked,
		[&]()
		{
			//int x = ui->tabWidget->indexOf(ProcessWidget);
			if (ui->tabWidget->indexOf(ProcessWidget) == -1)
			{
				//QWidget* ProcessWidget = new QWidget();//这个用于添加到最外层的TableWidget

				//添加进程列表的组件
				ProcessWidget = new QWidget();
				QVBoxLayout* layout = new QVBoxLayout(ProcessWidget);
				QTabWidget* ProceeTabWidget = new QTabWidget();
				QPushButton* Button_openProcess = new QPushButton("打开进程");
				QPushButton* Button_cancel = new QPushButton("取消");
				connect(Button_cancel, &QPushButton::clicked,
					[&]()
					{
						if (ui->tabWidget->indexOf(ProcessWidget) != -1)
						{
							ProcessWidget->deleteLater();
						}
					});
				QPushButton* attachProcess = new QPushButton("附加进程");

				layout->addWidget(ProceeTabWidget, 1);
				layout->addWidget(Button_openProcess, 1);
				layout->addWidget(Button_cancel, 1);
				layout->addWidget(attachProcess, 1);


				//这三个用于在进程窗口里添加新的大小
				QWidget* application = new QWidget();
				QWidget* process = new QWidget();
				QWidget* Windows = new QWidget();


				auto creatTableWidgetLayout = [&](QWidget* widget) -> QVBoxLayout*
					{
						QVBoxLayout* templayout = new QVBoxLayout(widget);
						QTableWidget* TablewidgetForProcess = new QTableWidget();
						TablewidgetForProcess->setColumnCount(3);
						TablewidgetForProcess->setHorizontalHeaderLabels({ "图标","进程PID","进程名称"});
						TablewidgetForProcess->setEditTriggers(QAbstractItemView::NoEditTriggers);
						TablewidgetForProcess->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // 图标列自适应 
						TablewidgetForProcess->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
						TablewidgetForProcess->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch); // 文本列自动拉伸 
						TablewidgetForProcess->setSelectionBehavior(QAbstractItemView::SelectRows);

						templayout->addWidget(TablewidgetForProcess);

						//testdata
						int rowCount = 5;
						TablewidgetForProcess->setRowCount(rowCount);


						for (int row = 0; row < rowCount; ++row) {
							// 第一列：图标 
							QTableWidgetItem* iconItem = new QTableWidgetItem();
							iconItem->setIcon(QIcon("C:\\Users\\Administrator\\Desktop\\cheatengine.ico"));  // 替换实际图标路径
							iconItem->setTextAlignment(Qt::AlignCenter);   // 图标居中 
							TablewidgetForProcess->setItem(row, 0, iconItem);

							// 第二列：Hex数据（假设原始数据为int类型）
							int hexValue = 0x1A3F + row; // 示例数据 
							QTableWidgetItem* hexItem = new QTableWidgetItem();
							hexItem->setText(QString::number(hexValue, 16).toUpper()); // 转大写Hex 
							hexItem->setTextAlignment(Qt::AlignCenter);
							TablewidgetForProcess->setItem(row, 1, hexItem);

							// 第三列：文本 
							QTableWidgetItem* textItem = new QTableWidgetItem("示例文本" + QString::number(row));
							textItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
							TablewidgetForProcess->setItem(row, 2, textItem);
						}
						return templayout;
					};

				creatTableWidgetLayout(application);
				creatTableWidgetLayout(process);
				creatTableWidgetLayout(Windows);

				ProceeTabWidget->addTab(application, "应用程序");
				ProceeTabWidget->addTab(process, "进程");
				ProceeTabWidget->addTab(Windows, "窗口");


				//在外层的TableWidget里创建一个进程列表的Widget

				ui->tabWidget->addTab(ProcessWidget, "进程列表 ");
				ui->tabWidget->setCurrentWidget(ProcessWidget);

			}
		}
	);

}

void CheatEngine_With_Qt::initAdd_Or_Change_Address_Dialog()
{
	connect(ui->pushButton_4_add_address, &QPushButton::clicked, [&]()
		{
			Add_Or_Change_Address_Dialog* Add_or_change_adress = new Add_Or_Change_Address_Dialog(this);
			Add_or_change_adress->setAttribute(Qt::WA_DeleteOnClose);
			Add_or_change_adress->show();
		}
	);
}



void CheatEngine_With_Qt::resetComboBox(QComboBox* changedComboBox)
{
	changedComboBox->clear();
	changedComboBox->addItems(original_Value_For_Find);
}

void CheatEngine_With_Qt::EnableCheckBox(QCheckBox* checkbox, bool enable)
{
	checkbox->setEnabled(enable);
	checkbox->setVisible(enable);

}

void CheatEngine_With_Qt::ValueInput2_enable(bool isenable)
{
	ui->ValueInput2->setVisible(isenable);
	ui->label_2->setVisible(isenable);
	ui->ValueInput2->setEnabled(isenable);
	ui->label_2->setEnabled(isenable);
}

bool CheatEngine_With_Qt::openprocessPrologue()
{
	//if(porcee)
	return false;
}


void CheatEngine_With_Qt::initCheckBoxVisibility(bool enable)
{
	SetSplitterVisibility(ui->splitter_Find_seletct_checkBoxs, false);
	EnableCheckBox(ui->checkBox_Not, true);

	QString text = ui->comboBox_Value_Data_Size->currentText();
	if (text == "单浮点" || text == "双浮点") EnableCheckBox(ui->checkBox_6_Only_Simple_Value, true);

}


void CheatEngine_With_Qt::SetLayoutVisibility(QLayout* targetLayout, bool enable)
{
	for (int i = 0; i < targetLayout->count(); ++i)
	{
		QWidget* widget = targetLayout->itemAt(i)->widget();
		if (widget)
		{
			widget->setVisible(enable);
			widget->setEnabled(enable);
		}
	}
}

void CheatEngine_With_Qt::SetSplitterVisibility(QSplitter* splitter, bool enable)
{
	QList<QWidget*> children = splitter->findChildren<QWidget*>();
	for (QWidget* child : children) {
		// 隐藏控件
		child->setVisible(enable);
		// 禁用控件（屏蔽用户交互）
		child->setEnabled(enable);
	}
}



CheatEngine_With_Qt::~CheatEngine_With_Qt()
{
}
