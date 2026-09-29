#include "processrunner.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <process.hpp>

namespace {

// Accumulates pipe output from the reader threads (tiny-process-library
// delivers chunks as they arrive; QProcess call sites read the full buffer
// after completion, so we just append and drain on demand).
class OutputSink {
public:
    void Append(const char* bytes, size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.append(bytes, n);
    }

    std::string Drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::move(buffer_);
    }

private:
    std::mutex mutex_;
    std::string buffer_;
};

} // namespace

struct ProcessRunner::Impl {
    std::unique_ptr<TinyProcessLib::Process> process;
    OutputSink stdoutSink;
    OutputSink stderrSink;

    // Exit recording + completion signalling. Every spawned process gets a
    // watcher thread that blocks in get_exit_status(); WaitForFinished waits
    // on the CV with a timeout, Kill kills then joins the watcher.
    std::mutex exitMutex;
    std::condition_variable exitCv;
    bool exited = false;
    bool killRequested = false; // set by Kill(); forces CrashExit bookkeeping
    int exitCode = -1;
    ProcessRunner::ExitStatus exitStatus = ProcessRunner::ExitStatus::NotStarted;
    FinishedHandler asyncHandler; // optional, runs on the watcher thread
    std::thread watcherThread;
};

ProcessRunner::~ProcessRunner() {
    if (impl_) {
        if (impl_->process)
            Kill();
        delete impl_;
    }
}

std::string ProcessRunner::BuildCommandLine(const std::string& program,
                                            const std::vector<std::string>& arguments) {
#ifdef _WIN32
    auto quote = [](const std::string& value) {
        if (value.find_first_of(" \t\"") == std::string::npos)
            return value;
        std::string out = "\"";
        std::size_t slashes = 0;
        for (char c : value) {
            if (c == '\\') {
                ++slashes;
            } else if (c == '"') {
                out.append(slashes * 2 + 1, '\\');
                out += '"';
                slashes = 0;
            } else {
                out.append(slashes, '\\');
                out += c;
                slashes = 0;
            }
        }
        out.append(slashes * 2, '\\');
        out += '"';
        return out;
    };
#else
    auto quote = [](const std::string& value) {
        std::string out = "'";
        for (char c : value) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += '\'';
        return out;
    };
#endif
    // The executable path needs the same quoting as an argument; Android SDK
    // and NDK installations frequently live below paths containing spaces.
    std::string cmd = quote(program);
    for (const auto& argument : arguments) {
        cmd += ' ';
        cmd += quote(argument);
    }
    return cmd;
}

bool ProcessRunner::Start() {
    return Spawn(nullptr);
}

bool ProcessRunner::StartAsync(FinishedHandler onFinished) {
    return Spawn(std::move(onFinished));
}

bool ProcessRunner::Spawn(FinishedHandler&& onFinished) {
    const std::string command = BuildCommandLine(program_, arguments_);

    // A ProcessRunner is reused for each meminfo/screenshot poll. A finished
    // child still owns a joinable watcher and pipe handles; release them
    // before replacing impl_ with the next invocation.
    if (impl_) {
        Kill();
        delete impl_;
        impl_ = nullptr;
    }

    impl_ = new Impl();
    impl_->asyncHandler = std::move(onFinished);
    Impl* impl = impl_;
    impl->process = std::make_unique<TinyProcessLib::Process>(
        command, workingDir_,
        [impl](const char* bytes, size_t n) { impl->stdoutSink.Append(bytes, n); },
        [impl](const char* bytes, size_t n) { impl->stderrSink.Append(bytes, n); });

    const auto pid = impl_->process->get_id();
#ifdef _WIN32
    if (pid == 0) {
#else
    if (pid == -1) {
#endif
        delete impl_;
        impl_ = nullptr;
        exitStatus_ = ExitStatus::CrashExit;
        exitCode_ = -1;
        return false;
    }

    exitStatus_ = ExitStatus::Running;
    exitCode_ = -1;

    impl_->watcherThread = std::thread([this, impl]() {
        const int code = impl->process->get_exit_status(); // blocks until exit
        // Negative codes are signal deaths on POSIX. On Windows a kill shows
        // as the child's own exit code, so a kill request always wins: the
        // watcher records the kill as CrashExit (QProcess parity - a killed
        // process reports CrashExit).
        ExitStatus status = code < 0 ? ExitStatus::CrashExit : ExitStatus::NormalExit;
        {
            std::lock_guard<std::mutex> lock(impl->exitMutex);
            if (impl->killRequested) {
                status = ExitStatus::CrashExit;
                impl->exitCode = -1;
            } else {
                impl->exitCode = code;
            }
            impl->exitStatus = status;
            impl->exited = true;
        }
        impl->exitCv.notify_all();
        if (impl->asyncHandler)
            impl->asyncHandler(impl->exitCode, status);
    });
    return true;
}

bool ProcessRunner::WaitForFinished(uint32_t timeoutMs) {
    if (!impl_)
        return false;

    std::unique_lock<std::mutex> lock(impl_->exitMutex);
    const bool exited = impl_->exitCv.wait_for(
        lock, std::chrono::milliseconds(timeoutMs), [impl = impl_]() { return impl->exited; });
    if (exited) {
        exitCode_ = impl_->exitCode;
        exitStatus_ = impl_->exitStatus;
    }
    return exited;
}

std::string ProcessRunner::ReadAllStdout() {
    return impl_ ? impl_->stdoutSink.Drain() : std::string();
}

std::string ProcessRunner::ReadAllStderr() {
    return impl_ ? impl_->stderrSink.Drain() : std::string();
}

bool ProcessRunner::IsRunning() const {
    if (!impl_)
        return false;
    std::lock_guard<std::mutex> lock(impl_->exitMutex);
    return !impl_->exited;
}

void ProcessRunner::Kill() {
    if (!impl_ || !impl_->process)
        return;

    {
        std::lock_guard<std::mutex> lock(impl_->exitMutex);
        impl_->killRequested = true;
    }
    impl_->process->kill(true);

    if (impl_->watcherThread.joinable())
        impl_->watcherThread.join();

    {
        std::lock_guard<std::mutex> lock(impl_->exitMutex);
        if (!impl_->exited) {
            impl_->exitCode = -1;
            impl_->exitStatus = ExitStatus::CrashExit;
            impl_->exited = true;
        }
        exitCode_ = impl_->exitCode;
        exitStatus_ = impl_->exitStatus;
    }
    impl_->exitCv.notify_all();
}
