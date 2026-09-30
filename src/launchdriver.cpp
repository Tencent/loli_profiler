#include "launchdriver.h"
#include "lolilogger.h"
#include "captureconfig.h"
#include "processrunner.h"
#include "runtimepaths.h"

#include <cctype>
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <filesystem>
#include <unistd.h>
#include <dirent.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

// Qt-free launch driver. Step order, adb arguments, timeouts and error
// strings mirror StartAppProcess::StartApp (src/startappprocess.cpp) with
// MainWindow::on_launchPushButton_clicked as the caller reference
// (design D10). See openspec/changes/remove-qt-dependency/
// launch-sequence.md for the documented step list and deviations.

namespace {

// Executable directory (applicationDirPath parity). Same approach as
// src/appsettings.cpp GetExecutableDir().
std::string GetExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameA(nullptr, path, sizeof(path) - 1);
    if (len == 0 || len >= sizeof(path) - 1)
        return ".";
    std::string full(path, len);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string::npos ? "." : full.substr(0, slash);
#elif defined(__APPLE__)
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, '\0');
    if (_NSGetExecutablePath(path.data(), &length) != 0)
        return ".";
    return std::filesystem::weakly_canonical(path.c_str()).parent_path().string();
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

// QThread::sleep(2) parity before the pid lookup: the app needs time to
// appear in /proc after the monkey launch.
void SleepSeconds(unsigned int seconds) {
#ifdef _WIN32
    Sleep(seconds * 1000);
#else
    sleep(seconds);
#endif
}

} // namespace

const char* LaunchDriver::StepLabel(int stepIndex) {
    // Strings copied from the Qt progress dialog setLabelText calls
    // (startappprocess.cpp). Original typos kept for parity.
    switch (stepIndex) {
    case StepPushLibloli: return "Pushing libloli.so to device.";
    case StepRootCheck: return "Checking for root device.";
    case StepPushConf: return "Pushing loli.conf to device.";
    case StepSetDebugApp: return "Marking apk debugable for next launch.";
    case StepLaunchApp: return "Launching apk.";
    case StepGetPid: return "Gettting pid.";
    case StepForwardPort: return "Forwadring tcp port.";
    default: return "Unknown step.";
    }
}

LaunchDriver::LaunchDriver() = default;

LaunchDriver::~LaunchDriver() {
    Stop();
}

void LaunchDriver::ClearLocalCache(const std::string& execDir) {
    // MainWindow: if (useCache_) remove every file in <exeDir>/cache.
    // Files only, subdirectories ignored (the Qt QDir(Files) filter
    // behaved the same).
    const std::string cacheDir = execDir + "/cache";
#ifdef _WIN32
    const std::string pattern = cacheDir + "/*";
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return; // no cache folder - nothing to do
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const std::string full = cacheDir + "/" + data.cFileName;
        remove(full.c_str());
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR* dir = opendir(cacheDir.c_str());
    if (!dir)
        return; // no cache folder - nothing to do
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string full = cacheDir + "/" + name;
        remove(full.c_str());
    }
    closedir(dir);
#endif
}

bool LaunchDriver::RunAdbStep(const std::string& adbPath,
                              const std::string& stepName,
                              const std::vector<std::string>& arguments) {
    // Blocking adb one-shot with progress-dialog error strings
    // (StartAppProcess::StartProcess parity: report spawn or wait
    // failures and abort the sequence).
    ProcessRunner process;
    process.SetProgram(adbPath);
    process.SetArguments(arguments);
    if (!process.Start()) {
        error_ = "erro starting: " + stepName;
        return false;
    }
    if (!process.WaitForFinished(10000)) {
        error_ = "erro finishing: " + stepName;
        return false;
    }
    if (process.GetExitCode() != 0) {
        error_ = stepName + ": " + process.ReadAllStderr();
        return false;
    }
    return true;
}

bool LaunchDriver::Run(const Config& config,
                       ProgressCallback onProgress,
                       InjectCompletionCallback onInjectFinished) {
    isRootDevice_ = false;
    pid_ = 0;
    error_.clear();
    startResult_ = false;
    injectCallback_ = std::move(onInjectFinished);

    const auto executableDirectory = GetExecutableDir();
    const std::string execDir = loli::RuntimeDirectory(executableDirectory).string();
    const std::string stateDir = loli::StateDirectory(executableDirectory).string();
    auto report = [&onProgress](int step) {
        if (onProgress)
            onProgress(step, StepLabel(step));
    };

    if (config.useCache) // clear local cache folder before the first push
        ClearLocalCache(stateDir);

    const bool hasSerial = !config.deviceSerial.empty();

    // Step 0: push libloli.so to /data/local/tmp
    // (working dir = executable dir, where remote/<compiler>/<arch> lives).
    {
        report(StepPushLibloli);
        std::vector<std::string> arguments;
        if (hasSerial)
            arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
        arguments.push_back("push");
        arguments.push_back("remote/" + config.compiler + "/" + config.arch + "/libloli.so");
        arguments.push_back("/data/local/tmp");
        ProcessRunner process;
        process.SetProgram(config.adbPath);
        process.SetArguments(arguments);
        process.SetWorkingDirectory(execDir);
        if (!process.Start()) {
            error_ = "erro starting: adb push remote/libloli.so /data/local/tmp";
            return false;
        }
        if (!process.WaitForFinished(10000)) {
            error_ = "erro finishing: adb push remote/libloli.so /data/local/tmp";
            return false;
        }
        if (process.GetExitCode() != 0) {
            error_ = "adb push libloli.so: " + process.ReadAllStderr();
            return false;
        }
    }

    // Step 1: root check - "adb shell su" waits for input on non-rooted
    // devices, so probe with a 3 s timeout. If it is still running and
    // produced no output at all, the device is rooted (empty su output =
    // the shell went straight to root). Either way the process is killed.
    {
        report(StepRootCheck);
        std::vector<std::string> arguments;
        if (hasSerial)
            arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
        arguments.push_back("shell");
        arguments.push_back("su");
        ProcessRunner process;
        process.SetProgram(config.adbPath);
        process.SetArguments(arguments);
        if (process.Start()) {
            if (!process.WaitForFinished(3000)) {
                isRootDevice_ = process.ReadAllStdout().empty();
                process.Kill();
            }
        }
        // Spawn failure leaves isRootDevice_ = false and does not abort
        // the sequence (Qt parity: waitForStarted failure was ignored).
    }

    // Step 2: push loli3.conf to /data/local/tmp.
    // Push the same AppData config that LoadCaptureConfig() reads and the
    // ImGui configuration dialog saves.
    {
        report(StepPushConf);
        std::vector<std::string> arguments;
        if (hasSerial)
            arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
        arguments.push_back("push");
        arguments.push_back(loli::CaptureConfigFilePath());
        arguments.push_back("/data/local/tmp");
        ProcessRunner process;
        process.SetProgram(config.adbPath);
        process.SetArguments(arguments);
        process.SetWorkingDirectory(execDir);
        if (!process.Start()) {
            error_ = "erro starting: adb push loli3.conf /data/local/tmp";
            return false;
        }
        if (!process.WaitForFinished(10000)) {
            error_ = "erro finishing: adb push loli3.conf /data/local/tmp";
            return false;
        }
        if (process.GetExitCode() != 0) {
            error_ = "adb push loli3.conf: " + process.ReadAllStderr();
            return false;
        }
    }

    // Step 3 + 4 (Launch mode only): mark the app debuggable and launch it.
    if (!config.enableInject) {
        {
            report(StepSetDebugApp);
            std::vector<std::string> arguments;
            if (hasSerial)
                arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
            arguments.push_back("shell");
            arguments.push_back("am");
            arguments.push_back("set-debug-app");
            arguments.push_back("-w");
            arguments.push_back(config.appName);
            if (!RunAdbStep(config.adbPath,
                            "adb shell am set-debug-app -w com.company.app", arguments))
                return false;
        }
        {
            report(StepLaunchApp);
            std::vector<std::string> arguments;
            if (hasSerial)
                arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
            arguments.push_back("shell");
            // Note: "monkey -p" is one argument (no space) exactly as in
            // the Qt original - it lands as a single adb shell token.
            arguments.push_back("monkey -p");
            arguments.push_back(config.appName);
            arguments.push_back("-c android.intent.category.LAUNCHER 1");
            if (!RunAdbStep(config.adbPath,
                            "adb shell monkey -p com.company.app"
                            " -c android.intent.category.LAUNCHER 1",
                            arguments))
                return false;
        }
    }

    // Step 5: find the app pid by scanning /proc cmdlines on the device.
    {
        report(StepGetPid);
        SleepSeconds(2); // initial Qt delay; cold starts may need more
        std::vector<std::string> arguments;
        if (hasSerial)
            arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
        arguments.push_back("shell");
        if (config.subProcessName.empty()) {
            arguments.push_back(
                "for p in /proc/[0-9]*; do [[ $(<$p/cmdline) = " + config.appName +
                " ]] && echo ${p##*/}; done");
        } else {
            arguments.push_back(
                "for p in /proc/[0-9]*; do [[ $(<$p/cmdline) = " + config.appName + ":" +
                config.subProcessName + " ]] && echo ${p##*/}; done");
        }
        for (int attempt = 0; attempt < 10 && pid_ == 0; ++attempt) {
            ProcessRunner process;
            process.SetProgram(config.adbPath);
            process.SetArguments(arguments);
            if (!process.Start() || !process.WaitForFinished(10000)) {
                error_ = "looking for target process PID failed";
                return false;
            }
            const std::string output = process.ReadAllStdout();
            try {
                pid_ = static_cast<uint32_t>(std::stoul(output));
            } catch (...) {
                pid_ = 0;
            }
            if (pid_ == 0 && attempt + 1 < 10)
                SleepSeconds(2);
        }
        if (pid_ == 0) {
            error_ = "target process did not start within 20 seconds";
            return false;
        }
    }

    // Step 6: forward a local port to the app's jdwp transport.
    // Port 8700 is the inject channel; the stacktrace channel's port 8000
    // forward stays the caller's job (it lives in the capture session).
    {
        report(StepForwardPort);
        std::vector<std::string> arguments;
        if (hasSerial)
            arguments.push_back("-s"), arguments.push_back(config.deviceSerial);
        arguments.push_back("forward");
        arguments.push_back("tcp:8700");
        arguments.push_back("jdwp:" + std::to_string(pid_));
        if (!RunAdbStep(config.adbPath, "adb forward tcp:8700 jdwp:xxxx", arguments))
            return false;
    }

    // Step 7 (async): python jdwp-shellifier.py inject. Started here and
    // left running - the caller polls IsRunning()/Stop(); completion (or
    // spawn failure) fires injectCallback_.
    std::vector<std::string> arguments;
    arguments.push_back((std::filesystem::path(execDir) / "jdwp-shellifier.py").string());
    arguments.push_back("--target");
    arguments.push_back("127.0.0.1");
    arguments.push_back("--port");
    arguments.push_back("8700");
    arguments.push_back("--break-on");
    arguments.push_back("android.app.Activity.onResume");
    arguments.push_back("--loadlib");
    arguments.push_back("libloli.so");
    injectRunner_.reset(new ProcessRunner());
    injectRunner_->SetProgram(config.pythonPath);
    injectRunner_->SetArguments(arguments);
    std::error_code stateError;
    std::filesystem::create_directories(stateDir, stateError);
    injectRunner_->SetWorkingDirectory(stateDir);

    // Capturing "this" is safe: Stop() (and the destructor via Stop())
    // always joins the watcher thread through Kill() before the runner
    // is destroyed, so the handler never outlives the driver.
    LaunchDriver* self = this;
    const bool started = injectRunner_->StartAsync([self](int exitCode, ProcessRunner::ExitStatus status) {
        // Watcher thread. Same success criterion as
        // StartAppProcess::OnProcessFinihed: output line containing
        // "Command successfully executed".
        bool ok = false;
        const std::string out = self->injectRunner_->ReadAllStdout();
        const std::string err = self->injectRunner_->ReadAllStderr();
        if (status == ProcessRunner::ExitStatus::NormalExit && exitCode == 0) {
            // jdwp-shellifier logs through Python logging, which writes to
            // STDERR; the success line must be searched across both streams.
            const std::string output = out + "\n" + err;
            ok = output.find("Command successfully executed") != std::string::npos;
            if (ok)
                LOLI_INFO("inject") << "exit=0 ok=true stdout_bytes=" << out.size()
                                    << " stderr_bytes=" << err.size();
            else
                LOLI_ERROR("inject") << "exit=0 success marker missing; stderr_tail="
                                     << err.substr(err.size() > 1000 ? err.size()-1000 : 0);
        } else {
            LOLI_ERROR("inject") << "exit=" << exitCode << " status="
                                 << static_cast<int>(status) << " stdout_tail="
                                 << out.substr(out.size() > 500 ? out.size()-500 : 0)
                                 << " stderr_tail="
                                 << err.substr(err.size() > 1000 ? err.size()-1000 : 0);
        }
        {
            std::lock_guard<std::mutex> lock(self->stateMutex_);
            self->startResult_ = ok;
        }
        if (self->injectCallback_)
            self->injectCallback_(ok);
    });
    if (!started) {
        // Spawn failure reports through the completion callback (Qt
        // parity: process errorOccurred -> OnProcessErrorOccurred).
        error_ = "python jdwp-shellifier.py";
        injectRunner_.reset();
        if (injectCallback_)
            injectCallback_(false);
        return false;
    }
    return true;
}

void LaunchDriver::Stop() {
    if (!injectRunner_)
        return;
    // Always Kill(), never just destroy: Kill() joins the watcher thread,
    // so the async handler (which reads this driver) has finished before
    // the runner is reset. Killing an already-exited process is a no-op.
    injectRunner_->Kill();
    injectRunner_.reset();
}

bool LaunchDriver::IsRunning() const {
    return injectRunner_ ? injectRunner_->IsRunning() : false;
}

bool LaunchDriver::GetStartResult() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return startResult_;
}
