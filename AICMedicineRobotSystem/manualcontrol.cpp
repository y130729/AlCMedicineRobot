#include "manualcontrol.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>

ManualControlDialog::ManualControlDialog(QWidget *parent)
    : QDialog(parent), m_pisocket(nullptr)
{
    setWindowTitle("手动控制 - 小车操控");
    setFixedSize(420, 480);
    setupUI();
}

ManualControlDialog::~ManualControlDialog()
{
}

void ManualControlDialog::setSocket(QTcpSocket *socket)
{
    m_pisocket = socket;
    if (m_pisocket && m_pisocket->state() == QAbstractSocket::ConnectedState) {
        statusLabel->setText("状态: 小车已连接");
        statusLabel->setStyleSheet("color: #2ecc71; font-size: 12px;");
    }
}

void ManualControlDialog::setupUI()
{
    setStyleSheet("background-color: #252525;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(25, 20, 25, 20);
    mainLayout->setSpacing(12);

    QLabel *titleLabel = new QLabel("小车手动控制");
    titleLabel->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    statusLabel = new QLabel("状态: 等待主窗口视频服务器连接...");
    statusLabel->setStyleSheet("color: #f39c12; font-size: 12px;");
    statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(statusLabel);

    QLabel *hintLabel = new QLabel("通过主窗口【系统设置】启动服务器后\n小车连接即可在此操控");
    hintLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(hintLabel);

    // ====== 十字键 D-Pad ======
    QGroupBox *controlBox = new QGroupBox("方向控制");
    controlBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 12px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 16px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
        }
    )");

    QGridLayout *grid = new QGridLayout();
    grid->setContentsMargins(10, 15, 10, 15);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    // 等大方向按钮样式
    QString dirBtnStyle = R"(
        QPushButton {
            background-color: #2c3e50;
            border: 2px solid #3498db;
            border-radius: 8px;
            color: #ecf0f1;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:pressed { background-color: #3498db; }
    )";

    // 前进 (上)
    forwardBtn = new QPushButton("上");
    forwardBtn->setFixedSize(90, 75);
    forwardBtn->setFocusPolicy(Qt::StrongFocus);
    forwardBtn->setStyleSheet(dirBtnStyle);
    grid->addWidget(forwardBtn, 0, 1, Qt::AlignCenter);

    // 左转
    leftBtn = new QPushButton("左");
    leftBtn->setFixedSize(90, 75);
    leftBtn->setFocusPolicy(Qt::StrongFocus);
    leftBtn->setStyleSheet(dirBtnStyle);
    grid->addWidget(leftBtn, 1, 0, Qt::AlignCenter);

    // 停止 (中间)
    QPushButton *stopBtn = new QPushButton("停");
    stopBtn->setFixedSize(90, 75);
    stopBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #c0392b;
            border: 2px solid #e74c3c;
            border-radius: 8px;
            color: #ffffff;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:pressed { background-color: #e74c3c; }
    )");
    grid->addWidget(stopBtn, 1, 1, Qt::AlignCenter);

    // 右转
    rightBtn = new QPushButton("右");
    rightBtn->setFixedSize(90, 75);
    rightBtn->setFocusPolicy(Qt::StrongFocus);
    rightBtn->setStyleSheet(dirBtnStyle);
    grid->addWidget(rightBtn, 1, 2, Qt::AlignCenter);

    // 后退 (下)
    backwardBtn = new QPushButton("下");
    backwardBtn->setFixedSize(90, 75);
    backwardBtn->setFocusPolicy(Qt::StrongFocus);
    backwardBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #2c3e50;
            border: 2px solid #e74c3c;
            border-radius: 8px;
            color: #ecf0f1;
            font-size: 16px;
            font-weight: bold;
        }
        QPushButton:pressed { background-color: #e74c3c; }
    )");
    grid->addWidget(backwardBtn, 2, 1, Qt::AlignCenter);

    controlBox->setLayout(grid);
    mainLayout->addWidget(controlBox);

    // 速度控制
    QHBoxLayout *speedLayout = new QHBoxLayout();
    speedLayout->setSpacing(10);

    speedUpBtn = new QPushButton("加速");
    speedUpBtn->setMinimumHeight(50);
    speedUpBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #34495e;
            border: none;
            border-radius: 4px;
            color: #2ecc71;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #2c3e50; }
    )");

    speedDownBtn = new QPushButton("减速");
    speedDownBtn->setMinimumHeight(50);
    speedDownBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #34495e;
            border: none;
            border-radius: 4px;
            color: #e74c3c;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #2c3e50; }
    )");

    speedLayout->addWidget(speedUpBtn);
    speedLayout->addWidget(speedDownBtn);
    mainLayout->addLayout(speedLayout);

    // 信号连接
    connect(forwardBtn, &QPushButton::pressed, this, &ManualControlDialog::onForwardPressed);
    connect(forwardBtn, &QPushButton::released, this, &ManualControlDialog::onForwardReleased);
    connect(backwardBtn, &QPushButton::pressed, this, &ManualControlDialog::onBackwardPressed);
    connect(backwardBtn, &QPushButton::released, this, &ManualControlDialog::onBackwardReleased);
    connect(leftBtn, &QPushButton::pressed, this, &ManualControlDialog::onLeftPressed);
    connect(leftBtn, &QPushButton::released, this, &ManualControlDialog::onLeftReleased);
    connect(rightBtn, &QPushButton::pressed, this, &ManualControlDialog::onRightPressed);
    connect(rightBtn, &QPushButton::released, this, &ManualControlDialog::onRightReleased);
    // 停止按钮独立连接
    connect(stopBtn, &QPushButton::clicked, this, [this](){ sendCommand("letStop"); });
    connect(speedUpBtn, &QPushButton::clicked, this, &ManualControlDialog::onSpeedUpClicked);
    connect(speedDownBtn, &QPushButton::clicked, this, &ManualControlDialog::onSpeedDownClicked);
}

void ManualControlDialog::sendCommand(const QString &cmd)
{
    if (m_pisocket && m_pisocket->state() == QAbstractSocket::ConnectedState) {
        // \r\n 作为命令结束符, 使 MCU 能将快速按键合并的帧拆分成多条命令
        m_pisocket->write(cmd.toUtf8() + "\r\n");
        qDebug() << "Car control sent:" << cmd;
    } else {
        qDebug() << "Car control: not connected, cannot send:" << cmd;
    }
}

void ManualControlDialog::onForwardPressed()
{
    sendCommand("forWard");
}

void ManualControlDialog::onForwardReleased()
{
    sendCommand("letStop");
}

void ManualControlDialog::onBackwardPressed()
{
    sendCommand("backWard");
}

void ManualControlDialog::onBackwardReleased()
{
    sendCommand("letStop");
}

void ManualControlDialog::onLeftPressed()
{
    sendCommand("lefTurn");
}

void ManualControlDialog::onLeftReleased()
{
    sendCommand("letStop");
}

void ManualControlDialog::onRightPressed()
{
    sendCommand("righTurn");
}

void ManualControlDialog::onRightReleased()
{
    sendCommand("letStop");
}

void ManualControlDialog::onSpeedUpClicked()
{
    sendCommand("speedUp");
}

void ManualControlDialog::onSpeedDownClicked()
{
    sendCommand("speedDown");
}
