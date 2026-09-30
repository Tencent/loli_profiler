#include "comparelaunch.h"
#include "processrunner.h"
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gui {
bool LaunchComparison(const std::filesystem::path& mainExecutable, std::string& error) {
    error.clear();
#ifdef _WIN32
    const char* name = "LoliProfilerCompare.exe";
#else
    const char* name = "LoliProfilerCompare";
#endif
    auto target = mainExecutable.parent_path() / name;
#ifdef __APPLE__
    if (!std::filesystem::is_regular_file(target))
        target = mainExecutable.parent_path().parent_path().parent_path().parent_path() / name;
#endif
    if (!std::filesystem::is_regular_file(target)) {
        error = "Cannot find comparison executable: " + target.u8string() + ". Build/package LoliProfilerCompare beside the profiler.";
        return false;
    }
#ifdef _WIN32
    // Create a process without owning its lifetime. Handles are closed immediately.
    const auto commandUtf8 = ProcessRunner::BuildCommandLine(target.u8string(), {});
    const int length = MultiByteToWideChar(CP_UTF8, 0, commandUtf8.c_str(), -1, nullptr, 0);
    std::wstring command(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, commandUtf8.c_str(), -1, &command[0], length);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto directory = target.parent_path().wstring();
    if (!CreateProcessW(target.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP,
                        nullptr, directory.c_str(), &startup, &process)) {
        error = "Cannot start comparison process (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    const std::string executable = target.string();
    int statusPipe[2];
    if (pipe(statusPipe) != 0) { error = std::strerror(errno); return false; }
    if (fcntl(statusPipe[1], F_SETFD, FD_CLOEXEC) < 0) {
        error = std::strerror(errno);
        close(statusPipe[0]); close(statusPipe[1]);
        return false;
    }
    const long descriptorLimit = sysconf(_SC_OPEN_MAX);
    const pid_t first = fork();
    if (first == 0) {
        close(statusPipe[0]);
        if (setsid() >= 0) {
            const pid_t second = fork();
            if (second > 0) _exit(0);
            if (second == 0) {
                // Do not keep the profiling app's device sockets or subprocess
                // pipes alive in the independent comparison process. Only use
                // async-signal-safe calls between fork and exec.
                for (long fd = 3; fd < descriptorLimit; ++fd)
                    if (fd != statusPipe[1]) close(static_cast<int>(fd));
                execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
            }
        }
        const int code = errno;
        (void)write(statusPipe[1], &code, sizeof(code));
        _exit(1);
    }
    close(statusPipe[1]);
    if (first < 0) { close(statusPipe[0]); error = std::strerror(errno); return false; }
    int status;
    while (waitpid(first, &status, 0) < 0 && errno == EINTR) {}
    int code = 0;
    ssize_t count;
    do { count = read(statusPipe[0], &code, sizeof(code)); } while (count < 0 && errno == EINTR);
    close(statusPipe[0]);
    if (count != 0) { error = "Cannot start comparison: " + std::string(std::strerror(count > 0 ? code : errno)); return false; }
    return true;
#endif
}
}
