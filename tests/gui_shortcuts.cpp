#include "shortcuts.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::array<ImGuiKey, 5> keys{
    ImGuiKey_O, ImGuiKey_R, ImGuiKey_S, ImGuiKey_Comma, ImGuiKey_Q};

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void Frame() {
    ImGui::GetIO().DeltaTime = 1.0f / 60;
    ImGui::NewFrame();
}

void Press(ImGuiKey key, ImGuiKey physicalModifier, bool expected,
           ImGuiKey extraModifier = ImGuiKey_None) {
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.AddKeyEvent(physicalModifier, true);
    if (extraModifier != ImGuiKey_None) io.AddKeyEvent(extraModifier, true);
    io.AddKeyEvent(key, true);
    Frame();
    Require(gui::IsApplicationShortcutPressed(key) == expected,
            "wrong physical modifier accepted for application shortcut");
    for (const auto other : keys)
        if (other != key)
            Require(!gui::IsApplicationShortcutPressed(other), "wrong action triggered");
    ImGui::EndFrame();
    Frame();
    Require(!gui::IsApplicationShortcutPressed(key), "held shortcut repeated");
    io.ClearInputKeys(); // Native dialog can consume the physical key-up.
    Require(!io.KeyCtrl && !io.KeySuper && io.KeyMods == ImGuiMod_None,
            "native dialog left stale modifiers");
    ImGui::EndFrame();
}
} // namespace

int main() {
    ImGui::CreateContext();
    int result = 0;
    try {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(640, 480);
        io.IniFilename = nullptr;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        for (const bool mac : {false, true}) {
            io.ConfigMacOSXBehaviors = mac;
            const auto primary = mac ? ImGuiMod_Super : ImGuiMod_Ctrl;
            const auto wrong = mac ? ImGuiMod_Ctrl : ImGuiMod_Super;
            for (const auto key : keys) {
                Press(key, static_cast<ImGuiKey>(primary), true);
                Press(key, static_cast<ImGuiKey>(wrong), false);
                Press(key, static_cast<ImGuiKey>(primary), false, ImGuiMod_Shift);
                Press(key, static_cast<ImGuiKey>(primary), false, static_cast<ImGuiKey>(wrong));
                Press(key, static_cast<ImGuiKey>(primary), true); // Works again after dialog cleanup.
            }
        }
        std::cout << "PASS all five application shortcuts: Command on macOS, Control elsewhere, exact modifiers, no repeats, dialog cleanup\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    ImGui::DestroyContext();
    return result;
}
