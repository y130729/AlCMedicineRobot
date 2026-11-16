#include "mainwindow.h"
#include "taskdialog.h"
#include "manualcontrol.h"
#include "settingsdialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QGroupBox>
#include <QHeaderView>
#include <QSpacerItem>
#include <QDateTime>
#include <QPixmap>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QHostAddress>
#include <cstring>

// ==================== P0 二维码识别(quirc) ====================
#if defined(HAVE_QUIRC)
extern "C" {
#include "quirc.h"
}
#endif

// ==================== P0 任务配送协议 ====================
// 站点表(与车端协议一致, 见 P0_task_delivery_design.md 3.3)
static const StationInfo kStations[] = {
    {"S1", "药房01"},
    {"S2", "病房001"},
    {"S3", "病房002"},
    {"S4", "车库01"}
};

QString MainWindow::stationName(const QString &id) const
{
    for (const StationInfo &s : kStations) {
        if (s.id == id)
            return s.name;
    }
    return id;
}

bool MainWindow::isProtocolTextLine(const QByteArray &line) const
{
    static const char *prefixes[] = {
        "ACK,", "ENTER_STATION,", "STATUS,", "TASK_ARRIVE,", "TASK_DONE,",
        "HEARTBEAT,", "LINK,", "STATS,", "BOOT_TIMEOUT,"
    };
    for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        if (line.startsWith(prefixes[i]))
            return true;
    }
    return false;
}

void MainWindow::handleProtocolLine(const QByteArray &line)
{
    QString s = QString::fromUtf8(line);
    QStringList p = s.split(',');
    if (s.startsWith("ACK,")) {
        if (p.size() >= 3)
            handleAck(p.at(1), p.at(2) == "1", p.size() >= 4 ? p.at(3) : QString());
    } else if (s.startsWith("ENTER_STATION,")) {
        if (p.size() >= 3) handleEnterStation(p.at(1), p.at(2).toInt());
    } else if (s.startsWith("STATUS,")) {
        if (p.size() >= 3) handleStatus(p.at(1), p.at(2));
    } else if (s.startsWith("TASK_ARRIVE,")) {
        if (p.size() >= 3)
            handleArrive(p.at(1), p.at(2), p.size() >= 4 ? p.at(3) : QString());
    } else if (s.startsWith("TASK_DONE,")) {
        if (p.size() >= 2) handleDone(p.at(1));
    } else if (s.startsWith("HEARTBEAT,")) {
        if (p.size() >= 3) handleHeartbeat(p.at(1).toInt(), p.at(2));
    } else if (s.startsWith("LINK,")) {
        addLog(QString("链路状态: %1").arg(p.size() >= 2 ? p.at(1) : QString()));
    } else if (s.startsWith("STATS,")) {
        addLog(QString("控制环统计: %1").arg(s.mid(6)));
    }
}

TaskInfo *MainWindow::findTask(const QString &taskId)
{
    QMap<QString, TaskInfo>::iterator it = m_tasks.find(taskId);
    return (it != m_tasks.end()) ? &it.value() : nullptr;
}

void MainWindow::updateTaskItem(TaskInfo *info)
{
    for (int i = 0; i < taskQueue->count(); ++i) {
        QListWidgetItem *it = taskQueue->item(i);
        if (it->data(Qt::UserRole).toString() == info->taskId) {
            it->setText(QString("%1  %2").arg(info->taskId).arg(info->state));
            if (info->state == "已完成" || info->state == "执行中")
                it->setForeground(QColor("#27ae60"));
            else if (info->state == "已取消" || info->state.contains("失败"))
                it->setForeground(QColor("#e74c3c"));
            else if (info->state == "已暂停" || info->state == "待装药" || info->state == "待取药")
                it->setForeground(QColor("#f39c12"));
            else
                it->setForeground(QColor("#7f8c8d"));
            break;
        }
    }
}

QString MainWindow::currentTaskId() const
{
    QListWidgetItem *cur = taskQueue->currentItem();
    if (cur) {
        QString id = cur->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            return id;
    }
    for (int i = 0; i < taskQueue->count(); ++i) {
        QString id = taskQueue->item(i)->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

// 通用下行命令发送: 记录待确认命令, 500ms 未回 ACK 重发(最多 3 次)
void MainWindow::sendCommandWithAck(const QString &cmd, const QString &taskId)
{
    // Append a monotonic seq so the MCU can recognise retransmissions of the
    // same command and replay the previous ACK instead of rejecting it after
    // the state machine has already advanced (idempotent TASK_*/STOP).
    m_pendingCmd = cmd + QString(",seq=%1").arg(++m_cmdSeq);
    sendCarCommand(m_pendingCmd);
    m_pendingTaskId = taskId;
    m_ackRetryCount = 0;
    m_ackTimer->start(500);
}

// 下发任务: TASK_START + 500ms ACK 重发(最多 3 次)
void MainWindow::sendTaskStart(const QString &taskId)
{
    TaskInfo *t = findTask(taskId);
    if (!t)
        return;
    t->state = "下发中";
    updateTaskItem(t);
    sendCommandWithAck(QString("TASK_START,%1,%2,%3").arg(taskId).arg(t->fromId).arg(t->toId), taskId);
}

void MainWindow::ackTimerTimeout()
{
    if (m_pendingTaskId.isEmpty())
        return;
    TaskInfo *t = findTask(m_pendingTaskId);
    if (!t) {
        m_pendingTaskId.clear();
        m_pendingCmd.clear();
        return;
    }
    if (m_ackRetryCount < 3) {
        m_ackRetryCount++;
        addLog(QString("任务 %1 未收到 ACK, 第 %2 次重发").arg(m_pendingTaskId).arg(m_ackRetryCount));
        if (!m_pendingCmd.isEmpty())
            sendCarCommand(m_pendingCmd);
        m_ackTimer->start(500);
    } else {
        if (m_pendingCmd.startsWith("TASK_START")) {
            t->state = "下发失败";
            updateTaskItem(t);
        }
        addLog(QString("任务 %1 命令超时(3 次重发无 ACK): %2")
               .arg(m_pendingTaskId).arg(m_pendingCmd.left(24)));
        m_pendingTaskId.clear();
        m_pendingCmd.clear();
        m_ackRetryCount = 0;
    }
}

void MainWindow::sendTaskConfirm(const QString &taskId, const QString &action, const QString &user)
{
    sendCommandWithAck(QString("TASK_CONFIRM,%1,%2,%3").arg(taskId).arg(action).arg(user), taskId);
}

void MainWindow::handleAck(const QString &taskId, bool ok, const QString &reason)
{
    bool resolvedPending = (taskId == m_pendingTaskId);
    bool wasStart = m_pendingCmd.startsWith("TASK_START");
    if (resolvedPending) {
        m_ackTimer->stop();
        m_pendingTaskId.clear();
        m_pendingCmd.clear();
        m_ackRetryCount = 0;
    }
    TaskInfo *t = findTask(taskId);
    if (!t) {
        addLog(QString("收到 ACK 但任务不存在: %1(ok=%2)").arg(taskId).arg(ok));
        return;
    }
    if (ok) {
        if (resolvedPending && wasStart) {
            t->state = "执行中";
            updateTaskItem(t);
            addLog(QString("任务 %1 已下发确认, 开始配送(%2→%3)").arg(taskId).arg(t->fromName).arg(t->toName));
            runStatusLabel->setText("执行中");
            runStatusLabel->setStyleSheet("color: #27ae60; font-size: 13px; font-weight: bold;");
        } else {
            addLog(QString("命令已确认(ACK=1): %1").arg(taskId));
        }
    } else {
        if (resolvedPending && wasStart) {
            t->state = "下发失败";
            updateTaskItem(t);
        }
        addLog(QString("任务 %1 命令被小车拒绝(ACK=0, %2)")
               .arg(taskId).arg(reason.isEmpty() ? "STATE" : reason));
    }
}

void MainWindow::handleStatus(const QString &taskId, const QString &state)
{
    QString display = state;
    if (state == "NAV")            display = "执行中";
    else if (state == "APPROACH")  display = "进站缓行";
    else if (state == "STOPPED")   display = "已停稳";
    else if (state == "WAIT_LOAD") display = "待装药";
    else if (state == "WAIT_UNLOAD") display = "待取药";
    else if (state == "PAUSED")    display = "已暂停";
    else if (state == "TIMEOUT")   display = "站点未确认";
    else if (state == "CANCELLED") display = "已取消";

    TaskInfo *t = findTask(taskId);
    if (t) {
        t->state = display;
        updateTaskItem(t);
    }
    addLog(QString("任务 %1 状态: %2").arg(taskId).arg(display));
    runStatusLabel->setText(display);
    runStatusLabel->setStyleSheet("color: #f39c12; font-size: 13px; font-weight: bold;");
}

void MainWindow::handleEnterStation(const QString &taskId, int seq)
{
    TaskInfo *t = findTask(taskId);
    if (!t) {
        addLog(QString("收到进站信号, 但任务不存在: %1").arg(taskId));
        return;
    }
    QString target = (seq == 0) ? t->fromId : t->toId;
    addLog(QString("任务 %1 进入识别区(seq=%2), 目标站点 %3").arg(taskId).arg(seq).arg(stationName(target)));
    openRecognitionWindow(taskId, target);
}

void MainWindow::openRecognitionWindow(const QString &taskId, const QString &stationId)
{
    m_recogTaskId = taskId;
    m_recogTargetStation = stationId;
    m_recogActive = true;
    m_recogRound = 1;
    m_recogTimer->start(2000);
    addLog(QString("二维码识别窗口开启(2s): 目标 %1").arg(stationName(stationId)));
}

void MainWindow::recogTimerTimeout()
{
    if (!m_recogActive)
        return;
    if (m_recogRound < 2) {
        m_recogRound++;
        m_recogTimer->start(2000);
        addLog("识别窗口超时, 重开一轮...");
    } else {
        m_recogActive = false;
        addLog("识别窗口两轮未命中, 站点未确认(车端已兜底停车)");
    }
}

void MainWindow::closeRecognitionWindow()
{
    m_recogActive = false;
    m_recogTimer->stop();
    m_recogTaskId.clear();
    m_recogTargetStation.clear();
}

QString MainWindow::tryDecodeQR(const QImage &img)
{
#if defined(HAVE_QUIRC)
    if (img.isNull())
        return QString();

    // 统一转 8bit 灰度(quirc 只接受单通道亮度图)
    QImage gray = img;
    if (gray.format() != QImage::Format_Grayscale8)
        gray = gray.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull())
        return QString();

    struct quirc *q = quirc_new();
    if (!q)
        return QString();
    if (quirc_resize(q, gray.width(), gray.height()) < 0) {
        quirc_destroy(q);
        return QString();
    }

    // 逐行拷贝到 quirc 缓冲(QImage 每行可能有对齐填充)
    uint8_t *buf = quirc_begin(q, nullptr, nullptr);
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        memcpy(buf + (size_t)y * gray.width(), line, (size_t)gray.width());
    }
    quirc_end(q);

    int count = quirc_count(q);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            QString text = QString::fromUtf8((const char *)data.payload,
                                             (int)data.payload_len).trimmed();
            quirc_destroy(q);
            return text;
        }
    }
    quirc_destroy(q);
    return QString();
#else
    (void)img;
    static bool notified = false;
    if (!notified) {
        notified = true;
        addLog("二维码识别模块未编译(HAVE_QUIRC 未定义), 站点识别暂不可用, 依赖车端兜底停车");
    }
    return QString();
#endif
}

void MainWindow::handleArrive(const QString &taskId, const QString &stationId, const QString &flag)
{
    TaskInfo *t = findTask(taskId);
    if (!t) {
        addLog(QString("收到到达信号, 但任务不存在: %1").arg(taskId));
        return;
    }
    if (flag == "FALLBACK") {
        closeRecognitionWindow();
        addLog(QString("任务 %1 兜底停车, 等待人工确认(站点 %2)").arg(taskId).arg(stationName(stationId)));
    }
    t->state = "已到达";
    updateTaskItem(t);
    QString fallbackNote = (flag == "FALLBACK")
        ? QStringLiteral("（兜底停车，站点未自动识别，请人工确认）") : QString();
    QString name = stationName(stationId);
    if (stationId == t->fromId) {
        QMessageBox box(this);
        box.setWindowTitle("到达取药点");
        box.setText(QString("%1 已到达, 请装药(任务 %2)%3").arg(name).arg(taskId)
                    .arg(fallbackNote));
        QPushButton *okBtn = box.addButton("确认装药", QMessageBox::AcceptRole);
        box.addButton("稍后处理", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == okBtn) {
            sendTaskConfirm(taskId, "load", "操作员");
            t->state = "装药确认, 出发";
            updateTaskItem(t);
            addLog(QString("任务 %1 已确认装药, 出发前往 %2").arg(taskId).arg(t->toName));
        }
    } else {
        QString medsText = t->meds.isEmpty() ? "(无药品清单)" : t->meds;
        QMessageBox box(this);
        box.setWindowTitle("到达病房");
        box.setText(QString("%1 已到达, 请核对取药(任务 %2)%4\n药品清单: %3")
                    .arg(name).arg(taskId).arg(medsText).arg(fallbackNote));
        QPushButton *okBtn = box.addButton("确认取药", QMessageBox::AcceptRole);
        box.addButton("稍后处理", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == okBtn) {
            sendTaskConfirm(taskId, "unload", "操作员");
            t->state = "取药确认";
            updateTaskItem(t);
            addLog(QString("任务 %1 已确认取药").arg(taskId));
        }
    }
}

void MainWindow::handleDone(const QString &taskId)
{
    TaskInfo *t = findTask(taskId);
    if (t) {
        t->state = "已完成";
        updateTaskItem(t);
    }
    addLog(QString("任务 %1 已完成, 小车回空闲").arg(taskId));
    runStatusLabel->setText("空闲");
    runStatusLabel->setStyleSheet("color: #7f8c8d; font-size: 13px; font-weight: bold;");
    taskDetailLabel->setText("无任务");
    for (int i = 0; i < taskQueue->count(); ++i) {
        if (taskQueue->item(i)->data(Qt::UserRole).toString() == taskId) {
            delete taskQueue->takeItem(i);
            refreshTaskCount();
            break;
        }
    }
    m_tasks.remove(taskId);
}

void MainWindow::handleHeartbeat(int battery, const QString &state)
{
    Q_UNUSED(state)
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    if (battery < 0) battery = 0;
    if (battery > 100) battery = 100;
    batteryBar->setValue(battery);
    if (!m_wifiOnline) {
        m_wifiOnline = true;
        addLog("小车已恢复在线");
    }
}

void MainWindow::checkOnlineState()
{
    if (!m_videoSocket || m_videoSocket->state() != QAbstractSocket::ConnectedState) {
        if (m_wifiOnline) {
            m_wifiOnline = false;
            addLog("小车离线(连接断开)");
            runStatusLabel->setText("离线");
            runStatusLabel->setStyleSheet("color: #e74c3c; font-size: 13px; font-weight: bold;");
        }
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_wifiOnline && (now - m_lastHeartbeatMs > 4000)) {
        m_wifiOnline = false;
        addLog("小车离线(超过 4s 未收到心跳)");
        runStatusLabel->setText("离线");
        runStatusLabel->setStyleSheet("color: #e74c3c; font-size: 13px; font-weight: bold;");
    } else if (!m_wifiOnline && (now - m_lastHeartbeatMs <= 4000)) {
        m_wifiOnline = true;
        addLog("小车已恢复在线");
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), taskCounter(2024), m_manualDlg(nullptr), m_settingsDlg(nullptr)
{
    setWindowTitle("AIC药品配送机器人系统");
    resize(1280, 740);
    setupUI();

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateTime);
    timer->start(1000);

    // 初始化视频流 TCP 服务器
    m_videoServer = new QTcpServer(this);
    m_videoSocket = new QTcpSocket(this);

    // P0 任务协议: ACK 重发 / 在线检测 / 识别窗口定时器
    m_ackTimer = new QTimer(this);
    m_ackTimer->setSingleShot(true);
    connect(m_ackTimer, &QTimer::timeout, this, &MainWindow::ackTimerTimeout);

    m_onlineTimer = new QTimer(this);
    connect(m_onlineTimer, &QTimer::timeout, this, &MainWindow::checkOnlineState);
    m_onlineTimer->start(3000);

    m_recogTimer = new QTimer(this);
    m_recogTimer->setSingleShot(true);
    connect(m_recogTimer, &QTimer::timeout, this, &MainWindow::recogTimerTimeout);

    m_recogActive = false;
    m_recogRound = 0;
    m_wifiOnline = false;
    m_lastHeartbeatMs = 0;
    m_ackRetryCount = 0;
    m_cmdSeq = 1;

    // 启动视频监听
    onVideoListenClicked();
}

MainWindow::~MainWindow()
{
}

// ==================== CRC16-CCITT ====================
uint16_t MainWindow::crc16_ccitt(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*buf++) << 8;
        for (int k = 0; k < 8; ++k) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ==================== 视频流 TCP 服务器 ====================
void MainWindow::onVideoListenClicked()
{
    if (m_videoServer->isListening()) {
        qDebug() << "Video server already listening";
        if (m_settingsDlg) {
            m_settingsDlg->appendDebug("server already listening on port 8086");
        }
        return;
    }
    if (m_videoServer->listen(QHostAddress::Any, 8086)) {
        qDebug() << "Video server listening on port 8086 ...";
        addLog("视频服务器已启动，端口 8086");
        if (m_settingsDlg) {
            m_settingsDlg->appendDebug("listening ... ");
        }
    } else {
        qDebug() << "Video server listen error";
        addLog("视频服务器启动失败");
        if (m_settingsDlg) {
            m_settingsDlg->appendDebug("listen err");
        }
    }
    connect(m_videoServer, SIGNAL(newConnection()), this, SLOT(videoNewClientConnect()));
}

void MainWindow::videoNewClientConnect()
{
    qDebug() << "Video server: new client connected";
    m_videoSocket = m_videoServer->nextPendingConnection();
    connect(m_videoSocket, SIGNAL(readyRead()), this, SLOT(readVideoMessage()));
    connect(m_videoSocket, SIGNAL(disconnected()), this, SLOT(videoDisConnect()));
    addLog("小车已连接到视频服务器");
    // 心跳宽限: 连接后 4s 内未收到 HEARTBEAT 判离线
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    m_wifiOnline = true;
    if (m_settingsDlg) {
        m_settingsDlg->appendDebug("new client connect");
    }

    // 将共享 socket 传递给手动控制弹窗
    if (m_manualDlg) {
        m_manualDlg->setSocket(m_videoSocket);
        addLog("手动控制面板已获得小车控制权");
    }
}

void MainWindow::readVideoMessage()
{
    static const QByteArray FRAME_HEADER = QByteArray::fromHex("AA55");
    static const QByteArray CHUNK_HEADER = QByteArray::fromHex("AA55A55A");

    QByteArray data = m_videoSocket->readAll();
    m_recvBuffer.append(data);

    auto showJpeg = [this](const QByteArray &jpegData) {
        QPixmap pixmap;
        if (!pixmap.loadFromData(jpegData)) {
            qDebug() << "Failed to load pixmap from jpeg data";
            return;
        }
        qDebug() << "Frame decoded OK, JPEG size:" << jpegData.size()
                 << "pixmap:" << pixmap.width() << "x" << pixmap.height();
        m_pixmap = pixmap;
        if (cameraLabel)
            cameraLabel->setPixmap(pixmap);

        if (m_recogActive) {
            QString qr = tryDecodeQR(pixmap.toImage());
            if (!qr.isEmpty()) {
                bool hit = (qr == m_recogTargetStation) ||
                           (qr.startsWith(m_recogTargetStation) &&
                            qr.size() <= m_recogTargetStation.size() + 1);
                if (hit) {
                    closeRecognitionWindow();
                    sendCommandWithAck(QString("STOP,%1,%2").arg(m_recogTaskId).arg(m_recogTargetStation), m_recogTaskId);
                    addLog(QString("识别到目标站点 %1, 已发送 STOP").arg(qr));
                } else {
                    addLog(QString("识别到非目标站点 %1, 忽略(仅记日志)").arg(qr));
                }
            }
        }
    };

    bool progress = true;
    while (progress)
    {
        progress = false;
        if (m_recvBuffer.isEmpty())
            break;

        // 1) A protocol text line may sit between two video chunks.
        int nl = m_recvBuffer.indexOf("\r\n");
        if (nl >= 0) {
            QByteArray line = m_recvBuffer.left(nl);
            if (isProtocolTextLine(line)) {
                m_recvBuffer.remove(0, nl + 2);
                handleProtocolLine(line);
                progress = true;
                continue;
            }
        }

        // 2) Chunked video packet: AA 55 A5 5A | total | offset | len | data | CRC
        if (m_recvBuffer.startsWith(CHUNK_HEADER))
        {
            if (m_recvBuffer.size() < 10)
                break;
            uint32_t total = (uint8_t)m_recvBuffer.at(4) |
                             ((uint8_t)m_recvBuffer.at(5) << 8);
            uint32_t offset = (uint8_t)m_recvBuffer.at(6) |
                              ((uint8_t)m_recvBuffer.at(7) << 8);
            uint32_t chunkLen = (uint8_t)m_recvBuffer.at(8) |
                                ((uint8_t)m_recvBuffer.at(9) << 8);

            if (total < 500 || total > 50 * 1024 || chunkLen == 0 ||
                chunkLen > 256 || offset + chunkLen > total) {
                qDebug() << "Invalid video chunk header, resync";
                m_recvBuffer.remove(0, 1);
                progress = true;
                continue;
            }
            uint32_t fullLen = 10 + chunkLen + 2;
            if ((uint32_t)m_recvBuffer.size() < fullLen)
                break;

            QByteArray payload = m_recvBuffer.mid(10, chunkLen);
            uint16_t recvCrc = (uint8_t)m_recvBuffer.at(10 + chunkLen) |
                               ((uint8_t)m_recvBuffer.at(10 + chunkLen + 1) << 8);
            uint16_t calcCrc = crc16_ccitt((const uint8_t*)payload.constData(), payload.size());
            if (calcCrc != recvCrc) {
                qDebug() << "Video chunk CRC mismatch, resync";
                m_recvBuffer.remove(0, 1);
                progress = true;
                continue;
            }

            if (offset == 0) {
                m_jpegAssembly.clear();
                m_jpegExpected = (int)total;
            }
            if ((int)total != m_jpegExpected ||
                (int)offset != m_jpegAssembly.size()) {
                // Lost a chunk: drop the partial frame and wait for offset 0.
                m_jpegAssembly.clear();
                m_jpegExpected = (int)total;
                if (offset != 0) {
                    m_recvBuffer.remove(0, fullLen);
                    progress = true;
                    continue;
                }
            }
            m_jpegAssembly.append(payload);
            m_recvBuffer.remove(0, fullLen);
            progress = true;

            if (m_jpegAssembly.size() == m_jpegExpected) {
                QByteArray jpeg = m_jpegAssembly;
                m_jpegAssembly.clear();
                m_jpegExpected = 0;
                showJpeg(jpeg);
            }
            continue;
        }

        // 3) Legacy complete frame: AA 55 | len32 | JPEG | CRC16
        if (m_recvBuffer.startsWith(FRAME_HEADER))
        {
            if (m_recvBuffer.size() < 6)
                break;
            uint32_t jpegLen = (uint8_t)m_recvBuffer.at(2) |
                ((uint8_t)m_recvBuffer.at(3) << 8) |
                ((uint8_t)m_recvBuffer.at(4) << 16) |
                ((uint8_t)m_recvBuffer.at(5) << 24);
            if (jpegLen < 500 || jpegLen > 50 * 1024) {
                m_recvBuffer.remove(0, 1);
                progress = true;
                continue;
            }
            uint32_t fullLen = 6 + jpegLen + 2;
            if ((uint32_t)m_recvBuffer.size() < fullLen)
                break;
            QByteArray jpegData = m_recvBuffer.mid(6, jpegLen);
            uint16_t recvCrc = (uint8_t)m_recvBuffer.at(6 + jpegLen) |
                               ((uint8_t)m_recvBuffer.at(6 + jpegLen + 1) << 8);
            uint16_t calcCrc = crc16_ccitt((const uint8_t*)jpegData.constData(), jpegData.size());
            if (calcCrc != recvCrc) {
                m_recvBuffer.remove(0, 1);
                progress = true;
                continue;
            }
            m_recvBuffer.remove(0, fullLen);
            showJpeg(jpegData);
            progress = true;
            continue;
        }

        // 4) A protocol line may be split across TCP reads: do not discard
        //    it just because its CRLF has not arrived yet.
        static const char *waiting[] = {
            "ACK,", "ENTER_STATION,", "STATUS,", "TASK_ARRIVE,", "TASK_DONE,",
            "HEARTBEAT,", "LINK,", "STATS,", "BOOT_TIMEOUT,"
        };
        bool incompleteLine = false;
        for (unsigned i = 0; i < sizeof(waiting) / sizeof(waiting[0]); ++i) {
            if (m_recvBuffer.startsWith(waiting[i])) {
                incompleteLine = true;
                break;
            }
        }
        if (incompleteLine)
            break;

        // 5) Resync to the next frame/chunk magic.
        int headIndex = m_recvBuffer.indexOf(FRAME_HEADER);
        if (headIndex < 0) {
            if (m_recvBuffer.size() > 1)
                m_recvBuffer.remove(0, m_recvBuffer.size() - 1);
            break;
        }
        if (headIndex > 0) {
            m_recvBuffer.remove(0, headIndex);
            progress = true;
            continue;
        }
        break;
    }

    // Bound the buffer if a sender never produces a valid header.
    const int MAX_BUFFER = 64 * 1024;
    if (m_recvBuffer.size() > MAX_BUFFER) {
        qDebug() << "Buffer overflow, discarding oldest data";
        m_recvBuffer.remove(0, m_recvBuffer.size() - MAX_BUFFER / 2);
    }
}

void MainWindow::videoDisConnect()
{
    m_jpegAssembly.clear();
    m_jpegExpected = 0;
    qDebug() << "Video server: client disconnected";
    addLog("小车视频连接断开");
    m_wifiOnline = false;
    m_lastHeartbeatMs = 0;
    if (m_settingsDlg) {
        m_settingsDlg->appendDebug("client disconnect");
    }
}

// ==================== UI Setup ====================
void MainWindow::setupUI()
{
    QWidget *cw = new QWidget(this);
    cw->setStyleSheet("background-color: #1a1a2e;");
    setCentralWidget(cw);

    QVBoxLayout *mainLayout = new QVBoxLayout(cw);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget *headerBar = new QWidget(cw);
    headerBar->setFixedHeight(36);
    headerBar->setStyleSheet("background-color: #16213e;");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(15, 0, 15, 0);

    QLabel *titleLabel = new QLabel("AIC药品配送机器人系统");
    titleLabel->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    for (int i = 0; i < 3; i++) {
        QLabel *dot = new QLabel();
        dot->setFixedSize(8, 8);
        const char *colors[] = {"#27ae60", "#f39c12", "#e74c3c"};
        dot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(colors[i]));
        headerLayout->addWidget(dot);
        if (i < 2) headerLayout->addSpacing(5);
    }

    mainLayout->addWidget(headerBar);

    QWidget *contentWidget = new QWidget(cw);
    QHBoxLayout *contentLayout = new QHBoxLayout(contentWidget);
    contentLayout->setContentsMargins(5, 5, 5, 5);
    contentLayout->setSpacing(5);

    QWidget *leftPanel = new QWidget(contentWidget);
    leftPanel->setFixedWidth(210);
    setupLeftPanel(leftPanel);

    QWidget *centerPanel = new QWidget(contentWidget);
    setupCenterPanel(centerPanel);

    QWidget *rightPanel = new QWidget(contentWidget);
    rightPanel->setFixedWidth(300);
    setupRightPanel(rightPanel);

    contentLayout->addWidget(leftPanel);
    contentLayout->addWidget(centerPanel, 1);
    contentLayout->addWidget(rightPanel);

    mainLayout->addWidget(contentWidget, 1);

    QWidget *bottomBar = new QWidget(cw);
    bottomBar->setFixedHeight(28);
    setupBottomBar(bottomBar);

    mainLayout->addWidget(bottomBar);
}

void MainWindow::setupLeftPanel(QWidget *panel)
{
    panel->setStyleSheet("background-color: transparent;");
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);

    QGroupBox *statusBox = new QGroupBox("机器人状态");
    statusBox->setStyleSheet(R"(
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
            left: 8px;
        }
    )");

    QGridLayout *statusLayout = new QGridLayout();
    statusLayout->setContentsMargins(8, 8, 8, 8);
    statusLayout->setHorizontalSpacing(6);
    statusLayout->setVerticalSpacing(6);

    auto addRow = [&](int row, const QString &label, QWidget *value) {
        QLabel *lbl = new QLabel(label);
        lbl->setStyleSheet("color: #7f8c8d; font-size: 11px;");
        statusLayout->addWidget(lbl, row, 0);
        statusLayout->addWidget(value, row, 1);
    };

    runStatusLabel = new QLabel("执行中");
    runStatusLabel->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold;");
    addRow(0, "运行状态", runStatusLabel);

    QLabel *s2 = new QLabel("0.8 m/s");
    s2->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: bold;");
    addRow(1, "当前速度", s2);

    QLabel *s3 = new QLabel("5格 强");
    s3->setStyleSheet("color: #2ecc71; font-size: 11px;");
    addRow(2, "信号强度", s3);

    taskCountLabel = new QLabel("2个");
    taskCountLabel->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: bold;");
    addRow(3, "任务数", taskCountLabel);

    QLabel *batLabel = new QLabel("电量");
    batLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    statusLayout->addWidget(batLabel, 4, 0);

    batteryBar = new QProgressBar();
    batteryBar->setFixedHeight(8);
    batteryBar->setRange(0, 100);
    batteryBar->setValue(0);  // 初始 0, 之后由 HEARTBEAT 报文更新
    batteryBar->setTextVisible(false);
    batteryBar->setStyleSheet(R"(
        QProgressBar {
            background-color: #34495e;
            border-radius: 4px;
            border: none;
        }
        QProgressBar::chunk {
            background-color: #27ae60;
            border-radius: 4px;
        }
    )");
    statusLayout->addWidget(batteryBar, 4, 1);

    statusBox->setLayout(statusLayout);
    layout->addWidget(statusBox);

    addTaskBtn = new QPushButton("+ 录入任务");
    addTaskBtn->setMinimumHeight(38);
    addTaskBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3498db;
            border: none;
            border-radius: 4px;
            color: #ffffff;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #2980b9; }
    )");
    layout->addWidget(addTaskBtn);

    auto addBtn = [&](const QString &text, const QString &textColor) {
        QPushButton *btn = new QPushButton(text);
        btn->setMinimumHeight(33);
        btn->setStyleSheet(QString(R"(
            QPushButton {
                background-color: #34495e;
                border: none;
                border-radius: 4px;
                color: %1;
                font-size: 12px;
            }
            QPushButton:hover { background-color: #2c3e50; }
        )").arg(textColor));
        layout->addWidget(btn);
        return btn;
    };

    callRobotBtn = addBtn("呼叫小车", "#bdc3c7");
    manualControlBtn = addBtn("手动控制", "#bdc3c7");
    returnChargeBtn = addBtn("返回充电", "#bdc3c7");
    pauseTaskBtn = addBtn("暂停任务", "#bdc3c7");
    cancelTaskBtn = addBtn("取消任务", "#e74c3c");
    systemSettingsBtn = addBtn("系统设置", "#bdc3c7");

    layout->addStretch();

    QGroupBox *taskQueueBox = new QGroupBox("任务队列");
    taskQueueBox->setStyleSheet(R"(
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
            left: 8px;
        }
    )");
    QVBoxLayout *taskQueueLayout = new QVBoxLayout();
    taskQueueLayout->setContentsMargins(8, 8, 8, 8);
    taskQueueLayout->setSpacing(5);

    taskDetailLabel = new QLabel("药房01 → 病房002");
    taskDetailLabel->setStyleSheet("color: #7f8c8d; font-size: 10px;");
    taskQueueLayout->addWidget(taskDetailLabel);

    taskQueue = new QListWidget();
    taskQueue->setFixedHeight(100);
    taskQueue->setStyleSheet(R"(
        QListWidget {
            background-color: #2c3e50;
            border: none;
            color: #bdc3c7;
            font-size: 11px;
        }
    )");

    QListWidgetItem *item1 = new QListWidgetItem("#T-2024  执行中");
    item1->setForeground(QColor("#3498db"));
    taskQueue->addItem(item1);

    QListWidgetItem *item2 = new QListWidgetItem("#T-2021  等待中");
    item2->setForeground(QColor("#7f8c8d"));
    taskQueue->addItem(item2);

    taskQueueLayout->addWidget(taskQueue);
    taskQueueBox->setLayout(taskQueueLayout);
    layout->addWidget(taskQueueBox);

    connect(addTaskBtn, &QPushButton::clicked, this, &MainWindow::onAddTaskClicked);
    connect(callRobotBtn, &QPushButton::clicked, this, &MainWindow::onCallRobotClicked);
    connect(manualControlBtn, &QPushButton::clicked, this, &MainWindow::onManualControlClicked);
    connect(returnChargeBtn, &QPushButton::clicked, this, &MainWindow::onReturnChargeClicked);
    connect(pauseTaskBtn, &QPushButton::clicked, this, &MainWindow::onPauseTaskClicked);
    connect(cancelTaskBtn, &QPushButton::clicked, this, &MainWindow::onCancelTaskClicked);
    connect(systemSettingsBtn, &QPushButton::clicked, this, &MainWindow::onSystemSettingsClicked);
}

void MainWindow::setupCenterPanel(QWidget *panel)
{
    panel->setStyleSheet("background-color: transparent;");
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel *mapLabel = new QLabel();
    mapLabel->setStyleSheet("background-color: #ffffff; border: 2px solid #2c3e50;");
    mapLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mapLabel->setScaledContents(true);

    QPixmap pixmap;
    // 优先使用 Qt 资源文件(跨系统可移植)
    pixmap.load(":/1784534321041.png");
    if (pixmap.isNull()) {
        // 回退：尝试文件系统路径
        QString baseDir = QCoreApplication::applicationDirPath();
        QStringList paths;
        paths << baseDir + "/../1784534321041.png"
              << baseDir + "/../../1784534321041.png"
              << baseDir + "/1784534321041.png";
        for (const QString &p : paths) {
            pixmap.load(p);
            if (!pixmap.isNull()) break;
        }
        if (pixmap.isNull()) {
            mapLabel->setText("地图图片加载失败\n" + baseDir);
        }
    }

    if (!pixmap.isNull()) {
        mapLabel->setPixmap(pixmap);
    } else if (mapLabel->text().isEmpty()) {
        mapLabel->setText("地图图片加载失败");
        mapLabel->setAlignment(Qt::AlignCenter);
        mapLabel->setWordWrap(true);
        mapLabel->setStyleSheet("background-color: #ffffff; color: #e74c3c; font-size: 12px;");
    }

    layout->addWidget(mapLabel);
}

void MainWindow::setupRightPanel(QWidget *panel)
{
    panel->setStyleSheet("background-color: transparent;");
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    QGroupBox *videoBox = new QGroupBox("实时画面 - CAM-1 (前置)");
    videoBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 11px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 14px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
        }
    )");
    QVBoxLayout *videoLayout = new QVBoxLayout();
    videoLayout->setContentsMargins(5, 8, 5, 5);

    cameraLabel = new QLabel();
    cameraLabel->setMinimumHeight(180);
    cameraLabel->setAlignment(Qt::AlignCenter);
    cameraLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cameraLabel->setScaledContents(true);
    cameraLabel->setText("等待视频连接...");
    cameraLabel->setStyleSheet("background-color: #000000; color: #7f8c8d; font-size: 12px; border-radius: 4px;");

    videoLayout->addWidget(cameraLabel);
    videoBox->setLayout(videoLayout);
    layout->addWidget(videoBox);

    medicineBox = new QGroupBox("配送药品清单 (任务 #T-2024)");
    medicineBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 11px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 14px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
        }
    )");
    QVBoxLayout *medicineLayout = new QVBoxLayout();
    medicineLayout->setContentsMargins(5, 8, 5, 5);

    medicineTable = new QTableWidget();
    medicineTable->setColumnCount(4);
    medicineTable->setRowCount(0);
    medicineTable->setHorizontalHeaderLabels({"药品", "数量", "目的地", "状态"});
    medicineTable->verticalHeader()->setVisible(false);
    medicineTable->setFixedHeight(125);
    medicineTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #2c3e50;
            border: none;
            gridline-color: #34495e;
            color: #bdc3c7;
            font-size: 11px;
        }
        QTableWidget::item {
            padding: 3px;
            border-bottom: 1px solid #34495e;
        }
        QHeaderView::section {
            background-color: #34495e;
            color: #7f8c8d;
            font-size: 10px;
            padding: 3px;
            border: none;
        }
    )");
    medicineTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto addRow = [&](const QString &name, const QString &count,
                       const QString &dest, const QString &status, const QColor &color) {
        int r = medicineTable->rowCount();
        medicineTable->setRowCount(r + 1);
        medicineTable->setItem(r, 0, new QTableWidgetItem(name));
        medicineTable->setItem(r, 1, new QTableWidgetItem(count));
        medicineTable->setItem(r, 2, new QTableWidgetItem(dest));
        QTableWidgetItem *s = new QTableWidgetItem(status);
        s->setForeground(color);
        medicineTable->setItem(r, 3, s);
    };

    addRow("阿莫西林胶囊", "2盒", "病房002", "配送中", QColor("#f39c12"));
    addRow("布洛芬片",     "1盒", "病房002", "配送中", QColor("#f39c12"));
    addRow("氯化钠注射液",  "3袋", "车库01", "待配送", QColor("#7f8c8d"));
    addRow("阿奇霉素片",   "1盒", "病房001", "待配送", QColor("#7f8c8d"));

    medicineLayout->addWidget(medicineTable);
    medicineBox->setLayout(medicineLayout);
    layout->addWidget(medicineBox);

    QGroupBox *logBox = new QGroupBox("运行日志");
    logBox->setStyleSheet(R"(
        QGroupBox {
            color: #bdc3c7;
            font-size: 11px;
            font-weight: bold;
            border: 1px solid #2c3e50;
            border-radius: 4px;
            padding-top: 14px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
        }
    )");
    QVBoxLayout *logLayout = new QVBoxLayout();
    logLayout->setContentsMargins(5, 8, 5, 5);

    logTextEdit = new QTextEdit();
    logTextEdit->setReadOnly(true);
    logTextEdit->setStyleSheet(R"(
        QTextEdit {
            background-color: #2c3e50;
            border: none;
            color: #bdc3c7;
            font-size: 10px;
            font-family: monospace;
        }
    )");

    logTextEdit->append("14:27:00 系统启动，连接云平台成功");
    logTextEdit->append("14:27:33 系统自检完成，所有传感器正常");
    logTextEdit->append("14:28:10 任务 #T-2021 已创建（药房01→病房001）");
    logTextEdit->append("14:28:10 任务 #T-2024 已创建（药房01→病房002）");
    logTextEdit->append("14:29:58 检测到走廊临时障碍物，重新规划路径");
    logTextEdit->append("14:30:22 路径规划完成，距离58m，预计3min");
    logTextEdit->append("14:31:45 机器人到达药房01，开始装载药品");
    logTextEdit->append("14:32:01 任务 #T-2024 开始执行");

    logLayout->addWidget(logTextEdit);
    logBox->setLayout(logLayout);
    layout->addWidget(logBox, 1);
}

void MainWindow::setupBottomBar(QWidget *bar)
{
    bar->setStyleSheet("background-color: #16213e;");
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(15, 0, 15, 0);

    QLabel *leftLabel = new QLabel("系统正常");
    leftLabel->setStyleSheet("color: #27ae60; font-size: 11px;");
    layout->addWidget(leftLabel);
    layout->addStretch();

    QLabel *infoLabel = new QLabel("机器人ID: RB-003 | IP: 192.168.1.105 | 坐标: (723, 563)");
    infoLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    layout->addWidget(infoLabel);
    layout->addStretch();

    statusLabel = new QLabel("ROS v2.1.0  |  STM32 F/W v1.3.2  |  " +
                              QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    statusLabel->setStyleSheet("color: #7f8c8d; font-size: 11px;");
    layout->addWidget(statusLabel);
}

void MainWindow::addLog(const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    logTextEdit->append(ts + " " + msg);
}

void MainWindow::refreshTaskCount()
{
    int count = taskQueue->count();
    taskCountLabel->setText(QString("%1个").arg(count));
}

void MainWindow::sendCarCommand(const QString &cmd)
{
    if (m_videoSocket && m_videoSocket->state() == QAbstractSocket::ConnectedState) {
        m_videoSocket->write(cmd.toUtf8() + "\r\n");
        addLog(QString("已发送指令: %1").arg(cmd));
    } else {
        addLog("小车未连接，指令未发送: " + cmd);
    }
}

// ==================== 按钮事件 ====================
void MainWindow::onAddTaskClicked()
{
    TaskDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        taskCounter++;
        QString taskId = QString("T-%1").arg(taskCounter);
        QString fromId = dlg.fromStationId();
        QString toId = dlg.toStationId();
        QString from = dlg.fromLocation();
        QString to = dlg.toLocation();
        QString pri = dlg.priority();
        QString meds = dlg.medicines();

        if (fromId == toId) {
            addLog("取药点与目的地相同, 已忽略该任务");
            return;
        }

        // 记录任务真实状态(药品清单留在上位机本地, 用于交接核对弹窗)
        TaskInfo info;
        info.taskId = taskId;
        info.fromId = fromId;
        info.toId = toId;
        info.fromName = from;
        info.toName = to;
        info.meds = meds;
        info.state = "待下发";
        info.done = false;
        m_tasks.insert(taskId, info);

        QColor priColor = QColor("#7f8c8d");
        if (pri == "紧急") priColor = QColor("#f39c12");
        else if (pri == "危重") priColor = QColor("#e74c3c");

        QString type = (pri == "危重" || pri == "紧急") ? "紧急" : "";

        QListWidgetItem *item = new QListWidgetItem(QString("%1  %2待配送").arg(taskId).arg(type));
        item->setForeground(priColor);
        item->setData(Qt::UserRole, taskId);
        taskQueue->addItem(item);
        taskQueue->scrollToBottom();

        if (!meds.isEmpty()) {
            QStringList medList = meds.split(",", Qt::SkipEmptyParts);
            for (const QString &m : medList) {
                QString trimmed = m.trimmed();
                if (trimmed.isEmpty()) continue;
                int r = medicineTable->rowCount();
                medicineTable->setRowCount(r + 1);
                medicineTable->setItem(r, 0, new QTableWidgetItem(trimmed));
                medicineTable->setItem(r, 1, new QTableWidgetItem("1份"));
                medicineTable->setItem(r, 2, new QTableWidgetItem(to));
                QTableWidgetItem *s = new QTableWidgetItem("待配送");
                s->setForeground(QColor("#7f8c8d"));
                medicineTable->setItem(r, 3, s);
            }
        }

        refreshTaskCount();

        taskDetailLabel->setText(QString("%1 → %2").arg(from).arg(to));

        medicineBox->setTitle(QString("配送药品清单 (任务 %1)").arg(taskId));

        addLog(QString("任务 %1 已创建（%2→%3）优先级: %4").arg(taskId).arg(from).arg(to).arg(pri));

        // 录入即下发: TASK_START + ACK 重发机制
        sendTaskStart(taskId);
    }
}

void MainWindow::onCallRobotClicked()
{
    // 重新下发第一个待下发/下发失败的任务
    QString taskId;
    for (QMap<QString, TaskInfo>::const_iterator it = m_tasks.constBegin();
         it != m_tasks.constEnd(); ++it) {
        if (it.value().state == "待下发" || it.value().state == "下发失败") {
            taskId = it.key();
            break;
        }
    }
    if (!taskId.isEmpty()) {
        sendTaskStart(taskId);
        addLog(QString("呼叫小车: 已重新下发任务 %1").arg(taskId));
        runStatusLabel->setText("下发中");
        runStatusLabel->setStyleSheet("color: #f39c12; font-size: 13px; font-weight: bold;");
    } else {
        addLog("没有待配送任务, 请先录入任务");
        runStatusLabel->setText("空闲");
        runStatusLabel->setStyleSheet("color: #7f8c8d; font-size: 13px; font-weight: bold;");
    }
}

void MainWindow::onManualControlClicked()
{
    if (!m_manualDlg) {
        m_manualDlg = new ManualControlDialog(this);
    }
    // 如果小车已连接，传递当前 socket
    if (m_videoSocket && m_videoSocket->state() == QAbstractSocket::ConnectedState) {
        m_manualDlg->setSocket(m_videoSocket);
    }
    m_manualDlg->show();
    m_manualDlg->raise();
    m_manualDlg->activateWindow();
    // 手动控制接管: 停止循迹
    sendCarCommand("stopTask");
    addLog("手动控制面板已打开");
    runStatusLabel->setText("手动控制");
    runStatusLabel->setStyleSheet("color: #3498db; font-size: 13px; font-weight: bold;");
}

void MainWindow::onReturnChargeClicked()
{
    // 返回充电 = 下发一条 S1→S4(车库01) 的任务
    taskCounter++;
    QString taskId = QString("T-%1").arg(taskCounter);
    TaskInfo info;
    info.taskId = taskId;
    info.fromId = "S1";
    info.toId = "S4";
    info.fromName = stationName("S1");
    info.toName = stationName("S4");
    info.meds = QString();
    info.state = "待下发";
    info.done = false;
    m_tasks.insert(taskId, info);

    QListWidgetItem *item = new QListWidgetItem(QString("%1  返回充电").arg(taskId));
    item->setData(Qt::UserRole, taskId);
    taskQueue->addItem(item);
    refreshTaskCount();

    addLog("返回充电指令已发送, 机器人正在前往车库01");
    sendTaskStart(taskId);
    runStatusLabel->setText("返回充电中");
    runStatusLabel->setStyleSheet("color: #f39c12; font-size: 13px; font-weight: bold;");
    // 电量由 HEARTBEAT 报文更新, 不再写死
}

void MainWindow::onPauseTaskClicked()
{
    // 按 task_id 下发 TASK_PAUSE(替代原 stopTask)
    QString taskId = currentTaskId();
    if (taskId.isEmpty()) {
        addLog("没有可暂停的任务");
        return;
    }
    sendCommandWithAck(QString("TASK_PAUSE,%1").arg(taskId), taskId);
    TaskInfo *t = findTask(taskId);
    if (t) {
        t->state = "已暂停";
        updateTaskItem(t);
    }
    addLog(QString("任务 %1 暂停指令已发送").arg(taskId));
    runStatusLabel->setText("已暂停");
    runStatusLabel->setStyleSheet("color: #f39c12; font-size: 13px; font-weight: bold;");
}

void MainWindow::onCancelTaskClicked()
{
    // 按 task_id 下发 TASK_CANCEL(替代原 stopTask)
    QString taskId = currentTaskId();
    if (taskId.isEmpty()) {
        addLog("没有可取消的任务");
        return;
    }
    sendCommandWithAck(QString("TASK_CANCEL,%1").arg(taskId), taskId);
    TaskInfo *t = findTask(taskId);
    if (t) {
        t->state = "已取消";
        updateTaskItem(t);
        m_tasks.remove(taskId);
    }
    for (int i = 0; i < taskQueue->count(); ++i) {
        if (taskQueue->item(i)->data(Qt::UserRole).toString() == taskId) {
            delete taskQueue->takeItem(i);
            refreshTaskCount();
            break;
        }
    }
    addLog(QString("任务 %1 已取消").arg(taskId));
    if (taskQueue->count() == 0) {
        runStatusLabel->setText("空闲");
        runStatusLabel->setStyleSheet("color: #7f8c8d; font-size: 13px; font-weight: bold;");
        taskDetailLabel->setText("无任务");
    }
}

void MainWindow::onSystemSettingsClicked()
{
    if (!m_settingsDlg) {
        m_settingsDlg = new SettingsDialog(this);
        // 将设置弹窗的"监听"按钮连接到视频服务器的启动
        connect(m_settingsDlg, &SettingsDialog::listenRequested, this, &MainWindow::onVideoListenClicked);
    }
    // 更新当前服务器状态
    if (m_videoServer && m_videoServer->isListening()) {
        m_settingsDlg->setListening();
        m_settingsDlg->appendDebug("server already listening on port 8086");
    }
    m_settingsDlg->show();
    m_settingsDlg->raise();
    m_settingsDlg->activateWindow();
    addLog("系统设置页面已打开");
}

void MainWindow::updateTime()
{
    statusLabel->setText("ROS v2.1.0  |  STM32 F/W v1.3.2  |  " +
                          QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}
