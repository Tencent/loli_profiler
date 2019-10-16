#ifndef STARTAPPPROCESS_H
#define STARTAPPPROCESS_H

#include "adbprocess.h"

class QProgressDialog;
class StartAppProcess : public AdbProcess {
public:
    StartAppProcess(QObject* parent = nullptr);

    void SetPythonPath(const QString& path) {
        pythonPath_ = path;
    }

    void SetJDWPPort(int port) {
        jdwpPort_ = port;
    }

    bool Result() const {
        return startResult_;
    }

    QString ErrorStr() const {
        return errorStr_;
    }

    void StartApp(const QString& appName, const QString& arch, QProgressDialog* dialog);

protected:
    void OnProcessFinihed() override;
    void OnProcessErrorOccurred() override;

private:
    bool startResult_ = false;
    QString errorStr_;
    QString pythonPath_;
    int jdwpPort_ = 8700;
};

#endif // STARTAPPPROCESS_H
