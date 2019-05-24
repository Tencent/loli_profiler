#include "stacktraceprocess.h"

#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

StackTraceProcess::StackTraceProcess(QObject* parent)
    : AdbProcess(parent) {}

void StackTraceProcess::DumpAsync(const QString& appIdentifier) {
    appIdentifier_ = appIdentifier;
    QStringList arguments;
    arguments << "shell" << "cat" << "/storage/emulated/0/Android/data/" + appIdentifier + "/files/loli.csv";
    ExecuteAsync(arguments);
}

void StackTraceProcess::OnProcessFinihed() {
    stackInfo_.clear();

    QString retStr = process_->readAll();
    process_->close();

    QTextStream stream(&retStr);
    QString line;
    while (stream.readLineInto(&line)) {
        auto words = line.split(',');
        if (words.size() == 0)
            continue;
        stackInfo_.push_back(words);
    }
}
