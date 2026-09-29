#ifndef LOLI_PROFILER_GUI_GUIDATABRIDGE_H
#define LOLI_PROFILER_GUI_GUIDATABRIDGE_H

// GuiDataBridge - the capture/session engine behind the ImGui panels.
//
// Qt-free since task 6.1 (design D12): the bridge owns the LoliCore capture
// services (LaunchDriver, StacktraceChannel, MemInfoDumper, Screenshot
// Capture) directly - no QObject/signals/Qt event loop. The ImGui frame
// loop calls Tick() once per frame; all callbacks fire on that thread.
// Record loading runs on a ThreadPool worker; the UI polls
// IsLoading()/LoadProgress().
//
// Snapshots: immutable GuiSnapshot PODs are swapped in only when data
// changes (capture ticks throttled to ~1 Hz, load completion, etc).

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "adbtools.h"
#include "guisnapshot.h"
#include "launchdriver.h"
#include "lolilogger.h"
#include "lolirecord.h"
#include "pathutilslite.h"
#include "stacktracechannel.h"
#include "threadpool.h"

namespace gui {

// Lightweight device info for enumeration.
struct DeviceInfoLite {
    std::string serial;
    std::string model;
    std::string device;
    std::string state;
};

class GuiDataBridge {
public:
    GuiDataBridge();
    ~GuiDataBridge();

    GuiDataBridge(const GuiDataBridge&) = delete;
    GuiDataBridge& operator=(const GuiDataBridge&) = delete;

    // ---- device / app enumeration (synchronous adb, call off-frame) ----
    std::vector<DeviceInfoLite> EnumerateDevices(std::string* error = nullptr);
    std::vector<std::string> ListInstalledApps(const std::string& deviceSerial);

    // ---- capture lifecycle ----
    bool StartCapture(const std::string& deviceSerial,
                      const std::string& appName,
                      const std::string& subProcessName,
                      const CaptureConfigSnapshot& config,
                      bool enableInject = false,  // Attach to running app
                      bool useCache = true);     // data optimization
    void StopCapture();
    bool IsCapturing() const { return isCapturing_; }
    bool ShouldMaskCaptureView() const {
        return isCapturing_ && (!captureConnectionEverEstablished_ || isConnected_.load());
    }
    bool IsFinalizing() const { return finalizing_.load(); }
    bool IsConnected() const { return isConnected_.load(); }
    std::string TakeLaunchError() { return std::exchange(launchError_, std::string()); }

    // ---- per-frame pump (call from the ImGui loop; drives everything) ----
    void Tick();

    // ---- record file I/O ----
    void LoadRecord(const std::string& path);
    bool IsLoading() const { return loading_.load(); }
    float LoadProgress() const { return loadProgress_.load(); }
    std::string LoadStatus() const;
    bool SaveRecord(const std::string& path);
    bool IsSaving() const { return saving_.load(); }
    bool LastSaveResult() const { return lastSaveResult_.load(); }

    // ---- config ----
    CaptureConfigSnapshot GetCaptureConfig() const;
    void SaveCaptureConfig(const CaptureConfigSnapshot& config);

    // Named capture-config presets (the "saved:" blocks in loli3.conf). The
    // current config is persisted alongside the presets on every save/delete.
    std::vector<std::pair<std::string, CaptureConfigSnapshot>> GetSavedCaptureConfigs() const;
    void SaveCaptureConfigPreset(const std::string& name, const CaptureConfigSnapshot& config);
    void DeleteCaptureConfigPreset(const std::string& name);

    // ---- snapshot access (ImGui render thread) ----
    std::shared_ptr<const GuiSnapshot> AcquireSnapshot() const;
    uint64_t SnapshotVersion() const;
    uint64_t SessionEpoch() const { return sessionEpoch_; }

    struct TimeRangeTrees {
        std::shared_ptr<StacktraceTree> all;
        std::shared_ptr<StacktraceTree> live;
        std::size_t recordCount = 0;
        std::size_t liveRecordCount = 0;
        uint64_t totalBytes = 0;
        uint64_t liveBytes = 0;
    };
    // Only call while capture/loading is idle. Returns false when no session
    // is available. The result is built on the existing worker pool.
    bool RequestTimeRangeTrees(double fromMs, double toMs);
    bool TryTakeTimeRangeTrees(TimeRangeTrees& out);

    struct LeakAnalysisResult {
        std::shared_ptr<StacktraceTree> tree;
        std::string error;
    };
    bool RequestPossibleLeaks(int32_t startMs, int32_t endMs, bool persistentOnly);
    bool TryTakePossibleLeaks(LeakAnalysisResult& out);

private:
    void AppendLog(const std::string& line,
                   loli::LogLevel level = loli::LogLevel::Info);
    void PublishSnapshot();
    void FixedUpdate();
    void DrainCaptureSamples();
    void OnStacktraceData(const std::vector<StacktraceChannel::RawStackInfo>& stacks,
                          const std::vector<StacktraceChannel::FreeInfo>& frees);
    void OnLaunchFinished(bool ok);

    // LoliCore capture services (owned).
    StacktraceChannel stacktraceChannel_;
    loli::MemInfoDumper memDumper_;
    loli::ScreenshotCapture screenshot_;
    LaunchDriver launch_;
    std::thread launchThread_;
    ThreadPool pool_;

    // ---- working/published snapshots ----
    std::shared_ptr<GuiSnapshot> working_;
    std::shared_ptr<const GuiSnapshot> published_;
    mutable std::mutex publishMutex_;
    uint64_t publishVersion_ = 0;

    // ---- session model for SaveRecord ----
    std::shared_ptr<loli::Session> sessionData_;
    bool sessionAllRecordsLive_ = false;
    uint64_t sessionEpoch_ = 0;
    std::future<TimeRangeTrees> timeRangeFuture_;
    std::shared_ptr<const loli::Session> timeRangeSession_;
    std::future<LeakAnalysisResult> leakFuture_;
    std::shared_ptr<const loli::Session> leakSession_;
    std::unordered_map<uint64_t, std::size_t> freeIndex_;
    std::unordered_map<uint64_t, std::size_t> liveRecordIndex_;
    std::vector<uint64_t> recordArrivalOrder_;
    uint64_t nextRecordOrdinal_ = 0;
    bool persistentOnly_ = false;
    std::atomic<bool> finalizing_{false};
    struct FinalizeResult {
        std::vector<SMapsSectionSnapshot> sections;
        std::size_t resolved = 0;
        std::string error;
    };
    std::future<FinalizeResult> finalizeFuture_;

    // ---- async record loading ----
    std::atomic<bool> loading_{false};
    std::atomic<float> loadProgress_{0.0f};
    std::string loadStatus_;
    mutable std::mutex loadStatusMutex_;
    struct LoadResult;
    std::future<std::shared_ptr<LoadResult>> loadFuture_;

    // ---- async record saving ----
    std::atomic<bool> saving_{false};
    std::atomic<bool> lastSaveResult_{false};
    struct SaveResult {
        bool ok = false;
        std::string path;
        std::size_t recordCount = 0;
    };
    std::future<SaveResult> saveFuture_;

    // ---- capture state ----
    std::string appName_;
    std::string subProcessName_;
    std::string deviceSerial_;
    std::string launchError_;
    std::string appPid_;
    int time_ = 0;
    std::chrono::steady_clock::time_point captureClockStart_{};
    int lastFixedSecond_ = -1;
    int lastScreenshotTime_ = 0;
    int lastPublishMs_ = 0;
    bool isCapturing_ = false;
    bool captureConnectionEverEstablished_ = false;
    std::atomic<bool> isConnected_{false};
    std::atomic<int> launchOutcome_{0}; // 0 pending, 1 success, -1 failure
    // Live-capture tree rebuild in flight (one worker build at a time).
    std::atomic<bool> treeRebuildInFlight_{false};
    std::atomic<uint64_t> captureGeneration_{0};
    std::mutex liveTreeMutex_;
    std::shared_ptr<StacktraceTree> pendingLiveTree_;
    struct PendingCaptureSample {
        uint64_t generation = 0;
        int32_t timeMs = 0;
        bool screenshot = false;
        loli::MemInfo memInfo;
        std::string appPid;
        std::vector<uint8_t> png;
    };
    std::mutex captureSampleMutex_;
    std::vector<PendingCaptureSample> captureSamples_;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_GUIDATABRIDGE_H
