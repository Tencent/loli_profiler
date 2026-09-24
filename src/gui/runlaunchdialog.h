#ifndef LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H
#define LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H

// RunLaunchDialog — modal dialog that consolidates device selection, app
// selection, and capture configuration (replaces the Qt dashboard layout).
// Pure C++17 + Dear ImGui; Qt only at the GuiDataBridge boundary.

#include <string>
#include <vector>

#include "guisnapshot.h"

class QString;
class QStringList;

namespace gui {

class GuiDataBridge;
struct DeviceInfoLite;  // defined in guidatabridge.h (Qt-side)

class RunLaunchDialog {
public:
    RunLaunchDialog() = default;

    // Arm the dialog to open on the next Render() and refresh device list.
    void Open(GuiDataBridge* bridge);

    // Draw the modal (call every frame). No-op unless open.
    void Render();

    bool IsOpen() const { return open_; }

private:
    void RefreshDevices();
    void RefreshApps();
    void LoadConfigFromBridge();

private:
    GuiDataBridge* bridge_ = nullptr;
    bool shouldOpen_ = false;
    bool open_ = false;

    // device selection
    struct DeviceItem { std::string serial, model, device, state; };
    std::vector<DeviceItem> devices_;
    int  selectedDevice_ = -1;

    // app selection
    std::vector<std::string> apps_;
    std::string appsForDevice_;
    char appSearch_[128] = {0};
    char appManual_[256] = {0};
    char subProcess_[256] = {0};

    // capture config
    CaptureConfigSnapshot config_;
    bool configLoaded_ = false;
    char whiteEntry_[512] = {0};
    char blackEntry_[512] = {0};

    static constexpr int kDefaultThreshold = 128;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H
