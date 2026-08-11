QT       += core gui widgets network

CONFIG   += c++11

TARGET = AICMedicineRobotSystem
TEMPLATE = app

SOURCES += main.cpp \
           mainwindow.cpp \
           loginwindow.cpp \
           taskdialog.cpp \
           manualcontrol.cpp \
           settingsdialog.cpp

HEADERS += mainwindow.h \
           loginwindow.h \
           taskdialog.h \
           manualcontrol.h \
           settingsdialog.h

RESOURCES += resources.qrc

# ==================== P0 二维码识别(quirc) ====================
# quirc 为纯 C 无依赖库: 将源码放入 quirc/ 目录后重新 qmake 即自动接入
# (设计文档 5.1: 站点二维码识别, 解码文本形如 "S1"~"S4")
exists(quirc/quirc.c) {
    INCLUDEPATH += quirc
    SOURCES += quirc/quirc.c \
               quirc/decode.c \
               quirc/identify.c \
               quirc/version_db.c
    DEFINES += HAVE_QUIRC
    QMAKE_CFLAGS += -std=gnu99
}
