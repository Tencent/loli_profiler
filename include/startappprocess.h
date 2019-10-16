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
};

#endif // STARTAPPPROCESS_H
