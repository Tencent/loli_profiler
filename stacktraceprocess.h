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

    const QVector<QPair<int, QString>>& GetFreeInfo() const {
        return freeInfo_;
    }

    void DumpAsync(const QString& appIdentifier);

protected:
    void OnProcessFinihed() override;

private:
    QString appIdentifier_;
    QVector<QStringList> stackInfo_;
    QVector<QPair<int, QString>> freeInfo_;
};

#endif // STACKTRACEPROCESS_H
