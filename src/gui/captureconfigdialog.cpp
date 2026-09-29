#include "captureconfigdialog.h"

#include "imgui.h"
#include "guidatabridge.h"

#include <algorithm>
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

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Right click to add/remove; double click to edit in place. Escape cancels,
// while Enter or clicking elsewhere commits the current nonempty value.
void EditStringList(const char* id, std::vector<std::string>& items,
                    EditableListState& state, float listHeight) {
    ImGui::PushID(id);
    auto beginEdit = [&](int i) {
        if (state.editIndex >= 0 && state.editIndex < static_cast<int>(items.size()) &&
            state.editIndex != i) {
            if (state.buffer[0] != '\0')
                items[state.editIndex] = state.buffer;
            else if (state.newItem) {
                items.erase(items.begin() + state.editIndex);
                if (i > state.editIndex) --i;
            }
        }
        state.editIndex = i;
        std::strncpy(state.buffer, items[i].c_str(), sizeof(state.buffer) - 1);
        state.buffer[sizeof(state.buffer) - 1] = '\0';
        state.focusEdit = true;
        state.scrollToEdit = false;
        state.newItem = false;
    };
    bool addItem = false;
    int removeItem = -1;

    if (ImGui::BeginChild("##entries", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
        if (items.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            for (int i = 0; i < (int)items.size(); i++) {
                ImGui::PushID(i);
                if (state.editIndex == i) {
                    if (state.focusEdit) {
                        ImGui::SetKeyboardFocusHere();
                        state.focusEdit = false;
                    }
                    ImGui::SetNextItemWidth(-1.0f);
                    const bool enter = ImGui::InputText("##edit", state.buffer,
                        sizeof(state.buffer), ImGuiInputTextFlags_EnterReturnsTrue);
                    if (state.scrollToEdit) {
                        ImGui::SetScrollHereY(1.0f);
                        state.scrollToEdit = false;
                    }
                    const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (cancel || enter || ImGui::IsItemDeactivated()) {
                        if (!cancel && state.buffer[0] != '\0')
                            items[i] = state.buffer;
                        else if (state.newItem)
                            removeItem = i;
                        state.editIndex = -1;
                        state.newItem = false;
                    }
                } else {
                    ImGui::Selectable(items[i].c_str());
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                        beginEdit(i);
                }
                if (ImGui::BeginPopupContextItem("##itemmenu")) {
                    if (ImGui::MenuItem("Add")) addItem = true;
                    if (ImGui::MenuItem("Edit")) beginEdit(i);
                    if (ImGui::MenuItem("Remove")) removeItem = i;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        if (ImGui::BeginPopupContextWindow("##listmenu", ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add")) addItem = true;
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    if (removeItem >= 0) {
        items.erase(items.begin() + removeItem);
        if (state.editIndex >= removeItem) state.editIndex = -1;
    }
    if (addItem) {
        items.emplace_back();
        state.editIndex = static_cast<int>(items.size()) - 1;
        state.buffer[0] = '\0';
        state.focusEdit = true;
        state.scrollToEdit = true;
        state.newItem = true;
    }

    ImGui::PopID();
}

} // namespace

void CaptureConfigDialog::Open(GuiDataBridge* bridge) {
    bridge_ = bridge;
    shouldOpen_ = true;
    Reload();
}

void CaptureConfigDialog::Reload() {
    if (!bridge_)
        return;
    config_ = bridge_->GetCaptureConfig();
    presets_ = bridge_->GetSavedCaptureConfigs();
    selectedPreset_ = -1;
    whiteListEdit_ = {};
    blackListEdit_ = {};
}

void CaptureConfigDialog::Render() {
    if (shouldOpen_) {
        ImGui::OpenPopup("Capture Configuration");
        shouldOpen_ = false;
        open_ = true;
    }
    if (!open_)
        return;

    // Wider layout keeps both pattern lists visible without an outer scroll.
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const float dialogW = std::min(720.0f, ImGui::GetIO().DisplaySize.x - 32.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dialogW, 0.0f),
                                        ImVec2(dialogW, 100000.0f));
    bool open = true;
    if (!ImGui::BeginPopupModal("Capture Configuration", &open,
                                ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoScrollbar)) {
        if (!open)
            open_ = false;
        return;
    }

    // ---- Presets ----
    ImGui::SeparatorText("Presets");
    {
        const char* preview = (selectedPreset_ >= 0 && selectedPreset_ < (int)presets_.size())
            ? presets_[selectedPreset_].first.c_str() : "<select preset>";
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##preset", preview)) {
            for (int i = 0; i < (int)presets_.size(); i++) {
                const bool sel = (selectedPreset_ == i);
                if (ImGui::Selectable(presets_[i].first.c_str(), sel))
                    selectedPreset_ = i;
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool presetActionsDisabled = selectedPreset_ < 0;
        if (presetActionsDisabled)
            ImGui::BeginDisabled();
        if (ImGui::Button("Load")) {
            config_ = presets_[selectedPreset_].second;
            whiteListEdit_ = {};
            blackListEdit_ = {};
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            bridge_->DeleteCaptureConfigPreset(presets_[selectedPreset_].first);
            presets_ = bridge_->GetSavedCaptureConfigs();
            selectedPreset_ = -1;
        }
        if (presetActionsDisabled)
            ImGui::EndDisabled();

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##presetname", "preset name...", presetName_,
                                 sizeof(presetName_));
        ImGui::SameLine();
        const bool savePresetDisabled = presetName_[0] == '\0';
        if (savePresetDisabled)
            ImGui::BeginDisabled();
        if (ImGui::Button("Save As Preset")) {
            bridge_->SaveCaptureConfigPreset(presetName_, config_);
            presets_ = bridge_->GetSavedCaptureConfigs();
            presetName_[0] = '\0';
        }
        if (savePresetDisabled)
            ImGui::EndDisabled();
    }

    // ---- Fields ----
    ImGui::SeparatorText("Capture");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0f));
    ImGui::InputInt("Threshold (bytes)", &config_.threshold);
    HelpMarker("Strict: minimum allocation size. Loose: sampling interval. Ignored by nostack.");
    if (config_.threshold < 0) config_.threshold = 0;
    ComboFromOptions("Mode",     config_.mode,     {"strict", "nostack", "loose"});
    HelpMarker("Strict records allocations at or above the threshold. Loose samples allocations. Nostack records sizes and libraries without call stacks.");
    ComboFromOptions("Build",    config_.build,    {"default", "instrumented", "framepointer"});
    HelpMarker("Default unwinds stacks. Instrumented uses an app-provided backtrace when available. Framepointer walks frame pointers.");
    ComboFromOptions("Type",     config_.type,     {"white list", "black list"});
    HelpMarker("White list hooks only listed libraries. Black list hooks detected libraries except those listed.");
    if (ComboFromOptions("Arch", config_.arch,
                         {"armeabi-v7a", "arm64-v8a", "armeabi", "x86", "x86_64"})) {
        if (config_.arch == "x86" || config_.arch == "x86_64") config_.compiler = "llvm";
        if (config_.arch == "armeabi") config_.compiler = "gcc";
    }
    HelpMarker("Target process ABI. It must match the app process, not the host computer.");
    const bool fixedCompiler = config_.arch == "x86" || config_.arch == "x86_64" || config_.arch == "armeabi";
    if (fixedCompiler) ImGui::BeginDisabled();
    ComboFromOptions("Compiler", config_.compiler, {"gcc", "llvm"});
    if (fixedCompiler) ImGui::EndDisabled();
    HelpMarker("Selects the packaged hook library. x86 and x86_64 use LLVM; armeabi uses GCC.");
    ComboFromOptions("Hook",     config_.hook,     {"malloc", "mmap"});
    HelpMarker("Malloc hooks the allocation family; mmap hooks mmap, mmap64, and munmap.");
    ImGui::PopStyleVar();

    // Leave room for list headers, the footer, and the modal's bottom padding.
    const float entryListHeight = std::clamp(
        ImGui::GetIO().DisplaySize.y - ImGui::GetCursorScreenPos().y - 160.0f,
        40.0f, 110.0f);
    if (ImGui::BeginTable("##capturelists", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Whitelist (captured if matching)");
        HelpMarker("Library names. Right click to add or remove; double click to edit.");
        EditStringList("##whitelist", config_.whitelist, whiteListEdit_, entryListHeight);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Blacklist (excluded if matching)");
        HelpMarker("Library names. Right click to add or remove; double click to edit.");
        EditStringList("##blacklist", config_.blacklist, blackListEdit_, entryListHeight);
        ImGui::EndTable();
    }

    // ---- Footer ----
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        bridge_->SaveCaptureConfig(config_);
        open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        open_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    if (!open)
        open_ = false;
}

} // namespace gui
