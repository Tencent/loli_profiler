#include "meminfoprocess.h"
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

MemInfoProcess::MemInfoProcess(QObject* parent)
    : AdbProcess(parent) {

}

void MemInfoProcess::DumpMemInfoAsync(const QString& appName) {
    appName_ = appName;
    QStringList arguments;
    arguments << "shell" << "dumpsys" << "meminfo" << "--package" << appName;
    ExecuteAsync(arguments);
}

void MemInfoProcess::OnProcessFinihed() {
    meminfo_.Reset();

    QString retStr = process_->readAll();
    process_->close();

    QTextStream stream(&retStr);
    QString line;
    QString appName = "[" + appName_ + "]";
    bool foundAppName = false;
    int errorReadCount = 0;
    int readCount = 0;
    while(stream.readLineInto(&line)) {
        if (!foundAppName) {
            if (errorReadCount < 2) {
                if (line.contains("error") || line.contains("No process found")) {
                    AdbProcessErrorOccurred(QProcess::ProcessError::UnknownError);
                    return;
                }
            }
            errorReadCount++;
            if (line.contains(appName)) {
                auto list = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
                if (list.size() > 5)
                    appPid_ = list[4];
                foundAppName = true;
            }
        }
        else {
            if (readCount > 22)
                break;
            if (line.size() == 0)
                continue;
            auto list = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
            if (list.size() < 2)
                continue;
            auto name = list.at(0);
            if (name == "Native") {
                meminfo_.NativeHeap = list.at(2).toUInt() / 1024;
            } else if (name == "Gfx") {
                meminfo_.GfxDev = list.at(2).toUInt() / 1024;
            } else if (name == "EGL") {
                meminfo_.EGLmtrack = list.at(2).toUInt() / 1024;
            } else if (name == "GL") {
                meminfo_.GLmtrack = list.at(2).toUInt() / 1024;
            } else if (name == "Unknown") {
                meminfo_.Unkonw = list.at(1).toUInt() / 1024;
            } else if (name == "TOTAL") {
                meminfo_.Total = list.at(1).toUInt() / 1024;
            }
            readCount++;
        }
    }
}
