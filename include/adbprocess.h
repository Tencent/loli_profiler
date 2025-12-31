#ifndef ADBPROCESS_H
#define ADBPROCESS_H

#include <QProcess>

class AdbProcess : public QObject {
    Q_OBJECT
public:
    AdbProcess(QObject* parent = nullptr);
    virtual ~AdbProcess();

    void SetExecutablePath(const QString& str) {
        execPath_ = str;
    }

    const QString& GetExecutablePath() const {
        return execPath_;
    }

    void SetDeviceSerial(const QString& serial) {
        deviceSerial_ = serial;
    }

    const QString& GetDeviceSerial() const {
        return deviceSerial_;
    }

    QProcess* Process() const {
        return process_;
    }

    void ExecuteAsync(const QStringList& arguments) {
        ExecuteAsync(execPath_, arguments);
    }

    void ExecuteAsync(const QString execPath, const QStringList& arguments) {
        running_ = true;
        process_->setProgram(execPath);
        SetArguments(process_, arguments);
        process_->start();
    }

    static void SetArguments(QProcess* process, const QStringList& arguments) {
#ifdef Q_OS_WIN
        auto clone = arguments;
        for (auto& argument : clone) {
            if (argument.contains(' ')) {
                argument = '\"' + argument + '\"';
            }
        }
        process->setNativeArguments(clone.join(' '));
#else
        process->setArguments(arguments);
#endif
    }

    void ExecuteAsync() {
        running_ = true;
        process_->start();
    }

    void WaitForFinished(int mills = 10000) {
        process_->waitForFinished(mills);
    }

    bool IsRunning() const {
        return running_;
    }

    bool HasErrors() const {
        return hasErrors_;
    }

    void Connect();
    void Disconnect();

signals:
    void ProcessFinished(AdbProcess* process);
    void ProcessErrorOccurred();

protected:
    virtual void OnProcessFinihed() = 0;
    virtual void OnProcessErrorOccurred() {}

protected slots:
    void AdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void AdbProcessErrorOccurred(QProcess::ProcessError);

protected:
    bool running_ = false;
    bool connected_ = false;
    bool hasErrors_ = false;
    QProcess *process_;
    QString execPath_;
    QString deviceSerial_;
};

#endif // ADBPROCESS_H
