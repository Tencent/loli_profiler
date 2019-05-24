#ifndef ADDRESSPROCESS_H
#define ADDRESSPROCESS_H

#include "adbprocess.h"
#include <QVector>
#include <QHash>

class AddressProcess : public AdbProcess {
public:
    AddressProcess(QObject* parent = nullptr);

    int GetConvertedCount() const { return convertedCount_; }

    void DumpAsync(const QString& symbloFile, QHash<QString, QString>* addrMap);

protected:
    void OnProcessFinihed() override;

private:
    QHash<QString, QString>* addrMap_;
    QStringList addrs_;
    int convertedCount_ = 0;
};

#endif // ADDRESSPROCESS_H
