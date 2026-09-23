#include "runlaunchdialog.h"

#include "imgui.h"
#include "guidatabridge.h"

#include <QString>
#include <QStringList>
#include <cstring>

namespace gui {

namespace {

// Combo helper for a fixed set of string options; returns true if changed.
bool ComboFromOptions(const char* label, std::string& current,
                      std::initializer_list<const char*> options) {
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (const char* opt : options) {
            const bool selected = (current == opt);
            if (ImGui::Selectable(opt, selected)) {
                current = opt;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void RunLaunchDialog::Open(GuiDataBridge* bridge) {
    bridge_ = bridge;
    shouldOpen_ = true;
    if (!configLoaded_)
        LoadConfigFromBridge();
    RefreshDevices();
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
    const auto devs = bridge_->EnumerateDevices();
    int firstOnline = -1;
    for (const auto& d : devs) {
        DeviceItem item;
        item.serial = d.serial.toStdString();
        item.model  = d.model.toStdString();
        item.device = d.device.toStdString();
        item.state  = d.state.toStdString();
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
}

void RunLaunchDialog::RefreshApps() {
    apps_.clear();
    if (!bridge_ || selectedDevice_ < 0)
        return;
    const QString serial = QString::fromStdString(devices_[selectedDevice_].serial);
    const QStringList list = bridge_->ListInstalledApps(serial);
    apps_.reserve(list.size());
    for (const auto& a : list)
        apps_.push_back(a.toStdString());
    appsForDevice_ = devices_[selectedDevice_].serial;
}

void RunLaunchDialog::Render() {
    if (shouldOpen_) {
        ImGui::OpenPopup("Run/Launch");
        shouldOpen_ = false;
        open_ = true;
    }
    if (!open_)
        return;

    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::BeginPopupModal("Run/Launch", &open, ImGuiWindowFlags_NoCollapse)) {
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
        if (selectedDevice_ >= 0 &&
            appsForDevice_ != devices_[selectedDevice_].serial)
            RefreshApps();
    }
    ImGui::InputTextWithHint("##appmanual", "package name (e.g. com.example.game)",
                             appManual_, sizeof(appManual_));
    ImGui::InputTextWithHint("##appsearch", "filter installed apps...",
                             appSearch_, sizeof(appSearch_));

    const float listHeight = 160.0f;
    if (ImGui::BeginChild("##applist", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
        if (selectedDevice_ < 0) {
            ImGui::TextDisabled("Select a device first.");
        } else if (apps_.empty() && appsForDevice_.empty()) {
            ImGui::TextDisabled("Click \"Refresh Apps\" to list installed apps.");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin((int)apps_.size());
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const auto& app = apps_[i];
                    if (appSearch_[0] != '\0' &&
                        app.find(appSearch_) == std::string::npos)
                        continue;
                    const bool sel = (appManual_ == app);
                    if (ImGui::Selectable(app.c_str(), sel)) {
                        std::strncpy(appManual_, app.c_str(), sizeof(appManual_) - 1);
                        appManual_[sizeof(appManual_) - 1] = '\0';
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::InputTextWithHint("##subproc", "sub-process name (optional)",
                             subProcess_, sizeof(subProcess_));

    // ---- Capture config ----
    if (ImGui::CollapsingHeader("Capture Configuration")) {
        ImGui::InputInt("Threshold (bytes)", &config_.threshold);
        if (config_.threshold < 0) config_.threshold = 0;
        ComboFromOptions("Mode",     config_.mode,     {"strict", "nostack", "loose"});
        ComboFromOptions("Build",    config_.build,    {"default", "debug"});
        ComboFromOptions("Type",     config_.type,     {"white list", "black list"});
        ComboFromOptions("Arch",     config_.arch,     {"armeabi-v7a", "arm64-v8a", "x86", "x86_64"});
        ComboFromOptions("Compiler", config_.compiler, {"gcc", "clang"});
        ComboFromOptions("Hook",     config_.hook,     {"malloc"});
        ImGui::TextDisabled("Whitelist/blacklist editing is done in the config file for now.");
    }

    // ---- Footer ----
    ImGui::Separator();
    const bool canLaunch = selectedDevice_ >= 0 && appManual_[0] != '\0';
    if (!canLaunch)
        ImGui::BeginDisabled();
    if (ImGui::Button("Launch", ImVec2(120, 0))) {
        const QString serial = QString::fromStdString(devices_[selectedDevice_].serial);
        const QString app    = QString::fromStdString(appManual_);
        const QString sub    = QString::fromStdString(subProcess_);
        bridge_->SaveCaptureConfig(config_);
        bridge_->StartCapture(serial, app, sub, config_);
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
