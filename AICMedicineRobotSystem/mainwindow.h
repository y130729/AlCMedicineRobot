#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QObject>
#include <QDebug>
#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>

#include "stdint.h"
#include <QMap>
#include <QImage>
#include <QDateTime>

class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QListWidget;
class QTextEdit;
class QGroupBox;

class ManualControlDialog;
class SettingsDialog;

// ==================== 任务配送协议(P0 闭环, 见 P0_task_delivery_design.md) ====================
// 站点表(与车端协议一致): S1 药房01 / S2 病房001 / S3 病房002 / S4 车库01
struct StationInfo
{
    QString id;
    QString name;
};

// 单个配送任务的上位机侧记录
struct TaskInfo
{
    QString taskId;     // 如 "T-2025"
    QString fromId;     // 取药点站点ID S1~S4
    QString toId;       // 目的地站点ID S1~S4
    QString fromName;   // 取药点名称(显示用)
    QString toName;     // 目的地名称(显示用)
    QString meds;       // 药品清单(本地记录, 用于交接核对弹窗)
    QString state;      // 待下发/下发中/执行中/进站缓行/待装药/待取药/已暂停/已取消/已完成/下发失败
    bool    done;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    uint16_t crc16_ccitt(const uint8_t *buf, uint32_t len);

private slots:
    void onAddTaskClicked();
    void onCallRobotClicked();
    void onManualControlClicked();
    void onReturnChargeClicked();
    void onPauseTaskClicked();
    void onCancelTaskClicked();
    void onSystemSettingsClicked();
    void updateTime();

    void onVideoListenClicked();
    void videoNewClientConnect();
    void readVideoMessage();
    void videoDisConnect();
    void ackTimerTimeout();
    void recogTimerTimeout();
    void checkOnlineState();

private:
    void setupUI();
    void setupLeftPanel(QWidget *panel);
    void setupCenterPanel(QWidget *panel);
    void setupRightPanel(QWidget *panel);
    void setupBottomBar(QWidget *bar);

    void addLog(const QString &msg);
    void refreshTaskCount();

    // Send a plain-text command to the car over the video/control TCP socket.
    // Every command is terminated with \r\n so the MCU can split merged frames.
    void sendCarCommand(const QString &cmd);

    // ---- P0 任务配送协议 ----
    bool isProtocolTextLine(const QByteArray &line) const;
    void handleProtocolLine(const QByteArray &line);
    void handleAck(const QString &taskId, bool ok, const QString &reason = QString());
    void handleStatus(const QString &taskId, const QString &state);
    void handleEnterStation(const QString &taskId, int seq);
    void handleArrive(const QString &taskId, const QString &stationId, const QString &flag = QString());
    void handleDone(const QString &taskId);
    void handleHeartbeat(int battery, const QString &state);

    void sendCommandWithAck(const QString &cmd, const QString &taskId);
    void sendTaskStart(const QString &taskId);
    void sendTaskConfirm(const QString &taskId, const QString &action, const QString &user);
    TaskInfo *findTask(const QString &taskId);
    void updateTaskItem(TaskInfo *info);
    QString currentTaskId() const;
    QString stationName(const QString &id) const;

    // 二维码识别窗口(M3, 接入 quirc 后启用)
    void openRecognitionWindow(const QString &taskId, const QString &stationId);
    void closeRecognitionWindow();
    QString tryDecodeQR(const QImage &img);

    QLabel *runStatusLabel;
    QLabel *taskCountLabel;
    QLabel *statusLabel;
    QLabel *taskDetailLabel;
    QLabel *cameraLabel;
    QProgressBar *batteryBar;
    QGroupBox *medicineBox;

    QPushButton *addTaskBtn;
    QPushButton *callRobotBtn;
    QPushButton *manualControlBtn;
    QPushButton *returnChargeBtn;
    QPushButton *pauseTaskBtn;
    QPushButton *cancelTaskBtn;
    QPushButton *systemSettingsBtn;

    QListWidget *taskQueue;
    QTableWidget *medicineTable;
    QTextEdit *logTextEdit;

    QTimer *timer;
    int taskCounter;

    // 视频/控制 统一 TCP 服务器 (端口 8086)
    QTcpServer *m_videoServer;
    QTcpSocket *m_videoSocket;
    QByteArray m_recvBuffer;
    QPixmap m_pixmap;
    // Chunked video reassembly (AA55A55A packets)
    QByteArray m_jpegAssembly;
    int m_jpegExpected = 0;

    // 手动控制弹窗
    ManualControlDialog *m_manualDlg;

    // ---- P0 任务配送状态 ----
    QMap<QString, TaskInfo> m_tasks;   // taskId -> 任务信息(真实状态机)
    QTimer *m_ackTimer;                // ACK 超时重发定时器(500ms, 最多 3 次)
    QString m_pendingTaskId;           // 正在等待 ACK 的任务
    QString m_pendingCmd;              // 待确认的完整下行命令(重发用)
    int m_ackRetryCount;               // 已重发次数
    quint32 m_cmdSeq;                  // per-command idempotency key (seq=)
    QTimer *m_onlineTimer;             // 在线检测定时器(3s)
    qint64 m_lastHeartbeatMs;          // 最近一次心跳时间(离线判定)
    bool m_wifiOnline;                 // 小车在线标志

    // 识别窗口状态
    QTimer *m_recogTimer;              // 识别窗口(2s/轮, 最多 2 轮)
    bool m_recogActive;
    int m_recogRound;
    QString m_recogTaskId;
    QString m_recogTargetStation;

    // 系统设置弹窗
    SettingsDialog *m_settingsDlg;
};

#endif
