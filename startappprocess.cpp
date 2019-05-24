#include "startappprocess.h"
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

StartAppProcess::StartAppProcess(QObject* parent)
    : AdbProcess(parent) {

}

void StartAppProcess::StartApp(const QString& appName) {
    QStringList arguments;
    arguments << "shell" << "monkey -p" << appName << "-c android.intent.category.LAUNCHER 1";
    ExecuteAsync(arguments);
}

void StartAppProcess::OnProcessFinihed() {
    startResult_ = false;

    QString retStr = process_->readAll();
    process_->close();

    QTextStream stream(&retStr);
    QString line;
    while(stream.readLineInto(&line)) {
        if (line.contains("Events injected: 1"))
        {
            startResult_ = true;
            break;
        }
    }
}
