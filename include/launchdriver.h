#ifndef LAUNCHDRIVER_H
#define LAUNCHDRIVER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Qt-free capture launch driver (task 3b.1, design D10/D12).
//
// Reproduces the Qt launch sequence step-for-step, in the same order as
// StartAppProcess::StartApp (src/startappprocess.cpp) driven from
// MainWindow::on_launchPushButton_clicked (src/mainwindow.cpp ~line 1943):
//   push libloli.so -> root check -> push loli.conf -> set-debug-app ->
//   monkey launch -> get pid -> adb forward 8700 -> python jdwp-shellifier.
//
// All steps except the python inject run SYNCHRONOUSLY on the calling
// thread (the Qt original blocked the UI thread through the progress
// dialog). Callers who need async behavior can run Run() on a worker.
// Only the python jdwp-shellifier step is asynchronous (ProcessRunner::
// StartAsync); its exit or spawn failure is reported through the
// completion callback passed to Run().
//
// No Qt types, no UI. The two former QMessageBox prompts (Launch vs
// Attach, Enable Data Optimization) become Config fields supplied by the
// caller (ImGui dialog options or CLI flags, per task 3b.3).
class LaunchDriver {
public:
    struct Config {
        std::string deviceSerial;   // adb -s target; empty = no serial
        std::string appName;        // e.g. com.company.app
        std::string subProcessName; // non-empty -> match "<app>:<sub>" pid
        std::string compiler;       // e.g. clang-4.0 (remote/<compiler>/...)
        std::string arch;           // e.g. arm64-v8a (remote/.../<arch>)
        bool enableInject = false;  // true = Attach to running app (the Qt
                                    // code called this interceptMode);
                                    // false = Launch a new instance
        bool useCache = false;      // Enable Data Optimization (strip
                                    // non-persistent); clears the local
                                    // cache/ folder next to the executable
        std::string adbPath;        // full path to the adb executable
        std::string pythonPath;     // full path to the python interpreter
    };

    // Canonical 0-based step indices (0..6) reported via the progress
    // callback. In Attach mode (enableInject) steps 3 and 4 do not run and
    // are not reported, so the caller may observe gaps in the sequence.
    enum Step {
        StepPushLibloli = 0,   // adb push remote/<compiler>/<arch>/libloli.so
        StepRootCheck = 1,     // adb shell su (timeout probe)
        StepPushConf = 2,      // adb push loli3.conf
        StepSetDebugApp = 3,   // adb shell am set-debug-app -w (Launch only)
        StepLaunchApp = 4,     // adb shell monkey -p ... (Launch only)
        StepGetPid = 5,        // adb shell for-loop over /proc cmdlines
        StepForwardPort = 6,   // adb forward tcp:8700 jdwp:<pid>
    };

    // Label for each step, matching the Qt progress dialog strings
    // (typos included for parity; StepRootCheck had no Qt label and got
    // this one added). Returns "Unknown step." outside 0..6.
    static const char* StepLabel(int stepIndex);

    // Progress notification: (stepIndex, label) reported when each blocking
    // step begins. Called on the thread that runs Run().
    using ProgressCallback = std::function<void(int stepIndex, const std::string& label)>;

    // Completion of the async python inject step. ok is true only when the
    // process exited normally with code 0 and its output contained
    // "Command successfully executed" (StartAppProcess::OnProcessFinihed
    // parity). May be invoked from the ProcessRunner watcher thread or,
    // if the driver is stopped/destroyed mid-run, synchronously from
    // Stop()/the destructor on the calling thread.
    using InjectCompletionCallback = std::function<void(bool ok)>;

    LaunchDriver();
    ~LaunchDriver();

    LaunchDriver(const LaunchDriver&) = delete;
    LaunchDriver& operator=(const LaunchDriver&) = delete;

    // Runs the launch sequence. Reports each blocking step through
    // onProgress, starts the async python inject step last, then returns.
    // Returns false on the first failed step (see GetError()); the async
    // step's outcome is reported later via onInjectFinished.
    bool Run(const Config& config,
             ProgressCallback onProgress,
             InjectCompletionCallback onInjectFinished);

    // Kills the async python inject process if it is running (its
    // completion callback fires with ok=false). Safe to call anytime.
    void Stop();

    // True while the async python inject process is alive.
    bool IsRunning() const;

    // True when the inject step reported "Command successfully executed"
    // (StartAppProcess::startResult_ parity). Meaningful only after the
    // inject completion callback has fired with ok=true.
    bool GetStartResult() const;

    // Root check outcome: true when "adb shell su" hung until the 3 s
    // timeout with no output (rooted device).
    bool IsRootDevice() const { return isRootDevice_; }

    // Application pid found in /proc (0 if not found - Qt parity: the
    // original forwarded jdwp:0 without checking).
    uint32_t GetPid() const { return pid_; }

    // Failure description for the step that aborted Run(), or the inject
    // error output. Empty until a failure occurs.
    const std::string& GetError() const { return error_; }

private:
    // Clears the files in <exeDir>/cache when useCache is set (MainWindow
    // parity; files only, subdirectories left alone).
    void ClearLocalCache(const std::string& execDir);

    bool RunAdbStep(const std::string& adbPath,
                    const std::string& stepName,
                    const std::vector<std::string>& arguments);

    std::unique_ptr<class ProcessRunner> injectRunner_;
    InjectCompletionCallback injectCallback_;

    mutable std::mutex stateMutex_; // guards startResult_ (watcher thread)
    bool startResult_ = false;

    bool isRootDevice_ = false;
    uint32_t pid_ = 0;
    std::string error_;
};

#endif // LAUNCHDRIVER_H
