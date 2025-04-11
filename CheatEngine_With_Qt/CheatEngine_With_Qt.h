#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_CheatEngine_With_Qt.h"
#include "Add_Or_Change_Address_Dialog.h"
#include <QComboBox>
#include <QString>

class CheatEngine_With_Qt : public QMainWindow
{
	Q_OBJECT

public:
	CheatEngine_With_Qt(QWidget* parent = nullptr);
	~CheatEngine_With_Qt();


	static void SetLayoutVisibility(QLayout* targetLayout, bool enable);
	static void SetSplitterVisibility(QSplitter* splitter, bool enable);
private:
	Ui::CheatEngine_With_QtClass* ui;


	void initCheckBoxVisibility(bool enable);
	void initoriginCombox(QComboBox* changedComboBox);
	void initoriginCombox_Value_Type();
	void initTabWidgetButton();
	void initFindWidget(QWidget* widget);
	void initAddressTable();
	void initOpenPorcessButton();
	void initAdd_Or_Change_Address_Dialog();


	void resetComboBox(QComboBox* changedComboBox);
	static  void  EnableCheckBox(QCheckBox* chebox, bool enable);





	QStringList originalOptions;
	QStringList original_Value_For_Find = { "精确数值","值大于","值小于","介于两者之间的值","未知的初始值" };
	QStringList next_Find_Value_For_Find = { "对比刚刚扫的增加的值","对比刚扫的减小的值","数值增加了多少","数值减小了多少","变了的值","没变的值" };
	QWidget* ProcessWidget;
	//QWidget* Add_Or_Change_Address_Widget;



	void ValueInput2_enable(bool isenable);

	//process manage
	bool openprocessPrologue();
};
