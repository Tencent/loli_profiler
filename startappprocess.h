#ifndef STARTAPPPROCESS_H
#define STARTAPPPROCESS_H

#include "adbprocess.h"

class StartAppProcess : public AdbProcess {
public:
    StartAppProcess(QObject* parent = nullptr);

    bool Result() const {
        return startResult_;
    }

    void StartApp(const QString& appName);

protected:
    void OnProcessFinihed() override;

private:
    bool startResult_ = false;
};

#endif // STARTAPPPROCESS_H
