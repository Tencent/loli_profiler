#ifndef PROCESSRUNNER_H
#define PROCESSRUNNER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Qt-free process execution facility (replaces QProcess usage in the core).
// Built on the vendored tiny-process-library (thirdparty/tiny-process-library).
//
// Usage patterns (mirroring the previous QProcess call sites):
//  - Blocking one-shot: Start(); WaitForFinished(timeoutMs); ReadAllStdout().
//  - Async long-running: StartAsync(onFinished); ...; Kill() to stop.
//  - Timeout with partial output (adb shell su root check): Start();
//    WaitForFinished(3000) returning false leaves accumulated stdout
//    readable, then Kill() - same observable behavior as
//    QProcess::waitForFinished() timeout + close().
class ProcessRunner {
public:
    enum class ExitStatus {
        NotStarted,
        Running,
        NormalExit,
        CrashExit,
    };

    // Completion notification for StartAsync. Called from the watcher thread
    // (not the UI thread) - callers marshal to their own event queue.
    using FinishedHandler = std::function<void(int exitCode, ExitStatus status)>;

    ProcessRunner() = default;
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    void SetProgram(const std::string& program) { program_ = program; }
    const std::string& GetProgram() const { return program_; }

    void SetArguments(const std::vector<std::string>& arguments) { arguments_ = arguments; }
    const std::vector<std::string>& GetArguments() const { return arguments_; }

    void SetWorkingDirectory(const std::string& dir) { workingDir_ = dir; }

    // Starts the process. Returns false if the spawn itself failed
    // (program not found etc. - equivalent to QProcess::waitForStarted()
    // returning false).
    bool Start();

    // Start with completion notification. onFinished runs on a watcher
    // thread once the process exits (or fails to start - then with the
    // spawn-failure exit code and CrashExit status).
    bool StartAsync(FinishedHandler onFinished);

    // Waits up to timeoutMs for exit. Returns true if the process exited
    // within the timeout. On false the process is still running and its
    // accumulated output remains readable; callers typically Kill() then.
    bool WaitForFinished(uint32_t timeoutMs);

    // Accumulated output so far (empty after a fresh Start).
    std::string ReadAllStdout();
    std::string ReadAllStderr();

    // True between a successful Start and process exit (or async completion).
    bool IsRunning() const;

    ExitStatus GetExitStatus() const { return exitStatus_; }
    int GetExitCode() const { return exitCode_; }

    // Kills the process and waits for the watcher/reader threads to finish.
    void Kill();

    // Builds the single command string passed to the OS, applying the same
    // quoting rules as AdbProcess::SetArguments (Windows: double-quote
    // arguments containing spaces, join with spaces; POSIX: shell-quoting
    // because the underlying library spawns via /bin/sh -c).
    static std::string BuildCommandLine(const std::string& program,
                                        const std::vector<std::string>& arguments);

private:
    bool Spawn(FinishedHandler&& onFinished);

    std::string program_;
    std::vector<std::string> arguments_;
    std::string workingDir_;

    struct Impl;
    Impl* impl_ = nullptr;

    ExitStatus exitStatus_ = ExitStatus::NotStarted;
    int exitCode_ = -1;
};

#endif // PROCESSRUNNER_H
