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

#include <atomic>
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
    // LoadRecord starts an asynchronous load on a worker thread and returns
    // immediately. Poll IsLoading()/LoadProgress() from the UI; when IsLoading()
    // flips to false the new data is published. This keeps the UI responsive on
    // very large records (hundreds of MB).
    void LoadRecord(const QString& path);
    bool IsLoading() const;
    // 0..1 progress and a short status line while loading.
    float LoadProgress() const;
    QString LoadStatus() const;
    bool SaveRecord(const QString& path);

    // ---- config ----
    CaptureConfigSnapshot GetCaptureConfig() const;
    void SaveCaptureConfig(const CaptureConfigSnapshot& config);

    // ---- snapshot access (called from ImGui render thread) ----
    // Cheap: returns a shared pointer to the current immutable snapshot. The
    // bridge swaps in a NEW snapshot object only when data actually changes
    // (on load completion, or on a throttled capture tick), never per frame.
    // Safe to hold across a frame; do not store long-term.
    std::shared_ptr<const GuiSnapshot> AcquireSnapshot() const;

    // Monotonic counter bumped each time the published snapshot changes.
    // Panels compare against a cached value to know when to rebuild views.
    uint64_t SnapshotVersion() const;

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
    void OnRecordLoaded();

private:
    void AppendLog(const QString& line);
    void PublishSnapshot();  // swap working copy into the published slot

private:
    // Core processes (owned)
    StartAppProcess*    startAppProcess_ = nullptr;
    ScreenshotProcess*  screenshotProcess_ = nullptr;
    MemInfoProcess*     memInfoProcess_ = nullptr;
    StackTraceProcess*  stacktraceProcess_ = nullptr;
    QVector<AddressProcess*> addrProcesses_;
    QTimer*             mainTimer_ = nullptr;

    // ---- capture live state (Qt thread) ----
    // Working copy that capture ticks accumulate into; published periodically.
    std::shared_ptr<GuiSnapshot> working_;
    // ---- published snapshot (read via AcquireSnapshot) ----
    mutable QMutex      publishMutex_;
    std::shared_ptr<const GuiSnapshot> published_;
    uint64_t            publishVersion_ = 0;
    // throttle capture publishes to ~4 Hz
    qint64              lastPublishMs_ = 0;

    // ---- async record loading ----
    struct LoadResult;  // pimpl-ish, defined in cpp
    std::atomic<bool>     loading_{false};
    std::atomic<float>    loadProgress_{0.0f};
    QString               loadStatus_;
    mutable QMutex        loadStatusMutex_;

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
