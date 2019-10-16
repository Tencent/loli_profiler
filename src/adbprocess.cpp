#include "adbprocess.h"
#include <QDebug>

AdbProcess::AdbProcess(QObject* parent) : QObject(parent) {
    process_ = new QProcess(this);
    Connect();
}

AdbProcess::~AdbProcess() {
    Disconnect();
    if (process_->state() != QProcess::ProcessState::NotRunning) {
        process_->kill();
        process_->close();
    }
}

void AdbProcess::Connect() {
    if (connected_)
        return;
    connect(process_, &QProcess::errorOccurred, this, &AdbProcess::AdbProcessErrorOccurred);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AdbProcess::AdbProcessFinished);
    connected_ = true;
}

void AdbProcess::Disconnect() {
    if (!connected_)
        return;
    disconnect(process_, &QProcess::errorOccurred, this, &AdbProcess::AdbProcessErrorOccurred);
    disconnect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AdbProcess::AdbProcessFinished);
    connected_ = false;
}

void AdbProcess::AdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitCode != 0 || exitStatus == QProcess::ExitStatus::CrashExit) {
        emit ProcessErrorOccurred();
        running_ = false;
        hasErrors_ = true;
        return;
    }
    hasErrors_ = false;
    running_ = false;
    OnProcessFinihed(); // handle returned data
    emit ProcessFinished(this); // then process the handled data
}

void AdbProcess::AdbProcessErrorOccurred(QProcess::ProcessError) {
    OnProcessErrorOccurred();
    running_ = false;
    hasErrors_ =  true;
    emit ProcessErrorOccurred();
}
