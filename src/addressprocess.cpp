#include "addressprocess.h"

#include <QTextStream>
#include <QDir>
#include <QDebug>

AddressProcess::AddressProcess(QObject* parent)
    : AdbProcess(parent) {}

void AddressProcess::DumpAsync(const QString& symbloFile, QStringList addrs, QHash<QString, QString>* addrMap) {
    convertedCount_ = 0;
    addrMap_ = addrMap;
    addrs_ = addrs;
    QStringList arguments;
    arguments << "-f" << "-C" << "-e" << symbloFile << addrs_;
    ExecuteAsync(arguments);
}

void AddressProcess::OnProcessFinihed() {
    QString retStr = process_->readAll();
    process_->close();

    QTextStream stream(&retStr);
    QString line;
    int index = -1;
    convertedCount_ = 0;
    while (stream.readLineInto(&line)) {
        index++;
        if (index % 2 != 0) // skip even lines for now, they store file & line info
            continue;
        if (line.startsWith('?') || line.size() == 0)
            continue;
        (*addrMap_)[addrs_[convertedCount_]] = line;
        convertedCount_++;
    }
    addrMap_ = nullptr;
}
