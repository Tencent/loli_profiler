#ifndef CLIPROFILER_H
#define CLIPROFILER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QHash>
#include <QUuid>
#include <QSet>

#include "screenshotprocess.h"
#include "meminfoprocess.h"
#include "stacktraceprocess.h"
#include "stacktracemodel.h"
#include "startappprocess.h"
#include "smaps/smapssection.h"

class CliProfiler : public QObject {
    Q_OBJECT
public:
    explicit CliProfiler(QObject *parent = nullptr);
    ~CliProfiler() override;

    struct CliOptions {
        QString appName;
        QString subProcessName;
        QString outputFile;
        QString symbolPath;
        QString deviceSerial;
        int duration = 0;  // seconds, 0 means wait for process exit
        bool attachMode = false;
        bool verbose = false;
    };

    bool Initialize(const CliOptions& options);
    void Start();

signals:
    void Finished(int exitCode);

private slots:
    void OnFixedUpdate();
    void OnStartAppProcessFinished(AdbProcess* process);
    void OnStartAppProcessErrorOccurred();
    void OnMemInfoProcessFinished(AdbProcess* process);
    void OnMemInfoProcessErrorOccurred();
    void OnScreenshotProcessFinished(AdbProcess* process);
    void OnScreenshotProcessErrorOccurred();
    void OnStacktraceDataReceived();
    void OnStacktraceConnectionLost();
    void OnDurationTimeout();
    void OnProcessExitCheckTimeout();

private:
    void Print(const QString& str);
    void PrintError(const QString& str);
    void ConnectionFailed();
    void StopCaptureProcess();
    void SaveToFile(QFile *file);
    void ReadSMapsFile(QFile* file);
    void ReadStacktraceData(const QVector<RawStackInfo>& stacks);
    void ReadStacktraceDataCache();
    void PushEmptySMapsFile();
    
    struct StacktraceData {
        QVector<QPair<HashString, quint64>> records_;
        QVector<QString> libraries_;
    };
    
    StacktraceData InterpretRecordsLibrary(int start, int count);
    void InterpretRecordLibrary(StackRecord& record, StacktraceData& data);
    void InterpretStacktraceData();
    bool LoadSymbolFile(const QString& symbolPath);
    void Cleanup(int exitCode);

private:
    CliOptions options_;
    
    // Profiling state
    bool isConnected_ = false;
    bool isCapturing_ = false;
    int time_ = 0;
    int lastScreenshotTime_ = 0;
    int maxMemInfoValue_ = 128;
    bool showJDWPErrorLog_ = false;
    
    // Processes
    StartAppProcess *startAppProcess_;
    ScreenshotProcess *screenshotProcess_;
    StackTraceProcess *stacktraceProcess_;
    MemInfoProcess *memInfoProcess_;
    
    // Timers
    QTimer* mainTimer_;
    QTimer* smapsTimer_;
    QTimer* durationTimer_;
    QTimer* processExitCheckTimer_;
    
    // Data structures
    StackTraceModel *stacktraceModel_;
    QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap_;
    QSet<QString> libraries_;
    QVector<StackRecord> recordsCache_;
    QHash<quint64, quint32> freeAddrMap_;
    QHash<QString, QHash<quint64, QString>> symbloMap_;
    QVector<QPair<int, QByteArray>> screenshots_;
    QHash<QString, SMapsSection> sMapsSections_;
    
    // Memory series data
    struct MemInfoPoint {
        int time;
        int total;
        int nativeHeap;
        int gfxDev;
        int eglMtrack;
        int glMtrack;
        int unknown;
    };
    QVector<MemInfoPoint> memInfoData_;
};

#endif // CLIPROFILER_H
