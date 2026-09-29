#ifndef CLICAPTURE_H
#define CLICAPTURE_H

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "adbtools.h"
#include "captureconfig.h"
#include "eventbus.h"
#include "lolirecord.h"
#include "launchdriver.h"
#include "stacktracechannel.h"
#include "symboltranslator.h"

// Qt-free headless capture engine (task 5.1): replaces CliProfiler's
// capture loop. Owns the same lifecycle - launch/attach via LaunchDriver,
// 1s fixed-update tick (meminfo + screenshot polling + stacktrace
// (re)connect), stacktrace data accumulation, smaps dump on stop, symbol
// translation, .loli save - entirely on LoliCore APIs (design D12: the GUI
// consumes the same building blocks).
//
// Threading: the consumer drives Tick() (the CLI main loop calls it every
// second; the GUI's frame loop can do the same). Callbacks from adb
// processes arrive on watcher threads and are marshalled into the session
// state via the EventBus; Tick() pumps it. All public state accessors are
// safe to call from the consumer thread after Tick().
namespace loli {

struct CliCaptureOptions {
    std::string appName;
    std::string subProcessName;
    std::string deviceSerial;
    std::string outputFile;
    std::string symbolPath;
    int duration = 0;              // seconds; 0 = until Stop()
    bool attachMode = false;       // attach to running app (enableInject)
    bool useCache = false;         // opt-in: retain only live allocations
    std::string adbPath;
    std::string pythonPath;
    std::string executableDir;     // for libloli.so push + cache/ folder
};

class CliCaptureSession {
    friend struct CliCaptureTestAccess;
public:
    // Log line sink (console + file in the CLI).
    using Logger = std::function<void(const std::string&)>;

    CliCaptureSession();
    ~CliCaptureSession();

    CliCaptureSession(const CliCaptureSession&) = delete;
    CliCaptureSession& operator=(const CliCaptureSession&) = delete;

    void SetLogger(Logger logger) { logger_ = std::move(logger); }

    // Starts the launch sequence on a worker thread (it blocks for the adb
    // steps). Returns false if a capture is already active. When the
    // sequence settles, OnLaunchFinished fires via Tick().
    bool Start(const CliCaptureOptions& options);

    // Requests a graceful stop (smaps dump + translate + save). Safe from
    // any thread; processed in Tick().
    void RequestStop();

    // Drives one fixed-update step: pumps events, ticks the 1s timer,
    // reconnects the stacktrace channel. Call from the consumer loop.
    void Tick();

    // Drain incoming allocation packets between the one-second device polls.
    // Must be called on the same consumer thread as Tick().
    void PumpChannel();

    // ---- state (consumer thread) ----
    bool IsCapturing() const { return isCapturing_.load(); }
    bool IsConnected() const { return isConnected_.load(); }
    bool IsLaunchComplete() const { return launchComplete_.load(); }
    bool IsStopping() const { return stopRequested_.load(); }
    // True when the session fully finished (success or failure). exitCode
    // mirrors the Qt CLI's Cleanup() code (0 success).
    bool IsFinished(int* exitCode = nullptr) const;

    const loli::Session& GetSession() const { return *session_; }

private:
    void OnLaunchFinished(bool ok, const std::string& error);
    void OnStackData(const std::vector<StacktraceChannel::RawStackInfo>& stacks,
                     const std::vector<StacktraceChannel::FreeInfo>& frees);
    void OnConnectionLost();
    void OnFixedUpdate();
    void BeginStopSequence();
    void FinishStopSequence();
    void Cleanup(int exitCode);
    void Log(const std::string& s);
    void LogError(const std::string& s);
    void QueueOnConsumer(std::function<void()> callback);
    // Blocking helpers used inside the stop sequence (worker thread).
    bool PullSMapsFile(std::string& outText);
    void AdbForwardRemoveAll();
    bool AdbPidAlive();

    CliCaptureOptions options_;
    Logger logger_;

    // Declared before the async workers so it remains alive while their
    // destructors join any watcher callback still posting its final result.
    std::mutex callbacksMutex_;
    std::vector<std::function<void()>> pendingCallbacks_;
    std::shared_ptr<loli::Session> session_;
    std::unordered_map<uint64_t, std::size_t> freeIndex_;
    std::unordered_map<uint64_t, std::size_t> liveRecordIndex_;
    std::vector<uint64_t> recordArrivalOrder_;
    uint64_t nextRecordOrdinal_ = 0;
    StacktraceChannel channel_;
    MemInfoDumper memDumper_;
    ScreenshotCapture screenshot_;

    EventBus bus_;                 // marshals watcher-thread callbacks to Tick()
    LaunchDriver launch_;
    std::thread launchThread_;

    std::atomic<bool> isCapturing_{false};
    std::atomic<bool> isConnected_{false};
    std::atomic<bool> launchComplete_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> finished_{false};
    std::atomic<int> exitCode_{-1};

    int time_ = 0;
    int lastScreenshotTime_ = 0;
    int maxMemInfoValue_ = 128;
    std::string appPid_;
    std::string smapsText_;       // direct read from the current process
    bool smapsDumpPending_ = false;   // sent SmapsDump cmd, awaiting ack/pull
    bool smapsDumpAcked_ = false;
    std::chrono::steady_clock::time_point smapsDeadline_;

    // Event-topic names for cross-thread marshalling.
    static constexpr const char* kEvLaunchDone = "cli.launchdone";
    static constexpr const char* kEvStopRequested = "cli.stopreq";
};

} // namespace loli

#endif // CLICAPTURE_H
