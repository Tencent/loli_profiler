#-------------------------------------------------
#
# Project created by QtCreator 2019-05-20T12:59:07
#
#-------------------------------------------------

QT       += core gui opengl charts network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = LoliProfiler
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS QT_NO_PROCESS_COMBINED_ARGUMENT_START

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

INCLUDEPATH += $$PWD/include $$PWD/src/lz4

SOURCES += \
        src/smaps/statsmapsdialog.cpp \
        src/smaps/visualizesmapsdialog.cpp \
        src/adbprocess.cpp \
        src/addressprocess.cpp \
        src/charttooltipitem.cpp \
        src/configdialog.cpp \
        src/customgraphicsview.cpp \
        src/fixedscrollarea.cpp \
        src/interactivechartview.cpp \
        src/lz4/lz4.c \
        src/main.cpp \
        src/mainwindow.cpp \
        src/memgraphicsview.cpp \
        src/meminfoprocess.cpp \
        src/pathutils.cpp \
        src/screenshotprocess.cpp \
        src/selectappdialog.cpp \
        src/stacktracemodel.cpp \
        src/stacktraceprocess.cpp \
        src/stacktraceproxymodel.cpp \
        src/startappprocess.cpp \
        src/treemapgraphicsview.cpp

HEADERS += \
        include/adbprocess.h \
        include/addressprocess.h \
        include/charttooltipitem.h \
        include/configdialog.h \
        include/customgraphicsview.h \
        include/fixedscrollarea.h \
        include/interactivechartview.h \
        include/pathutils.h \
        include/selectappdialog.h \
        include/smaps/smapssection.h \
        include/smaps/statsmapsdialog.h \
        include/smaps/visualizesmapsdialog.h \
        src/lz4/lz4.h \
        include/mainwindow.h \
        include/memgraphicsview.h \
        include/meminfoprocess.h \
        include/screenshotprocess.h \
        include/stacktracemodel.h \
        include/stacktraceprocess.h \
        include/stacktraceproxymodel.h \
        include/startappprocess.h \
        include/timeprofiler.h \
        include/treemapgraphicsview.h

FORMS += \
        configdialog.ui \
        mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res/icon.qrc

RC_ICONS = res/devices.ico
ICON = res/devices.icns

DISTFILES +=
