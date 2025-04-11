#pragma once
#include "ui_Add_Or_Change_Address_Dialog.h"
#include "CheatEngine_With_Qt.h"

class Add_Or_Change_Address_Dialog  : public QDialog
{
	Q_OBJECT

public:
	Add_Or_Change_Address_Dialog(QWidget *parent);
	~Add_Or_Change_Address_Dialog();
	void initConfirmClicked();

	void initPointerCheckBox();

	void initComboBoxStringOption();

	Ui::Dialog_Add_Or_Change_Address* Add_Or_Change_Address_ui;

private:
	unsigned int offset_count = 1;
};
