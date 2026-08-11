#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QObject>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void setListening();
    void appendDebug(const QString &msg);

signals:
    void listenRequested();

private slots:
    void onListenClicked();

private:
    void setupUI();

    QPushButton *listenBtn;
    QLabel *statusLabel;
    QTextEdit *debugEdit;
};

#endif // SETTINGSDIALOG_H
