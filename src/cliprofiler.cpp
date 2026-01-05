#include "cliprofiler.h"
#include "configdialog.h"
#include "pathutils.h"
#include "hashstring.h"
#include "clilogger.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <QRegExp>
#include <QElapsedTimer>
#include <QSettings>
#include <QtConcurrent>
#include <QThread>

#include <iostream>
#include <algorithm>
#include <limits>

#define APP_MAGIC 0xA4B3C2D1
#define APP_VERSION 106

CliProfiler::CliProfiler(QObject *parent) : QObject(parent) {
    stacktraceModel_ = new StackTraceModel(this);
    
    startAppProcess_ = new StartAppProcess(this);
    connect(startAppProcess_, &StartAppProcess::ProcessFinished, 
        this, &CliProfiler::OnStartAppProcessFinished);
    connect(startAppProcess_, &StartAppProcess::ProcessErrorOccurred, 
        this, &CliProfiler::OnStartAppProcessErrorOccurred);

    screenshotProcess_ = new ScreenshotProcess(this);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessFinished, 
        this, &CliProfiler::OnScreenshotProcessFinished);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessErrorOccurred, 
        this, &CliProfiler::OnScreenshotProcessErrorOccurred);

    memInfoProcess_ = new MemInfoProcess(this);
    connect(memInfoProcess_, &MemInfoProcess::ProcessFinished, 
        this, &CliProfiler::OnMemInfoProcessFinished);
    connect(memInfoProcess_, &MemInfoProcess::ProcessErrorOccurred, 
        this, &CliProfiler::OnMemInfoProcessErrorOccurred);

    stacktraceProcess_ = new StackTraceProcess(this);
    connect(stacktraceProcess_, &StackTraceProcess::DataReceived, 
        this, &CliProfiler::OnStacktraceDataReceived);
    connect(stacktraceProcess_, &StackTraceProcess::ConnectionLost, 
        this, &CliProfiler::OnStacktraceConnectionLost);
    connect(stacktraceProcess_, &StackTraceProcess::SMapsDumped, [this]() {
        smapsTimer_->stop();
        StopCaptureProcess();
    });

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, &QTimer::timeout, this, &CliProfiler::OnFixedUpdate);
    
    smapsTimer_ = new QTimer(this);
    smapsTimer_->setSingleShot(true);
    smapsTimer_->setInterval(10000);
    connect(smapsTimer_, &QTimer::timeout, [this]() {
        Print("Dump proc/smaps command timeout.");
        StopCaptureProcess();
    });
    
    durationTimer_ = new QTimer(this);
    durationTimer_->setSingleShot(true);
    connect(durationTimer_, &QTimer::timeout, this, &CliProfiler::OnDurationTimeout);
    
    processExitCheckTimer_ = new QTimer(this);
    connect(processExitCheckTimer_, &QTimer::timeout, this, &CliProfiler::OnProcessExitCheckTimeout);
}

CliProfiler::~CliProfiler() {
}

bool CliProfiler::Initialize(const CliOptions& options) {
    CLI_LOG("[Initialize] Starting initialization...");
    options_ = options;
    
    // Parse config
    CLI_LOG("[Initialize] Parsing config file...");
    ConfigDialog::ParseConfigFile();
    
    // Load SDK/NDK paths from QSettings (same as GUI mode)
    CLI_LOG("[Initialize] Loading SDK/NDK paths from settings...");
    QSettings settings("MoreFun", "LoliProfiler");
    QString sdkPath = settings.value("AndroidSDK").toString();
    QString ndkPath = settings.value("AndroidNDK").toString();
    
    CLI_LOG(QString("[Initialize] SDK path from settings: %1").arg(sdkPath));
    CLI_LOG(QString("[Initialize] NDK path from settings: %1").arg(ndkPath));
    
    PathUtils::SetSDKPath(sdkPath);
    PathUtils::SetNDKPath(ndkPath);
    PathUtils::LoadPythonPathSettings();
    
    // Validate ADB path
    CLI_LOG("[Initialize] Checking ADB path...");
    auto adbPath = PathUtils::GetADBExecutablePath();
    CLI_LOG(QString("[Initialize] ADB path: %1").arg(adbPath));
    if (adbPath.isEmpty() || !QFile::exists(adbPath)) {
        PrintError("Android SDK not found. Please configure Android SDK path in GUI first or set ANDROID_HOME environment variable.");
        return false;
    }
    
    // Validate Python path
    CLI_LOG("[Initialize] Checking Python path...");
    auto pythonPath = PathUtils::GetPythonExecutablePath();
    CLI_LOG(QString("[Initialize] Python path: %1").arg(pythonPath));
    if (pythonPath.isEmpty() || !QFile::exists(pythonPath)) {
        PrintError("Python not found. Please configure Android NDK path in GUI first or set ANDROID_NDK_ROOT environment variable.");
        return false;
    }
    
    // Set device serial if specified
    if (!options_.deviceSerial.isEmpty()) {
        CLI_LOG(QString("[Initialize] Setting device serial: %1").arg(options_.deviceSerial));
        stacktraceProcess_->SetDeviceSerial(options_.deviceSerial);
        startAppProcess_->SetDeviceSerial(options_.deviceSerial);
        memInfoProcess_->SetDeviceSerial(options_.deviceSerial);
        screenshotProcess_->SetDeviceSerial(options_.deviceSerial);
    }
    
    // Set executable paths
    CLI_LOG("[Initialize] Setting executable paths...");
    screenshotProcess_->SetExecutablePath(adbPath);
    stacktraceProcess_->SetExecutablePath(adbPath);
    memInfoProcess_->SetExecutablePath(adbPath);
    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath);
    
    CLI_LOG("[Initialize] Initialization complete!");
    return true;
}

void CliProfiler::Start() {
    CLI_LOG("[Start] Starting CLI profiler...");
    Print("Starting CLI profiler...");
    Print(QString("App: %1").arg(options_.appName));
    if (!options_.subProcessName.isEmpty()) {
        Print(QString("SubProcess: %1").arg(options_.subProcessName));
    }
    Print(QString("Output: %1").arg(options_.outputFile));
    if (!options_.symbolPath.isEmpty()) {
        Print(QString("Symbol: %1").arg(options_.symbolPath));
    }
    if (options_.duration > 0) {
        Print(QString("Duration: %1 seconds").arg(options_.duration));
    } else {
        Print("Duration: Until process exits");
    }
    Print(QString("Launch mode: %1").arg(options_.attachMode ? "Attach" : "Launch"));
    Print("Data optimization: Enabled");
    
    CLI_LOG("[Start] Clearing cache folder...");
    // Clear cache folder
    auto cachePath = QCoreApplication::applicationDirPath() + "/cache";
    auto cacheDir = QDir(cachePath);
    if (cacheDir.exists()) {
        auto files = cacheDir.entryList(QDir::Filter::Files);
        for (auto fileName : files) {
            cacheDir.remove(fileName);
        }
    }
    
    CLI_LOG("[Start] Clearing data structures...");
    // Clear data structures
    libraries_.clear();
    stacktraceModel_->clear();
    sMapsSections_.clear();
    screenshots_.clear();
    symbloMap_.clear();
    recordsCache_.clear();
    freeAddrMap_.clear();
    callStackMap_.clear();
    HashString::hashmap_.clear();
    memInfoData_.clear();
    
    maxMemInfoValue_ = 128;
    time_ = 0;
    lastScreenshotTime_ = 0;
    
    CLI_LOG("[Start] Getting configuration settings...");
    // Start profiling
    auto settings = ConfigDialog::GetCurrentSettings();
    showJDWPErrorLog_ = false;
    
    CLI_LOG("[Start] Starting application...");
    CLI_LOG(QString("[Start]   App name: %1").arg(options_.appName));
    CLI_LOG(QString("[Start]   Subprocess: %1").arg(options_.subProcessName));
    CLI_LOG(QString("[Start]   Compiler: %1").arg(settings.compiler_));
    CLI_LOG(QString("[Start]   Arch: %1").arg(settings.arch_));
    CLI_LOG(QString("[Start]   Attach mode: %1").arg(options_.attachMode ? "true" : "false"));
    
    Print("Starting application...");
    startAppProcess_->StartApp(options_.appName, options_.subProcessName, 
        settings.compiler_, settings.arch_, options_.attachMode, nullptr);
    
    isCapturing_ = true;
    CLI_LOG("[Start] Start() method completed, waiting for app to start...");
}

void CliProfiler::Print(const QString& str) {
    // Always print to log file and stdout in CLI mode
    CLI_LOG(str);
}

void CliProfiler::PrintError(const QString& str) {
    CLI_ERROR(str);
}

void CliProfiler::ConnectionFailed() {
    isConnected_ = false;
    isCapturing_ = false;
    
    if (screenshotProcess_->IsRunning())
        screenshotProcess_->Process()->kill();
    stacktraceProcess_->Disconnect();
    
    // adb forward --remove-all
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    QStringList forwardArgs;
    if (!options_.deviceSerial.isEmpty()) {
        forwardArgs << "-s" << options_.deviceSerial;
    }
    forwardArgs << "forward" << "--remove-all";
    AdbProcess::SetArguments(&process, forwardArgs);
    process.start();
    if (process.waitForStarted()) {
        process.waitForFinished();
    }
}

void CliProfiler::OnFixedUpdate() {
    if (!isConnected_)
        return;
    
    // Don't request meminfo & screenshot while dumping smaps file
    if (!smapsTimer_->isActive()) {
        if (time_ - lastScreenshotTime_ >= 5 && !screenshotProcess_->IsRunning()) {
            lastScreenshotTime_ = time_;
            screenshotProcess_->CaptureScreenshot();
        }
        if (!memInfoProcess_->IsRunning() && !memInfoProcess_->HasErrors()) {
            memInfoProcess_->DumpMemInfoAsync(options_.appName, options_.subProcessName);
        }
    }
    
    if (!stacktraceProcess_->IsConnecting() && !stacktraceProcess_->IsConnected()) {
        stacktraceProcess_->ConnectToServer(8000);
        Print("Connecting to application server...");
    }
    
    time_++;
}

void CliProfiler::OnStartAppProcessFinished(AdbProcess* process) {
    CLI_LOG("[OnStartAppProcessFinished] Callback triggered");
    auto startAppProcess = static_cast<StartAppProcess*>(process);
    if (!startAppProcess->Result()) {
        CLI_LOG("[OnStartAppProcessFinished] Start app failed!");
        ConnectionFailed();
        PrintError("Error starting app");
        Cleanup(1);
        return;
    }
    
    CLI_LOG("[OnStartAppProcessFinished] App started successfully!");
    isConnected_ = true;
    lastScreenshotTime_ = time_ = 0;
    Print("Application started!");
    
    // Start main timer
    CLI_LOG("[OnStartAppProcessFinished] Starting main timer...");
    mainTimer_->start(1000);
    
    // Start duration timer if specified
    if (options_.duration > 0) {
        CLI_LOG(QString("[OnStartAppProcessFinished] Starting duration timer for %1 seconds...").arg(options_.duration));
        durationTimer_->start(options_.duration * 1000);
        Print(QString("Profiling for %1 seconds...").arg(options_.duration));
    } else {
        CLI_LOG("[OnStartAppProcessFinished] Starting process exit check timer...");
        // Start process exit check timer
        processExitCheckTimer_->start(2000);
        Print("Profiling until process exits...");
    }
    
    CLI_LOG("[OnStartAppProcessFinished] Dumping initial meminfo...");
    memInfoProcess_->DumpMemInfoAsync(options_.appName, options_.subProcessName);
    CLI_LOG("[OnStartAppProcessFinished] Callback complete");
}

void CliProfiler::OnStartAppProcessErrorOccurred() {
    CLI_LOG("[OnStartAppProcessErrorOccurred] Error callback triggered!");
    ConnectionFailed();
    PrintError("Error starting app: " + startAppProcess_->ErrorStr());
    Cleanup(1);
}

void CliProfiler::OnMemInfoProcessFinished(AdbProcess* process) {
    auto memInfoProcess = static_cast<MemInfoProcess*>(process);
    auto curMemInfo = memInfoProcess->GetMemInfo();
    maxMemInfoValue_ = std::max(maxMemInfoValue_, std::max(256, static_cast<int>(curMemInfo.Total * 1.2f)));
    
    MemInfoPoint point;
    point.time = time_;
    point.total = curMemInfo.Total;
    point.nativeHeap = curMemInfo.NativeHeap;
    point.gfxDev = curMemInfo.GfxDev;
    point.eglMtrack = curMemInfo.EGLmtrack;
    point.glMtrack = curMemInfo.GLmtrack;
    point.unknown = curMemInfo.Unknown;
    memInfoData_.push_back(point);
}

void CliProfiler::OnMemInfoProcessErrorOccurred() {
    Print("Error occurred when dumping meminfo");
}

void CliProfiler::OnScreenshotProcessFinished(AdbProcess* process) {
    auto screenshotProcess = static_cast<ScreenshotProcess*>(process);
    // In CLI mode, just save the raw screenshot bytes
    auto bytes = screenshotProcess->GetScreenshotBytes();
    if (!bytes.isEmpty()) {
        screenshots_.push_back(qMakePair(time_, bytes));
    }
}

void CliProfiler::OnScreenshotProcessErrorOccurred() {
    Print("Error occurred when capturing screenshot");
}

void CliProfiler::OnStacktraceDataReceived() {
    if (!isConnected_ || !isCapturing_)
        return;
    
    const auto& stacks = stacktraceProcess_->GetStackInfo();
    const auto& frees = stacktraceProcess_->GetFreeInfo();
    
    // Cache data
    auto cacheDirPath = QCoreApplication::applicationDirPath() + "/cache";
    if (!QDir(cacheDirPath).exists()) {
        QDir().mkdir(cacheDirPath);
    }
    
    static quint32 cacheIndex = 0;
    auto cachePath = QString("%1/cache_%2.bin").arg(cacheDirPath).arg(cacheIndex);
    QFile cacheFile(cachePath);
    if (!cacheFile.open(QFile::OpenModeFlag::WriteOnly | QFile::OpenModeFlag::Append)) {
        Print("Failed to open cache file: " + cachePath);
        return;
    }
    
    QDataStream stream(&cacheFile);
    stream << static_cast<qint32>(stacks.size());
    for (auto& stack : stacks) {
        stream << stack.seq_ << stack.addr_ << stack.size_ << stack.time_
               << stack.library_.hashcode_ << stack.recType_ << stack.stacktraces_;
    }
    cacheFile.flush();
    
    if (cacheFile.size() > 1024 * 1024 * 512) {
        cacheIndex++;
    }
    cacheFile.close();
    
    // Read free call infos
    if (frees.size() > 0) {
        for (const auto& free : frees) {
            const auto address = free.second;
            auto curSeq = freeAddrMap_[address];
            if (free.first > curSeq) {
                freeAddrMap_[address] = free.first;
            }
        }
    }
}

void CliProfiler::OnStacktraceConnectionLost() {
    if (!isCapturing_)
        return;
    PrintError("Connection lost");
    ConnectionFailed();
    Cleanup(1);
}

void CliProfiler::OnDurationTimeout() {
    Print("Duration elapsed, stopping capture...");
    
    // Trigger smaps dump
    auto appPid = memInfoProcess_->GetAppPid();
    if (startAppProcess_->GetSMapsByRunAs(options_.appName, appPid)) {
        StopCaptureProcess();
        return;
    }
    
    // Create a temporary smaps file for apk
    PushEmptySMapsFile();
    auto type = static_cast<quint8>(loliCommands::SMAPS_DUMP);
    stacktraceProcess_->Send(reinterpret_cast<const char*>(&type), 1);
    smapsTimer_->start();
}

void CliProfiler::OnProcessExitCheckTimeout() {
    // Check if process is still running
    QProcess checkProcess;
    checkProcess.setProgram(PathUtils::GetADBExecutablePath());
    QStringList args;
    if (!options_.deviceSerial.isEmpty()) {
        args << "-s" << options_.deviceSerial;
    }
    args << "shell" << "pidof" << options_.appName;
    AdbProcess::SetArguments(&checkProcess, args);
    checkProcess.start();
    
    if (checkProcess.waitForStarted()) {
        checkProcess.waitForFinished(5000);
        QString output = checkProcess.readAllStandardOutput().trimmed();
        
        if (output.isEmpty()) {
            Print("Process exited, stopping capture...");
            processExitCheckTimer_->stop();
            OnDurationTimeout();  // Reuse the same stop logic
        }
    }
}

void CliProfiler::PushEmptySMapsFile() {
    QTemporaryFile smapsFile;
    smapsFile.open();
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    QStringList pushArgs;
    if (!options_.deviceSerial.isEmpty()) {
        pushArgs << "-s" << options_.deviceSerial;
    }
    pushArgs << "push" << smapsFile.fileName() << "/data/local/tmp/smaps.txt";
    AdbProcess::SetArguments(&process, pushArgs);
    process.start();
    process.waitForStarted();
    process.waitForFinished();
    process.close();
}

void CliProfiler::ReadSMapsFile(QFile* file) {
    QTextStream stream(file);
    SMapsSection total;
    SMapsSection* curSection = nullptr;
    QRegExp sectionTitleRx(
        "([0-9a-z]+)\\-([0-9a-z]+)\\s([0-9a-z-]+)\\s([0-9a-z]+)\\s([0-9a-z]+)\\:([0-9a-z]+)\\s([0-9a-z]+)");
    
    auto Demangle = [](const QString& name) {
        auto slashIndex = name.lastIndexOf('/');
        if (slashIndex > 0)
            return name.right(name.size() - slashIndex - 1);
        return name;
    };
    
    while (!stream.atEnd()) {
        auto line = stream.readLine();
        if (line.isEmpty())
            continue;
        auto strList = line.split(' ', QString::SkipEmptyParts);
        if (sectionTitleRx.indexIn(line) != -1) {
            int matchedLength = sectionTitleRx.matchedLength();
            if (matchedLength == -1)
                continue;
            QString libName = line.right(line.length() - matchedLength).trimmed();
            if (!libName.startsWith('[')) {
                libName = Demangle(libName);
            }
            if (libName.isEmpty()) {
                libName = "anonymous";
            }
            curSection = &sMapsSections_[libName];
            auto start = sectionTitleRx.cap(1).toULongLong(nullptr, 16);
            auto end = sectionTitleRx.cap(2).toULongLong(nullptr, 16);
            auto offset = sectionTitleRx.cap(4).toULongLong(nullptr, 16);
            curSection->addrs_.push_back(SMapsSectionAddr(start, end, offset));
        } else if (strList.size() >= 2) {
            if (curSection == nullptr)
                continue;
            auto word = strList[0];
            auto value = strList[1].toUInt();
            if (word == "Size:") {
                curSection->virtual_ += value;
                total.virtual_ += value;
            } else if (word == "Rss:") {
                curSection->rss_ += value;
                total.rss_ += value;
            } else if (word == "Pss:") {
                curSection->pss_ += value;
                total.pss_ += value;
            } else if (word == "Shared_Clean:") {
                curSection->sharedClean_ += value;
                total.sharedClean_ += value;
            } else if (word == "Shared_Dirty:") {
                curSection->sharedDirty_ += value;
                total.sharedDirty_ += value;
            } else if (word == "Private_Dirty:") {
                curSection->privateDirty_ += value;
                total.privateDirty_ += value;
            } else if (word == "Private_Clean:") {
                curSection->privateClean_ += value;
                total.privateClean_ += value;
            }
        }
    }
}

void CliProfiler::ReadStacktraceData(const QVector<RawStackInfo>& stacks) {
    auto isNoStack = ConfigDialog::IsNoStackMode();
    if (stacks.size() > 0) {
        for (const auto& stack : stacks) {
            StackRecord record;
            record.uuid_ = QUuid::createUuid();
            record.seq_ = stack.seq_;
            record.time_ = stack.time_;
            record.size_ = stack.size_;
            record.addr_ = stack.addr_;
            if (isNoStack) {
                record.library_ = HashString(stack.library_);
            } else {
                auto& callstack = callStackMap_[record.uuid_];
                for (int i = 0; i < stack.stacktraces_.size(); i++) {
                    callstack.append(qMakePair(QString(), stack.stacktraces_[i]));
                }
            }
            recordsCache_.push_back(record);
        }
    }
}

void CliProfiler::ReadStacktraceDataCache() {
    auto cachePath = QCoreApplication::applicationDirPath() + "/cache";
    QDir cacheDir(cachePath);
    auto files = cacheDir.entryList(QDir::Filter::Files, QDir::SortFlag::Time);
    quint32 recordCount = 0;
    QVector<RawStackInfo> stacks;
    
    for (auto filePath : files) {
        QFile file(cachePath + "/" + filePath);
        stacks.clear();
        if (file.open(QFile::OpenModeFlag::ReadOnly)) {
            QDataStream stream(&file);
            while (!stream.atEnd()) {
                qint32 size;
                stream >> size;
                recordCount += size;
                for (int i = 0; i < size; i++) {
                    RawStackInfo stack;
                    stream >> stack.seq_ >> stack.addr_ >> stack.size_ >> stack.time_
                           >> stack.library_.hashcode_ >> stack.recType_ >> stack.stacktraces_;
                    // ignore freed records
                    auto it = freeAddrMap_.find(stack.addr_);
                    if (it != freeAddrMap_.end()) {
                        if (stack.seq_ < it.value())
                            continue;
                    }
                    stacks.push_back(stack);
                }
            }
            ReadStacktraceData(stacks);
            file.remove();
        }
    }
    Print(QString("Cached %1 records.").arg(recordCount));
}

void CliProfiler::InterpretRecordLibrary(StackRecord& record, StacktraceData& data) {
    auto it = callStackMap_.find(record.uuid_);
    if (it == callStackMap_.end())
        return;
    
    static QVector<QPair<HashString, SMapsSection*>> sMapsCache_;
    if (sMapsCache_.isEmpty()) {
        for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
            auto& library = it.key();
            auto& sections = it.value();
            if (library.endsWith(".so")) {
                sMapsCache_.push_back(qMakePair(HashString(library), &sections));
            }
        }
    }
    
    auto& callStack = it.value();
    for (int i = 0; i < callStack.size(); i++) {
        auto& libName = callStack[i].first;
        auto& funcAddr = callStack[i].second;
        bool found = false;
        for (int i = 0; i < sMapsCache_.size(); i++) {
            const auto& cache = sMapsCache_[i];
            quint64 symbolVAddr;
            if (cache.second->Contains(funcAddr, record.size_, symbolVAddr)) {
                funcAddr = symbolVAddr;
                libName = cache.first;
                data.records_.push_back(qMakePair(libName, funcAddr));
                found = true;
                break;
            }
        }
        if (!found)
            libName = HashString(qHash("unknown"));
    }
    
    if (callStack.size() == 0)
        return;
    
    record.library_ = callStack[0].first;
    record.funcAddr_ = callStack[0].second;
    int level = 1;
    while (record.library_.Get() == "libloli.so" && level < callStack.size()) {
        record.library_ = callStack[level].first;
        record.funcAddr_ = callStack[level].second;
        level++;
    }
    
    if (record.library_.hashcode_ == 0) {
        record.library_ = callStack[0].first = HashString(qHash("unknown"));
    }
    
    if (!data.libraries_.contains(record.library_.Get()))
        data.libraries_.push_back(record.library_.Get());
}

CliProfiler::StacktraceData CliProfiler::InterpretRecordsLibrary(int start, int count) {
    StacktraceData data;
    for (int i = 0; i < count; i++)
        InterpretRecordLibrary(recordsCache_[start + i], data);
    return data;
}

void CliProfiler::InterpretStacktraceData() {
    if (ConfigDialog::IsNoStackMode()) {
        for (int i = 0; i < recordsCache_.size(); i++) {
            auto& record = recordsCache_[i];
            if (!libraries_.contains(record.library_.Get())) {
                libraries_.insert(record.library_.Get());
            }
        }
    } else {
        Print("Translating stack traces...");
        HashString::hashmap_.insert(qHash("unknown"), "unknown");
        
        auto threadCount = std::max(2, QThread::idealThreadCount());
        auto payload = recordsCache_.size() / threadCount;
        QVector<QFuture<StacktraceData>> futures;
        int currentIndex = 0;
        
        for (int i = 0; i < threadCount - 1; i++) {
            futures.push_back(QtConcurrent::run(this, &CliProfiler::InterpretRecordsLibrary, currentIndex, payload));
            currentIndex += payload;
        }
        futures.push_back(QtConcurrent::run(this, &CliProfiler::InterpretRecordsLibrary, 
            currentIndex, recordsCache_.size() - currentIndex));
        
        Print(QString("Translating %1 records using %2 threads...").arg(recordsCache_.size()).arg(threadCount));
        
        for (int i = 0; i < futures.size(); i++) {
            auto& future = futures[i];
            const auto& trace = future.result();
            for (const auto& record : trace.records_) {
                if (!symbloMap_.contains(record.first.Get()))
                    symbloMap_.insert(record.first.Get(), {});
                auto& symblos = symbloMap_[record.first.Get()];
                if (!symblos.contains(record.second)) {
                    symblos.insert(record.second, "");
                }
            }
            for (const auto& library : trace.libraries_) {
                if (!libraries_.contains(library)) {
                    libraries_.insert(library);
                }
            }
        }
    }
    
    stacktraceModel_->append(recordsCache_);
    recordsCache_.clear();
}

bool CliProfiler::LoadSymbolFile(const QString& symbolPath) {
    if (symbolPath.isEmpty() || !QFile::exists(symbolPath)) {
        return false;
    }
    
    Print(QString("Loading symbol file: %1").arg(symbolPath));
    
    auto nmPath = PathUtils::GetNDKToolPath("nm", ConfigDialog::GetCurrentSettings().arch_ != "arm64-v8a");
    if (nmPath.isEmpty() || !QFile::exists(nmPath)) {
        PrintError("NDK tool 'nm' not found");
        return false;
    }
    
    QFileInfo info(symbolPath);
    auto soName = info.baseName() + ".so";
    auto it = symbloMap_.find(soName);
    if (it == symbloMap_.end()) {
        Print(QString("Warning: No addresses found for library %1").arg(soName));
        return false;
    }
    
    QString symbolMapFilePath = symbolPath + ".txt";
    
    // Run nm to extract symbols
    Print("Extracting symbols...");
    {
        QProcess nmProcess;
        AdbProcess::SetArguments(&nmProcess, QStringList() << "-nCS" << symbolPath);
        nmProcess.setStandardOutputFile(symbolMapFilePath);
        nmProcess.setProgram(nmPath);
        nmProcess.start();
        if (!nmProcess.waitForStarted()) {
            PrintError("Failed to start nm process");
            return false;
        }
        nmProcess.waitForFinished(-1);  // Wait indefinitely
    }
    
    // Parse symbol map
    Print("Parsing symbols...");
    struct SymbolRecord {
        QString name;
        quint64 addr;
        quint32 size;
    };
    QVector<SymbolRecord> sortedRecords;
    
    {
        QFile symbolMapFile(symbolMapFilePath);
        if (!symbolMapFile.open(QFile::OpenModeFlag::ReadOnly)) {
            PrintError(QString("Failed to read symbol map: %1").arg(symbolMapFilePath));
            return false;
        }
        
        QRegExp recordRx("([0-9a-z]+)\\s([0-9a-z]+)\\s(\\w)\\s(.+)");
        QTextStream in(&symbolMapFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (recordRx.indexIn(line) < 0)
                continue;
            SymbolRecord record;
            record.name = recordRx.cap(4);
            record.size = recordRx.cap(2).toUInt(nullptr, 16);
            record.addr = recordRx.cap(1).toULong(nullptr, 16);
            sortedRecords.push_back(record);
        }
        symbolMapFile.close();
    }
    
    Print(QString("Loaded %1 symbols, translating addresses...").arg(sortedRecords.size()));
    
    // Translate addresses
    if (sortedRecords.size() > 0) {
        auto& addrMap = it.value();
        int translatedCount = 0;
        for (auto addrMapIt = addrMap.begin(); addrMapIt != addrMap.end(); ++addrMapIt) {
            if (addrMapIt.value().size() != 0)
                continue;
            auto addr = addrMapIt.key();
            int left = 0;
            int right = sortedRecords.size() - 1;
            while (left != right) {
                auto mid = static_cast<int>(std::ceil(static_cast<float>(left + right) / 2));
                auto& midRecord = sortedRecords[mid];
                if (addr >= midRecord.addr && addr <= midRecord.addr + midRecord.size) {
                    addrMapIt.value() = midRecord.name;
                    translatedCount++;
                    break;
                } else if (addr < midRecord.addr) {
                    right = mid - 1;
                } else {
                    left = mid;
                }
            }
        }
        Print(QString("Translated %1 addresses").arg(translatedCount));
    }
    
    // Clean up temp file
    QFile::remove(symbolMapFilePath);
    
    return true;
}

void CliProfiler::StopCaptureProcess() {
    mainTimer_->stop();
    durationTimer_->stop();
    processExitCheckTimer_->stop();
    
    ConnectionFailed();
    Print("Stopping capture...");
    
    // Pull smaps file
    auto smapsPath = QCoreApplication::applicationDirPath() + "/smaps.txt";
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    QStringList pullArgs;
    if (!options_.deviceSerial.isEmpty()) {
        pullArgs << "-s" << options_.deviceSerial;
    }
    pullArgs << "pull" << "/data/local/tmp/smaps.txt" << smapsPath;
    AdbProcess::SetArguments(&process, pullArgs);
    process.start();
    process.waitForStarted();
    process.waitForFinished();
    process.close();
    
    QFile file(smapsPath);
    bool readSMaps = false;
    if (file.exists()) {
        if (file.open(QFile::OpenModeFlag::ReadOnly)) {
            ReadSMapsFile(&file);
            readSMaps = true;
        }
        file.remove();
    }
    
    if (!readSMaps) {
        Print("Failed to read proc/pid/smaps");
    } else {
        Print("Reading cached record files...");
        ReadStacktraceDataCache();
        InterpretStacktraceData();
    }
    
    Print(QString("Captured %1 records.").arg(stacktraceModel_->rowCount()));
    
    // Load symbol file if specified
    if (!options_.symbolPath.isEmpty()) {
        LoadSymbolFile(options_.symbolPath);
    }
    
    // Save to output file
    Print(QString("Saving to %1...").arg(options_.outputFile));
    QFile outputFile(options_.outputFile);
    
    // Delete existing file if present to ensure clean write
    if (outputFile.exists()) {
        if (!outputFile.remove()) {
            PrintError(QString("Failed to delete existing file: %1").arg(options_.outputFile));
            Cleanup(1);
            return;
        }
    }
    
    // Open in WriteOnly | Truncate mode to ensure file is overwritten, not appended
    if (outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        SaveToFile(&outputFile);
        outputFile.close();
        Print("Profile saved successfully!");
        Cleanup(0);
    } else {
        PrintError("Failed to save output file");
        Cleanup(1);
    }
}

void CliProfiler::SaveToFile(QFile *file) {
    QDataStream stream(file);
    stream << static_cast<quint32>(APP_MAGIC);
    stream << static_cast<qint32>(APP_VERSION);
    stream.setVersion(QDataStream::Qt_5_12);
    
    // meminfo charts
    stream << static_cast<qint32>(maxMemInfoValue_);
    stream << static_cast<qint32>(6);  // 6 series
    
    // Create series data from memInfoData_
    QVector<QVector<QPointF>> seriesData(6);
    for (const auto& point : memInfoData_) {
        seriesData[0].append(QPointF(point.time, point.total));
        seriesData[1].append(QPointF(point.time, point.nativeHeap));
        seriesData[2].append(QPointF(point.time, point.gfxDev));
        seriesData[3].append(QPointF(point.time, point.eglMtrack));
        seriesData[4].append(QPointF(point.time, point.glMtrack));
        seriesData[5].append(QPointF(point.time, point.unknown));
    }
    
    for (int i = 0; i < 6; i++) {
        stream << static_cast<qint32>(seriesData[i].size());
        for (const auto& point : seriesData[i]) {
            stream << point;
        }
    }
    
    // string hashes
    stream << HashString::hashmap_;
    
    // callstack tree view
    qint32 count = stacktraceModel_->rowCount();
    stream << count;
    for (int i = 0; i < count; i++) {
        auto& record = stacktraceModel_->recordAt(i);
        stream << record.uuid_.toString();
        stream << record.seq_;
        stream << record.time_;
        stream << record.size_;
        stream << record.addr_;
        stream << record.funcAddr_;
        stream << record.library_.hashcode_;
    }
    
    // callstack map
    stream << static_cast<qint32>(callStackMap_.size());
    for (auto it = callStackMap_.begin(); it != callStackMap_.end(); ++it) {
        stream << it.key().toString();
        auto& callstacks = it.value();
        stream << static_cast<qint32>(callstacks.size());
        for (auto stack : callstacks) {
            stream << stack.first.hashcode_ << stack.second;
        }
    }
    
    // symbol map
    stream << static_cast<qint32>(symbloMap_.size());
    for (auto it = symbloMap_.begin(); it != symbloMap_.end(); ++it) {
        stream << it.key();
        stream << static_cast<qint32>(it.value().size());
        for (auto it1 = it.value().begin(); it1 != it.value().end(); ++it1) {
            stream << it1.key();
            stream << it1.value();
        }
    }
    
    // freeaddr map
    stream << static_cast<qint32>(freeAddrMap_.size());
    for (auto it = freeAddrMap_.begin(); it != freeAddrMap_.end(); ++it) {
        stream << it.key();
        stream << static_cast<quint32>(it.value());
    }
    
    // screen shots
    stream << static_cast<qint32>(screenshots_.size());
    for (auto& pair : screenshots_) {
        stream << static_cast<qint32>(pair.first);
        stream << pair.second;
    }
    
    // smaps
    stream << static_cast<qint32>(sMapsSections_.size());
    for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
        stream << it.key();
        auto& section = it.value();
        stream << static_cast<qint32>(section.addrs_.size());
        for (auto& addr : section.addrs_) {
            stream << addr.start_;
            stream << addr.end_;
            stream << addr.offset_;
        }
        stream << section.virtual_;
        stream << section.rss_;
        stream << section.pss_;
        stream << section.privateClean_;
        stream << section.privateDirty_;
        stream << section.sharedClean_;
        stream << section.sharedDirty_;
    }
}

void CliProfiler::Cleanup(int exitCode) {
    // Ask user if kill app (only in verbose mode)
    if (options_.verbose && exitCode == 0) {
        std::cout << "Kill the profiling app? (y/n): ";
        std::string response;
        std::getline(std::cin, response);
        
        if (response == "y" || response == "Y" || response == "yes") {
            QProcess killApp;
            QStringList killArgs;
            if (!options_.deviceSerial.isEmpty()) {
                killArgs << "-s" << options_.deviceSerial;
            }
            killArgs << "shell" << "am" << "force-stop" << options_.appName;
            AdbProcess::SetArguments(&killApp, killArgs);
            killApp.setProgram(PathUtils::GetADBExecutablePath());
            killApp.start();
            killApp.waitForStarted();
            killApp.waitForFinished();
            Print("App killed.");
        }
    }
    
    emit Finished(exitCode);
}
