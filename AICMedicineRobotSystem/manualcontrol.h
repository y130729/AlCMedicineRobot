#ifndef MANUALCONTROL_H
#define MANUALCONTROL_H

#include <QDialog>
#include <QObject>
#include <QDebug>
#include <QTcpSocket>
#include <QPushButton>
#include <QLabel>

class ManualControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManualControlDialog(QWidget *parent = nullptr);
    ~ManualControlDialog();

    void setSocket(QTcpSocket *socket);

private slots:
    void onForwardPressed();
    void onForwardReleased();
    void onBackwardPressed();
    void onBackwardReleased();
    void onLeftPressed();
    void onLeftReleased();
    void onRightPressed();
    void onRightReleased();
    void onSpeedUpClicked();
    void onSpeedDownClicked();

private:
    void setupUI();
    void sendCommand(const QString &cmd);

    QTcpSocket *m_pisocket;

    QPushButton *forwardBtn;
    QPushButton *backwardBtn;
    QPushButton *leftBtn;
    QPushButton *rightBtn;
    QPushButton *speedUpBtn;
    QPushButton *speedDownBtn;
    QLabel *statusLabel;
};

#endif // MANUALCONTROL_H
