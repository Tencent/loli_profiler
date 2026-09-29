#include "clicapture.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "hashstringlite.h"
#include "smaps/smapssectionlite.h"
#include "symboltranslator.h"
#include "pathutilslite.h"

namespace loli {

namespace {

// Reads a whole file into a string (smaps pull target).
bool ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream in(path.c_str());
    if (!in.good())
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

void SleepSeconds(int s) {
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

} // namespace

CliCaptureSession::CliCaptureSession()
    : session_(std::make_shared<loli::Session>()) {
    channel_.SetDataHandler([this](const std::vector<StacktraceChannel::RawStackInfo>& s,
                                   const std::vector<StacktraceChannel::FreeInfo>& f) {
        OnStackData(s, f);
    });
    channel_.SetConnectionLostHandler([this]() { OnConnectionLost(); });
    channel_.SetSmapsDumpedHandler([this]() { smapsDumpAcked_ = true; });
    // Launch completion is posted from the worker thread; the subscription
    // delivers it inside Tick() on the consumer thread.
    bus_.Subscribe(kEvLaunchDone, [this](const EventBus::Event& e) {
        if (e.payload.compare(0, 5, "fail:") == 0)
            OnLaunchFinished(false, e.payload.substr(5));
        else
            OnLaunchFinished(e.payload == "ok", std::string());
    });
}

CliCaptureSession::~CliCaptureSession() {
    if (launchThread_.joinable())
        launchThread_.join();
}

void CliCaptureSession::Log(const std::string& s) {
    if (logger_)
        logger_(s);
}

void CliCaptureSession::LogError(const std::string& s) {
    if (logger_)
        logger_("[ERROR] " + s);
}

void CliCaptureSession::QueueOnConsumer(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    pendingCallbacks_.push_back(std::move(callback));
}

bool CliCaptureSession::Start(const CliCaptureOptions& options) {
    if (isCapturing_.load())
        return false;

    options_ = options;
    session_->Clear();
    freeIndex_.clear();
    liveRecordIndex_.clear();
    recordArrivalOrder_.clear();
    nextRecordOrdinal_ = 0;
    appPid_.clear();
    smapsText_.clear();
    smapsDumpPending_ = false;
    smapsDumpAcked_ = false;
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        pendingCallbacks_.clear();
    }

    // Validate tools (Qt CLI Initialize parity).
    if (options_.adbPath.empty()) {
        LogError("Android SDK not found. Configure the SDK path or set ANDROID_HOME.");
        finished_ = true;
        exitCode_ = 1;
        return false;
    }
    if (options_.pythonPath.empty()) {
        LogError("Python not found. Configure the NDK/python path first.");
        finished_ = true;
        exitCode_ = 1;
        return false;
    }

    channel_.SetAdbPath(options_.adbPath);
    channel_.SetDeviceSerial(options_.deviceSerial);
    memDumper_.SetAdbPath(options_.adbPath);
    memDumper_.SetDeviceSerial(options_.deviceSerial);
    screenshot_.SetAdbPath(options_.adbPath);
    screenshot_.SetDeviceSerial(options_.deviceSerial);

    isCapturing_ = true;
    launchComplete_ = false;
    finished_ = false;
    exitCode_ = -1;
    time_ = 0;
    lastScreenshotTime_ = 0;
    maxMemInfoValue_ = 128;

    // useCache: clear the local cache/ folder (MainWindow/Qt CLI parity).
    if (options_.useCache) {
        const std::string cacheDir = options_.executableDir + "/cache";
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        const std::string pattern = cacheDir + "\\*";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    DeleteFileA((cacheDir + "\\" + fd.cFileName).c_str());
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#endif
    }

    // Launch sequence on a worker (blocking adb steps); result marshalled
    // through the bus into Tick().
    const CaptureConfig cc = LoadCaptureConfig();
    LaunchDriver::Config cfg;
    cfg.deviceSerial = options_.deviceSerial;
    cfg.appName = options_.appName;
    cfg.subProcessName = options_.subProcessName;
    cfg.compiler = cc.compiler;
    cfg.arch = cc.arch;
    cfg.enableInject = options_.attachMode;
    cfg.useCache = options_.useCache;
    cfg.adbPath = options_.adbPath;
    cfg.pythonPath = options_.pythonPath;

    if (launchThread_.joinable())
        launchThread_.join();
    launchThread_ = std::thread([this, cfg]() {
        const bool ok = launch_.Run(
            cfg,
            [this](int, const std::string&) {
                // step progress logging is available here if wanted
            },
            [this](bool injectOk) {
                bus_.Post(kEvLaunchDone, injectOk ? "ok" : "fail");
            });
        if (!ok) {
            // The blocking steps failed before the async inject started.
            bus_.Post(kEvLaunchDone, std::string("fail:") + launch_.GetError());
        }
    });

    Log("Starting application...");
    return true;
}

void CliCaptureSession::RequestStop() {
    stopRequested_ = true;
}

void CliCaptureSession::Tick() {
    bus_.Pump();
    std::vector<std::function<void()>> pending;
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        pending.swap(pendingCallbacks_);
    }
    for (auto& callback : pending)
        callback();

    if (stopRequested_.load() && !smapsDumpPending_ && isConnected_.load()) {
        BeginStopSequence();
    } else if (stopRequested_.load() && !isConnected_.load() && !smapsDumpPending_) {
        // Stop requested but never connected: clean up immediately.
        Log("Stop requested, but not connected. Exiting...");
        Cleanup(1);
        return;
    }

    if (smapsDumpPending_) {
        channel_.Pump();
        if (smapsDumpAcked_ || std::chrono::steady_clock::now() > smapsDeadline_) {
            if (!smapsDumpAcked_)
                Log("Dump proc/smaps command timeout.");
            FinishStopSequence();
        }
        return; // no further polling during the smaps dump
    }

    if (isCapturing_.load() && isConnected_.load() && launchComplete_.load()) {
        OnFixedUpdate();
    } else if (isCapturing_.load() && !isConnected_.load() &&
               !channel_.IsConnecting() && !channel_.IsConnected()) {
        // Disconnected mid-capture (app restart): keep trying to reconnect so
        // FixedUpdate's guard above doesn't deadlock us out of the loop.
        channel_.ConnectToServer(8000);
        Log("Connecting to application server...");
    }
    if (isCapturing_.load()) {
        // Keep pumping the channel even mid-reconnect so a settled connect
        // is detected on the next Tick.
        channel_.Pump();
        // A mid-capture app restart is followed by libloli re-registering its
        // server; once the channel is connected again, resume the connected
        // state (meminfo/screenshot polling picks back up on FixedUpdate).
        if (!isConnected_.load() && channel_.IsConnected()) {
            isConnected_ = true;
            Log("Reconnected to application server.");
        }
    }
}

void CliCaptureSession::OnFixedUpdate() {
    // meminfo + screenshot polling (Qt OnFixedUpdate parity).
    if (time_ - lastScreenshotTime_ >= 5 && !screenshot_.IsRunning()) {
        lastScreenshotTime_ = time_;
        screenshot_.CaptureAsync([this](bool ok, std::vector<uint8_t> png) {
            QueueOnConsumer([this, ok, png = std::move(png)]() mutable {
                if (!ok || png.empty())
                    return;
                loli::Screenshot shot;
                // The .loli screenshot timestamp is in seconds (Qt CLI
                // wrote its one-second poll counter directly).
                shot.timeMs = time_;
                shot.jpeg = std::move(png);
                session_->screenshots.push_back(std::move(shot));
            });
        });
    }
    if (!memDumper_.IsRunning()) {
        memDumper_.DumpAsync(
            options_.appName, options_.subProcessName,
            [this](bool ok, const MemInfo& info, const std::string& appPid) {
              QueueOnConsumer([this, ok, info, appPid]() {
                if (!ok) {
                    Log("Error occurred when dumping meminfo");
                    return;
                }
                if (!appPid.empty() && !appPid_.empty() && appPid != appPid_) {
                    Log("App PID changed from " + appPid_ + " to " + appPid +
                        "; discarding stacks captured with the previous ASLR layout.");
                    session_->records.clear();
                    session_->callStackMap.clear();
                    session_->freeAddrMap.clear();
                    freeIndex_.clear();
                    liveRecordIndex_.clear();
                    recordArrivalOrder_.clear();
                    nextRecordOrdinal_ = 0;
                    session_->symbolMap.clear();
                    session_->memSeries.clear();
                    session_->screenshots.clear();
                }
                appPid_ = appPid;
                maxMemInfoValue_ = std::max(maxMemInfoValue_,
                                            std::max(256, static_cast<int>(info.Total * 1.2f)));
                if (session_->memSeries.size() < 6)
                    session_->memSeries.resize(6);
                // .loli meminfo X values use seconds for Qt file parity.
                const double t = static_cast<double>(time_);
                session_->memSeries[0].push_back({t, static_cast<double>(info.Total)});
                session_->memSeries[1].push_back({t, static_cast<double>(info.NativeHeap)});
                session_->memSeries[2].push_back({t, static_cast<double>(info.GfxDev)});
                session_->memSeries[3].push_back({t, static_cast<double>(info.EGLmtrack)});
                session_->memSeries[4].push_back({t, static_cast<double>(info.GLmtrack)});
                session_->memSeries[5].push_back({t, static_cast<double>(info.Unknown)});
              });
            });
    }

    if (!channel_.IsConnecting() && !channel_.IsConnected()) {
        channel_.ConnectToServer(8000);
        Log("Connecting to application server...");
    }

    channel_.Pump();
    time_++;
}

void CliCaptureSession::OnLaunchFinished(bool ok, const std::string& error) {
    if (!ok) {
        LogError("Error starting app: " + error);
        Cleanup(1);
        return;
    }
    isConnected_ = true;
    launchComplete_ = true;
    lastScreenshotTime_ = time_ = 0;
    Log("Application started!");
    if (options_.duration > 0) {
        Log("Profiling for " + std::to_string(options_.duration) + " seconds...");
    } else {
        Log("Profiling... Press Ctrl+C to stop.");
    }
    // Initial meminfo dump happens on the next OnFixedUpdate.
}

void CliCaptureSession::OnStackData(
    const std::vector<StacktraceChannel::RawStackInfo>& stacks,
    const std::vector<StacktraceChannel::FreeInfo>& frees) {
    if (!isConnected_.load() || !isCapturing_.load())
        return;

    const bool noStack = IsNoStackMode();
    auto removeLiveAt = [&](std::size_t index) {
        auto& records = session_->records;
        session_->callStackMap.erase(records[index].uuid);
        liveRecordIndex_.erase(records[index].addr);
        const std::size_t last = records.size() - 1;
        if (index != last) {
            records[index] = std::move(records[last]);
            recordArrivalOrder_[index] = recordArrivalOrder_[last];
            liveRecordIndex_[records[index].addr] = index;
        }
        records.pop_back();
        recordArrivalOrder_.pop_back();
    };
    for (const auto& stack : stacks) {
        if (options_.useCache) {
            const auto freed = freeIndex_.find(stack.addr);
            if (freed != freeIndex_.end() &&
                session_->freeAddrMap[freed->second].second > stack.seq)
                continue;
            const auto old = liveRecordIndex_.find(stack.addr);
            if (old != liveRecordIndex_.end()) {
                if (session_->records[old->second].seq > stack.seq)
                    continue;
                removeLiveAt(old->second);
            }
        }
        loli::Record record;
        record.uuid = LoliUuid::CreateUuid();
        record.seq = stack.seq;
        record.time = static_cast<int32_t>(stack.time);
        record.size = static_cast<int32_t>(stack.size);
        record.addr = stack.addr;
        if (noStack) {
            // nostack mode: library name in the record, interned
            // (HashStringLite ctor computes the qHash-compatible code and
            // interns into the shared lite table; mirror into the session's
            // own table for the .loli writer).
            HashStringLite lib(stack.library);
            record.libHash = lib.hashcode_;
            session_->internTable.emplace(record.libHash, stack.library);
        } else {
            loli::CallStack& callstack = session_->callStackMap[record.uuid];
            callstack.reserve(stack.stacktraces.size());
            for (const uint64_t addr : stack.stacktraces)
                callstack.emplace_back(0u, addr);
        }
        session_->records.push_back(record);
        if (options_.useCache) {
            liveRecordIndex_[record.addr] = session_->records.size() - 1;
            recordArrivalOrder_.push_back(nextRecordOrdinal_++);
        }
    }

    for (const auto& free : frees) {
        const auto found = freeIndex_.find(free.second);
        if (found == freeIndex_.end()) {
            freeIndex_.emplace(free.second, session_->freeAddrMap.size());
            session_->freeAddrMap.emplace_back(free.second, free.first);
        } else {
            uint32_t& latestSeq = session_->freeAddrMap[found->second].second;
            if (free.first > latestSeq)
                latestSeq = free.first;
        }
        if (options_.useCache) {
            const auto live = liveRecordIndex_.find(free.second);
            if (live != liveRecordIndex_.end() &&
                session_->records[live->second].seq < free.first)
                removeLiveAt(live->second);
        }
    }
}

void CliCaptureSession::PumpChannel() {
    if (isCapturing_.load() && !finished_.load())
        channel_.Pump();
}

void CliCaptureSession::OnConnectionLost() {
    if (!isCapturing_.load())
        return;
    // Real-world flows (e.g. the UAM game skipping its DLC dialog) restart the
    // app process mid-capture; libloli re-registers its socket server in the
    // new process and profiling can continue. Instead of the Qt fatal path,
    // drop the connection state and let FixedUpdate() reconnect. The data
    // accumulates across the restart (a fresh app instance resets the
    // record sequence; the capture keeps what it already received).
    Log("Connection lost; the app may have restarted. Reconnecting...");
    isConnected_ = false;
    if (stopRequested_.load()) {
        // User asked to stop while the connection dropped: finish now.
        Cleanup(1);
        return;
    }
    // FixedUpdate() reconnects on its next Tick (ConnectToServer when not
    // connected). If the app is really dead, the reconnect just fails
    // quietly until the user stops or the duration elapses.
}

void CliCaptureSession::BeginStopSequence() {
    smapsDumpPending_ = true;
    smapsDumpAcked_ = false;
    smapsText_.clear();
    smapsDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    Log("Duration elapsed, stopping capture...");
    // Ask the hook to stop producing data before any blocking smaps read or
    // serialization. Keep pumping the channel until it acknowledges so the
    // server can finish sending older packets and receive this command.
    Log("Waiting for device smaps dump...");
    const char command = static_cast<char>(StacktraceChannel::LoliCommand::SmapsDump);
    channel_.Send(&command, 1);
}

void CliCaptureSession::FinishStopSequence() {
    smapsDumpPending_ = false;

    if (options_.useCache && recordArrivalOrder_.size() == session_->records.size()) {
        std::vector<std::size_t> order(recordArrivalOrder_.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return recordArrivalOrder_[a] < recordArrivalOrder_[b];
        });
        std::vector<loli::Record> sorted;
        sorted.reserve(order.size());
        for (const std::size_t index : order)
            sorted.push_back(std::move(session_->records[index]));
        session_->records = std::move(sorted);
        recordArrivalOrder_.clear();
        liveRecordIndex_.clear();
    }

    // Pull + parse the smaps snapshot (Qt StopCaptureProcess parity: the
    // sections map runtime addresses to (library, virtual address)).
    std::string smapsText = std::move(smapsText_);
    std::unordered_map<std::string, SMapsSectionLite> smaps;
    // Only trust the remote file after an acknowledgement. Without it, that
    // file might belong to a previous process with a different ASLR layout.
    bool readSMaps = !smapsText.empty() ||
                      (smapsDumpAcked_ && PullSMapsFile(smapsText));
    if (!readSMaps && AdbPidAlive()) {
        // The socket command may time out under extreme traffic. A debuggable
        // app still allows a direct, process-specific smaps snapshot.
        ProcessRunner runner;
        std::vector<std::string> args;
        if (!options_.deviceSerial.empty())
            args.push_back("-s"), args.push_back(options_.deviceSerial);
        args.insert(args.end(), {"shell", "run-as", options_.appName, "cat",
                                 "/proc/" + appPid_ + "/smaps"});
        runner.SetProgram(options_.adbPath);
        runner.SetArguments(args);
        if (runner.Start() && runner.WaitForFinished(30000) && runner.GetExitCode() == 0) {
            smapsText = runner.ReadAllStdout();
            readSMaps = !smapsText.empty();
        }
    }
    if (!readSMaps) {
        Log("Failed to read proc/pid/smaps");
    } else {
        ParseSmapsText(smapsText, smaps);
    }

    // Resolve each callstack frame to (library, symbol vaddr) using the smaps
    // sections (Qt CliProfiler::InterpretRecordLibrary parity: frame library
    // comes from the containing section; addr becomes vaddr = addr - bias).
    if (!smaps.empty()) {
        const SMapsAddressIndex addressIndex(smaps);
        std::unordered_map<std::string, uint32_t> libraryHashes;
        size_t resolved = 0;
        for (auto& kv : session_->callStackMap) {
            for (auto& frame : kv.second) {
                const std::string* library = nullptr;
                uint64_t vaddr = 0;
                if (!addressIndex.Resolve(frame.second, library, vaddr))
                    continue;
                auto cached = libraryHashes.find(*library);
                if (cached == libraryHashes.end()) {
                    const uint32_t hash = HashStringLite(*library).hashcode_;
                    cached = libraryHashes.emplace(*library, hash).first;
                    session_->internTable.emplace(hash, *library);
                }
                frame.first = cached->second;
                frame.second = vaddr;
                ++resolved;
            }
        }
        Log("Resolved " + std::to_string(resolved) + " frames to libraries via smaps.");

        // Store the smaps sections in the session (part of the .loli output).
        for (const auto& kv : smaps) {
            loli::SMapsSection sec;
            for (const auto& a : kv.second.addrs_)
                sec.addrs.emplace_back(a.start_, a.end_, a.offset_);
            sec.virtual_ = kv.second.virtual_;
            sec.rss_ = kv.second.rss_;
            sec.pss_ = kv.second.pss_;
            sec.privateClean_ = kv.second.privateClean_;
            sec.privateDirty_ = kv.second.privateDirty_;
            sec.sharedClean_ = kv.second.sharedClean_;
            sec.sharedDirty_ = kv.second.sharedDirty_;
            session_->smapsSections.emplace(kv.first, std::move(sec));
        }
    }

    Log("Captured " + std::to_string(session_->records.size()) + " records.");

    // Translate only frames mapped to the requested library. Other libraries
    // may have the same virtual addresses and must keep their own names.
    if (!options_.symbolPath.empty()) {
        Log("Translating symbols...");
        std::string symLibName = options_.symbolPath;
        const std::size_t slash = symLibName.find_last_of("/\\");
        if (slash != std::string::npos)
            symLibName = symLibName.substr(slash + 1);
        std::vector<uint64_t> addrs;
        for (const auto& kv : session_->callStackMap)
            for (const auto& frame : kv.second) {
                const auto lib = session_->internTable.find(frame.first);
                if (lib != session_->internTable.end() && lib->second == symLibName)
                    addrs.push_back(frame.second);
            }
        std::sort(addrs.begin(), addrs.end());
        addrs.erase(std::unique(addrs.begin(), addrs.end()), addrs.end());

        auto& map = session_->symbolMap[symLibName];
        std::string symbolizerPath;
        const std::string ndk = PathUtilsLite::GetNDKPath();
        if (!ndk.empty()) {
#ifdef _WIN32
            const std::string candidate = ndk + "/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe";
#else
            const std::string candidate = ndk + "/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-symbolizer";
#endif
            std::ifstream probe(candidate.c_str());
            if (probe.good())
                symbolizerPath = candidate;
        }
        if (!symbolizerPath.empty()) {
            std::unordered_map<uint64_t, std::string> resolved;
            const size_t count = loli::TranslateAddressesSymbolizer(
                symbolizerPath, options_.symbolPath, addrs, resolved);
            for (const auto& kv : resolved)
                map[kv.first] = kv.second;
            Log("llvm-symbolizer translated " + std::to_string(count) +
                " / " + std::to_string(addrs.size()) + " addresses.");
        }

        // The nm tool lives next to the NDK python (same as the Qt flow's
        // GetNDKToolPath("nm", arch != "arm64-v8a")); the arch must match
        // the capture config, else nm rejects the .so format.
        const std::string nmPath = PathUtilsLite::GetNDKToolPath(
            "nm", loli::LoadCaptureConfig().arch == "armeabi-v7a");

        // Extract the nm table once, then resolve every distinct frame vaddr
        // via containment ([symbol, symbol+size]) - exact-address keys would
        // almost never match since frames land INSIDE functions
        // (Qt MainWindow::TryAddNewAddress parity).
        std::vector<loli::SymbolRecord> table;
        if (map.size() < addrs.size() &&
            ExtractSymbolTable(nmPath.empty() ? "nm" : nmPath,
                               options_.symbolPath, table)) {
            size_t translated = 0;
            for (const uint64_t addr : addrs) {
                if (map.find(addr) != map.end())
                    continue;
                const std::string name = loli::LookupSymbol(table, addr);
                if (!name.empty()) {
                    map.emplace(addr, name);
                    ++translated;
                }
            }
            Log("Translated " + std::to_string(translated) + " symbols.");
        } else if (map.empty() && !addrs.empty()) {
            Log("Failed to extract symbols from " + options_.symbolPath);
        }
    }

    // Write the .loli.
    Log("Saving to " + options_.outputFile + "...");
    std::vector<uint8_t> bytes;
    if (!loli::WriteSession(*session_, bytes)) {
        LogError("Failed to serialize session");
        Cleanup(1);
        return;
    }
    std::ofstream out(options_.outputFile.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        LogError("Failed to save output file");
        Cleanup(1);
        return;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out.good()) {
        LogError("Failed to save output file");
        Cleanup(1);
        return;
    }
    Log("Profile saved successfully!");
    Cleanup(0);
}

bool CliCaptureSession::PullSMapsFile(std::string& outText) {
    const std::string localPath = options_.outputFile + ".smaps.tmp";
    std::remove(localPath.c_str());
    ProcessRunner pull;
    std::vector<std::string> args;
    if (!options_.deviceSerial.empty())
        args.push_back("-s"), args.push_back(options_.deviceSerial);
    args.push_back("pull");
    args.push_back("/data/local/tmp/smaps.txt");
    args.push_back(localPath);
    pull.SetProgram(options_.adbPath);
    pull.SetArguments(args);
    if (!pull.Start() || !pull.WaitForFinished(30000) || pull.GetExitCode() != 0)
        return false;
    if (!ReadTextFile(localPath, outText))
        return false;
    std::remove(localPath.c_str());
    return !outText.empty();
}

void CliCaptureSession::AdbForwardRemoveAll() {
    ProcessRunner r;
    std::vector<std::string> args;
    if (!options_.deviceSerial.empty())
        args.push_back("-s"), args.push_back(options_.deviceSerial);
    args.push_back("forward");
    args.push_back("--remove-all");
    r.SetProgram(options_.adbPath);
    r.SetArguments(args);
    if (r.Start())
        r.WaitForFinished(10000);
}

bool CliCaptureSession::AdbPidAlive() {
    if (appPid_.empty())
        return false;
    ProcessRunner r;
    std::vector<std::string> args;
    if (!options_.deviceSerial.empty())
        args.push_back("-s"), args.push_back(options_.deviceSerial);
    args.push_back("shell");
    args.push_back("pidof");
    args.push_back(options_.appName);
    r.SetProgram(options_.adbPath);
    r.SetArguments(args);
    if (!r.Start() || !r.WaitForFinished(5000))
        return false;
    return !r.ReadAllStdout().empty();
}

void CliCaptureSession::Cleanup(int exitCode) {
    isCapturing_ = false;
    isConnected_ = false;
    launchComplete_ = false;
    channel_.Disconnect();
    AdbForwardRemoveAll();
    if (launchThread_.joinable())
        launchThread_.detach(); // it may still be waiting on adb; harmless
    exitCode_ = exitCode;
    finished_ = true;
}

bool CliCaptureSession::IsFinished(int* exitCode) const {
    if (exitCode && finished_.load())
        *exitCode = exitCode_.load();
    return finished_.load();
}

} // namespace loli
