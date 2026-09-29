#ifndef LOLI_PROFILER_LOGGER_H
#define LOLI_PROFILER_LOGGER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace loli {

// Shared by the desktop GUI, CLI, and Qt-free core. Lower values are more
// severe, so a minimum level can suppress less important records.
enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

struct LogEntry {
    uint64_t sequence = 0;
    LogLevel level = LogLevel::Info;
    std::string line;
};

class LoliLogger {
public:
    using Clock = std::chrono::steady_clock;
    static LoliLogger& Instance();

    bool StartFile(const std::string& path, bool append = true);
    void StopFile();
    std::string FilePath() const;
    void SetConsoleEnabled(bool enabled);
    void SetMinLevel(LogLevel level);
    bool Enabled(LogLevel level) const;

    void Write(LogLevel level, const char* category, const std::string& message,
               const char* file = nullptr, int line = 0,
               const char* function = nullptr);
    void Log(const char* category, const std::string& message) {
        Write(LogLevel::Info, category, message);
    }
    void LogStage(const char* category, const char* stage,
                  Clock::time_point start, const std::string& detail = {});
    std::vector<LogEntry> ReadSince(uint64_t& cursor) const;
    static double ResidentMiB();

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> lines_;
    uint64_t nextSequence_ = 1;
    std::ofstream file_;
    std::string filePath_;
    bool consoleEnabled_ = false;
    std::atomic<LogLevel> minLevel_{LogLevel::Info};
    static constexpr std::size_t kMaxLines = 5000;
};

// Stream-style record, similar to FURYI/FURYE. The destructor publishes one
// complete message with source location, regardless of the calling thread.
class LogRecord {
public:
    LogRecord(LogLevel level, const char* category, const char* file,
              int line, const char* function)
        : level_(level), category_(category), file_(file),
          line_(line), function_(function) {}
    ~LogRecord() noexcept;

    LogRecord(const LogRecord&) = delete;
    LogRecord& operator=(const LogRecord&) = delete;

    template <typename T>
    LogRecord& operator<<(const T& value) {
        if (LoliLogger::Instance().Enabled(level_))
            stream_ << value;
        return *this;
    }
    LogRecord& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
        if (LoliLogger::Instance().Enabled(level_))
            stream_ << manipulator;
        return *this;
    }
    LogRecord& operator<<(std::ios_base& (*manipulator)(std::ios_base&)) {
        if (LoliLogger::Instance().Enabled(level_))
            stream_ << manipulator;
        return *this;
    }

private:
    LogLevel level_;
    const char* category_;
    const char* file_;
    int line_;
    const char* function_;
    std::ostringstream stream_;
};

} // namespace loli

#define LOLI_DEBUG(category) ::loli::LogRecord(::loli::LogLevel::Debug, category, __FILE__, __LINE__, __func__)
#define LOLI_INFO(category)  ::loli::LogRecord(::loli::LogLevel::Info,  category, __FILE__, __LINE__, __func__)
#define LOLI_WARN(category)  ::loli::LogRecord(::loli::LogLevel::Warn,  category, __FILE__, __LINE__, __func__)
#define LOLI_ERROR(category) ::loli::LogRecord(::loli::LogLevel::Error, category, __FILE__, __LINE__, __func__)

#endif
