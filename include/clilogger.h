#ifndef CLILOGGER_H
#define CLILOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>

class CliLogger {
public:
    static CliLogger& Instance();
    
    void Init(const QString& logPath);
    void Log(const QString& message);
    void Error(const QString& message);
    void Close();
    
private:
    CliLogger();
    ~CliLogger();
    
    QFile logFile_;
    QTextStream* stream_;
    QMutex mutex_;
    bool initialized_;
};

// Convenience macros
#define CLI_LOG(msg) CliLogger::Instance().Log(msg)
#define CLI_ERROR(msg) CliLogger::Instance().Error(msg)

#endif // CLILOGGER_H
