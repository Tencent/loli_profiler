#-------------------------------------------------
#
# Project created by QtCreator 2020-09-19T18:03:39
#
#-------------------------------------------------

QT       += core gui script

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

include(../../src/qconsolewidget.pri)

TARGET = scriptconsole
TEMPLATE = app


SOURCES += main.cpp \
    scriptsession.cpp \
    qscriptcompleter.cpp

HEADERS  += \
    scriptsession.h \
    qscriptcompleter.h

