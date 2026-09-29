#ifndef LOLI_PROFILER_GUI_CAPTURECONFIGDIALOG_H
#define LOLI_PROFILER_GUI_CAPTURECONFIGDIALOG_H

// CaptureConfigDialog — modal dialog for the capture configuration
// (threshold/mode/build/type/arch/compiler/hook + whitelist/blacklist).
// Split out of RunLaunchDialog so the launch flow stays small; these settings
// rarely change once settled. Supports named presets (save/load/delete) backed
// by the "saved:" blocks in loli3.conf.
//
// Fixed-size, non-resizable. Pure C++17 + Dear ImGui; no Qt.

#include <string>
#include <vector>

#include "guisnapshot.h"

namespace gui {

class GuiDataBridge;

struct EditableListState {
    int editIndex = -1;
    char buffer[512] = {};
    bool focusEdit = false;
    bool scrollToEdit = false;
    bool newItem = false;
};

class CaptureConfigDialog {
public:
    CaptureConfigDialog() = default;

    // Arm the dialog to open on the next Render() and load config + presets.
    void Open(GuiDataBridge* bridge);

    // Draw the modal (call every frame). No-op unless open.
    void Render();

    bool IsOpen() const { return open_; }

private:
    void Reload();

    GuiDataBridge* bridge_ = nullptr;
    bool shouldOpen_ = false;
    bool open_ = false;

    CaptureConfigSnapshot config_;

    // Presets (name -> config), refreshed on Open and after save/delete.
    std::vector<std::pair<std::string, CaptureConfigSnapshot>> presets_;
    int  selectedPreset_ = -1;
    char presetName_[128] = {0};

    EditableListState whiteListEdit_;
    EditableListState blackListEdit_;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_CAPTURECONFIGDIALOG_H
