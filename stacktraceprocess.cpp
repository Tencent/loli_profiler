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

enum loliFlags {
    FREE_ = 0,
    MALLOC_ = 1,
    CALLOC_ = 2,
};

void StackTraceProcess::OnProcessFinihed() {
    stackInfo_.clear();
    freeInfo_.clear();

    QString retStr = process_->readAll();
    process_->close();

    QTextStream stream(&retStr);
    QString line;
    while (stream.readLineInto(&line)) {
        auto words = line.split(',');
        if (words.size() == 0)
            continue;
        auto type = words[0].toInt();
        if (type == FREE_) {
            if (words.size() < 3)
                continue;
            freeInfo_.push_back(qMakePair(words[1].toInt(), words[2]));
        } else {
            words.removeAt(0);
            stackInfo_.push_back(words);
        }
    }
}
