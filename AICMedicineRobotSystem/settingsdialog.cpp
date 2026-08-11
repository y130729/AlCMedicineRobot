#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QGroupBox>
#include <QDateTime>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("系统设置");
    setFixedSize(480, 480);
    setupUI();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setListening()
{
    listenBtn->setText("监听中...");
    listenBtn->setEnabled(false);
    statusLabel->setText("服务器状态: 运行中");
    statusLabel->setStyleSheet("color: #2ecc71; font-size: 12px; font-weight: bold;");
}

void SettingsDialog::appendDebug(const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    debugEdit->append(ts + "  " + msg);
}

void SettingsDialog::setupUI()
{
    setStyleSheet("background-color: #252525;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel("系统设置");
    titleLabel->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // 服务器信息
    QGroupBox *infoBox = new QGroupBox("服务器信息");
    infoBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 12px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 14px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
        }
    )");
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(12, 8, 12, 8);

    QLabel *verLabel = new QLabel("STM32 F/W: v1.3.2  |  ROS: v2.1.0");
    verLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    infoLayout->addWidget(verLabel);

    QLabel *robotLabel = new QLabel("机器人ID: RB-003  |  IP: 192.168.1.105");
    robotLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    infoLayout->addWidget(robotLabel);

    QLabel *portLabel = new QLabel("监听端口: 8086 (视频 + 小车控制共用)");
    portLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    infoLayout->addWidget(portLabel);

    statusLabel = new QLabel("服务器状态: 未启动");
    statusLabel->setStyleSheet("color: #e74c3c; font-size: 12px; font-weight: bold;");
    infoLayout->addWidget(statusLabel);

    infoBox->setLayout(infoLayout);
    mainLayout->addWidget(infoBox);

    // 监听按钮
    listenBtn = new QPushButton("开始监听");
    listenBtn->setMinimumHeight(45);
    listenBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3498db;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #2980b9; }
        QPushButton:disabled {
            background-color: #27ae60;
            color: #ffffff;
        }
    )");
    mainLayout->addWidget(listenBtn);

    // 调试输出区域
    QGroupBox *debugBox = new QGroupBox("调试输出");
    debugBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 12px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 14px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
        }
    )");
    QVBoxLayout *debugLayout = new QVBoxLayout();
    debugLayout->setContentsMargins(8, 8, 8, 8);

    debugEdit = new QTextEdit();
    debugEdit->setReadOnly(true);
    debugEdit->setStyleSheet(R"(
        QTextEdit {
            background-color: #1a1a1a;
            border: 1px solid #34495e;
            border-radius: 4px;
            color: #2ecc71;
            font-size: 11px;
            font-family: "Consolas", "Courier New", monospace;
        }
    )");

    debugLayout->addWidget(debugEdit);
    debugBox->setLayout(debugLayout);
    mainLayout->addWidget(debugBox, 1);

    connect(listenBtn, &QPushButton::clicked, this, &SettingsDialog::onListenClicked);
}

void SettingsDialog::onListenClicked()
{
    setListening();
    appendDebug("listening ... ");
    qDebug() << "listening ... ";

    emit listenRequested();
}
