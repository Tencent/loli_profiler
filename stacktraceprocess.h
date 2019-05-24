#ifndef STACKTRACEPROCESS_H
#define STACKTRACEPROCESS_H

#include "adbprocess.h"
#include <QVector>

class StackTraceProcess : public AdbProcess {
public:
    StackTraceProcess(QObject* parent = nullptr);

    const QVector<QStringList>& GetStackInfo() const {
        return stackInfo_;
    }

    void DumpAsync(const QString& appIdentifier);

protected:
    void OnProcessFinihed() override;

private:
    QString appIdentifier_;
    QVector<QStringList> stackInfo_;
};

#endif // STACKTRACEPROCESS_H
