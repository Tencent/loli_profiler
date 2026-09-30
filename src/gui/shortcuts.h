#pragma once

#include <imgui.h>

namespace gui {

inline bool IsApplicationShortcutPressed(ImGuiKey key) {
    // ImGui's macOS behavior normalizes physical Command to ImGuiMod_Ctrl.
    // Use that logical modifier on every platform, including text editing.
    // Exact chords also avoid firing Cmd+O for Cmd+Shift+O or Ctrl+Cmd+O.
    return ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | key);
}

} // namespace gui
