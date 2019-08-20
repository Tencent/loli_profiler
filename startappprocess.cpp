#include "startappprocess.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

StartAppProcess::StartAppProcess(QObject* parent)
    : AdbProcess(parent) {

}

void StartAppProcess::StartApp(const QString& appName) {
    startResult_ = false;
    errorStr_ = QString();
    auto execPath = GetExecutablePath();
    QStringList arguments;
    { // push remote folder to /data/local/tmp
        arguments << "push" << "remote/libloli.so" << "/data/local/tmp";
        QProcess process;
        process.setWorkingDirectory(QCoreApplication::applicationDirPath());
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb push remote/libloli.so /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb push remote/libloli.so /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
    }
    { // push remote folder to /data/local/tmp
        arguments.clear();
        arguments << "push" << "remote/loli.conf" << "/data/local/tmp";
        QProcess process;
        process.setWorkingDirectory(QCoreApplication::applicationDirPath());
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb push remote/loli.conf /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb push remote/loli.conf /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
    }
    { // set app as debugable for next launch
        arguments.clear();
        arguments << "shell" << "am" << "set-debug-app" << "-w" << appName;
        QProcess process;
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb shell am set-debug-app -w com.company.app";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb shell am set-debug-app -w com.company.app";
            emit ProcessErrorOccurred();
            return;
        }
    }
    { // launch the app
        arguments.clear();
        arguments << "shell" << "monkey -p" << appName << "-c android.intent.category.LAUNCHER 1";
        QProcess process;
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb shell monkey -p com.company.app -c android.intent.category.LAUNCHER 1";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb shell monkey -p com.company.app -c android.intent.category.LAUNCHER 1";
            emit ProcessErrorOccurred();
            return;
        }
    }
    unsigned int pid = 0;
    { // adb jdwp
        arguments.clear();
        arguments << "jdwp";
        QProcess process;
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb jdwp";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished(3000)) {
            process.kill();
            QString retStr = process.readAll();
            auto lines = retStr.split('\n', QString::SkipEmptyParts);
            if (lines.count() == 0) {
                errorStr_ = "erro interpreting: adb jdwp";
                emit ProcessErrorOccurred();
                return;
            }
            pid = lines[lines.count() - 1].trimmed().toUInt();
        }
    }
    { // adb forward
        arguments.clear();
        arguments << "forward" << "tcp:8700" << ("jdwp:" + QString::number(pid));
        QProcess process;
        process.start(execPath, arguments);
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb forward tcp:8700 jdwp:xxxx";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb forward tcp:8700 jdwp:xxxx";
            emit ProcessErrorOccurred();
            return;
        }
    }
    // python jdwp-shellifier.py
    process_->setWorkingDirectory(QCoreApplication::applicationDirPath());
    ExecuteAsync(pythonPath_ + " jdwp-shellifier.py --target 127.0.0.1 --port 8700 --break-on android.app.Activity.onResume --loadlib libloli.so");
}

void StartAppProcess::OnProcessFinihed() {
    QString retStr = process_->readAll();
    process_->close();
    QTextStream stream(&retStr);
    QString line;
    while(stream.readLineInto(&line)) {
        if (line.contains("Command successfully executed"))
        {
            startResult_ = true;
            break;
        }
    }
}
