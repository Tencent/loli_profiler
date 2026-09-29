#ifndef LOLI_PROFILER_GUI_GUISNAPSHOT_H
#define LOLI_PROFILER_GUI_GUISNAPSHOT_H

// POD snapshot types consumed by the ImGui panels.
// Pure C++17 — NO Qt includes. The GuiDataBridge (the only Qt-aware file in
// the GUI target) fills these from core profiling state once per frame/update.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gui {

// Forward decl: the aggregated call tree is built on a worker thread and
// handed to the UI inside the snapshot (avoids a UI-thread rebuild).
class StacktraceTree;

// A single allocation/free record (mirrors core StackRecord, Qt-free).
struct RecordSnapshot {
    uint32_t seq = 0;
    int32_t  timeMs = 0;      // capture-relative time
    int32_t  size = 0;        // allocation size (0/negative for free markers)
    uint64_t addr = 0;
    uint64_t funcAddr = 0;
    std::string library;
};

// One frame of a resolved callstack (leaf-first or root-first per caller).
struct StackFrameSnapshot {
    std::string funcName;     // resolved symbol or hex address fallback
    std::string library;
    uint64_t    funcAddr = 0;
};

// A node in the merged callstack tree (aggregated by function).
struct CallTreeNode {
    std::string funcName;
    std::string library;
    uint64_t    totalSize = 0;   // aggregated bytes at/below this node
    uint32_t    allocCount = 0;  // aggregated allocation count
    int32_t     parent = -1;     // index into a flat node array, -1 = root
    int32_t     firstChild = -1; // index of first child (siblings linked)
    int32_t     nextSibling = -1;
};

// Timeline sample for the memory-info chart.
struct MemInfoSample {
    int32_t  timeMs = 0;
    uint32_t total = 0;
    uint32_t nativeHeap = 0;
    uint32_t gfxDev = 0;
    uint32_t eglMtrack = 0;
    uint32_t glMtrack = 0;
    uint32_t unknown = 0;
};

// One captured screenshot (JPEG bytes + capture time).
struct ScreenshotSnapshot {
    int32_t timeMs = 0;
    std::vector<uint8_t> jpegBytes;
};

// smaps statistics for one library/section.
struct SMapsSectionSnapshot {
    std::string name;
    uint32_t virtualSize = 0;
    uint32_t rss = 0;
    uint32_t pss = 0;
    uint32_t sharedClean = 0;
    uint32_t sharedDirty = 0;
    uint32_t privateClean = 0;
    uint32_t privateDirty = 0;
};

// A discovered Android device (adb).
struct DeviceSnapshot {
    std::string serial;
    std::string model;
    std::string device;
    std::string state;
    bool        authorized = true;
};

// Capture configuration mirrored from core ConfigDialog::Settings.
struct CaptureConfigSnapshot {
    int         threshold = 128;
    std::string mode = "strict";
    std::string build = "default";
    std::string type = "white list";
    std::string arch = "armeabi-v7a";
    std::string compiler = "gcc";
    std::string hook = "malloc";
    std::vector<std::string> whitelist;
    std::vector<std::string> blacklist;
};

// Live capture/session state shown in the status panel.
struct CaptureStateSnapshot {
    bool     connected = false;
    bool     capturing = false;
    int32_t  elapsedMs = 0;
    uint64_t recordCount = 0;
    std::string appName;
    std::string deviceSerial;
};

// The whole per-frame snapshot handed to panels.
struct GuiSnapshot {
    CaptureStateSnapshot             capture;
    std::vector<DeviceSnapshot>      devices;
    std::vector<std::string>         installedApps;
    std::vector<RecordSnapshot>      records;      // flat allocation records
    // The aggregated call tree, pre-built on the loader worker thread. The UI
    // adopts it (move) when a new snapshot version arrives. Null when empty.
    std::shared_ptr<StacktraceTree>  stackTree;
    std::shared_ptr<StacktraceTree>  liveStackTree; // filtered by free sequence
    bool                             liveTreeUsesAll = false; // saved free filter excludes nothing
    std::vector<MemInfoSample>       memTimeline;
    std::vector<ScreenshotSnapshot>  screenshots;
    std::vector<SMapsSectionSnapshot> smaps;
    std::vector<std::string>         logLines;     // console output tail
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_GUISNAPSHOT_H
