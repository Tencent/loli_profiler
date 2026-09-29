// LoliProfilerCLI (Qt-free, task 5.1): headless capture + --dump/--compare
// on the Qt-free core. Capture orchestration goes through loli::CliCapture
// Session (the same LaunchDriver/StacktraceChannel/adbtools building blocks
// the GUI uses, per design D12); --dump/--compare go through the Qt-free
// ProfileComparatorLite.
#include "clicapture.h"
#include "appsettings.h"
#include "captureconfig.h"
#include "lolilogger.h"
#include "pathutilslite.h"

#include <atomic>
#include <iostream>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace {

void PrintUsage() {
    std::cout << "LoliProfiler CLI - Android Memory Profiling Tool (Qt-free core)\n\n";
    std::cout << "  LoliProfilerCLI --app <package_name> --out <output.loli> [options]\n";
    std::cout << "  LoliProfilerCLI --compare <baseline.loli> <comparison.loli> --out <output> [options]\n";
    std::cout << "  LoliProfilerCLI --dump <profile.loli> --out <output.txt|output.db> [options]\n\n";
    std::cout << "  LoliProfilerCLI --symbolize <profile.loli> --symbol <lib.so> [--out <output.loli>]\n\n";
    std::cout << "Capture options:\n";
    std::cout << "  --app <name>           Target application package name\n";
    std::cout << "  --out <file>           Output .loli file path\n";
    std::cout << "  --symbol <path>        Symbol file (.so/.sym) for address translation\n";
    std::cout << "  --subprocess <name>    Target subprocess name\n";
    std::cout << "  --device <serial>      Device serial number (required if multiple devices)\n";
    std::cout << "  --duration <seconds>   Profiling duration in seconds (omit for Ctrl+C stop)\n";
    std::cout << "  --attach               Attach to running app instead of launching\n";
    std::cout << "  --verbose              Verbose output\n";
    std::cout << "  --log-file <path>      Write timestamped CLI diagnostics to a file\n";
    std::cout << "  --log-level <level>    debug, info (default), warn, or error\n";
    std::cout << "  --enable-memory-optimization\n";
    std::cout << "                         Keep only allocations live at stop (large captures)\n\n";
    std::cout << "Compare/dump options:\n";
    std::cout << "  --compare              Compare two .loli files (baseline vs comparison)\n";
    std::cout << "  --dump                 Export a .loli file to text or SQLite (.db)\n";
    std::cout << "  --symbolize            Resolve saved call stacks with a matching library\n";
    std::cout << "  --out <path>           Output path\n";
    std::cout << "  --skip-root-levels <n> Root call stack frames to skip in comparison\n\n";
    std::cout << "Examples:\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli --duration 60\n";
    std::cout << "  LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt\n";
    std::cout << "  LoliProfilerCLI --dump profile.loli --out dump.txt\n\n";
}

std::string GetOptionValue(const std::vector<std::string>& args,
                           const std::string& name, bool* found = nullptr) {
    const std::string prefix = "--" + name + "=";
    const std::string flag = "--" + name;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == flag && i + 1 < args.size()) {
            if (found)
                *found = true;
            return args[i + 1];
        }
        if (args[i].rfind(prefix, 0) == 0) {
            if (found)
                *found = true;
            return args[i].substr(prefix.size());
        }
    }
    if (found)
        *found = false;
    return std::string();
}

bool HasOption(const std::vector<std::string>& args, const std::string& name) {
    const std::string flag = "--" + name;
    for (const auto& a : args)
        if (a == flag || a == name)
            return true;
    return false;
}

std::atomic<bool> gStopRequested(false);

void OnSignal(int) {
    gStopRequested.store(true);
}

std::string ExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameA(nullptr, path, sizeof(path) - 1);
    if (len == 0 || len >= sizeof(path) - 1)
        return ".";
    std::string full(path, len);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string::npos ? "." : full.substr(0, slash);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0)
        return ".";
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
#else
    char path[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0)
        return ".";
    std::string full(path, static_cast<std::size_t>(len));
    const std::size_t slash = full.find_last_of('/');
    return slash == std::string::npos ? "." : full.substr(0, slash);
#endif
}

// ---------------------------------------------------------------------------
// Capture mode
// ---------------------------------------------------------------------------
int RunCaptureMode(const std::vector<std::string>& args) {
    loli::CliCaptureOptions options;
    options.appName = GetOptionValue(args, "app");
    options.outputFile = GetOptionValue(args, "out");
    options.symbolPath = GetOptionValue(args, "symbol");
    options.subProcessName = GetOptionValue(args, "subprocess");
    options.deviceSerial = GetOptionValue(args, "device");
    const bool verbose = HasOption(args, "verbose");
    options.attachMode = HasOption(args, "attach");
    options.useCache = HasOption(args, "enable-memory-optimization");
    const std::string durationStr = GetOptionValue(args, "duration");
    if (!durationStr.empty())
        options.duration = atoi(durationStr.c_str());

    if (options.appName.empty() || options.outputFile.empty()) {
        std::cerr << "Error: --app and --out are required for capture mode\n";
        PrintUsage();
        return 1;
    }

    const std::string exeDir = ExecutableDir();
    options.executableDir = exeDir;

    // Tool discovery (Qt CLI Initialize parity): load persisted SDK/NDK
    // paths, then trigger the searches (SetSDKPath/SetNDKPath with an empty
    // arg falls back to ANDROID_HOME / the standard install locations).
    {
        AppSettings settings;
        PathUtilsLite::SetSDKPath(settings.Get("AndroidSDK"));
        PathUtilsLite::SetNDKPath(settings.Get("AndroidNDK"));
        PathUtilsLite::LoadPythonPathSettings();
    }
    options.adbPath = PathUtilsLite::GetADBExecutablePath();
    options.pythonPath = PathUtilsLite::GetPythonExecutablePath();

    auto logLine = [](const std::string& s) {
        LOLI_INFO("capture") << s;
    };
    auto logError = [](const std::string& s) {
        LOLI_ERROR("capture") << s;
    };

    logLine("=== LoliProfiler CLI Mode (Qt-free) ===");
    logLine("App: " + options.appName +
            (options.subProcessName.empty() ? "" : ":" + options.subProcessName));
    logLine("Output: " + options.outputFile);
    if (!options.symbolPath.empty())
        logLine("Symbol: " + options.symbolPath);
    logLine(options.duration > 0
                ? "Duration: " + std::to_string(options.duration) + " seconds"
                : "Duration: Press Ctrl+C to stop");
    logLine(std::string("Launch mode: ") + (options.attachMode ? "Attach" : "Launch"));

    loli::CliCaptureSession session;
    session.SetLogger(logLine);
    if (!session.Start(options)) {
        logError("Failed to start capture session");
        return 1;
    }

    // Qt pumped socket events continuously while its one-second timer polled
    // meminfo and screenshots. Keep those cadences separate here too.
    std::signal(SIGINT, OnSignal);
    int elapsed = 0;
    int lastLoggedSecond = -1;
    auto nextTick = std::chrono::steady_clock::now();
    while (!session.IsFinished()) {
        session.PumpChannel();
        if (gStopRequested.load() && !session.IsStopping())
            session.RequestStop();
        const auto now = std::chrono::steady_clock::now();
        if (now < nextTick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        session.Tick();
        nextTick = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (session.IsCapturing() && session.IsLaunchComplete() &&
            session.IsConnected() && options.duration > 0) {
            ++elapsed;
            if (elapsed >= options.duration && !session.IsStopping()) {
                logLine("Duration elapsed, stopping capture...");
                session.RequestStop();
            }
            if (verbose && elapsed != lastLoggedSecond) {
                lastLoggedSecond = elapsed;
                logLine("[" + std::to_string(elapsed) + "s] capturing...");
            }
        }
    }

    int exitCode = 0;
    session.IsFinished(&exitCode);
    logLine(exitCode == 0 ? "Capture complete." : "Capture failed.");
    return exitCode;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);

    if (args.empty()) {
        PrintUsage();
        return 1;
    }
    if (HasOption(args, "help") || HasOption(args, "-h")) {
        PrintUsage();
        return 0;
    }

    auto& logger = loli::LoliLogger::Instance();
    logger.SetConsoleEnabled(true);
    const std::string level = GetOptionValue(args, "log-level");
    if (level == "debug") logger.SetMinLevel(loli::LogLevel::Debug);
    else if (level == "warn") logger.SetMinLevel(loli::LogLevel::Warn);
    else if (level == "error") logger.SetMinLevel(loli::LogLevel::Error);
    else if (!level.empty() && level != "info") {
        LOLI_ERROR("cli") << "Invalid --log-level: " << level;
        return 1;
    }
    std::string logPath = GetOptionValue(args, "log-file");
    const bool fileMode = HasOption(args, "dump") || HasOption(args, "compare") ||
                          HasOption(args, "symbolize");
    // File modes already print stable report text to stdout/stderr. Their
    // diagnostics go to the optional log file without duplicating that text.
    if (fileMode)
        logger.SetConsoleEnabled(false);
    if (logPath.empty() && !fileMode)
        logPath = ExecutableDir() + "/cli_profiler.log";
    if (!logPath.empty() && !logger.StartFile(logPath, /*append=*/false))
        LOLI_ERROR("cli") << "Could not open diagnostic log file: " << logPath;

    // Mode dispatch: --dump, --compare and --symbolize are file-processing
    // modes handled in cli_fileops (ported with the comparator); capture here.
    if (fileMode) {
        extern int RunFileModes(const std::vector<std::string>& args); // cli_fileops.cpp
        LOLI_INFO("cli") << "file mode begin";
        const int result = RunFileModes(args);
        LOLI_INFO("cli") << "file mode complete exit=" << result;
        logger.StopFile();
        return result;
    }

    const int result = RunCaptureMode(args);
    logger.StopFile();
    return result;
}
