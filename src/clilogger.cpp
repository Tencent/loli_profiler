#include "clilogger.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QMutexLocker>
#include <iostream>

CliLogger& CliLogger::Instance() {
    static CliLogger instance;
    return instance;
}

CliLogger::CliLogger() : stream_(nullptr), initialized_(false) {
}

CliLogger::~CliLogger() {
    Close();
}

void CliLogger::Init(const QString& logPath) {
    QMutexLocker locker(&mutex_);
    
    if (initialized_) {
        return;
    }
    
    logFile_.setFileName(logPath);
    if (logFile_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        stream_ = new QTextStream(&logFile_);
        initialized_ = true;
        
        QString startMsg = QString("=== LoliProfiler CLI Log Started at %1 ===")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        *stream_ << startMsg << endl;
        stream_->flush();
        
        // Also try stdout
        std::cout << startMsg.toStdString() << std::endl;
        std::cout.flush();
    } else {
        // Can't write to log file, at least try stderr
        std::cerr << "FATAL: Could not open log file: " << logPath.toStdString() << std::endl;
    }
}

void CliLogger::Log(const QString& message) {
    QMutexLocker locker(&mutex_);
    
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString logLine = QString("[%1] %2").arg(timestamp).arg(message);
    
    // Write to file
    if (initialized_ && stream_) {
        *stream_ << logLine << endl;
        stream_->flush();
    }
    
    // Also write to stdout
    std::cout << logLine.toStdString() << std::endl;
    std::cout.flush();
}

void CliLogger::Error(const QString& message) {
    QMutexLocker locker(&mutex_);
    
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString logLine = QString("[%1] ERROR: %2").arg(timestamp).arg(message);
    
    // Write to file
    if (initialized_ && stream_) {
        *stream_ << logLine << endl;
        stream_->flush();
    }
    
    // Also write to stderr
    std::cerr << logLine.toStdString() << std::endl;
    std::cerr.flush();
}

void CliLogger::Close() {
    QMutexLocker locker(&mutex_);
    
    if (initialized_) {
        if (stream_) {
            QString endMsg = QString("=== LoliProfiler CLI Log Ended at %1 ===")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
            *stream_ << endMsg << endl;
            delete stream_;
            stream_ = nullptr;
        }
        logFile_.close();
        initialized_ = false;
    }
}
