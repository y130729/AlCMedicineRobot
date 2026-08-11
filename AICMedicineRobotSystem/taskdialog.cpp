#include "taskdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

TaskDialog::TaskDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("录入新任务");
    setFixedSize(440, 400);
    setupUI();
}

TaskDialog::~TaskDialog()
{
}

void TaskDialog::setupUI()
{
    setStyleSheet("background-color: #252525;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(35, 25, 35, 25);
    mainLayout->setSpacing(16);

    QLabel *titleLabel = new QLabel("录入新任务");
    titleLabel->setStyleSheet("color: #ffffff; font-size: 20px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    QLabel *fromLabel = new QLabel("取药点");
    fromLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    mainLayout->addWidget(fromLabel);

    fromCombo = new QComboBox();
    fromCombo->addItems({"药房01 (S1)"});
    fromCombo->setMinimumHeight(36);
    fromCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #34495e;
            border: 1px solid #7f8c8d;
            border-radius: 4px;
            padding: 6px 10px;
            color: #ecf0f1;
            font-size: 13px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background-color: #34495e;
            color: #ecf0f1;
            selection-background-color: #3498db;
        }
    )");
    mainLayout->addWidget(fromCombo);

    QLabel *toLabel = new QLabel("目的地");
    toLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    mainLayout->addWidget(toLabel);

    toCombo = new QComboBox();
    toCombo->addItems({"病房001 (S2)", "病房002 (S3)", "车库01 (S4)"});
    toCombo->setMinimumHeight(36);
    toCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #34495e;
            border: 1px solid #7f8c8d;
            border-radius: 4px;
            padding: 6px 10px;
            color: #ecf0f1;
            font-size: 13px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background-color: #34495e;
            color: #ecf0f1;
            selection-background-color: #3498db;
        }
    )");
    mainLayout->addWidget(toCombo);

    QLabel *priorityLabel = new QLabel("优先级");
    priorityLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    mainLayout->addWidget(priorityLabel);

    priorityCombo = new QComboBox();
    priorityCombo->addItems({"普通", "紧急", "危重"});
    priorityCombo->setMinimumHeight(36);
    priorityCombo->setStyleSheet(R"(
        QComboBox {
            background-color: #34495e;
            border: 1px solid #7f8c8d;
            border-radius: 4px;
            padding: 6px 10px;
            color: #ecf0f1;
            font-size: 13px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background-color: #34495e;
            color: #ecf0f1;
            selection-background-color: #3498db;
        }
    )");
    mainLayout->addWidget(priorityCombo);

    QLabel *medicineLabel = new QLabel("药品清单");
    medicineLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    mainLayout->addWidget(medicineLabel);

    medicineEdit = new QLineEdit();
    medicineEdit->setPlaceholderText("输入药品名称，多个用逗号分隔");
    medicineEdit->setMinimumHeight(36);
    medicineEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #34495e;
            border: 1px solid #7f8c8d;
            border-radius: 4px;
            padding: 6px 10px;
            color: #ecf0f1;
            font-size: 13px;
        }
    )");
    mainLayout->addWidget(medicineEdit);

    mainLayout->addSpacing(8);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);

    cancelButton = new QPushButton("取消");
    cancelButton->setMinimumHeight(38);
    cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #34495e;
            border: none;
            border-radius: 4px;
            color: #bdc3c7;
            font-size: 13px;
            min-width: 80px;
        }
        QPushButton:hover { background-color: #2c3e50; }
    )");
    buttonLayout->addWidget(cancelButton);

    buttonLayout->addStretch();

    confirmButton = new QPushButton("确认录入");
    confirmButton->setMinimumHeight(38);
    confirmButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3498db;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 13px;
            font-weight: bold;
            min-width: 100px;
        }
        QPushButton:hover { background-color: #2980b9; }
    )");
    buttonLayout->addWidget(confirmButton);

    mainLayout->addLayout(buttonLayout);

    connect(confirmButton, &QPushButton::clicked, this, &TaskDialog::onConfirmClicked);
    connect(cancelButton, &QPushButton::clicked, this, &TaskDialog::onCancelClicked);
}

void TaskDialog::onConfirmClicked()
{
    accept();
}

void TaskDialog::onCancelClicked()
{
    reject();
}

QString TaskDialog::fromLocation() const
{
    QString t = fromCombo->currentText();
    int i = t.indexOf(" (");
    return (i >= 0) ? t.left(i) : t;
}

QString TaskDialog::toLocation() const
{
    QString t = toCombo->currentText();
    int i = t.indexOf(" (");
    return (i >= 0) ? t.left(i) : t;
}

QString TaskDialog::fromStationId() const
{
    QString t = fromCombo->currentText();
    int i = t.indexOf('(');
    return (i >= 0) ? t.mid(i + 1, 2) : "S1";
}

QString TaskDialog::toStationId() const
{
    QString t = toCombo->currentText();
    int i = t.indexOf('(');
    return (i >= 0) ? t.mid(i + 1, 2) : "S2";
}

QString TaskDialog::priority() const { return priorityCombo->currentText(); }
QString TaskDialog::medicines() const { return medicineEdit->text(); }
