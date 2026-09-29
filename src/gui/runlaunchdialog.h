#ifndef LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H
#define LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H

// RunLaunchDialog — modal dialog that consolidates device selection, app
// selection, and capture configuration (replaces the Qt dashboard layout).
// Pure C++17 + Dear ImGui; Qt only at the GuiDataBridge boundary.

#include <string>
#include <vector>

#include "guisnapshot.h"

namespace gui {

class GuiDataBridge;
class CaptureConfigDialog;
struct DeviceInfoLite;  // defined in guidatabridge.h (Qt-side)

class RunLaunchDialog {
public:
    RunLaunchDialog() = default;

    // Arm the dialog to open on the next Render() and refresh device list.
    void Open(GuiDataBridge* bridge);

    // Draw the modal (call every frame). No-op unless open.
    void Render();

    bool IsOpen() const { return open_; }

    // The capture-config editor lives in a separate dialog; main() owns it and
    // hands it over so Run/Launch can open it on demand.
    void SetConfigDialog(CaptureConfigDialog* dlg) { configDialog_ = dlg; }

private:
    void RefreshDevices();
    void RefreshApps();
    void LoadConfigFromBridge();

private:
    GuiDataBridge* bridge_ = nullptr;
    CaptureConfigDialog* configDialog_ = nullptr;
    bool shouldOpen_ = false;
    bool open_ = false;
    bool returnFromConfiguration_ = false;
    bool preflightErrorOpen_ = false;
    std::string preflightError_;
    bool attachToRunningApp_ = false;
    bool retainAllRecords_ = false;

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

    // capture config (read-only summary here; edited in CaptureConfigDialog)
    CaptureConfigSnapshot config_;
    bool configLoaded_ = false;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_RUNLAUNCHDIALOG_H
