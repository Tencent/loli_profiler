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

    QProcess* Process() const {
        return process_;
    }

    void ExecuteAsync(const QString command) {
        running_ = true;
        process_->start(command);
    }

    void ExecuteAsync(const QStringList& arguments) {
        running_ = true;
        process_->start(execPath_, arguments);
    }

    void ExecuteAsync(const QString execPath, const QStringList& arguments) {
        running_ = true;
        process_->start(execPath, arguments);
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

protected slots:
    void AdbProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void AdbProcessErrorOccurred(QProcess::ProcessError);

protected:
    bool running_ = false;
    bool connected_ = false;
    bool hasErrors_ = false;
    QProcess *process_;
    QString execPath_;
};

#endif // ADBPROCESS_H
