#include "startappprocess.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QRegularExpression>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QThread>
#include <QDebug>

StartAppProcess::StartAppProcess(QObject* parent)
    : AdbProcess(parent) {

}

void StartAppProcess::StartApp(const QString& appName, const QString& arch, QProgressDialog* dialog) {
    startResult_ = false;
    errorStr_ = QString();
    auto execPath = GetExecutablePath();
    QStringList arguments;
    { // push remote folder to /data/local/tmp
        dialog->setLabelText("Pushing libloli.so to device.");
        arguments << "push" << "remote/" + arch + "/libloli.so" << "/data/local/tmp";
        QProcess process;
        process.setWorkingDirectory(QCoreApplication::applicationDirPath());
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
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
        dialog->setValue(dialog->value() + 1);
    }
    { // push remote folder to /data/local/tmp
        dialog->setLabelText("Pushing loli.conf to device.");
        arguments.clear();
        arguments << "push" << "loli.conf" << "/data/local/tmp";
        QProcess process;
        process.setWorkingDirectory(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first());
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted()) {
            errorStr_ = "erro starting: adb push loli.conf /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
        if (!process.waitForFinished()) {
            errorStr_ = "erro finishing: adb push loli.conf /data/local/tmp";
            emit ProcessErrorOccurred();
            return;
        }
        dialog->setValue(dialog->value() + 1);
    }
    { // set app as debugable for next launch
        dialog->setLabelText("Marking apk debugable for next launch.");
        arguments.clear();
        arguments << "shell" << "am" << "set-debug-app" << "-w" << appName;
        QProcess process;
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
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
        dialog->setValue(dialog->value() + 1);
    }
    { // launch the app
        dialog->setLabelText("Launching apk.");
        arguments.clear();
        arguments << "shell" << "monkey -p" << appName << "-c android.intent.category.LAUNCHER 1";
        QProcess process;
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
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
        dialog->setValue(dialog->value() + 1);
    }
    unsigned int pid = 0;
    { // adb jdwp
        QThread::sleep(1);
        dialog->setLabelText("Gettting jdwp id.");
        arguments.clear();
        arguments << "jdwp";
        QProcess process;
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
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
        dialog->setValue(dialog->value() + 1);
    }
    { // adb forward
        dialog->setLabelText("Forwadring tcp port.");
        arguments.clear();
        arguments << "forward" << "tcp:8700" << ("jdwp:" + QString::number(pid));
        QProcess process;
        process.setProgram(execPath);
        process.setArguments(arguments);
        process.start();
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
        dialog->setValue(dialog->value() + 1);
    }
    // python jdwp-shellifier.py
    errorStr_ = "python jdwp-shellifier.py";
    dialog->setLabelText("Injecting libloli.so to target application.");
    process_->setWorkingDirectory(QCoreApplication::applicationDirPath());
    process_->setProgram(pythonPath_);
    process_->setArguments(QStringList() << "jdwp-shellifier.py" << "--target" << "127.0.0.1" << "--port" << "8700" << "--break-on" << "android.app.Activity.onResume" << "--loadlib" << "libloli.so");
    ExecuteAsync();
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

void StartAppProcess::OnProcessErrorOccurred() {
    errorStr_ = process_->readAll();
}
