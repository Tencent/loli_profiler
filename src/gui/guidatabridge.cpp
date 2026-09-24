// GuiDataBridge — Qt-aware seam between core profiling and the Qt-free ImGui GUI.
// See guidatabridge.h for the architecture note.

#include "guidatabridge.h"

#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "configdialog.h"
#include "meminfoprocess.h"
#include "screenshotprocess.h"
#include "startappprocess.h"
#include "addressprocess.h"
#include "pathutils.h"

namespace gui {

// Must match src/mainwindow.cpp serialization.
static const quint32 kAppMagic   = 0xA4B3C2D1;
static const qint32  kAppVersion = 106;

// Result produced by the background record loader. Moved into the bridge on
// completion (see OnRecordLoaded).
struct GuiDataBridge::LoadResult {
    bool ok = false;
    QString path;
    QString fileName;
    QVector<StackRecord> records;
    QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap;
    QHash<QString, QHash<quint64, QString>> symbolMap;
    QVector<QVector<QPair<double,double>>> memSeries;
    QVector<QPair<qint32, QByteArray>> screenshots;
    QHash<QString, SMapsSection> sMapsSections;
};

GuiDataBridge::GuiDataBridge(QObject* parent)
    : QObject(parent)
{
    startAppProcess_   = new StartAppProcess(this);
    screenshotProcess_ = new ScreenshotProcess(this);
    memInfoProcess_    = new MemInfoProcess(this);
    stacktraceProcess_ = new StackTraceProcess(this);

    mainTimer_ = new QTimer(this);
    mainTimer_->setInterval(1000);

    working_   = std::make_shared<GuiSnapshot>();
    published_ = working_;

    connect(mainTimer_, &QTimer::timeout, this, &GuiDataBridge::FixedUpdate);
    connect(stacktraceProcess_, &StackTraceProcess::DataReceived,
            this, &GuiDataBridge::OnStacktraceData);
    connect(stacktraceProcess_, &StackTraceProcess::ConnectionLost,
            this, &GuiDataBridge::OnStacktraceConnectionLost);
    connect(memInfoProcess_, &MemInfoProcess::ProcessFinished,
            this, &GuiDataBridge::OnMemInfoFinished);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessFinished,
            this, &GuiDataBridge::OnScreenshotFinished);
}

GuiDataBridge::~GuiDataBridge() = default;

void GuiDataBridge::AppendLog(const QString& line) {
    if (working_)
        working_->logLines.push_back(line.toStdString());
}

// ---------------------------------------------------------------------------
// snapshot publish / acquire
// ---------------------------------------------------------------------------
void GuiDataBridge::PublishSnapshot() {
    QMutexLocker lock(&publishMutex_);
    published_ = working_;
    ++publishVersion_;
}

std::shared_ptr<const GuiSnapshot> GuiDataBridge::AcquireSnapshot() const {
    QMutexLocker lock(&publishMutex_);
    return published_;
}

uint64_t GuiDataBridge::SnapshotVersion() const {
    QMutexLocker lock(&publishMutex_);
    return publishVersion_;
}

bool GuiDataBridge::IsLoading() const { return loading_.load(); }
float GuiDataBridge::LoadProgress() const { return loadProgress_.load(); }
QString GuiDataBridge::LoadStatus() const {
    QMutexLocker lock(&loadStatusMutex_);
    return loadStatus_;
}

// ---------------------------------------------------------------------------
// device / app enumeration
// ---------------------------------------------------------------------------
QVector<DeviceInfoLite> GuiDataBridge::EnumerateDevices() {
    QVector<DeviceInfoLite> out;
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    process.setArguments(QStringList() << "devices" << "-l");
    process.start();
    if (!process.waitForFinished(5000))
        return out;
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const auto lines = output.split('\n', Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); i++) {  // skip "List of devices attached"
        const auto& line = lines[i];
        if (line.startsWith('*'))
            continue;
        const auto parts = line.simplified().split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        DeviceInfoLite d;
        d.serial = parts.value(0);
        d.state  = parts.value(1);
        for (const auto& p : parts) {
            if (p.startsWith("model:"))  d.model  = p.mid(6);
            if (p.startsWith("device:")) d.device = p.mid(7);
        }
        out.push_back(d);
    }
    return out;
}

QStringList GuiDataBridge::ListInstalledApps(const QString& deviceSerial) {
    QStringList out;
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    QStringList args;
    if (!deviceSerial.isEmpty())
        args << "-s" << deviceSerial;
    args << "shell" << "pm" << "list" << "packages" << "-3";
    process.setArguments(args);
    process.start();
    if (!process.waitForFinished(10000))
        return out;
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const auto lines = output.split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        QString pkg = line.simplified();
        if (pkg.startsWith("package:"))
            pkg = pkg.mid(8);
        if (!pkg.isEmpty())
            out << pkg;
    }
    out.sort();
    return out;
}

// ---------------------------------------------------------------------------
// capture lifecycle
// ---------------------------------------------------------------------------
bool GuiDataBridge::StartCapture(const QString& deviceSerial,
                                 const QString& appName,
                                 const QString& subProcessName,
                                 const CaptureConfigSnapshot& config) {
    if (isCapturing_)
        return false;

    deviceSerial_   = deviceSerial;
    appName_        = appName;
    subProcessName_ = subProcessName;

    // Persist capture config, then read back what the core will use.
    SaveCaptureConfig(config);
    const ConfigDialog::Settings settings = ConfigDialog::GetCurrentSettings();

    // Fresh working snapshot for the session.
    working_ = std::make_shared<GuiSnapshot>();
    working_->capture.capturing = true;
    working_->capture.connected = false;
    working_->capture.appName = appName.toStdString();
    working_->capture.deviceSerial = deviceSerial.toStdString();
    PublishSnapshot();

    time_ = 0;
    lastScreenshotTime_ = 0;

    AppendLog(QString("Launching %1 on %2 ...").arg(appName, deviceSerial));

    const QString adbPath = PathUtils::GetADBExecutablePath();
    const QString pythonPath = PathUtils::GetPythonExecutablePath();
    if (adbPath.isEmpty() || !QFile::exists(adbPath)) {
        AppendLog("ADB not found. Configure the SDK path in settings.");
        working_->capture.capturing = false;
        PublishSnapshot();
        return false;
    }

    stacktraceProcess_->SetDeviceSerial(deviceSerial_);
    startAppProcess_->SetDeviceSerial(deviceSerial_);
    memInfoProcess_->SetDeviceSerial(deviceSerial_);
    screenshotProcess_->SetDeviceSerial(deviceSerial_);

    stacktraceProcess_->ForwardPort(8000);
    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath);
    startAppProcess_->StartApp(appName_, subProcessName_,
                               settings.compiler_, settings.arch_,
                               /*interceptMode=*/false, /*dialog=*/nullptr);

    isCapturing_ = true;
    mainTimer_->start();
    return true;
}

void GuiDataBridge::StopCapture() {
    if (!isCapturing_)
        return;
    isCapturing_ = false;
    mainTimer_->stop();
    stacktraceProcess_->Disconnect();
    if (screenshotProcess_->IsRunning())
        screenshotProcess_->Process()->kill();

    if (working_) {
        working_->capture.capturing = false;
        working_->capture.connected = false;
    }
    PublishSnapshot();
}

void GuiDataBridge::FixedUpdate() {
    if (!isCapturing_)
        return;
    if (time_ - lastScreenshotTime_ >= 5 && !screenshotProcess_->IsRunning()) {
        lastScreenshotTime_ = time_;
        screenshotProcess_->SetDeviceSerial(deviceSerial_);
        screenshotProcess_->CaptureScreenshot();
    }
    if (!memInfoProcess_->IsRunning() && !memInfoProcess_->HasErrors()) {
        memInfoProcess_->SetDeviceSerial(deviceSerial_);
        memInfoProcess_->DumpMemInfoAsync(appName_, subProcessName_);
    }
    if (!stacktraceProcess_->IsConnecting() && !stacktraceProcess_->IsConnected()) {
        stacktraceProcess_->ConnectToServer(8000);
        AppendLog("Connecting to application server ...");
    }
    time_++;

    // Throttled publish so the UI refreshes ~1 Hz during capture.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastPublishMs_ >= 1000) {
        lastPublishMs_ = now;
        if (working_)
            working_->capture.elapsedMs = time_ * 1000;
        PublishSnapshot();
    }
}

// ---------------------------------------------------------------------------
// core signal handlers (run on Qt thread) — mutate working_, publish is throttled
// ---------------------------------------------------------------------------
void GuiDataBridge::OnStacktraceData() {
    if (!working_)
        return;
    const auto& stacks = stacktraceProcess_->GetStackInfo();
    const bool isNoStack = ConfigDialog::IsNoStackMode();
    for (const auto& stack : stacks) {
        RecordSnapshot rs;
        rs.seq = stack.seq_;
        rs.timeMs = static_cast<int32_t>(stack.time_);
        rs.size = static_cast<int32_t>(stack.size_);
        rs.addr = stack.addr_;
        rs.funcAddr = 0;
        rs.library = isNoStack ? stack.library_.Get().toStdString() : std::string();
        working_->records.push_back(std::move(rs));
        // frames are resolved on demand for capture; left empty here.
        working_->recordFrames.emplace_back();
    }
    working_->capture.recordCount = working_->records.size();
    working_->capture.connected = stacktraceProcess_->IsConnected();
}

void GuiDataBridge::OnStacktraceConnectionLost() {
    AppendLog("Stacktrace connection lost.");
    if (working_)
        working_->capture.connected = false;
    PublishSnapshot();
}

void GuiDataBridge::OnMemInfoFinished() {
    if (!working_)
        return;
    const auto& info = memInfoProcess_->GetMemInfo();
    MemInfoSample s;
    s.timeMs     = time_ * 1000;
    s.total      = info.Total;
    s.nativeHeap = info.NativeHeap;
    s.gfxDev     = info.GfxDev;
    s.eglMtrack  = info.EGLmtrack;
    s.glMtrack   = info.GLmtrack;
    s.unknown    = info.Unknown;
    working_->memTimeline.push_back(s);
}

void GuiDataBridge::OnScreenshotFinished() {
    if (!working_)
        return;
    const QByteArray bytes = screenshotProcess_->GetScreenshotBytes();
    if (bytes.isEmpty())
        return;
    ScreenshotSnapshot ss;
    ss.timeMs = time_ * 1000;
    ss.jpegBytes.assign(bytes.begin(), bytes.end());
    working_->screenshots.push_back(std::move(ss));
    PublishSnapshot();  // screenshots are cheap; publish promptly
}

void GuiDataBridge::OnStartAppFinished() {}
void GuiDataBridge::OnStartAppError() {
    AppendLog("Failed to start application.");
}

// ---------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------
CaptureConfigSnapshot GuiDataBridge::GetCaptureConfig() const {
    const auto s = ConfigDialog::GetCurrentSettings();
    CaptureConfigSnapshot c;
    c.threshold = s.threshold_;
    c.mode      = s.mode_.toStdString();
    c.build     = s.build_.toStdString();
    c.type      = s.type_.toStdString();
    c.arch      = s.arch_.toStdString();
    c.compiler  = s.compiler_.toStdString();
    c.hook      = s.hook_.toStdString();
    for (const auto& w : s.whitelist_) c.whitelist.push_back(w.toStdString());
    for (const auto& b : s.blacklist_) c.blacklist.push_back(b.toStdString());
    return c;
}

void GuiDataBridge::SaveCaptureConfig(const CaptureConfigSnapshot& config) {
    ConfigDialog::Settings s = ConfigDialog::GetCurrentSettings();
    s.threshold_ = config.threshold;
    s.mode_      = QString::fromStdString(config.mode);
    s.build_     = QString::fromStdString(config.build);
    s.type_      = QString::fromStdString(config.type);
    s.arch_      = QString::fromStdString(config.arch);
    s.compiler_  = QString::fromStdString(config.compiler);
    s.hook_      = QString::fromStdString(config.hook);
    s.whitelist_.clear();
    for (const auto& w : config.whitelist) s.whitelist_ << QString::fromStdString(w);
    s.blacklist_.clear();
    for (const auto& b : config.blacklist) s.blacklist_ << QString::fromStdString(b);
    ConfigDialog::SetCurrentSettings(s);
    AppendLog("Capture configuration saved.");
}

// ---------------------------------------------------------------------------
// record file I/O (async load reusing the exact .loli QDataStream layout)
// ---------------------------------------------------------------------------
void GuiDataBridge::LoadRecord(const QString& path) {
    if (loading_.load())
        return;  // a load is already in flight
    loading_.store(true);
    loadProgress_.store(0.0f);
    {
        QMutexLocker lock(&loadStatusMutex_);
        loadStatus_ = "Opening...";
    }

    std::atomic<float>* progress = &loadProgress_;
    auto future = QtConcurrent::run([path, progress]() -> std::shared_ptr<LoadResult> {
        auto result = std::make_shared<LoadResult>();
        result->path = path;
        result->fileName = QFileInfo(path).fileName();

        QFile file(path);
        if (!file.open(QFile::ReadOnly))
            return result;
        const qint64 fileSize = file.size();
        QDataStream stream(&file);

        quint32 magic;
        stream >> magic;
        if (magic != kAppMagic)
            return result;
        qint32 version;
        stream >> version;
        if (version != kAppVersion)
            return result;

        auto setProgress = [&](qint64 pos) {
            if (fileSize > 0)
                progress->store((float)pos / (float)fileSize);
        };

        // meminfo series
        qint32 maxMemInfoValue;
        stream >> maxMemInfoValue;
        qint32 seriesCount;
        stream >> seriesCount;
        result->memSeries.resize(seriesCount);
        for (int i = 0; i < seriesCount; i++) {
            int pointsCount;
            stream >> pointsCount;
            auto& series = result->memSeries[i];
            series.reserve(pointsCount);
            for (int j = 0; j < pointsCount; j++) {
                QPointF point;
                stream >> point;
                series.push_back(qMakePair(point.x(), point.y()));
            }
        }
        setProgress(file.pos());

        // string intern table (shared static; read on worker is safe because
        // we rebuild it on the Qt thread before use — here we just consume).
        QHash<quint32, QString> hashmap;
        stream >> hashmap;
        HashString::hashmap_ = hashmap;
        setProgress(file.pos());

        // records
        qint32 value;
        stream >> value;
        result->records.reserve(value);
        for (int i = 0; i < value; i++) {
            StackRecord record;
            QString str;
            stream >> str;
            record.uuid_ = QUuid::fromString(str);
            stream >> record.seq_ >> record.time_ >> record.size_
                   >> record.addr_ >> record.funcAddr_ >> record.library_.hashcode_;
            result->records.push_back(record);
            if ((i & 0xFFFF) == 0)
                setProgress(file.pos());
        }

        // callstack map
        stream >> value;
        result->callStackMap.reserve(value);
        for (int i = 0; i < value; i++) {
            QString uuid;
            qint32 len;
            stream >> uuid >> len;
            QVector<QPair<HashString, quint64>> callstack;
            callstack.reserve(len);
            for (int j = 0; j < len; j++) {
                QPair<HashString, quint64> pair;
                stream >> pair.first.hashcode_ >> pair.second;
                callstack.push_back(pair);
            }
            result->callStackMap.insert(QUuid::fromString(uuid), callstack);
            if ((i & 0x3FFF) == 0)
                setProgress(file.pos());
        }

        // symbol map
        stream >> value;
        for (int i = 0; i < value; i++) {
            QString lib;
            stream >> lib;
            qint32 size;
            stream >> size;
            auto& map = result->symbolMap[lib];
            map.reserve(size);
            for (int j = 0; j < size; j++) {
                quint64 key;
                QString name;
                stream >> key >> name;
                map[key] = name;
            }
        }
        setProgress(file.pos());

        // freeaddr map (read + discard; only used during capture filtering)
        stream >> value;
        for (int i = 0; i < value; i++) {
            quint64 addr; quint32 seq;
            stream >> addr >> seq;
        }

        // screenshots
        stream >> value;
        result->screenshots.reserve(value);
        for (int i = 0; i < value; i++) {
            qint32 time;
            QByteArray ba;
            stream >> time >> ba;
            result->screenshots.push_back(qMakePair(time, ba));
        }

        // smaps
        stream >> value;
        result->sMapsSections.reserve(value);
        for (int i = 0; i < value; i++) {
            QString name;
            stream >> name;
            SMapsSection section;
            qint32 size;
            stream >> size;
            section.addrs_.reserve(size);
            for (int j = 0; j < size; j++) {
                quint64 start, end, offset;
                stream >> start >> end >> offset;
                section.addrs_.push_back(SMapsSectionAddr(start, end, offset));
            }
            stream >> section.virtual_ >> section.rss_ >> section.pss_
                   >> section.privateClean_ >> section.privateDirty_
                   >> section.sharedClean_ >> section.sharedDirty_;
            result->sMapsSections.insert(name, section);
        }

        setProgress(fileSize);
        result->ok = true;
        return result;
    });

    auto* watcher = new QFutureWatcher<std::shared_ptr<LoadResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        OnRecordLoaded();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void GuiDataBridge::OnRecordLoaded() {
    // The sender is the QFutureWatcher we created in LoadRecord. qobject_cast
    // on the templated watcher requires Q_OBJECT, so use static_cast — we own
    // the sender and know its exact type.
    auto* watcher = static_cast<QFutureWatcher<std::shared_ptr<LoadResult>>*>(sender());
    std::shared_ptr<LoadResult> result = watcher ? watcher->result() : nullptr;

    loading_.store(false);
    if (!result || !result->ok) {
        AppendLog("Failed to load record file.");
        {
            QMutexLocker lock(&loadStatusMutex_);
            loadStatus_.clear();
        }
        return;
    }

    // Build a fresh snapshot from the loaded data (Qt thread now).
    auto snap = std::make_shared<GuiSnapshot>();

    const auto& records = result->records;
    snap->records.reserve(records.size());
    snap->recordFrames.resize(records.size());

    // resolve symbols helper
    const auto& symbolMap = result->symbolMap;
    auto resolve = [&](const QString& lib, quint64 addr) -> QString {
        auto libIt = symbolMap.find(lib);
        if (libIt != symbolMap.end()) {
            auto nameIt = libIt.value().find(addr);
            if (nameIt != libIt.value().end() && !nameIt.value().isEmpty())
                return nameIt.value();
        }
        return QString("0x%1").arg(addr, 0, 16);
    };

    for (int i = 0; i < records.size(); i++) {
        const auto& r = records[i];
        RecordSnapshot rs;
        rs.seq = r.seq_;
        rs.timeMs = r.time_;
        rs.size = r.size_;
        rs.addr = r.addr_;
        rs.funcAddr = r.funcAddr_;
        rs.library = r.library_.Get().toStdString();
        snap->records.push_back(std::move(rs));

        auto csIt = result->callStackMap.find(r.uuid_);
        if (csIt != result->callStackMap.end()) {
            const auto& callstack = csIt.value();
            auto& frames = snap->recordFrames[i];
            frames.reserve(callstack.size());
            for (int j = callstack.size() - 1; j >= 0; j--) {
                const QString lib = callstack[j].first.Get();
                const quint64 addr = callstack[j].second;
                StackFrameSnapshot f;
                f.library = lib.toStdString();
                f.funcAddr = addr;
                f.funcName = resolve(lib, addr).toStdString();
                frames.push_back(std::move(f));
            }
        }
    }

    // meminfo series -> timeline
    if (!result->memSeries.isEmpty()) {
        const auto& s0 = result->memSeries[0];
        int count = s0.size();
        snap->memTimeline.reserve(count);
        for (int j = 0; j < count; j++) {
            MemInfoSample s;
            s.timeMs = static_cast<int32_t>(s0.value(j).first);
            s.total      = (uint32_t)result->memSeries.value(0).value(j).second;
            s.nativeHeap = (uint32_t)result->memSeries.value(1).value(j).second;
            s.gfxDev     = (uint32_t)result->memSeries.value(2).value(j).second;
            s.eglMtrack  = (uint32_t)result->memSeries.value(3).value(j).second;
            s.glMtrack   = (uint32_t)result->memSeries.value(4).value(j).second;
            s.unknown    = (uint32_t)result->memSeries.value(5).value(j).second;
            snap->memTimeline.push_back(s);
        }
    }

    // screenshots
    snap->screenshots.reserve(result->screenshots.size());
    for (const auto& sc : result->screenshots) {
        ScreenshotSnapshot ss;
        ss.timeMs = sc.first;
        ss.jpegBytes.assign(sc.second.begin(), sc.second.end());
        snap->screenshots.push_back(std::move(ss));
    }

    // smaps
    snap->smaps.reserve(result->sMapsSections.size());
    for (auto it = result->sMapsSections.begin(); it != result->sMapsSections.end(); ++it) {
        SMapsSectionSnapshot s;
        s.name = it.key().toStdString();
        s.virtualSize = it.value().virtual_;
        s.rss = it.value().rss_;
        s.pss = it.value().pss_;
        s.sharedClean = it.value().sharedClean_;
        s.sharedDirty = it.value().sharedDirty_;
        s.privateClean = it.value().privateClean_;
        s.privateDirty = it.value().privateDirty_;
        snap->smaps.push_back(std::move(s));
    }

    snap->capture.capturing = false;
    snap->capture.connected = false;
    snap->capture.recordCount = snap->records.size();
    snap->capture.appName = result->fileName.toStdString();
    snap->logLines = working_ ? working_->logLines : std::vector<std::string>{};

    working_ = snap;
    PublishSnapshot();

    {
        QMutexLocker lock(&loadStatusMutex_);
        loadStatus_.clear();
    }
    AppendLog(QString("Loaded %1 (%2 records).")
                  .arg(result->fileName)
                  .arg(snap->records.size()));
}

bool GuiDataBridge::SaveRecord(const QString& path) {
    // Deferred: writing .loli from the bridge requires the same serialization
    // as MainWindow::SaveToFile. Left unimplemented until capture-save is wired.
    (void)path;
    return false;
}

} // namespace gui
