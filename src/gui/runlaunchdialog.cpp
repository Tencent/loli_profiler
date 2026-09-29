#include "runlaunchdialog.h"

#include "imgui.h"
#include "guidatabridge.h"
#include "captureconfigdialog.h"

#include <algorithm>
#include <cstring>

namespace gui {

void RunLaunchDialog::Open(GuiDataBridge* bridge) {
    bridge_ = bridge;
    returnFromConfiguration_ = false;
    if (!configLoaded_)
        LoadConfigFromBridge();
    RefreshDevices();
    shouldOpen_ = preflightError_.empty();
    preflightErrorOpen_ = !preflightError_.empty();
}

void RunLaunchDialog::LoadConfigFromBridge() {
    if (!bridge_)
        return;
    config_ = bridge_->GetCaptureConfig();
    configLoaded_ = true;
}

void RunLaunchDialog::RefreshDevices() {
    devices_.clear();
    selectedDevice_ = -1;
    if (!bridge_)
        return;
    const auto devs = bridge_->EnumerateDevices(&preflightError_);
    int firstOnline = -1;
    for (const auto& d : devs) {
        DeviceItem item;
        item.serial = d.serial;
        item.model  = d.model;
        item.device = d.device;
        item.state  = d.state;
        if (item.state == "device" && firstOnline < 0)
            firstOnline = static_cast<int>(devices_.size());
        devices_.push_back(std::move(item));
    }
    // Preselect the only device, else the first online one.
    if (devices_.size() == 1)
        selectedDevice_ = 0;
    else if (firstOnline >= 0)
        selectedDevice_ = firstOnline;
    apps_.clear();
    appsForDevice_.clear();
    if (preflightError_.empty() && firstOnline < 0)
        preflightError_ = "No online Android device was found. Connect or authorize a device, then retry.";
}

void RunLaunchDialog::RefreshApps() {
    apps_.clear();
    if (!bridge_ || selectedDevice_ < 0)
        return;
    const std::string serial = devices_[selectedDevice_].serial;
    const std::vector<std::string> list = bridge_->ListInstalledApps(serial);
    apps_.reserve(list.size());
    for (const auto& a : list)
        apps_.push_back(a);
    appsForDevice_ = devices_[selectedDevice_].serial;
}

void RunLaunchDialog::Render() {
    if (returnFromConfiguration_ && configDialog_ && !configDialog_->IsOpen()) {
        config_ = bridge_->GetCaptureConfig();
        returnFromConfiguration_ = false;
        shouldOpen_ = true;
    }
    if (preflightErrorOpen_) {
        ImGui::OpenPopup("ADB connection problem");
        preflightErrorOpen_ = false;
    }
    if (ImGui::BeginPopupModal("ADB connection problem", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(520.0f);
        ImGui::TextUnformatted(preflightError_.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Retry", ImVec2(120, 0))) {
            RefreshDevices();
            if (preflightError_.empty()) {
                ImGui::CloseCurrentPopup();
                shouldOpen_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (shouldOpen_) {
        ImGui::OpenPopup("Run/Launch");
        shouldOpen_ = false;
        open_ = true;
    }
    if (!open_)
        return;

    // Let the modal fit its complete form. Only the app list scrolls.
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const float dialogW = std::min(560.0f, ImGui::GetIO().DisplaySize.x - 32.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dialogW, 0.0f),
                                        ImVec2(dialogW, 100000.0f));
    bool open = true;
    if (!ImGui::BeginPopupModal("Run/Launch", &open,
                                ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoScrollbar)) {
        if (!open)
            open_ = false;
        return;
    }

    // ---- Device ----
    ImGui::SeparatorText("Device");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
    if (ImGui::SmallButton("Refresh##dev"))
        RefreshDevices();

    if (devices_.empty()) {
        ImGui::TextDisabled("No devices found. Connect a device and click Refresh.");
    } else {
        const char* preview = (selectedDevice_ >= 0)
            ? devices_[selectedDevice_].serial.c_str() : "<select device>";
        if (ImGui::BeginCombo("##device", preview)) {
            for (int i = 0; i < (int)devices_.size(); i++) {
                const auto& d = devices_[i];
                std::string label = d.serial;
                if (!d.model.empty())  label += " - " + d.model;
                label += " (" + (d.state.empty() ? d.device : d.state) + ")";
                const bool sel = (selectedDevice_ == i);
                if (ImGui::Selectable(label.c_str(), sel)) {
                    if (selectedDevice_ != i) {
                        selectedDevice_ = i;
                        apps_.clear();
                        appsForDevice_.clear();
                    }
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // ---- App ----
    ImGui::SeparatorText("Application");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    if (ImGui::SmallButton("Refresh Apps")) {
        if (selectedDevice_ >= 0)
            RefreshApps();
    }
    ImGui::InputTextWithHint("##appmanual", "package name (e.g. com.example.game)",
                             appManual_, sizeof(appManual_));
    ImGui::InputTextWithHint("##appsearch", "filter installed apps...",
                             appSearch_, sizeof(appSearch_));

    const float listHeight = std::clamp(
        ImGui::GetIO().DisplaySize.y - 392.0f,
        80.0f, 220.0f);
    if (ImGui::BeginChild("##applist", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
        if (selectedDevice_ < 0) {
            ImGui::TextDisabled("Select a device first.");
        } else if (apps_.empty() && appsForDevice_.empty()) {
            ImGui::TextDisabled("Click \"Refresh Apps\" to list installed apps.");
        } else {
            // The clipper must receive one submitted row for every index it
            // counts. Build the filtered index first instead of skipping rows
            // inside its range (which also breaks its first-row measurement).
            std::vector<int> visibleApps;
            visibleApps.reserve(apps_.size());
            for (int i = 0; i < static_cast<int>(apps_.size()); ++i) {
                if (appSearch_[0] == '\0' ||
                    apps_[i].find(appSearch_) != std::string::npos)
                    visibleApps.push_back(i);
            }
            if (visibleApps.empty()) {
                ImGui::TextDisabled("No matching apps.");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visibleApps.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const auto& app = apps_[visibleApps[row]];
                        const bool sel = (appManual_ == app);
                        if (ImGui::Selectable(app.c_str(), sel)) {
                            std::strncpy(appManual_, app.c_str(), sizeof(appManual_) - 1);
                            appManual_[sizeof(appManual_) - 1] = '\0';
                        }
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::InputTextWithHint("##subproc", "sub-process name (optional)",
                             subProcess_, sizeof(subProcess_));

    ImGui::Checkbox("Attach to running app", &attachToRunningApp_);

    // ---- Capture config (summary + link to the dedicated dialog) ----
    ImGui::SeparatorText("Capture Config");
    ImGui::TextDisabled("%s | %s | %s | %s | threshold %d",
                        config_.arch.c_str(), config_.compiler.c_str(),
                        config_.mode.c_str(), config_.build.c_str(),
                        config_.threshold);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f);
    if (ImGui::SmallButton("Edit Configuration...")) {
        if (configDialog_) {
            configDialog_->Open(bridge_);
            returnFromConfiguration_ = true;
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::TextUnformatted("Record retention");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(270.0f);
    const char* retentionLabel = retainAllRecords_
        ? "All allocations (full history)"
        : "Live at stop (smaller file)";
    if (ImGui::BeginCombo("##recordRetention", retentionLabel)) {
        if (ImGui::Selectable("Live at stop (smaller file)", !retainAllRecords_))
            retainAllRecords_ = false;
        if (ImGui::Selectable("All allocations (full history)", retainAllRecords_))
            retainAllRecords_ = true;
        ImGui::EndCombo();
    }

    // ---- Footer ----
    ImGui::Separator();
    const bool canLaunch = selectedDevice_ >= 0 && appManual_[0] != '\0';
    if (!canLaunch)
        ImGui::BeginDisabled();
    if (ImGui::Button(attachToRunningApp_ ? "Attach" : "Launch", ImVec2(120, 0))) {
        const std::string serial = devices_[selectedDevice_].serial;
        const std::string app    = appManual_;
        const std::string sub    = subProcess_;
        // Pull the latest saved config (the config dialog may have changed it).
        config_ = bridge_->GetCaptureConfig();
        bridge_->StartCapture(serial, app, sub, config_,
                               /*enableInject=*/attachToRunningApp_,
                               /*useCache=*/!retainAllRecords_);
        open_ = false;
        ImGui::CloseCurrentPopup();
    }
    if (!canLaunch)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        open_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    if (!open)
        open_ = false;
}

} // namespace gui
