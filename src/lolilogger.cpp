#include "lolilogger.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#include <fstream>
#endif

namespace loli {
namespace {

const char* LevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Info: return "INFO";
    case LogLevel::Debug: return "DEBUG";
    }
    return "INFO";
}

const char* BaseName(const char* path) {
    if (!path) return nullptr;
    const char* name = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

} // namespace

LoliLogger& LoliLogger::Instance() {
    static LoliLogger logger;
    return logger;
}

bool LoliLogger::StartFile(const std::string& path, bool append) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.close();
    file_.clear();
    file_.open(path, std::ios::out | (append ? std::ios::app : std::ios::trunc));
    if (!file_.good()) {
        filePath_.clear();
        return false;
    }
    filePath_ = path;
    file_ << "\n=== LoliProfiler diagnostics ===\n";
    file_.flush();
    return true;
}

void LoliLogger::StopFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.close();
    filePath_.clear();
}

std::string LoliLogger::FilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filePath_;
}

void LoliLogger::SetConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

void LoliLogger::SetMinLevel(LogLevel level) {
    minLevel_.store(level, std::memory_order_relaxed);
}

bool LoliLogger::Enabled(LogLevel level) const {
    return static_cast<int>(level) <=
           static_cast<int>(minLevel_.load(std::memory_order_relaxed));
}

void LoliLogger::Write(LogLevel level, const char* category,
                       const std::string& message, const char* file,
                       int line, const char* function) {
    if (!Enabled(level)) return;
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream prefix;
    prefix << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << milliseconds
           << " [" << LevelName(level) << "] ["
           << (category ? category : "general") << "] [thread "
           << std::this_thread::get_id() << ']';
    if (file)
        prefix << " [" << BaseName(file) << ':' << line
               << (function ? std::string(" ") + function : std::string()) << ']';
    prefix << ' ';

    std::lock_guard<std::mutex> lock(mutex_);
    std::istringstream input(message);
    std::string text;
    bool wrote = false;
    while (std::getline(input, text)) {
        wrote = true;
        const std::string full = prefix.str() + text;
        lines_.push_back({nextSequence_++, level, full});
        if (lines_.size() > kMaxLines) lines_.pop_front();
        if (file_.is_open()) file_ << full << '\n';
        if (consoleEnabled_)
            (level == LogLevel::Error ? std::cerr : std::cout) << full << '\n';
    }
    if (!wrote) {
        const std::string full = prefix.str();
        lines_.push_back({nextSequence_++, level, full});
        if (lines_.size() > kMaxLines) lines_.pop_front();
        if (file_.is_open()) file_ << full << '\n';
        if (consoleEnabled_)
            (level == LogLevel::Error ? std::cerr : std::cout) << full << '\n';
    }
    if (file_.is_open()) file_.flush();
    if (consoleEnabled_) {
        std::cout.flush();
        std::cerr.flush();
    }
}

void LoliLogger::LogStage(const char* category, const char* stage,
                          Clock::time_point start, const std::string& detail) {
    const double elapsedMs = std::chrono::duration<double, std::milli>(
        Clock::now() - start).count();
    std::ostringstream out;
    out << stage << " elapsed_ms=" << std::fixed << std::setprecision(1)
        << elapsedMs << " rss_mib=" << std::setprecision(1) << ResidentMiB();
    if (!detail.empty()) out << ' ' << detail;
    Write(LogLevel::Info, category, out.str());
}

std::vector<LogEntry> LoliLogger::ReadSince(uint64_t& cursor) const {
    std::vector<LogEntry> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : lines_)
        if (entry.sequence > cursor) result.push_back(entry);
    if (!lines_.empty()) cursor = lines_.back().sequence;
    return result;
}

double LoliLogger::ResidentMiB() {
    constexpr double mib = 1024.0 * 1024.0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)))
        return static_cast<double>(counters.WorkingSetSize) / mib;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<double>(info.resident_size) / mib;
#else
    std::ifstream statm("/proc/self/statm");
    unsigned long total = 0, resident = 0;
    if (statm >> total >> resident)
        return static_cast<double>(resident) * sysconf(_SC_PAGESIZE) / mib;
#endif
    return 0.0;
}

LogRecord::~LogRecord() noexcept {
    try {
        LoliLogger::Instance().Write(level_, category_, stream_.str(),
                                     file_, line_, function_);
    } catch (...) {
        // Logging must never terminate the capture or GUI during unwinding.
    }
}

} // namespace loli
