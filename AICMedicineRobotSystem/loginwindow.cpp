#include "loginwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSpacerItem>

LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("AIC 药品配送机器人系统");
    setFixedSize(960, 580);
    setupUI();
}

LoginWindow::~LoginWindow()
{
}

void LoginWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setStyleSheet("background-color: #1a1a2e;");
    setCentralWidget(centralWidget);

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(0);

    QWidget *leftPanel = new QWidget(centralWidget);
    leftPanel->setStyleSheet(R"(
        background-color: #16213e;
        border-top-left-radius: 10px;
        border-bottom-left-radius: 10px;
    )");

    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    leftLayout->addStretch(2);

    QLabel *logoLabel = new QLabel(leftPanel);
    logoLabel->setFixedSize(80, 80);
    logoLabel->setStyleSheet("background-color: #2c3e50; border-radius: 12px;");
    logoLabel->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(logoLabel, 0, Qt::AlignCenter);

    leftLayout->addSpacing(25);

    QLabel *titleLabel = new QLabel("AIC 药品配送机器人系统");
    titleLabel->setStyleSheet("color: #3498db; font-size: 22px; font-weight: bold;");
    leftLayout->addWidget(titleLabel, 0, Qt::AlignCenter);

    leftLayout->addSpacing(10);

    QLabel *subtitleLabel = new QLabel("智能院内药品配送管理平台");
    subtitleLabel->setStyleSheet("color: #95a5a6; font-size: 13px;");
    leftLayout->addWidget(subtitleLabel, 0, Qt::AlignCenter);

    leftLayout->addStretch(3);

    QWidget *rightPanel = new QWidget(centralWidget);
    rightPanel->setStyleSheet(R"(
        background-color: #252525;
        border-top-right-radius: 10px;
        border-bottom-right-radius: 10px;
    )");

    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(70, 60, 70, 50);
    rightLayout->setSpacing(20);

    QLabel *welcomeLabel = new QLabel("欢迎登录");
    welcomeLabel->setStyleSheet("color: #ffffff; font-size: 26px; font-weight: bold;");
    rightLayout->addWidget(welcomeLabel);

    QLabel *hintLabel = new QLabel("请输入账号密码进入系统");
    hintLabel->setStyleSheet("color: #7f8c8d; font-size: 13px;");
    rightLayout->addWidget(hintLabel);

    rightLayout->addSpacing(10);

    QLabel *usernameLabel = new QLabel("用户名");
    usernameLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    rightLayout->addWidget(usernameLabel);

    usernameEdit = new QLineEdit();
    usernameEdit->setPlaceholderText("请输入用户名");
    usernameEdit->setMinimumHeight(40);
    usernameEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #34495e;
            border: 1px solid #3498db;
            border-radius: 4px;
            padding: 8px 12px;
            color: #ecf0f1;
            font-size: 14px;
        }
    )");
    rightLayout->addWidget(usernameEdit);

    QLabel *passwordLabel = new QLabel("密码");
    passwordLabel->setStyleSheet("color: #bdc3c7; font-size: 13px;");
    rightLayout->addWidget(passwordLabel);

    passwordEdit = new QLineEdit();
    passwordEdit->setPlaceholderText("请输入密码");
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setMinimumHeight(40);
    passwordEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #34495e;
            border: 1px solid #7f8c8d;
            border-radius: 4px;
            padding: 8px 12px;
            color: #ecf0f1;
            font-size: 14px;
        }
    )");
    rightLayout->addWidget(passwordEdit);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    rememberCheckBox = new QCheckBox("记住密码");
    rememberCheckBox->setStyleSheet(R"(
        QCheckBox {
            color: #bdc3c7;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #3498db;
            border-radius: 3px;
        }
        QCheckBox::indicator:checked {
            background-color: #3498db;
        }
    )");
    rememberCheckBox->setChecked(true);
    bottomLayout->addWidget(rememberCheckBox);
    bottomLayout->addStretch();

    QLabel *forgotLabel = new QLabel("<a href='#' style='color: #3498db;'>忘记密码?</a>");
    forgotLabel->setStyleSheet("font-size: 12px;");
    bottomLayout->addWidget(forgotLabel);

    rightLayout->addLayout(bottomLayout);

    rightLayout->addSpacing(10);

    loginButton = new QPushButton("登录");
    loginButton->setMinimumHeight(42);
    loginButton->setStyleSheet(R"(
        QPushButton {
            background-color: #3498db;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 15px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #2980b9;
        }
        QPushButton:pressed {
            background-color: #1f618d;
        }
    )");
    rightLayout->addWidget(loginButton);

    rightLayout->addSpacing(35);

    QHBoxLayout *versionLayout = new QHBoxLayout();
    versionLayout->addStretch();

    QLabel *stmLabel = new QLabel("STM32 F/W v1.3.2");
    stmLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    versionLayout->addWidget(stmLabel);

    QLabel *sep = new QLabel("  |  ");
    sep->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    versionLayout->addWidget(sep);

    QLabel *rosLabel = new QLabel("ROS v2.1.0");
    rosLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    versionLayout->addWidget(rosLabel);

    versionLayout->addStretch();
    rightLayout->addLayout(versionLayout);

    mainLayout->addWidget(leftPanel, 4);
    mainLayout->addWidget(rightPanel, 5);

    connect(loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
}

void LoginWindow::onLoginClicked()
{
    if (!usernameEdit->text().isEmpty() && !passwordEdit->text().isEmpty()) {
        emit loginSuccess();
    }
}
