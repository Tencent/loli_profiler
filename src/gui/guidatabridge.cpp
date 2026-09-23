// GuiDataBridge — Qt-aware seam between core profiling and the Qt-free ImGui GUI.
// See guidatabridge.h for the architecture note.

#include "guidatabridge.h"

#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
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

GuiDataBridge::GuiDataBridge(QObject* parent)
    : QObject(parent)
{
    startAppProcess_   = new StartAppProcess(this);
    screenshotProcess_ = new ScreenshotProcess(this);
    memInfoProcess_    = new MemInfoProcess(this);
    stacktraceProcess_ = new StackTraceProcess(this);

    mainTimer_ = new QTimer(this);
    mainTimer_->setInterval(1000);

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
    QMutexLocker lock(&mutex_);
    snapshot_.logLines.push_back(line.toStdString());
    // keep the console tail bounded
    if (snapshot_.logLines.size() > 2000)
        snapshot_.logLines.erase(snapshot_.logLines.begin(),
                                 snapshot_.logLines.begin() + (snapshot_.logLines.size() - 2000));
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

    // Persist the capture config through the existing settings store so the
    // injected library + StartAppProcess pick it up (same as the Qt GUI).
    ConfigDialog::Settings settings;
    settings.threshold_ = config.threshold;
    settings.mode_      = QString::fromStdString(config.mode);
    settings.build_     = QString::fromStdString(config.build);
    settings.type_      = QString::fromStdString(config.type);
    settings.arch_      = QString::fromStdString(config.arch);
    settings.compiler_  = QString::fromStdString(config.compiler);
    settings.hook_      = QString::fromStdString(config.hook);
    for (const auto& w : config.whitelist) settings.whitelist_ << QString::fromStdString(w);
    for (const auto& b : config.blacklist) settings.blacklist_ << QString::fromStdString(b);
    // NOTE: the core persists settings via ConfigDialog; the bridge assumes the
    // caller (RunLaunchDialog) has already written them. We read them back here.
    settings = ConfigDialog::GetCurrentSettings();

    {
        QMutexLocker lock(&mutex_);
        snapshot_.capture.capturing = true;
        snapshot_.capture.connected = false;
        snapshot_.capture.appName = appName.toStdString();
        snapshot_.capture.deviceSerial = deviceSerial.toStdString();
        snapshot_.records.clear();
        snapshot_.callTree.clear();
        snapshot_.memTimeline.clear();
        snapshot_.screenshots.clear();
        snapshot_.smaps.clear();
    }
    callStackMap_.clear();
    symbolMap_.clear();
    libraries_.clear();
    recordsCache_.clear();
    freeAddrMap_.clear();
    sMapsSections_.clear();
    time_ = 0;
    lastScreenshotTime_ = 0;

    AppendLog(QString("Launching %1 on %2 ...").arg(appName, deviceSerial));

    const QString adbPath = PathUtils::GetADBExecutablePath();
    const QString pythonPath = PathUtils::GetPythonExecutablePath();
    if (adbPath.isEmpty() || !QFile::exists(adbPath)) {
        AppendLog("ADB not found. Configure the SDK path in settings.");
        QMutexLocker lock(&mutex_);
        snapshot_.capture.capturing = false;
        return false;
    }

    stacktraceProcess_->SetDeviceSerial(deviceSerial_);
    startAppProcess_->SetDeviceSerial(deviceSerial_);
    memInfoProcess_->SetDeviceSerial(deviceSerial_);
    screenshotProcess_->SetDeviceSerial(deviceSerial_);

    stacktraceProcess_->ForwardPort(8000);
    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath);
    // Launch (not attach) by default; inject/attach toggle is a RunLaunchDialog option.
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

    QMutexLocker lock(&mutex_);
    snapshot_.capture.capturing = false;
    snapshot_.capture.connected = false;
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
}

// ---------------------------------------------------------------------------
// core signal handlers (run on Qt thread)
// ---------------------------------------------------------------------------
void GuiDataBridge::OnStacktraceData() {
    QMutexLocker lock(&mutex_);
    const auto& stacks = stacktraceProcess_->GetStackInfo();
    const auto& frees  = stacktraceProcess_->GetFreeInfo();
    for (const auto& f : frees)
        freeAddrMap_.insert(f.second, f.first);

    const bool isNoStack = ConfigDialog::IsNoStackMode();
    for (const auto& stack : stacks) {
        auto it = freeAddrMap_.find(stack.addr_);
        if (it != freeAddrMap_.end() && stack.seq_ < it.value())
            continue;  // freed record

        StackRecord record;
        record.uuid_ = QUuid::createUuid();
        record.seq_  = stack.seq_;
        record.time_ = static_cast<qint32>(stack.time_);
        record.size_ = static_cast<qint32>(stack.size_);
        record.addr_ = stack.addr_;
        if (isNoStack) {
            record.library_ = HashString(stack.library_);
        } else {
            auto& callstack = callStackMap_[record.uuid_];
            for (auto addr : stack.stacktraces_)
                callstack.append(qMakePair(QString(), addr));
        }
        recordsCache_.push_back(record);

        RecordSnapshot rs;
        rs.seq = record.seq_;
        rs.timeMs = record.time_;
        rs.size = record.size_;
        rs.addr = record.addr_;
        rs.funcAddr = record.funcAddr_;
        rs.library = record.library_.Get().toStdString();
        snapshot_.records.push_back(std::move(rs));
    }
    snapshot_.capture.recordCount = snapshot_.records.size();
    snapshot_.capture.connected = stacktraceProcess_->IsConnected();
    snapshot_.capture.elapsedMs = time_ * 1000;
}

void GuiDataBridge::OnStacktraceConnectionLost() {
    AppendLog("Stacktrace connection lost.");
    QMutexLocker lock(&mutex_);
    snapshot_.capture.connected = false;
}

void GuiDataBridge::OnMemInfoFinished() {
    const auto& info = memInfoProcess_->GetMemInfo();
    QMutexLocker lock(&mutex_);
    MemInfoSample s;
    s.timeMs     = time_ * 1000;
    s.total      = info.Total;
    s.nativeHeap = info.NativeHeap;
    s.gfxDev     = info.GfxDev;
    s.eglMtrack  = info.EGLmtrack;
    s.glMtrack   = info.GLmtrack;
    s.unknown    = info.Unknown;
    snapshot_.memTimeline.push_back(s);
}

void GuiDataBridge::OnScreenshotFinished() {
    const QByteArray bytes = screenshotProcess_->GetScreenshotBytes();
    if (bytes.isEmpty())
        return;
    QMutexLocker lock(&mutex_);
    ScreenshotSnapshot ss;
    ss.timeMs = time_ * 1000;
    ss.jpegBytes.assign(bytes.begin(), bytes.end());
    snapshot_.screenshots.push_back(std::move(ss));
}

void GuiDataBridge::OnStartAppFinished() {}
void GuiDataBridge::OnStartAppError() {
    AppendLog("Failed to start application.");
}

// ---------------------------------------------------------------------------
// snapshot access
// ---------------------------------------------------------------------------
void GuiDataBridge::UpdateSnapshot(GuiSnapshot& out) {
    QMutexLocker lock(&mutex_);
    out = snapshot_;
}

bool GuiDataBridge::IsCapturing() const { return isCapturing_; }
bool GuiDataBridge::IsConnected() const { return isConnected_; }

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
// record file I/O (reuses the exact .loli QDataStream layout)
// ---------------------------------------------------------------------------
bool GuiDataBridge::LoadRecord(const QString& path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
        return false;
    QDataStream stream(&file);

    quint32 magic;
    stream >> magic;
    if (magic != kAppMagic)
        return false;
    qint32 version;
    stream >> version;
    if (version != kAppVersion)
        return false;

    // meminfo series
    qint32 maxMemInfoValue;
    stream >> maxMemInfoValue;
    qint32 seriesCount;
    stream >> seriesCount;
    QVector<QVector<QPair<double,double>>> series(seriesCount);
    for (int i = 0; i < seriesCount; i++) {
        int pointsCount;
        stream >> pointsCount;
        for (int j = 0; j < pointsCount; j++) {
            QPointF point;
            stream >> point;
            series[i].push_back(qMakePair(point.x(), point.y()));
        }
    }

    HashString::hashmap_.clear();
    stream >> HashString::hashmap_;

    // records
    QVector<StackRecord> records;
    QSet<QString> libraries;
    qint32 value;
    stream >> value;
    records.reserve(value);
    for (int i = 0; i < value; i++) {
        StackRecord record;
        QString str;
        stream >> str;
        record.uuid_ = QUuid::fromString(str);
        stream >> record.seq_ >> record.time_ >> record.size_
               >> record.addr_ >> record.funcAddr_ >> record.library_.hashcode_;
        if (!record.library_.Get().isEmpty())
            libraries.insert(record.library_.Get());
        records.push_back(record);
    }

    // callstack map
    QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap;
    stream >> value;
    for (int i = 0; i < value; i++) {
        QString uuid;
        qint32 len;
        stream >> uuid >> len;
        QVector<QPair<HashString, quint64>> callstack;
        for (int j = 0; j < len; j++) {
            QPair<HashString, quint64> pair;
            stream >> pair.first.hashcode_ >> pair.second;
            callstack.push_back(pair);
        }
        callStackMap.insert(QUuid::fromString(uuid), callstack);
    }

    // symbol map
    QHash<QString, QHash<quint64, QString>> symbolMap;
    stream >> value;
    for (int i = 0; i < value; i++) {
        QString lib;
        stream >> lib;
        qint32 size;
        stream >> size;
        auto& map = symbolMap[lib];
        for (int j = 0; j < size; j++) {
            quint64 key;
            QString name;
            stream >> key >> name;
            map[key] = name;
        }
    }

    // freeaddr map
    QHash<quint64, quint32> freeAddrMap;
    stream >> value;
    for (int i = 0; i < value; i++) {
        quint64 addr;
        quint32 seq;
        stream >> addr >> seq;
        freeAddrMap.insert(addr, seq);
    }

    // screenshots
    QVector<QPair<qint32, QByteArray>> screenshots;
    stream >> value;
    screenshots.reserve(value);
    for (int i = 0; i < value; i++) {
        qint32 time;
        QByteArray ba;
        stream >> time >> ba;
        screenshots.push_back(qMakePair(time, ba));
    }

    // smaps
    QHash<QString, SMapsSection> sMapsSections;
    stream >> value;
    for (int i = 0; i < value; i++) {
        QString name;
        stream >> name;
        SMapsSection section;
        qint32 size;
        stream >> size;
        for (int j = 0; j < size; j++) {
            quint64 start, end, offset;
            stream >> start >> end >> offset;
            section.addrs_.push_back(SMapsSectionAddr(start, end, offset));
        }
        stream >> section.virtual_ >> section.rss_ >> section.pss_
               >> section.privateClean_ >> section.privateDirty_
               >> section.sharedClean_ >> section.sharedDirty_;
        sMapsSections.insert(name, section);
    }

    // Commit into the snapshot store.
    QMutexLocker lock(&mutex_);
    callStackMap_  = callStackMap;
    symbolMap_     = symbolMap;
    libraries_     = libraries;
    freeAddrMap_   = freeAddrMap;
    sMapsSections_ = sMapsSections;
    recordsCache_  = records;

    snapshot_.records.clear();
    snapshot_.records.reserve(records.size());
    for (const auto& r : records) {
        RecordSnapshot rs;
        rs.seq = r.seq_;
        rs.timeMs = r.time_;
        rs.size = r.size_;
        rs.addr = r.addr_;
        rs.funcAddr = r.funcAddr_;
        rs.library = r.library_.Get().toStdString();
        snapshot_.records.push_back(std::move(rs));
    }

    // meminfo series -> timeline (series order fixed: Total, NativeHeap, GfxDev, EGL, GL, Unknown)
    snapshot_.memTimeline.clear();
    if (!series.isEmpty()) {
        int count = series[0].size();
        for (int j = 0; j < count; j++) {
            MemInfoSample s;
            s.timeMs = static_cast<int32_t>(series[0][j].first);
            s.total      = series.value(0).value(j).second;
            s.nativeHeap = series.value(1).value(j).second;
            s.gfxDev     = series.value(2).value(j).second;
            s.eglMtrack  = series.value(3).value(j).second;
            s.glMtrack   = series.value(4).value(j).second;
            s.unknown    = series.value(5).value(j).second;
            snapshot_.memTimeline.push_back(s);
        }
    }

    // screenshots
    snapshot_.screenshots.clear();
    for (const auto& sc : screenshots) {
        ScreenshotSnapshot ss;
        ss.timeMs = sc.first;
        ss.jpegBytes.assign(sc.second.begin(), sc.second.end());
        snapshot_.screenshots.push_back(std::move(ss));
    }

    // smaps
    snapshot_.smaps.clear();
    for (auto it = sMapsSections.begin(); it != sMapsSections.end(); ++it) {
        SMapsSectionSnapshot s;
        s.name = it.key().toStdString();
        s.virtualSize = it.value().virtual_;
        s.rss = it.value().rss_;
        s.pss = it.value().pss_;
        s.sharedClean = it.value().sharedClean_;
        s.sharedDirty = it.value().sharedDirty_;
        s.privateClean = it.value().privateClean_;
        s.privateDirty = it.value().privateDirty_;
        snapshot_.smaps.push_back(std::move(s));
    }

    snapshot_.capture.capturing = false;
    snapshot_.capture.connected = false;
    snapshot_.capture.recordCount = snapshot_.records.size();
    snapshot_.capture.appName = QFileInfo(file).fileName().toStdString();

    RebuildCallTreeLocked();
    return true;
}

bool GuiDataBridge::SaveRecord(const QString& path) {
    // Deferred: writing .loli from the bridge requires the same serialization
    // as MainWindow::SaveToFile. Left unimplemented until capture-save is wired.
    (void)path;
    return false;
}

void GuiDataBridge::RebuildCallTreeLocked() {
    // Resolve each record's callstack (root-first) into StackFrameSnapshot so
    // the ImGui StacktraceTree can aggregate and render. Symbol names come from
    // symbolMap_ (populated from the record file or nm resolution); unresolved
    // addresses fall back to a hex string, like MainWindow::TryAddNewAddress.
    const auto& records = snapshot_.records;
    snapshot_.recordFrames.clear();
    snapshot_.recordFrames.resize(records.size());

    auto resolve = [&](const QString& lib, quint64 addr) -> QString {
        auto libIt = symbolMap_.find(lib);
        if (libIt != symbolMap_.end()) {
            auto nameIt = libIt.value().find(addr);
            if (nameIt != libIt.value().end() && !nameIt.value().isEmpty())
                return nameIt.value();
        }
        return QString("0x%1").arg(addr, 0, 16);
    };

    // We iterate recordsCache_ (which carries uuid_ -> callStackMap_).
    for (int i = 0; i < recordsCache_.size(); i++) {
        const auto& record = recordsCache_[i];
        auto csIt = callStackMap_.find(record.uuid_);
        if (csIt == callStackMap_.end())
            continue;
        const auto& callstack = csIt.value();
        auto& frames = snapshot_.recordFrames[i];
        frames.reserve(callstack.size());
        // callstack is leaf-first (innermost frame first); emit root-first.
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

} // namespace gui
