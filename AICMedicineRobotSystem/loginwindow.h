#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QMainWindow>

class QLineEdit;
class QCheckBox;
class QPushButton;

class LoginWindow : public QMainWindow
{
    Q_OBJECT

public:
    LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();

signals:
    void loginSuccess();

private slots:
    void onLoginClicked();

private:
    void setupUI();

    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QCheckBox *rememberCheckBox;
    QPushButton *loginButton;
};

#endif
