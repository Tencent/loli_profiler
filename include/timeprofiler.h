#ifndef TIMEPROFILER_H
#define TIMEPROFILER_H

#include <chrono>
#include <vector>
#include <QDebug>
#include <QString>

using sclock = std::chrono::steady_clock;

struct TimerProfiler{
    TimerProfiler(const QString &msg) : msg_(msg) {
        startTime_ = sclock::now();
    }
    ~TimerProfiler() {
        auto ms = std::chrono::duration<double, std::milli>(sclock::now() - startTime_);
        qDebug() << msg_ << " " << QString::number(ms.count()) << " ms" << endl;
    }
private:
    std::chrono::time_point<std::chrono::steady_clock> startTime_;
    QString msg_;
};

#endif // TIMEPROFILER_H
