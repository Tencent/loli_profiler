# Launch sequence (task 3b.1) - Qt reference vs Qt-free LaunchDriver

The Qt launch flow lives in two places:

- Caller-side: `MainWindow::on_launchPushButton_clicked` (src/mainwindow.cpp
  ~line 1943) - device serial selection, the two prompt dialogs, cache
  clearing, progress dialog setup, state reset.
- Sequence itself: `StartAppProcess::StartApp` (src/startappprocess.cpp) -
  the adb steps and the async python inject.

The Qt-free port is `LaunchDriver` (include/launchdriver.h +
src/launchdriver.cpp), a UI-free LoliCore API per design D12: the two
QMessageBox prompts become `Config::enableInject` / `Config::useCache`
fields supplied by the caller (ImGui Run/Launch dialog or CLI flags),
and the QProgressDialog label updates become a progress callback.

## Driver API surface

```cpp
struct LaunchDriver::Config {
    std::string deviceSerial;   // adb -s target; empty = no serial
    std::string appName;        // com.company.app
    std::string subProcessName; // non-empty -> match "<app>:<sub>"
    std::string compiler;       // remote/<compiler>/<arch>/libloli.so
    std::string arch;
    bool enableInject = false;  // true = Attach (Qt: interceptMode)
    bool useCache = false;      // Enable Data Optimization
    std::string adbPath;        // path to adb executable
    std::string pythonPath;     // path to python interpreter
};

bool Run(const Config&, ProgressCallback, InjectCompletionCallback);
void Stop();            // kill the async inject process
bool IsRunning() const; // inject process alive?
bool GetStartResult() const; // inject output said "Command successfully executed"
bool IsRootDevice() const;
uint32_t GetPid() const;     // 0 if not found (reported as launch error)
const std::string& GetError() const;
```

`Run()` executes steps 0-6 synchronously on the calling thread (the Qt
original blocked the UI thread through the progress dialog; callers who
need async run `Run()` on a worker). Only the python inject step is
async (`ProcessRunner::StartAsync`), reported via the completion
callback; the caller polls `IsRunning()` / calls `Stop()` to cancel.

## Step list (in exact Qt order)

| # | Step | Label (Qt progress dialog string, typos kept) | Command / action |
|---|------|-------|------------------|
| 0 | StepPushLibloli | "Pushing libloli.so to device." | `adb push remote/<compiler>/<arch>/libloli.so /data/local/tmp` (cwd = exe dir) |
| 1 | StepRootCheck | "Checking for root device." | `adb shell su`, 3000 ms timeout probe; still running + empty output => rooted; then Kill |
| 2 | StepPushConf | "Pushing loli.conf to device." | `adb push <AppData>/LoliProfiler/loli3.conf /data/local/tmp` (the file read by `LoadCaptureConfig`) |
| 3 | StepSetDebugApp | "Marking apk debugable for next launch." | `adb shell am set-debug-app -w <app>` (Launch mode only) |
| 4 | StepLaunchApp | "Launching apk." | `adb shell monkey -p <app> -c android.intent.category.LAUNCHER 1` (Launch mode only) |
| 5 | StepGetPid | "Gettting pid." | sleep 2 s, then retry `adb shell "for p in /proc/[0-9]*; do [[ $(<$p/cmdline) = <app>[:<sub>] ]] && echo ${p##*/}; done"` for up to 20 s; report a launch error if still absent |
| 6 | StepForwardPort | "Forwadring tcp port." | `adb forward tcp:8700 jdwp:<pid>` |
| 7 | (async inject) | "Injecting libloli.so to target application." | `python jdwp-shellifier.py --target 127.0.0.1 --port 8700 --break-on android.app.Activity.onResume --loadlib libloli.so` (cwd = exe dir), async |

Notes carried over from the Qt original:

- In Attach mode (`enableInject = true`) steps 3 and 4 do not run and are
  not reported, so the progress callback sees indices 0,1,2,5,6.
- Step 1: `adb shell su` waits for input on non-rooted devices. The probe
  uses ProcessRunner's timeout semantics: `Start()`;
  `WaitForFinished(3000)` returning false leaves partial stdout readable;
  `ReadAllStdout().empty()` => rooted; then `Kill()`. A spawn failure is
  ignored (leaves `isRootDevice_ = false`) exactly like the Qt code
  ignoring a `waitForStarted` failure.
- Step 5 pid: 0 means "not found". Cold launches can take longer than the
  Qt two-second delay, so the driver retries instead of forwarding `jdwp:0`.
- Error strings reproduce `StartAppProcess::StartProcess` verbatim
  (including the "erro starting/finishing" typos): e.g.
  `"erro starting: adb push remote/libloli.so /data/local/tmp"`.
- Port 8700 is the jdwp/inject channel only. The stacktrace channel's
  port 8000 forward is NOT part of the driver - it belongs to the capture
  session (caller's job), matching the Qt split.

## Shell quoting

The Qt code used `QProcess::setNativeArguments` on Windows (space-
containing arguments double-quoted) and `setArguments` on POSIX.
`ProcessRunner::BuildCommandLine` implements exactly this convention, so
every adb/python invocation goes through ProcessRunner unchanged. The
`monkey -p` and `-c android.intent.category.LAUNCHER 1` arguments are
passed as single tokens exactly as the Qt QStringList did.

## Inject completion criterion

`StartAppProcess::OnProcessFinihed` set `startResult_ = true` when the
inject output contained a line with "Command successfully executed". The
driver's async handler applies the same rule (plus a normal-exit /
exit-code-0 check), stored in `startResult_`, guarded by a mutex because
the handler runs on the ProcessRunner watcher thread. `Stop()` always
calls `Kill()` (not just destroy) so the watcher thread is joined before
the runner is released - the handler never outlives the driver.

## Deviations from the Qt original

1. **Absolute config path for the loli3.conf push (step 2).** The Qt code
   used AppData as its working directory. The Qt-free driver passes the
   absolute `CaptureConfigFilePath()` and checks that `adb push` succeeded,
   so launch and the configuration dialog use the same saved file.
2. **Cache clear scope.** `MainWindow` cleared only files inside
   `<exeDir>/cache` (QDir::Files filter). The driver reproduces that
   (Win32 `FindFirstFile` / POSIX `dirent`, files only, subdirectories
   ignored) and runs it at the start of `Run()` instead of in the GUI
   click handler.
3. **Prompt dialogs become Config fields.** Launch vs Attach and Enable
   Data Optimization are `enableInject` / `useCache` on `Config` (task
   3b.3 moves the ImGui dialog / CLI flags onto them).
4. **Async inject spawn failure.** Qt reported it via the QProcess error
   signal chain; the driver reports it synchronously through the inject
   completion callback with `ok = false` and sets `error_`.
5. **Step labels.** The root check had no progress-dialog label in the Qt
   code (the dialog stayed on the previous label during it); the driver
   reports a label for all 7 steps for consistent progress UX. All other
   labels are verbatim, typos included ("Gettting", "Forwadring",
   "debugable").
