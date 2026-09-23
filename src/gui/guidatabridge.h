#ifndef LOLI_PROFILER_GUI_GUIDATABRIDGE_H
#define LOLI_PROFILER_GUI_GUIDATABRIDGE_H

// GuiDataBridge — the ONLY Qt-aware file in the ImGui GUI target.
//
// It owns the core profiling processes (ADB, stacktrace, meminfo, screenshot,
// address resolution) and converts their Qt-signal output into Qt-free POD
// snapshots (guisnapshot.h) that the ImGui panels consume. Panels never touch
// Qt; they call bridge methods and read the latest GuiSnapshot.
//
// Threading: core processes emit signals on the Qt event thread. The bridge
// copies data into a mutex-protected snapshot store; the ImGui render thread
// calls UpdateSnapshot() once per frame to take a consistent copy.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QMutex>

#include <memory>

#include "guisnapshot.h"
#include "hashstring.h"
#include "stacktraceprocess.h"
#include "stacktracemodel.h"
#include "smaps/smapssection.h"

class StartAppProcess;
class ScreenshotProcess;
class MemInfoProcess;
class AddressProcess;
class QTimer;

namespace gui {

// Lightweight device info for enumeration (Qt-side; converted to POD in snapshots).
struct DeviceInfoLite {
    QString serial;
    QString model;
    QString device;
    QString state;
};

class GuiDataBridge : public QObject {
    Q_OBJECT
public:
    explicit GuiDataBridge(QObject* parent = nullptr);
    ~GuiDataBridge() override;

    // ---- device / app enumeration (synchronous, cheap) ----
    QVector<DeviceInfoLite> EnumerateDevices();
    QStringList ListInstalledApps(const QString& deviceSerial);

    // ---- capture lifecycle ----
    bool StartCapture(const QString& deviceSerial,
                      const QString& appName,
                      const QString& subProcessName,
                      const CaptureConfigSnapshot& config);
    void StopCapture();

    // ---- record file I/O ----
    bool LoadRecord(const QString& path);
    bool SaveRecord(const QString& path);

    // ---- config ----
    CaptureConfigSnapshot GetCaptureConfig() const;
    void SaveCaptureConfig(const CaptureConfigSnapshot& config);

    // ---- snapshot access (called from ImGui render thread) ----
    // Copies the latest state into `out`. Thread-safe.
    void UpdateSnapshot(GuiSnapshot& out);

    bool IsCapturing() const;
    bool IsConnected() const;

private slots:
    void FixedUpdate();
    void OnStacktraceData();
    void OnStacktraceConnectionLost();
    void OnMemInfoFinished();
    void OnScreenshotFinished();
    void OnStartAppFinished();
    void OnStartAppError();

private:
    void AppendLog(const QString& line);
    void ReadStacktraceData(const QVector<RawStackInfo>& stacks);
    void RebuildCallTreeLocked();

private:
    // Core processes (owned)
    StartAppProcess*    startAppProcess_ = nullptr;
    ScreenshotProcess*  screenshotProcess_ = nullptr;
    MemInfoProcess*     memInfoProcess_ = nullptr;
    StackTraceProcess*  stacktraceProcess_ = nullptr;
    QVector<AddressProcess*> addrProcesses_;
    QTimer*             mainTimer_ = nullptr;

    // ---- protected by mutex_ (written on Qt thread, read for snapshot) ----
    mutable QMutex      mutex_;
    GuiSnapshot         snapshot_;     // the live store
    // callstack per record uuid -> resolved frames
    QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap_;
    QHash<QString, QHash<quint64, QString>> symbolMap_; // lib -> addr -> name
    QSet<QString>       libraries_;
    QVector<StackRecord> recordsCache_;
    QHash<quint64, quint32> freeAddrMap_;
    QHash<QString, SMapsSection> sMapsSections_;

    // session bookkeeping
    QString  appPid_;
    QString  appName_;
    QString  subProcessName_;
    QString  deviceSerial_;
    int      time_ = 0;
    int      lastScreenshotTime_ = 0;
    bool     isCapturing_ = false;
    bool     isConnected_ = false;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_GUIDATABRIDGE_H
