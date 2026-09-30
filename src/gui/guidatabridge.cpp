#include "guidatabridge.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numeric>
#include <filesystem>

#include "captureconfig.h"
#include "lolilogger.h"
#include "hashstringlite.h"
#include "loliuuid.h"
#include "stacktracetree.h"
#include "leakdetect.h"
#include "smaps/smapssectionlite.h"

namespace gui {

// Result produced by the background record loader.
struct GuiDataBridge::LoadResult {
    bool ok = false;
    std::string path;
    std::string fileName;
    std::shared_ptr<GuiSnapshot> snapshot;
    std::shared_ptr<loli::Session> session;
    bool allRecordsLive = false;
    std::string error;
};

namespace {

bool AllSavedRecordsLive(const loli::Session& session) {
    if (session.freeAddrMap.empty() || session.records.empty())
        return false;
    std::unordered_map<uint64_t, uint32_t> freedAt;
    freedAt.reserve(session.freeAddrMap.size());
    for (const auto& entry : session.freeAddrMap) {
        auto& latest = freedAt[entry.first];
        latest = std::max(latest, entry.second);
    }
    for (const auto& record : session.records) {
        const auto it = freedAt.find(record.addr);
        if (it != freedAt.end() && record.seq < it->second)
            return false;
    }
    return true;
}

// Qt writes chart X coordinates and screenshot timestamps in seconds, while
// allocation records use milliseconds. Early Qt-free captures wrote all three
// in milliseconds. Normalize the session's chart/shot fields to Qt seconds
// when loading so SaveRecord can write it directly without cloning the large
// callstack map. GuiSnapshot converts them back to ms for the current UI.
bool LooksLikeMilliseconds(const std::vector<double>& times, int32_t recordMaxMs) {
    if (times.empty()) return false;
    const double last = *std::max_element(times.begin(), times.end());
    if (recordMaxMs >= 10000 && last > 0.0) {
        const double ratio = static_cast<double>(recordMaxMs) / last;
        if (ratio >= 100.0) return false;
        if (ratio <= 10.0) return true;
    }
    std::vector<double> gaps;
    const size_t limit = std::min(times.size(), size_t{1024});
    for (size_t i = 1; i < limit; ++i) {
        const double gap = times[i] - times[i - 1];
        if (gap > 0.0) gaps.push_back(gap);
    }
    if (!gaps.empty()) {
        auto mid = gaps.begin() + gaps.size() / 2;
        std::nth_element(gaps.begin(), mid, gaps.end());
        return *mid >= 100.0;
    }
    return last >= 1000.0 && recordMaxMs > 0 && last > recordMaxMs / 10.0;
}

void NormalizeSessionTimelineToQtSeconds(loli::Session& session) {
    int32_t recordMaxMs = 0;
    for (const auto& record : session.records)
        recordMaxMs = std::max(recordMaxMs, record.time);
    std::vector<double> memTimes;
    if (!session.memSeries.empty()) {
        memTimes.reserve(session.memSeries[0].size());
        for (const auto& point : session.memSeries[0])
            memTimes.push_back(point.time);
    }
    std::vector<double> shotTimes;
    shotTimes.reserve(session.screenshots.size());
    for (const auto& shot : session.screenshots)
        shotTimes.push_back(shot.timeMs);
    const bool memWasMs = LooksLikeMilliseconds(memTimes, recordMaxMs);
    const bool shotWasMs = memTimes.empty()
        ? LooksLikeMilliseconds(shotTimes, recordMaxMs) : memWasMs;
    if (memWasMs)
        for (auto& series : session.memSeries)
            for (auto& point : series)
                point.time /= 1000.0;
    if (shotWasMs)
        for (auto& shot : session.screenshots)
            shot.timeMs /= 1000;

    // One early ImGui capture path incremented its nominal "seconds" counter
    // once per rendered frame. Those files have a long chart, sparse chart
    // samples, and screenshot gaps far above the intended five seconds, while
    // allocation record times still reflect elapsed milliseconds. The exact
    // per-sample wall clock was not stored, so align the chart approximately
    // with the last allocation time and warn when doing so.
    if (session.memSeries.empty() || session.memSeries[0].size() < 20 ||
        recordMaxMs < 10000)
        return;
    const auto& samples = session.memSeries[0];
    const double chartSeconds = samples.back().time;
    const double captureSeconds = static_cast<double>(recordMaxMs) / 1000.0;
    const double chartSpan = chartSeconds - samples.front().time;
    const double sampleGap = chartSpan / static_cast<double>(samples.size() - 1);
    bool screenshotGapConfirms = false;
    if (session.screenshots.size() >= 10) {
        const double shotSpan = static_cast<double>(session.screenshots.back().timeMs -
                                                    session.screenshots.front().timeMs);
        screenshotGapConfirms = shotSpan /
            static_cast<double>(session.screenshots.size() - 1) > 10.0;
    }
    if (chartSeconds > captureSeconds * 10.0 && sampleGap > 2.5 &&
        (session.screenshots.size() >= 10 ? screenshotGapConfirms
                                          : chartSeconds > captureSeconds * 20.0)) {
        const double scale = captureSeconds / chartSeconds;
        for (auto& series : session.memSeries)
            for (auto& point : series)
                point.time *= scale;
        for (auto& shot : session.screenshots)
            shot.timeMs = static_cast<int32_t>(std::round(shot.timeMs * scale));
        LOLI_WARN("file") << "Legacy GUI capture used frame-count timeline units; "
                          << "approximately rescaled chart_seconds=" << chartSeconds
                          << " to record_seconds=" << captureSeconds
                          << " factor=" << scale;
    }
}

int32_t SecondsToSnapshotMs(double seconds) {
    const double millis = std::round(seconds * 1000.0);
    return static_cast<int32_t>(std::clamp(millis, 0.0,
        static_cast<double>(std::numeric_limits<int32_t>::max())));
}

std::string RunAdbText(const std::string& adb, const std::string& serial,
                       std::vector<std::string> command, uint32_t timeoutMs = 10000) {
    ProcessRunner process;
    if (!serial.empty()) {
        command.insert(command.begin(), serial);
        command.insert(command.begin(), "-s");
    }
    process.SetProgram(adb);
    process.SetArguments(command);
    if (!process.Start())
        return {};
    if (!process.WaitForFinished(timeoutMs)) {
        process.Kill();
        return {};
    }
    return process.GetExitCode() == 0 ? process.ReadAllStdout() : std::string();
}

std::string DeviceSmaps(const std::string& adb, const std::string& serial,
                        const std::string& app, const std::string& subProcess,
                        std::string pid) {
    if (pid.empty()) {
        const std::string processName = subProcess.empty() ? app :
            app + (subProcess[0] == ':' ? "" : ":") + subProcess;
        const std::string output = RunAdbText(adb, serial,
                                              {"shell", "pidof", processName});
        pid = output.substr(0, output.find_first_of(" \r\n\t"));
    }
    if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        }))
        return {};
    const std::string procPath = "/proc/" + pid + "/smaps";
    std::string text = RunAdbText(adb, serial,
                                  {"shell", "run-as", app, "cat", procPath});
    if (text.find("Size:") != std::string::npos)
        return text;
    text = RunAdbText(adb, serial, {"shell", "su", "-c", "cat " + procPath});
    if (text.find("Size:") != std::string::npos)
        return text;
    // Some su implementations do not forward large stdout streams reliably.
    // Write a capture-specific file and pull it, as the CLI stop flow does.
    const std::string remote = "/data/local/tmp/loli_gui_smaps_" + pid + ".txt";
    RunAdbText(adb, serial,
               {"shell", "su", "-c", "cat " + procPath + " > " + remote});
    const auto local = std::filesystem::temp_directory_path() /
                       ("loli_gui_smaps_" + pid + ".txt");
    std::error_code ignored;
    std::filesystem::remove(local, ignored);
    RunAdbText(adb, serial, {"pull", remote, local.string()});
    std::ifstream file(local, std::ios::binary);
    if (file.good())
        text.assign(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(local, ignored);
    RunAdbText(adb, serial, {"shell", "su", "-c", "rm -f " + remote});
    return text.find("Size:") != std::string::npos ? text : std::string();
}

// Reads a whole file (record loader).
bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.good())
        return false;
    in.seekg(0, std::ios::end);
    const std::streampos size = in.tellg();
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char*>(out.data()), size);
    return in.good() || in.eof();
}

std::string BaseNameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Splits on whitespace runs.
std::vector<std::string> SplitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        const std::size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i > start)
            out.emplace_back(s, start, i - start);
    }
    return out;
}


// Resolves a frame's function name from the session symbol map. The map is
// keyed by frame virtual address (the CLI stamps it after nm containment
// lookup). Check the frame's own library first — keyed by full smaps path or
// by basename depending on the producer. Virtual addresses are *not* unique
// across libraries, so another library's symbols must never be used here.
std::string ResolveSymbolName(const loli::Session& session, const std::string& lib,
                              uint64_t addr) {
    auto inLib = [&](const std::string& key) -> std::string {
        auto it = session.symbolMap.find(key);
        if (it == session.symbolMap.end())
            return std::string();
        auto symIt = it->second.find(addr);
        return symIt != it->second.end() ? symIt->second : std::string();
    };
    std::string name = inLib(lib);
    if (name.empty())
        name = inLib(BaseNameOf(lib));
    if (name.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%llx",
                      static_cast<unsigned long long>(addr));
        name = buf;
    }
    return name;
}

// Builds the aggregated call tree from a session (shared by the record
// loader and the live-capture incremental rebuild; task 6.2).
void BuildTreeFromSession(const loli::Session& session, StacktraceTree& tree,
                          bool liveOnly = false, bool timeRange = false,
                          double fromMs = 0.0, double toMs = 0.0) {
    const auto& records = session.records;
    const size_t n = records.size();
    std::unordered_map<uint64_t, uint32_t> freeSequence;
    if (liveOnly) {
        freeSequence.reserve(session.freeAddrMap.size());
        for (const auto& entry : session.freeAddrMap)
            freeSequence[entry.first] = entry.second;
    }
    std::vector<uint32_t> recordSizes;
    std::vector<std::vector<StacktraceTree::RawFrameIdx>> recordFrames;
    const size_t reserveCount = timeRange ? std::min(n, size_t{100000}) : n;
    recordSizes.reserve(reserveCount);
    recordFrames.reserve(reserveCount);
    std::vector<std::string> funcNames;
    std::vector<std::string> libNames;
    std::unordered_map<std::string, uint32_t> funcIdxOf;
    std::unordered_map<std::string, uint32_t> libIdxOf;
    auto funcIdxFor = [&](const std::string& s) -> uint32_t {
        auto it = funcIdxOf.find(s);
        if (it != funcIdxOf.end()) return it->second;
        const uint32_t k = static_cast<uint32_t>(funcNames.size());
        funcNames.push_back(s);
        funcIdxOf.emplace(s, k);
        return k;
    };
    auto libIdxFor = [&](const std::string& s) -> uint32_t {
        auto it = libIdxOf.find(s);
        if (it != libIdxOf.end()) return it->second;
        const uint32_t k = static_cast<uint32_t>(libNames.size());
        libNames.push_back(s);
        libIdxOf.emplace(s, k);
        return k;
    };
    for (size_t i = 0; i < n; i++) {
        if (timeRange && (records[i].time < fromMs || records[i].time > toMs))
            continue;
        if (liveOnly) {
            const auto freed = freeSequence.find(records[i].addr);
            if (freed != freeSequence.end() && records[i].seq < freed->second)
                continue;
        }
        const int32_t sz = records[i].size;
        recordSizes.push_back(sz > 0 ? static_cast<uint32_t>(sz) : 0u);
        recordFrames.emplace_back();
        auto csIt = session.callStackMap.find(records[i].uuid);
        if (csIt == session.callStackMap.end() || csIt->second.empty())
            continue;
        const auto& cs = csIt->second;
        auto& frames = recordFrames.back();
        frames.reserve(cs.size());
        for (int j = static_cast<int>(cs.size()) - 1; j >= 0; j--) {
            const std::string lib = [&]() {
                auto it = session.internTable.find(cs[static_cast<size_t>(j)].first);
                return it != session.internTable.end() ? it->second
                                                       : std::string("unknown");
            }();
            const uint64_t addr = cs[static_cast<size_t>(j)].second;
            const std::string name = ResolveSymbolName(session, lib, addr);
            StacktraceTree::RawFrameIdx f;
            f.funcName = funcIdxFor(name);
            f.library = libIdxFor(lib);
            frames.push_back(f);
        }
    }
    tree.BuildFromRecords(recordSizes, recordFrames, funcNames, libNames);
}


} // namespace

GuiDataBridge::GuiDataBridge() {
    working_ = std::make_shared<GuiSnapshot>();
    published_ = working_;

    stacktraceChannel_.SetDataHandler(
        [this](const std::vector<StacktraceChannel::RawStackInfo>& stacks,
               const std::vector<StacktraceChannel::FreeInfo>& frees) {
            OnStacktraceData(stacks, frees);
        });
    stacktraceChannel_.SetConnectionLostHandler([this]() {
        AppendLog("Stacktrace connection lost.", loli::LogLevel::Warn);
        isConnected_ = false;
        if (working_)
            working_->capture.connected = false;
        PublishSnapshot();
    });
}

GuiDataBridge::~GuiDataBridge() {
    StopCapture();
    if (launchThread_.joinable())
        launchThread_.join();
    pool_.WaitIdle();
}

void GuiDataBridge::AppendLog(const std::string& line, loli::LogLevel level) {
    loli::LoliLogger::Instance().Write(level, "capture", line);
    if (working_)
        working_->logLines.push_back(line);
}

void GuiDataBridge::PublishSnapshot() {
    std::lock_guard<std::mutex> lock(publishMutex_);
    published_ = working_;
    ++publishVersion_;
}

std::shared_ptr<const GuiSnapshot> GuiDataBridge::AcquireSnapshot() const {
    std::lock_guard<std::mutex> lock(publishMutex_);
    return published_;
}

uint64_t GuiDataBridge::SnapshotVersion() const {
    std::lock_guard<std::mutex> lock(publishMutex_);
    return publishVersion_;
}

bool GuiDataBridge::RequestTimeRangeTrees(double fromMs, double toMs) {
    if (isCapturing_ || loading_.load() || finalizing_.load() ||
        !sessionData_ || sessionData_->records.empty() || fromMs > toMs)
        return false;
    LOLI_INFO("range") << "filter begin from_ms=" << fromMs
                       << " to_ms=" << toMs;
    const std::shared_ptr<const loli::Session> session = sessionData_;
    const bool allRecordsLive = sessionAllRecordsLive_;
    timeRangeSession_ = session;
    timeRangeFuture_ = pool_.Submit([session, fromMs, toMs, allRecordsLive]() {
        const auto totalStart = loli::LoliLogger::Clock::now();
        TimeRangeTrees result;
        result.all = std::make_shared<StacktraceTree>();
        const auto allStart = loli::LoliLogger::Clock::now();
        BuildTreeFromSession(*session, *result.all, false, true, fromMs, toMs);
        loli::LoliLogger::Instance().LogStage("range", "all_tree", allStart,
            "nodes=" + std::to_string(result.all->Nodes().size()));
        if (!session->freeAddrMap.empty()) {
            if (allRecordsLive) {
                result.live = result.all;
                loli::LoliLogger::Instance().Log("range", "live_tree aliases all_tree: saved free events exclude no records");
            } else {
                result.live = std::make_shared<StacktraceTree>();
                const auto liveStart = loli::LoliLogger::Clock::now();
                BuildTreeFromSession(*session, *result.live, true, true, fromMs, toMs);
                loli::LoliLogger::Instance().LogStage("range", "live_tree", liveStart,
                    "nodes=" + std::to_string(result.live->Nodes().size()));
            }
        }
        std::unordered_map<uint64_t, uint32_t> freeSequence;
        if (!allRecordsLive) {
            freeSequence.reserve(session->freeAddrMap.size());
            for (const auto& entry : session->freeAddrMap)
                freeSequence[entry.first] = entry.second;
        }
        for (const auto& record : session->records) {
            if (record.time < fromMs || record.time > toMs || record.size <= 0)
                continue;
            ++result.recordCount;
            result.totalBytes += static_cast<uint32_t>(record.size);
            const auto freed = freeSequence.find(record.addr);
            if (allRecordsLive || freed == freeSequence.end() || record.seq >= freed->second) {
                ++result.liveRecordCount;
                result.liveBytes += static_cast<uint32_t>(record.size);
            }
        }
        loli::LoliLogger::Instance().LogStage("range", "complete", totalStart,
            "from_ms=" + std::to_string(static_cast<int64_t>(fromMs)) +
            " to_ms=" + std::to_string(static_cast<int64_t>(toMs)) +
            " records=" + std::to_string(result.recordCount) +
            " bytes=" + std::to_string(result.totalBytes) +
            " persistent_records=" + std::to_string(result.liveRecordCount) +
            " persistent_bytes=" + std::to_string(result.liveBytes));
        return result;
    });
    return true;
}

bool GuiDataBridge::TryTakeTimeRangeTrees(TimeRangeTrees& out) {
    if (!timeRangeFuture_.valid() ||
        timeRangeFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    try {
        TimeRangeTrees result = timeRangeFuture_.get();
        if (timeRangeSession_ != sessionData_)
            return false;
        out = std::move(result);
        return true;
    } catch (const std::exception& e) {
        AppendLog(std::string("Time range filter failed: ") + e.what(),
                  loli::LogLevel::Error);
        return false;
    }
}

bool GuiDataBridge::RequestPossibleLeaks(int32_t startMs, int32_t endMs,
                                         bool persistentOnly) {
    if (isCapturing_ || loading_.load() || finalizing_.load() ||
        !sessionData_ || sessionData_->records.empty() ||
        startMs < 0 || endMs <= startMs ||
        (leakFuture_.valid() &&
         leakFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
        return false;
    const std::shared_ptr<const loli::Session> session = sessionData_;
    leakSession_ = session;
    leakFuture_ = pool_.Submit([session, startMs, endMs, persistentOnly]() {
        const auto start = loli::LoliLogger::Clock::now();
        LeakAnalysisResult result;
        result.tree = BuildPossibleLeaksTree(*session, startMs, endMs, persistentOnly);
        result.tree->ExpandAtLeast(10 * 1024 * 1024);
        loli::LoliLogger::Instance().LogStage("leaks", "complete", start,
            "from_ms=" + std::to_string(startMs) +
            " to_ms=" + std::to_string(endMs) +
            " nodes=" + std::to_string(result.tree->Nodes().size()) +
            " persistent=" + (persistentOnly ? "true" : "false"));
        return result;
    });
    return true;
}

bool GuiDataBridge::TryTakePossibleLeaks(LeakAnalysisResult& out) {
    if (!leakFuture_.valid() ||
        leakFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    try {
        out = leakFuture_.get();
        if (leakSession_ != sessionData_) {
            out = LeakAnalysisResult{};
            out.error = "The record changed during leak analysis.";
        }
    } catch (const std::exception& e) {
        out = LeakAnalysisResult{};
        out.error = e.what();
    }
    return true;
}

std::string GuiDataBridge::LoadStatus() const {
    std::lock_guard<std::mutex> lock(loadStatusMutex_);
    return loadStatus_;
}

// ---------------------------------------------------------------------------
// device / app enumeration
// ---------------------------------------------------------------------------
std::vector<DeviceInfoLite> GuiDataBridge::EnumerateDevices(std::string* error) {
    std::vector<DeviceInfoLite> out;
    if (error) error->clear();
    ProcessRunner process;
    const std::string adb = PathUtilsLite::GetADBExecutablePath();
    if (adb.empty() || !PathUtilsLite::FileExists(adb)) {
        if (error) *error = "ADB was not found. Set Android SDK in Settings.";
        return out;
    }
    process.SetProgram(adb);
    process.SetArguments({"devices", "-l"});
    if (!process.Start()) {
        if (error) *error = "Could not start ADB:\n" + adb + "\nCheck that the SDK path is valid and ADB is not blocked by another program.";
        return out;
    }
    if (!process.WaitForFinished(5000)) {
        process.Kill();
        if (error) *error = "ADB did not respond within 5 seconds. Close other programs using ADB, then retry.";
        return out;
    }
    if (process.GetExitCode() != 0) {
        std::string detail = process.ReadAllStderr();
        if (detail.empty()) detail = process.ReadAllStdout();
        if (error) *error = "ADB failed (exit code " + std::to_string(process.GetExitCode()) + ").\n" + detail + "\nClose other programs using ADB, then retry.";
        return out;
    }
    const std::string output = process.ReadAllStdout();
    std::size_t start = 0;
    bool firstLine = true;
    for (std::size_t i = 0; i <= output.size(); ++i) {
        if (i != output.size() && output[i] != '\n')
            continue;
        std::string line = output.substr(start, i - start);
        start = i + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (firstLine) { // "List of devices attached"
            firstLine = false;
            continue;
        }
        if (line.empty() || line[0] == '*')
            continue;
        const auto parts = SplitWhitespace(line);
        if (parts.empty())
            continue;
        DeviceInfoLite d;
        d.serial = parts[0];
        d.state = parts.size() > 1 ? parts[1] : std::string();
        for (const auto& p : parts) {
            if (p.rfind("model:", 0) == 0)
                d.model = p.substr(6);
            if (p.rfind("device:", 0) == 0)
                d.device = p.substr(7);
        }
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<std::string> GuiDataBridge::ListInstalledApps(const std::string& deviceSerial) {
    std::vector<std::string> out;
    ProcessRunner process;
    process.SetProgram(PathUtilsLite::GetADBExecutablePath());
    std::vector<std::string> args;
    if (!deviceSerial.empty())
        args.push_back("-s"), args.push_back(deviceSerial);
    args.push_back("shell");
    args.push_back("pm");
    args.push_back("list");
    args.push_back("packages");
    args.push_back("-3");
    process.SetArguments(args);
    if (!process.Start() || !process.WaitForFinished(10000))
        return out;
    const std::string output = process.ReadAllStdout();
    std::size_t start = 0;
    for (std::size_t i = 0; i <= output.size(); ++i) {
        if (i != output.size() && output[i] != '\n')
            continue;
        std::string line = output.substr(start, i - start);
        start = i + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // trim
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (line.rfind("package:", 0) == 0)
            line = line.substr(8);
        if (!line.empty())
            out.push_back(line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// capture lifecycle
// ---------------------------------------------------------------------------
bool GuiDataBridge::StartCapture(const std::string& deviceSerial,
                                 const std::string& appName,
                                 const std::string& subProcessName,
                                 const CaptureConfigSnapshot& config,
                                 bool enableInject,
                                 bool useCache) {
    if (isCapturing_ || finalizing_.load() || loading_.load() || saving_.load())
        return false;

    deviceSerial_ = deviceSerial;
    appName_ = appName;
    subProcessName_ = subProcessName;

    SaveCaptureConfig(config);
    const loli::CaptureConfig cc = loli::LoadCaptureConfig();

    // Fresh working snapshot for the session.
    working_ = std::make_shared<GuiSnapshot>();
    working_->capture.capturing = true;
    working_->capture.connected = false;
    working_->capture.appName = appName;
    working_->capture.deviceSerial = deviceSerial;
    PublishSnapshot();

    // Fresh session model for SaveRecord.
    sessionData_ = std::make_shared<loli::Session>();
    sessionAllRecordsLive_ = false;
    ++sessionEpoch_;
    ++captureGeneration_;
    {
        std::lock_guard<std::mutex> lock(captureSampleMutex_);
        captureSamples_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(liveTreeMutex_);
        pendingLiveTree_.reset();
    }
    freeIndex_.clear();
    liveRecordIndex_.clear();
    recordArrivalOrder_.clear();
    nextRecordOrdinal_ = 0;
    persistentOnly_ = useCache;

    time_ = 0;
    captureClockStart_ = {};
    lastFixedSecond_ = -1;
    lastScreenshotTime_ = 0;
    isConnected_ = false;
    captureConnectionEverEstablished_ = false;
    launchOutcome_ = 0;
    launchError_.clear();

    AppendLog("Launching " + appName + " on " + deviceSerial + " ...");
    loli::LoliLogger::Instance().Log("launch",
        "config app=" + appName + " device=" + deviceSerial +
        " arch=" + cc.arch + " compiler=" + cc.compiler +
        " mode=" + cc.mode + " build=" + cc.build +
        " hook=" + cc.hook + " threshold=" + std::to_string(cc.threshold) +
        " retention=" + (useCache ? "live-at-stop" : "all") +
        " attach=" + (enableInject ? "true" : "false"));

    const std::string adbPath = PathUtilsLite::GetADBExecutablePath();
    const std::string pythonPath = PathUtilsLite::GetPythonExecutablePath();
    if (adbPath.empty() || !PathUtilsLite::FileExists(adbPath)) {
        AppendLog("ADB not found. Configure the SDK path in settings.",
                  loli::LogLevel::Error);
        working_->capture.capturing = false;
        PublishSnapshot();
        return false;
    }

    stacktraceChannel_.SetAdbPath(adbPath);
    stacktraceChannel_.SetDeviceSerial(deviceSerial_);
    memDumper_.SetAdbPath(adbPath);
    memDumper_.SetDeviceSerial(deviceSerial_);
    screenshot_.SetAdbPath(adbPath);
    screenshot_.SetDeviceSerial(deviceSerial_);

    // Launch sequence on a worker (blocking adb steps); the async injection
    // callback reports the final result to Tick().
    LaunchDriver::Config cfg;
    cfg.deviceSerial = deviceSerial_;
    cfg.appName = appName_;
    cfg.subProcessName = subProcessName_;
    cfg.compiler = cc.compiler;
    cfg.arch = cc.arch;
    cfg.enableInject = enableInject;
    cfg.useCache = useCache;
    cfg.adbPath = adbPath;
    cfg.pythonPath = pythonPath;

    if (launchThread_.joinable())
        launchThread_.join();
    const auto launchStarted = loli::LoliLogger::Clock::now();
    launchThread_ = std::thread([this, cfg, launchStarted]() {
        auto stepStarted = launchStarted;
        std::string previousStep;
        const bool ok = launch_.Run(
            cfg,
            [&stepStarted, &previousStep](int step, const std::string& label) {
                if (!previousStep.empty())
                    loli::LoliLogger::Instance().LogStage("launch",
                        previousStep.c_str(), stepStarted);
                previousStep = label;
                stepStarted = loli::LoliLogger::Clock::now();
                loli::LoliLogger::Instance().Log("launch",
                    "step=" + std::to_string(step) + " begin " + label);
            },
            [this, launchStarted](bool injectOk) {
                loli::LoliLogger::Instance().LogStage("launch", "injection_result",
                    launchStarted, std::string("ok=") + (injectOk ? "true" : "false"));
                launchOutcome_.store(injectOk ? 1 : -1);
            });
        if (!previousStep.empty())
            loli::LoliLogger::Instance().LogStage("launch",
                previousStep.c_str(), stepStarted, ok ? "ok=true" : "ok=false");
        if (!ok)
            launchOutcome_.store(-1);
    });

    isCapturing_ = true;
    return true;
}

void GuiDataBridge::StopCapture() {
    if (!isCapturing_)
        return;
    const auto stopStarted = loli::LoliLogger::Clock::now();
    isCapturing_ = false;
    isConnected_ = false;
    launch_.Stop();
    stacktraceChannel_.Disconnect();
    memDumper_.Stop();
    screenshot_.Stop();
    if (launchThread_.joinable())
        launchThread_.join();
    DrainCaptureSamples();

    // Swap-removal bounds memory while capturing; restore arrival order in
    // the saved record, as the Qt disk-cache filter did.
    if (persistentOnly_ && sessionData_ && working_ &&
        sessionData_->records.size() == working_->records.size() &&
        sessionData_->records.size() == recordArrivalOrder_.size()) {
        const std::size_t count = sessionData_->records.size();
        std::vector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return recordArrivalOrder_[a] < recordArrivalOrder_[b];
        });
        std::vector<loli::Record> records;
        std::vector<RecordSnapshot> snapshots;
        records.reserve(count);
        snapshots.reserve(count);
        for (const std::size_t index : order) {
            records.push_back(std::move(sessionData_->records[index]));
            snapshots.push_back(std::move(working_->records[index]));
        }
        sessionData_->records = std::move(records);
        working_->records = std::move(snapshots);
        liveRecordIndex_.clear();
        recordArrivalOrder_.clear();
    }

    if (working_) {
        working_->capture.capturing = false;
        working_->capture.connected = false;
    }
    PublishSnapshot();
    loli::LoliLogger::Instance().LogStage("capture", "stop_disconnect", stopStarted,
        "records=" + std::to_string(sessionData_ ? sessionData_->records.size() : 0));

    if (sessionData_ && !sessionData_->records.empty()) {
        finalizing_.store(true);
        AppendLog("Reading device smaps and resolving callstack libraries...");
        const auto session = sessionData_;
        const std::string adb = PathUtilsLite::GetADBExecutablePath();
        const std::string serial = deviceSerial_;
        const std::string app = appName_;
        const std::string subProcess = subProcessName_;
        const std::string pid = appPid_;
        finalizeFuture_ = pool_.Submit([session, adb, serial, app, subProcess, pid]() {
            const auto finalizeStarted = loli::LoliLogger::Clock::now();
            FinalizeResult result;
            const std::string smapsText = DeviceSmaps(adb, serial, app,
                                                       subProcess, pid);
            if (smapsText.empty()) {
                result.error = "Could not read device smaps. The saved record will keep raw frame addresses.";
                loli::LoliLogger::Instance().LogStage("capture", "smaps_finalization",
                    finalizeStarted, result.error);
                return result;
            }
            std::unordered_map<std::string, SMapsSectionLite> smaps;
            ParseSmapsText(smapsText, smaps);
            if (smaps.empty()) {
                result.error = "Device smaps did not contain usable mappings.";
                loli::LoliLogger::Instance().LogStage("capture", "smaps_finalization",
                    finalizeStarted, result.error);
                return result;
            }
            const SMapsAddressIndex index(smaps);
            std::unordered_map<std::string, uint32_t> hashes;
            for (auto& entry : session->callStackMap) {
                for (auto& frame : entry.second) {
                    const std::string* library = nullptr;
                    uint64_t vaddr = 0;
                    if (!index.Resolve(frame.second, library, vaddr))
                        continue;
                    auto cached = hashes.find(*library);
                    if (cached == hashes.end()) {
                        const uint32_t hash = HashStringLite::Hash(*library);
                        cached = hashes.emplace(*library, hash).first;
                        session->internTable.emplace(hash, *library);
                    }
                    frame.first = cached->second;
                    frame.second = vaddr;
                    ++result.resolved;
                }
            }
            for (const auto& entry : smaps) {
                loli::SMapsSection section;
                for (const auto& range : entry.second.addrs_)
                    section.addrs.emplace_back(range.start_, range.end_, range.offset_);
                section.virtual_ = entry.second.virtual_;
                section.rss_ = entry.second.rss_;
                section.pss_ = entry.second.pss_;
                section.sharedClean_ = entry.second.sharedClean_;
                section.sharedDirty_ = entry.second.sharedDirty_;
                section.privateClean_ = entry.second.privateClean_;
                section.privateDirty_ = entry.second.privateDirty_;
                session->smapsSections.emplace(entry.first, std::move(section));

                SMapsSectionSnapshot snap;
                snap.name = entry.first;
                snap.virtualSize = entry.second.virtual_;
                snap.rss = entry.second.rss_;
                snap.pss = entry.second.pss_;
                snap.sharedClean = entry.second.sharedClean_;
                snap.sharedDirty = entry.second.sharedDirty_;
                snap.privateClean = entry.second.privateClean_;
                snap.privateDirty = entry.second.privateDirty_;
                result.sections.push_back(std::move(snap));
            }
            loli::LoliLogger::Instance().LogStage("capture", "smaps_finalization",
                finalizeStarted, "resolved_frames=" + std::to_string(result.resolved) +
                    " sections=" + std::to_string(result.sections.size()));
            return result;
        });
    }
}

void GuiDataBridge::FixedUpdate() {
    if (time_ - lastScreenshotTime_ >= 5 && !screenshot_.IsRunning()) {
        lastScreenshotTime_ = time_;
        const int32_t sampleTimeMs = time_ * 1000;
        const uint64_t generation = captureGeneration_.load();
        screenshot_.CaptureAsync([this, sampleTimeMs, generation](bool ok, std::vector<uint8_t> png) {
            if (!ok || png.empty())
                return;
            PendingCaptureSample sample;
            sample.generation = generation;
            sample.timeMs = sampleTimeMs;
            sample.screenshot = true;
            sample.png = std::move(png);
            std::lock_guard<std::mutex> lock(captureSampleMutex_);
            captureSamples_.push_back(std::move(sample));
        });
    }
    if (!memDumper_.IsRunning()) {
        const int32_t sampleTimeMs = time_ * 1000;
        const uint64_t generation = captureGeneration_.load();
        memDumper_.DumpAsync(
            appName_, subProcessName_,
            [this, sampleTimeMs, generation](bool ok, const loli::MemInfo& info,
                                             const std::string& appPid) {
                if (!ok)
                    return;
                PendingCaptureSample sample;
                sample.generation = generation;
                sample.timeMs = sampleTimeMs;
                sample.memInfo = info;
                sample.appPid = appPid;
                std::lock_guard<std::mutex> lock(captureSampleMutex_);
                captureSamples_.push_back(std::move(sample));
            });
    }

    if (!stacktraceChannel_.IsConnecting() && !stacktraceChannel_.IsConnected()) {
        stacktraceChannel_.ConnectToServer(8000);
        AppendLog("Connecting to application server ...");
    }

}

void GuiDataBridge::DrainCaptureSamples() {
    std::vector<PendingCaptureSample> samples;
    {
        std::lock_guard<std::mutex> lock(captureSampleMutex_);
        samples.swap(captureSamples_);
    }
    if (!working_ || !sessionData_)
        return;
    for (auto& sample : samples) {
        if (sample.generation != captureGeneration_.load())
            continue;
        if (sample.screenshot) {
            loli::Screenshot saved;
            saved.timeMs = sample.timeMs / 1000; // Qt .loli wire unit: seconds
            saved.jpeg = sample.png;
            sessionData_->screenshots.push_back(std::move(saved));
            ScreenshotSnapshot shot;
            shot.timeMs = sample.timeMs;
            shot.jpegBytes = std::move(sample.png);
            working_->screenshots.push_back(std::move(shot));
            PublishSnapshot();
            continue;
        }
        appPid_ = std::move(sample.appPid);
        MemInfoSample s;
        s.timeMs = sample.timeMs;
        s.total = sample.memInfo.Total;
        s.nativeHeap = sample.memInfo.NativeHeap;
        s.gfxDev = sample.memInfo.GfxDev;
        s.eglMtrack = sample.memInfo.EGLmtrack;
        s.glMtrack = sample.memInfo.GLmtrack;
        s.unknown = sample.memInfo.Unknown;
        working_->memTimeline.push_back(s);
        if (sessionData_->memSeries.size() < 6)
            sessionData_->memSeries.resize(6);
        const double t = static_cast<double>(s.timeMs) / 1000.0;
        sessionData_->memSeries[0].push_back({t, static_cast<double>(s.total)});
        sessionData_->memSeries[1].push_back({t, static_cast<double>(s.nativeHeap)});
        sessionData_->memSeries[2].push_back({t, static_cast<double>(s.gfxDev)});
        sessionData_->memSeries[3].push_back({t, static_cast<double>(s.eglMtrack)});
        sessionData_->memSeries[4].push_back({t, static_cast<double>(s.glMtrack)});
        sessionData_->memSeries[5].push_back({t, static_cast<double>(s.unknown)});
    }
}

void GuiDataBridge::OnLaunchFinished(bool ok) {
    if (!ok) {
        StopCapture();
        launchError_ = launch_.GetError();
        if (launchError_.empty()) launchError_ = "The application did not start.";
        AppendLog("Failed to start application: " + launchError_,
                  loli::LogLevel::Error);
        return;
    }
    isConnected_ = true;
    captureConnectionEverEstablished_ = true;
    lastScreenshotTime_ = time_ = 0;
    captureClockStart_ = std::chrono::steady_clock::now();
    lastFixedSecond_ = -1;
    AppendLog("Application started!");
    if (working_)
        working_->capture.connected = true;
    PublishSnapshot();
}

void GuiDataBridge::OnStacktraceData(
    const std::vector<StacktraceChannel::RawStackInfo>& stacks,
    const std::vector<StacktraceChannel::FreeInfo>& frees) {
    if (!working_ || !captureConnectionEverEstablished_)
        return;
    if (!isConnected_.load() && stacktraceChannel_.IsConnected()) {
        isConnected_ = true;
        AppendLog("Reconnected to application server.");
    }
    if (!isConnected_.load())
        return;
    const bool isNoStack = loli::IsNoStackMode();
    auto removeLiveAt = [&](std::size_t index) {
        auto& records = sessionData_->records;
        sessionData_->callStackMap.erase(records[index].uuid);
        liveRecordIndex_.erase(records[index].addr);
        const std::size_t last = records.size() - 1;
        if (index != last) {
            records[index] = std::move(records[last]);
            working_->records[index] = std::move(working_->records[last]);
            recordArrivalOrder_[index] = recordArrivalOrder_[last];
            liveRecordIndex_[records[index].addr] = index;
        }
        records.pop_back();
        working_->records.pop_back();
        recordArrivalOrder_.pop_back();
    };
    for (const auto& stack : stacks) {
        if (persistentOnly_ && sessionData_) {
            const auto freed = freeIndex_.find(stack.addr);
            if (freed != freeIndex_.end() &&
                sessionData_->freeAddrMap[freed->second].second > stack.seq)
                continue;
            const auto old = liveRecordIndex_.find(stack.addr);
            if (old != liveRecordIndex_.end()) {
                if (sessionData_->records[old->second].seq > stack.seq)
                    continue;
                removeLiveAt(old->second);
            }
        }
        RecordSnapshot rs;
        rs.seq = stack.seq;
        rs.timeMs = static_cast<int32_t>(stack.time);
        rs.size = static_cast<int32_t>(stack.size);
        rs.addr = stack.addr;
        rs.funcAddr = 0;
        rs.library = isNoStack ? stack.library : std::string();
        working_->records.push_back(std::move(rs));

        // Session model for SaveRecord (mirrors CliCaptureSession).
        if (sessionData_) {
            loli::Record rec;
            rec.uuid = LoliUuid::CreateUuid();
            rec.seq = stack.seq;
            rec.time = static_cast<int32_t>(stack.time);
            rec.size = static_cast<int32_t>(stack.size);
            rec.addr = stack.addr;
            if (isNoStack) {
                rec.libHash = HashStringLite(stack.library).hashcode_;
                sessionData_->internTable.emplace(rec.libHash, stack.library);
            } else {
                loli::CallStack& callstack = sessionData_->callStackMap[rec.uuid];
                callstack.reserve(stack.stacktraces.size());
                for (const uint64_t addr : stack.stacktraces)
                    callstack.emplace_back(0u, addr);
            }
            sessionData_->records.push_back(rec);
            if (persistentOnly_)
                liveRecordIndex_[rec.addr] = sessionData_->records.size() - 1;
            if (persistentOnly_)
                recordArrivalOrder_.push_back(nextRecordOrdinal_++);
        }
    }
    working_->capture.recordCount = working_->records.size();
    working_->capture.connected = stacktraceChannel_.IsConnected();

    // frees keep max-seq per address (filtering parity).
    if (sessionData_) {
        for (const auto& free : frees) {
            const auto found = freeIndex_.find(free.second);
            if (found == freeIndex_.end()) {
                freeIndex_.emplace(free.second, sessionData_->freeAddrMap.size());
                sessionData_->freeAddrMap.emplace_back(free.second, free.first);
            } else {
                uint32_t& latestSeq = sessionData_->freeAddrMap[found->second].second;
                if (free.first > latestSeq)
                    latestSeq = free.first;
            }
            if (persistentOnly_) {
                const auto live = liveRecordIndex_.find(free.second);
                if (live != liveRecordIndex_.end() &&
                    sessionData_->records[live->second].seq < free.first)
                    removeLiveAt(live->second);
            }
        }
    }
    working_->capture.recordCount = working_->records.size();
}

// ---------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------
CaptureConfigSnapshot GuiDataBridge::GetCaptureConfig() const {
    const loli::CaptureConfig s = loli::LoadCaptureConfig();
    CaptureConfigSnapshot c;
    c.threshold = s.threshold;
    c.mode = s.mode;
    c.build = s.build;
    c.type = s.type;
    c.arch = s.arch;
    c.compiler = s.compiler;
    c.hook = s.hook;
    c.whitelist = s.whitelist;
    c.blacklist = s.blacklist;
    return c;
}

void GuiDataBridge::SaveCaptureConfig(const CaptureConfigSnapshot& config) {
    loli::CaptureConfig s;
    s.threshold = config.threshold;
    s.mode = config.mode;
    s.build = config.build;
    s.type = config.type;
    s.arch = config.arch;
    s.compiler = config.compiler;
    s.hook = config.hook;
    s.whitelist = config.whitelist;
    s.blacklist = config.blacklist;
    loli::StoreCaptureConfig(s);
    AppendLog("Capture configuration saved.");
}

namespace {
// Snapshot -> LoliCore CaptureConfig.
loli::CaptureConfig ToCoreConfig(const CaptureConfigSnapshot& c) {
    loli::CaptureConfig s;
    s.threshold = c.threshold;
    s.mode = c.mode;
    s.build = c.build;
    s.type = c.type;
    s.arch = c.arch;
    s.compiler = c.compiler;
    s.hook = c.hook;
    s.whitelist = c.whitelist;
    s.blacklist = c.blacklist;
    return s;
}
// LoliCore CaptureConfig -> snapshot.
CaptureConfigSnapshot ToSnapshotConfig(const loli::CaptureConfig& s) {
    CaptureConfigSnapshot c;
    c.threshold = s.threshold;
    c.mode = s.mode;
    c.build = s.build;
    c.type = s.type;
    c.arch = s.arch;
    c.compiler = s.compiler;
    c.hook = s.hook;
    c.whitelist = s.whitelist;
    c.blacklist = s.blacklist;
    return c;
}
} // namespace

std::vector<std::pair<std::string, CaptureConfigSnapshot>>
GuiDataBridge::GetSavedCaptureConfigs() const {
    loli::SavedCaptureConfigs saved;
    loli::LoadCaptureConfig(&saved);
    std::vector<std::pair<std::string, CaptureConfigSnapshot>> out;
    out.reserve(saved.entries.size());
    for (const auto& e : saved.entries)
        out.emplace_back(e.first, ToSnapshotConfig(e.second));
    return out;
}

void GuiDataBridge::SaveCaptureConfigPreset(const std::string& name,
                                            const CaptureConfigSnapshot& config) {
    if (name.empty())
        return;
    loli::SavedCaptureConfigs saved;
    const loli::CaptureConfig current = loli::LoadCaptureConfig(&saved);
    loli::CaptureConfig* slot = nullptr;
    for (auto& e : saved.entries) {
        if (e.first == name) { slot = &e.second; break; }
    }
    if (slot)
        *slot = ToCoreConfig(config);
    else
        saved.entries.emplace_back(name, ToCoreConfig(config));
    loli::StoreCaptureConfig(current, &saved);
    AppendLog("Capture preset saved: " + name);
}

void GuiDataBridge::DeleteCaptureConfigPreset(const std::string& name) {
    loli::SavedCaptureConfigs saved;
    const loli::CaptureConfig current = loli::LoadCaptureConfig(&saved);
    auto& entries = saved.entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const std::pair<std::string, loli::CaptureConfig>& e) {
                                     return e.first == name;
                                 }),
                  entries.end());
    loli::StoreCaptureConfig(current, &saved);
    AppendLog("Capture preset deleted: " + name);
}

// ---------------------------------------------------------------------------
// per-frame pump
// ---------------------------------------------------------------------------
void GuiDataBridge::Tick() {
    DrainCaptureSamples();
    if (finalizeFuture_.valid() &&
        finalizeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            FinalizeResult result = finalizeFuture_.get();
            if (working_) {
                working_->smaps = std::move(result.sections);
                AppendLog(result.error.empty()
                    ? "Resolved " + std::to_string(result.resolved) +
                      " frames to libraries via smaps."
                    : result.error, result.error.empty()
                        ? loli::LogLevel::Info : loli::LogLevel::Warn);
                PublishSnapshot();
            }
        } catch (const std::exception& e) {
            AppendLog(std::string("Smaps finalization failed: ") + e.what(),
                      loli::LogLevel::Error);
            PublishSnapshot();
        }
        finalizing_.store(false);
    }
    {
        std::lock_guard<std::mutex> lock(liveTreeMutex_);
        if (pendingLiveTree_ && working_) {
            working_->stackTree = std::move(pendingLiveTree_);
            PublishSnapshot();
        }
    }
    const int launchOutcome = launchOutcome_.exchange(0);
    if (launchOutcome != 0 && isCapturing_) {
        OnLaunchFinished(launchOutcome > 0);
        if (launchOutcome < 0)
            return;
    }

    // Capture samples use elapsed wall-clock seconds; the UI frame loop can
    // render many times per second. Keep socket draining every frame while
    // meminfo/screenshot/connection polling runs once per elapsed second.
    if (isCapturing_ && captureConnectionEverEstablished_) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - captureClockStart_).count();
        const int second = static_cast<int>(std::min<int64_t>(
            elapsed, std::numeric_limits<int>::max()));
        if (second != lastFixedSecond_) {
            time_ = second;
            lastFixedSecond_ = second;
            FixedUpdate();
        }
    }
    if (isCapturing_) {
        stacktraceChannel_.Pump();
        if (captureConnectionEverEstablished_ &&
            stacktraceChannel_.IsConnected() && !isConnected_.load()) {
            isConnected_ = true;
            AppendLog("Reconnected to application server.");
            if (working_) working_->capture.connected = true;
            PublishSnapshot();
        }
    }

    // Adopt completed file work on the GUI thread. Workers never replace the
    // active session or append to a snapshot that the renderer is reading.
    if (loadFuture_.valid() &&
        loadFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const auto result = loadFuture_.get();
            if (result->ok) {
                working_ = result->snapshot;
                sessionData_ = result->session;
                sessionAllRecordsLive_ = result->allRecordsLive;
                ++sessionEpoch_;
                AppendLog("Loaded " + result->fileName + " (" +
                          std::to_string(result->snapshot->records.size()) + " records).");
                PublishSnapshot();
            } else {
                AppendLog("Failed to load " + result->fileName + ": " + result->error,
                          loli::LogLevel::Error);
            }
        } catch (const std::exception& e) {
            AppendLog(std::string("Failed to load record: ") + e.what(),
                      loli::LogLevel::Error);
        }
        {
            std::lock_guard<std::mutex> lock(loadStatusMutex_);
            loadStatus_.clear();
        }
        loading_.store(false);
    }
    if (saveFuture_.valid() &&
        saveFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const SaveResult result = saveFuture_.get();
            lastSaveResult_.store(result.ok);
            AppendLog(result.ok
                          ? "Saved " + result.path + " (" +
                                std::to_string(result.recordCount) + " records)."
                          : "Failed to save " + result.path + ".",
                      result.ok ? loli::LogLevel::Info : loli::LogLevel::Error);
        } catch (const std::exception& e) {
            lastSaveResult_.store(false);
            AppendLog(std::string("Failed to save record: ") + e.what(),
                      loli::LogLevel::Error);
        }
        saving_.store(false);
    }

    // Publish throttled snapshots during capture.
    if (isCapturing_ && isConnected_.load()) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const int nowMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        if (nowMs - lastPublishMs_ >= 1000) {
            lastPublishMs_ = nowMs;
            if (working_)
                working_->capture.elapsedMs = time_ * 1000;

            // Live-capture incremental tree build (task 6.2): rebuild the
            // aggregated tree from the session on a worker at the ~1 Hz
            // publish cadence (records accumulate; a full rebuild from the
            // accumulated session keeps the algorithm identical to load).
            if (sessionData_ && !sessionData_->records.empty() &&
                !treeRebuildInFlight_.load()) {
                treeRebuildInFlight_ = true;
                // The capture loop mutates records, including swap-removal in
                // persistent mode. Build from a stable copy on the worker.
                auto session = std::make_shared<loli::Session>(*sessionData_);
                const uint64_t generation = captureGeneration_.load();
                pool_.Submit([this, session, generation]() {
                    const auto start = loli::LoliLogger::Clock::now();
                    auto tree = std::make_shared<StacktraceTree>();
                    BuildTreeFromSession(*session, *tree);
                    loli::LoliLogger::Instance().LogStage("capture", "live_tree_rebuild",
                        start, "records=" + std::to_string(session->records.size()) +
                            " nodes=" + std::to_string(tree->Nodes().size()));
                    {
                        std::lock_guard<std::mutex> lock(liveTreeMutex_);
                        if (captureGeneration_.load() == generation)
                            pendingLiveTree_ = std::move(tree);
                    }
                    treeRebuildInFlight_ = false;
                });
            }
            PublishSnapshot();
        }
    }
}

// ---------------------------------------------------------------------------
// record file I/O (async load on the thread pool; async save)
// ---------------------------------------------------------------------------
void GuiDataBridge::LoadRecord(const std::string& path) {
    if (loading_.load() || saving_.load() || finalizing_.load() || isCapturing_)
        return;
    loading_.store(true);
    loli::LoliLogger::Instance().Log("file", "load_begin path=" + path);
    loadProgress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(loadStatusMutex_);
        loadStatus_ = "Opening...";
    }

    std::atomic<float>* progress = &loadProgress_;
    loadFuture_ = pool_.Submit([this, path, progress]() -> std::shared_ptr<LoadResult> {
        const auto totalStart = loli::LoliLogger::Clock::now();
        auto result = std::make_shared<LoadResult>();
        result->path = path;
        result->fileName = BaseNameOf(path);

        std::vector<uint8_t> bytes;
        const auto readStart = loli::LoliLogger::Clock::now();
        if (!ReadFileBytes(path, bytes)) {
            result->error = "open failed";
            loli::LoliLogger::Instance().LogStage("file", "read_failed", readStart,
                "path=" + path);
            return result;
        }
        loli::LoliLogger::Instance().LogStage("file", "read_bytes", readStart,
            "bytes=" + std::to_string(bytes.size()));
        auto session = std::make_shared<loli::Session>();
        std::string err;
        const auto parseStart = loli::LoliLogger::Clock::now();
        if (!loli::ReadSession(bytes.data(), bytes.size(), *session, &err)) {
            result->error = err;
            loli::LoliLogger::Instance().LogStage("file", "parse_failed", parseStart,
                "error=" + err);
            return result;
        }
        std::size_t frameOccurrences = 0;
        for (const auto& entry : session->callStackMap)
            frameOccurrences += entry.second.size();
        std::size_t symbolAddresses = 0;
        for (const auto& entry : session->symbolMap)
            symbolAddresses += entry.second.size();
        loli::LoliLogger::Instance().LogStage("file", "parse_session", parseStart,
            "records=" + std::to_string(session->records.size()) +
            " stacks=" + std::to_string(session->callStackMap.size()) +
            " frames=" + std::to_string(frameOccurrences) +
            " symbol_libs=" + std::to_string(session->symbolMap.size()) +
            " symbol_addresses=" + std::to_string(symbolAddresses) +
            " free_addresses=" + std::to_string(session->freeAddrMap.size()) +
            " screenshots=" + std::to_string(session->screenshots.size()) +
            " smaps_sections=" + std::to_string(session->smapsSections.size()) +
            " timeline_samples=" + std::to_string(session->memSeries.empty() ? 0 :
                                                    session->memSeries[0].size()));
        const auto normalizeStart = loli::LoliLogger::Clock::now();
        NormalizeSessionTimelineToQtSeconds(*session);
        loli::LoliLogger::Instance().LogStage("file", "normalize_time",
            normalizeStart);
        const auto liveCheckStart = loli::LoliLogger::Clock::now();
        result->allRecordsLive = AllSavedRecordsLive(*session);
        loli::LoliLogger::Instance().LogStage("file", "check_persistent",
            liveCheckStart, std::string("all_records_live=") +
                (result->allRecordsLive ? "true" : "false"));
        progress->store(0.4f);
        {
            std::lock_guard<std::mutex> lock(loadStatusMutex_);
            loadStatus_ = "Building tree...";
        }

        // Build the snapshot on the worker (tree via the shared builder).
        const auto snapshotStart = loli::LoliLogger::Clock::now();
        auto snap = std::make_shared<GuiSnapshot>();
        snap->records.reserve(session->records.size());
        uint64_t allocationBytes = 0;
        int32_t lastRecordMs = 0;
        for (const auto& r : session->records) {
            if (r.size > 0)
                allocationBytes += static_cast<uint32_t>(r.size);
            lastRecordMs = std::max(lastRecordMs, r.time);
            RecordSnapshot rs;
            rs.seq = r.seq;
            rs.timeMs = r.time;
            rs.size = r.size;
            rs.addr = r.addr;
            rs.funcAddr = r.funcAddr;
            auto it = session->internTable.find(r.libHash);
            rs.library = it != session->internTable.end() ? it->second : std::string();
            snap->records.push_back(std::move(rs));
        }
        loli::LoliLogger::Instance().LogStage("file", "records_snapshot",
            snapshotStart, "records=" + std::to_string(snap->records.size()) +
                " allocation_bytes=" + std::to_string(allocationBytes) +
                " last_record_ms=" + std::to_string(lastRecordMs));

        auto tree = std::make_shared<StacktraceTree>();
        const auto allTreeStart = loli::LoliLogger::Clock::now();
        BuildTreeFromSession(*session, *tree);
        loli::LoliLogger::Instance().LogStage("file", "all_tree", allTreeStart,
            "nodes=" + std::to_string(tree->Nodes().size()));
        snap->stackTree = tree;
        if (!session->freeAddrMap.empty()) {
            if (result->allRecordsLive) {
                snap->liveTreeUsesAll = true;
                loli::LoliLogger::Instance().Log("file", "live_tree aliases all_tree: saved free events exclude no records");
            } else {
                auto liveTree = std::make_shared<StacktraceTree>();
                const auto liveTreeStart = loli::LoliLogger::Clock::now();
                BuildTreeFromSession(*session, *liveTree, true);
                loli::LoliLogger::Instance().LogStage("file", "live_tree", liveTreeStart,
                    "nodes=" + std::to_string(liveTree->Nodes().size()));
                snap->liveStackTree = std::move(liveTree);
            }
        }
        progress->store(0.8f);

        // mem timeline
        if (!session->memSeries.empty()) {
            const auto& s0 = session->memSeries[0];
            snap->memTimelineSeriesMask = 0;
            for (size_t k = 0; k < std::min(session->memSeries.size(), size_t{6}); ++k)
                if (!session->memSeries[k].empty())
                    snap->memTimelineSeriesMask |= static_cast<uint8_t>(1u << k);
            snap->memTimeline.reserve(s0.size());
            for (size_t j = 0; j < s0.size(); j++) {
                const auto value = [&](size_t series) {
                    return series < session->memSeries.size() &&
                           j < session->memSeries[series].size()
                        ? static_cast<uint32_t>(session->memSeries[series][j].value) : 0u;
                };
                MemInfoSample m;
                m.timeMs = SecondsToSnapshotMs(s0[j].time);
                m.total = value(0);
                m.nativeHeap = value(1);
                m.gfxDev = value(2);
                m.eglMtrack = value(3);
                m.glMtrack = value(4);
                m.unknown = value(5);
                snap->memTimeline.push_back(m);
            }
        }

        // screenshots
        snap->screenshots.reserve(session->screenshots.size());
        for (const auto& sc : session->screenshots) {
            ScreenshotSnapshot ss;
            ss.timeMs = SecondsToSnapshotMs(sc.timeMs);
            ss.jpegBytes = sc.jpeg;
            snap->screenshots.push_back(std::move(ss));
        }

        // smaps
        snap->smaps.reserve(session->smapsSections.size());
        for (const auto& kv : session->smapsSections) {
            SMapsSectionSnapshot s;
            s.name = kv.first;
            s.virtualSize = static_cast<uint32_t>(kv.second.virtual_);
            s.rss = static_cast<uint32_t>(kv.second.rss_);
            s.pss = static_cast<uint32_t>(kv.second.pss_);
            s.sharedClean = static_cast<uint32_t>(kv.second.sharedClean_);
            s.sharedDirty = static_cast<uint32_t>(kv.second.sharedDirty_);
            s.privateClean = static_cast<uint32_t>(kv.second.privateClean_);
            s.privateDirty = static_cast<uint32_t>(kv.second.privateDirty_);
            snap->smaps.push_back(std::move(s));
        }

        snap->capture.capturing = false;
        snap->capture.connected = false;
        snap->capture.recordCount = snap->records.size();
        snap->capture.appName = result->fileName;

        result->snapshot = std::move(snap);
        result->session = std::move(session);
        result->ok = true;
        progress->store(1.0f);
        loli::LoliLogger::Instance().LogStage("file", "load_complete", totalStart,
            "records=" + std::to_string(result->snapshot->records.size()) +
            " screenshots=" + std::to_string(result->snapshot->screenshots.size()));
        return result;
    });
}

bool GuiDataBridge::SaveRecord(const std::string& path) {
    if (saving_.load() || loading_.load() || finalizing_.load() || isCapturing_)
        return false;
    if (!sessionData_ || sessionData_->records.empty())
        return false;

    saving_.store(true);
    loli::LoliLogger::Instance().Log("file", "save_begin path=" + path +
        " records=" + std::to_string(sessionData_->records.size()));
    saveFuture_ = pool_.Submit([path, session = sessionData_]() -> SaveResult {
        const auto totalStart = loli::LoliLogger::Clock::now();
        SaveResult result;
        result.path = path;
        result.recordCount = session->records.size();
        std::vector<uint8_t> bytes;
        const auto serializeStart = loli::LoliLogger::Clock::now();
        const bool okWrite = loli::WriteSession(*session, bytes);
        loli::LoliLogger::Instance().LogStage("file", "serialize_session",
            serializeStart, "bytes=" + std::to_string(bytes.size()) +
                " ok=" + (okWrite ? "true" : "false"));
        bool okFile = false;
        if (okWrite) {
            const auto writeStart = loli::LoliLogger::Clock::now();
            std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
            if (f.good()) {
                f.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                okFile = f.good();
            }
            loli::LoliLogger::Instance().LogStage("file", "write_file", writeStart,
                "bytes=" + std::to_string(bytes.size()) +
                    " ok=" + (okFile ? "true" : "false"));
        }
        result.ok = okWrite && okFile;
        loli::LoliLogger::Instance().LogStage("file", "save_complete", totalStart,
            "ok=" + std::string(result.ok ? "true" : "false"));
        return result;
    });
    return true;
}

} // namespace gui
