#include "Add_Or_Change_Address_Dialog.h"


Add_Or_Change_Address_Dialog::Add_Or_Change_Address_Dialog(QWidget* parent)
	: QDialog(parent),
	Add_Or_Change_Address_ui(new Ui::Dialog_Add_Or_Change_Address())
{
	Add_Or_Change_Address_ui->setupUi(this);

	initConfirmClicked();
	initPointerCheckBox();
	initComboBoxStringOption();


	//CheatEngine_With_Qt::SetLayoutVisibility(Add_Or_Change_Address_ui->verticalLayout_add_pointer, false);
}

Add_Or_Change_Address_Dialog::~Add_Or_Change_Address_Dialog()
{

}

void Add_Or_Change_Address_Dialog::initConfirmClicked()
{
	connect(Add_Or_Change_Address_ui->buttonBox, &QDialogButtonBox::accepted, [&]()
		{

		}
	);
}

void Add_Or_Change_Address_Dialog::initPointerCheckBox()
{


	//init add addresess widget
	Add_Or_Change_Address_ui->widget_add_address->setEnabled(false);
	Add_Or_Change_Address_ui->widget_add_address->setVisible(false);
	connect(Add_Or_Change_Address_ui->checkBox_3_pointer, &QCheckBox::checkStateChanged, [&](Qt::CheckState checkstate)
		{
			if (checkstate == Qt::Checked)
			{
				Add_Or_Change_Address_ui->widget_add_address->setEnabled(checkstate);
				Add_Or_Change_Address_ui->widget_add_address->setVisible(checkstate);
			}
			else
			{
				Add_Or_Change_Address_ui->widget_add_address->setEnabled(false);
				Add_Or_Change_Address_ui->widget_add_address->setVisible(false);
			}

		}
	);

	connect(Add_Or_Change_Address_ui->pushButton_add_offset, &QPushButton::clicked, [=](bool clicked)
		{
			auto horizontalLayout = new QHBoxLayout();
			QSizePolicy sizePolicy2(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed);
			QSizePolicy sizePolicy1(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Preferred);



			horizontalLayout->setObjectName(QString("horizontalLayout_offset%1").arg(offset_count));
			//auto name = horizontalLayout->objectName();

			auto pushButton_add_offset_left = new QPushButton(Add_Or_Change_Address_ui->widget_add_address);
			pushButton_add_offset_left->setObjectName(QString("pushButton_add_offset_left%1").arg(offset_count));
			sizePolicy2.setHeightForWidth(pushButton_add_offset_left->sizePolicy().hasHeightForWidth());
			pushButton_add_offset_left->setSizePolicy(sizePolicy2);
			pushButton_add_offset_left->setMaximumSize(QSize(30, 16777215));
			pushButton_add_offset_left->setText("<");


			horizontalLayout->addWidget(pushButton_add_offset_left);

			auto lineEdit_add_offset = new QLineEdit(Add_Or_Change_Address_ui->widget_add_address);
			lineEdit_add_offset->setObjectName(QString("lineEdit_add_offset%1").arg(offset_count));
			sizePolicy2.setHeightForWidth(lineEdit_add_offset->sizePolicy().hasHeightForWidth());
			lineEdit_add_offset->setSizePolicy(sizePolicy2);
			lineEdit_add_offset->setMaximumSize(QSize(80, 16777215));

			horizontalLayout->addWidget(lineEdit_add_offset);

			auto pushButton_add_offset_right = new QPushButton(Add_Or_Change_Address_ui->widget_add_address);
			pushButton_add_offset_right->setObjectName(QString("pushButton_add_offset_right%1").arg(offset_count));
			sizePolicy2.setHeightForWidth(pushButton_add_offset_right->sizePolicy().hasHeightForWidth());
			pushButton_add_offset_right->setSizePolicy(sizePolicy2);
			pushButton_add_offset_right->setMaximumSize(QSize(30, 16777215));
			pushButton_add_offset_right->setText(">");

			horizontalLayout->addWidget(pushButton_add_offset_right);

			auto label_compute_offset = new QLabel(Add_Or_Change_Address_ui->widget_add_address);
			label_compute_offset->setObjectName(QString("label_compute_offset%1").arg(offset_count));
			sizePolicy1.setHeightForWidth(label_compute_offset->sizePolicy().hasHeightForWidth());
			label_compute_offset->setSizePolicy(sizePolicy1);
			label_compute_offset->setText("???+?=??");

			horizontalLayout->addWidget(label_compute_offset);

			//auto horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

			//horizontalLayout->addItem(horizontalSpacer_2);

			offset_count++;
			Add_Or_Change_Address_ui->verticalLayout_2->addLayout(horizontalLayout);
		}
	);

	connect(Add_Or_Change_Address_ui->pushButton_2_delete_offset, &QPushButton::clicked, [=](bool clicked)
		{
			if (offset_count > 1)
			{
				auto offset_Widget = Add_Or_Change_Address_ui->widget_add_address;
				auto delete_Layout = offset_Widget->findChild<QHBoxLayout*>(QString("horizontalLayout_offset%1").arg(offset_count - 1));

				for (int i = 0; i < delete_Layout->count(); i++)
				{
					delete_Layout->itemAt(i)->widget()->deleteLater();
				}
				delete_Layout->deleteLater();
				offset_count--;
			}
		}
	);
}

void Add_Or_Change_Address_Dialog::initComboBoxStringOption()
{
	Add_Or_Change_Address_ui->widget_string_option->setEnabled(false);
	Add_Or_Change_Address_ui->widget_string_option->setVisible(false);
	connect(Add_Or_Change_Address_ui->checkBox_4_Unicode, &QCheckBox::checkStateChanged, [=](Qt::CheckState checkstate)
		{
			if (checkstate == Qt::Checked)
			{
				Add_Or_Change_Address_ui->checkBox_5_CodePage->setChecked(false);
			}
		}
	);
	connect(Add_Or_Change_Address_ui->checkBox_5_CodePage, &QCheckBox::checkStateChanged, [=](Qt::CheckState checkstate)
		{
			if (checkstate == Qt::Checked)
			{
				Add_Or_Change_Address_ui->checkBox_4_Unicode->setChecked(false);
			}
		}
	);
	connect(Add_Or_Change_Address_ui->comboBox, &QComboBox::currentTextChanged, [&](const QString& text)
		{
			if (text == "字符串")
			{
				Add_Or_Change_Address_ui->widget_string_option->setEnabled(true);
				Add_Or_Change_Address_ui->widget_string_option->setVisible(true);

				Add_Or_Change_Address_ui->widget->setEnabled(false);

			}
			else
			{
				Add_Or_Change_Address_ui->widget_string_option->setEnabled(false);
				Add_Or_Change_Address_ui->widget_string_option->setVisible(false);

				Add_Or_Change_Address_ui->widget->setEnabled(true);
			}

		}
	);
}