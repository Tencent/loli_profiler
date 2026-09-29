#ifndef ADBTOOLS_H
#define ADBTOOLS_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "processrunner.h"

// Qt-free adb tool wrappers (task 4.2) replacing MemInfoProcess,
// ScreenshotProcess and AddressProcess. They run on ProcessRunner with the
// same adb commands and output parsing as the Qt originals. All are
// asynchronous (callback on a watcher thread - marshal to the consumer's
// event queue) or synchronous one-shots, matching each Qt call site's use.
//
// Design D12: plain LoliCore APIs, no UI types. The screenshot is returned
// as raw PNG bytes (adb exec-out screencap -p) - JPEG re-encoding was done
// by QPixmap in the GUI and stays a GUI concern; consumers that want JPEG
// encode it where they have an image library.

namespace loli {

struct MemInfo {
    uint32_t Total = 0;
    uint32_t NativeHeap = 0;
    uint32_t GfxDev = 0;
    uint32_t EGLmtrack = 0;
    uint32_t GLmtrack = 0;
    uint32_t Unknown = 0;

    void Reset() { *this = MemInfo(); }
};

// Parses `adb shell dumpsys meminfo --package <app>` output exactly like
// MemInfoProcess::OnProcessFinihed: finds the "[app]" header line (pid in
// column 4), then scans the following ~22 lines for Native/Gfx/EGL/GL/
// Unknown/TOTAL rows (values / 1024). Returns false when the output shows
// an error / no process found in the first two lines.
bool ParseMemInfoDump(const std::string& output, const std::string& appName,
                      MemInfo& outInfo, std::string& outAppPid);

// Runs dumpsys meminfo for the app asynchronously (MemInfoProcess::
// DumpMemInfoAsync parity, including the ':subProcess' suffix). onFinished
// runs on a watcher thread once the dump is parsed. Returns false when the
// adb spawn itself failed. Only one dump may be in flight per instance.
class MemInfoDumper {
public:
    using ResultHandler = std::function<void(bool ok, const MemInfo& info,
                                             const std::string& appPid)>;

    void SetAdbPath(const std::string& path) { adbPath_ = path; }
    void SetDeviceSerial(const std::string& serial) { deviceSerial_ = serial; }

    bool DumpAsync(const std::string& appName, const std::string& subProcessName,
                   ResultHandler onFinished);

    bool IsRunning() const { return runner_.IsRunning(); }
    void Stop() { runner_.Kill(); }

private:
    std::string adbPath_;
    std::string deviceSerial_;
    ProcessRunner runner_;
};

// Captures a device screenshot (adb exec-out screencap -p) asynchronously.
// onFinished receives the raw PNG bytes on a watcher thread.
class ScreenshotCapture {
public:
    using ResultHandler = std::function<void(bool ok, std::vector<uint8_t> pngBytes)>;

    void SetAdbPath(const std::string& path) { adbPath_ = path; }
    void SetDeviceSerial(const std::string& serial) { deviceSerial_ = serial; }

    bool CaptureAsync(ResultHandler onFinished);

    bool IsRunning() const { return runner_.IsRunning(); }
    void Stop() { runner_.Kill(); }

private:
    std::string adbPath_;
    std::string deviceSerial_;
    ProcessRunner runner_;
};

// Symbol address lookup via the NDK addr2line tool (AddressProcess parity:
// `-f -C -e <symbolFile> <addrs...>`, function names on even output lines).
// Synchronous: run on a worker and fan out per library file.
// Returns the number of resolved addresses written into outMap.
size_t ResolveAddresses(const std::string& addr2linePath, const std::string& symbolFile,
                        const std::vector<std::string>& addresses,
                        std::unordered_map<std::string, std::string>& outMap);

} // namespace loli

#endif // ADBTOOLS_H
