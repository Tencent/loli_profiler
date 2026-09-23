// LoliProfilerImGui - Dear ImGui + SFML application shell.
// This is the entry point for the experimental ImGui-based GUI.
// Qt5::Core is used for QSettings (theme persistence) and QString/QFile via
// GuiDataBridge; no Qt Widgets/Gui.

#include "imgui.h"
#include "imgui-SFML.h"

#include "themes.h"
#include "filedialogs.h"
#include "guidatabridge.h"
#include "guisnapshot.h"
#include "runlaunchdialog.h"

#include <QSettings>
#include <QString>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <cstdio>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Formats bytes into a human-readable string (up to "1023.9 KB").
std::string FormatBytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buf;
}

std::string FormatElapsed(int32_t ms) {
    int totalSeconds = ms / 1000;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", totalSeconds / 3600,
                  (totalSeconds / 60) % 60, totalSeconds % 60);
    return buf;
}

// ---------------------------------------------------------------------------
// Panel renderers (each is called between ImGui::Begin/End by the caller)
// ---------------------------------------------------------------------------

void DrawCaptureStatusPanel(const gui::GuiSnapshot& snapshot) {
    const gui::CaptureStateSnapshot& cap = snapshot.capture;
    ImGui::Text("Status:   %s", cap.capturing ? "CAPTURING" : (cap.connected ? "Connected" : "Idle"));
    ImGui::Text("App:      %s", cap.appName.empty() ? "-" : cap.appName.c_str());
    ImGui::Text("Device:   %s", cap.deviceSerial.empty() ? "-" : cap.deviceSerial.c_str());
    ImGui::Text("Records:  %llu", static_cast<unsigned long long>(cap.recordCount));
    ImGui::Text("Elapsed:  %s", FormatElapsed(cap.elapsedMs).c_str());
}

void DrawStacktracePanel(const gui::GuiSnapshot& snapshot) {
    if (snapshot.records.empty()) {
        ImGui::TextUnformatted("No allocation records loaded.");
        return;
    }

    if (!ImGui::BeginTable("stacktrace_table", 5,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Seq", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(snapshot.records.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const gui::RecordSnapshot& rec = snapshot.records[row];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", rec.seq);
            ImGui::TableNextColumn();
            ImGui::Text("%d ms", rec.timeMs);
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(static_cast<uint64_t>(rec.size)).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("0x%llX", static_cast<unsigned long long>(rec.addr));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(rec.library.empty() ? "-" : rec.library.c_str());
        }
    }
    ImGui::EndTable();
}

void DrawTimelinePanel(const gui::GuiSnapshot& snapshot) {
    if (snapshot.memTimeline.empty()) {
        ImGui::TextUnformatted("No memory timeline data.");
        return;
    }
    ImGui::Text("Samples: %zu", snapshot.memTimeline.size());
    ImGui::Text("Latest total: %s",
                FormatBytes(snapshot.memTimeline.back().total).c_str());
}

void DrawTreemapPanel(const gui::GuiSnapshot& snapshot) {
    ImGui::Text("Call tree nodes: %zu", snapshot.callTree.size());
    ImGui::TextUnformatted("Treemap visualization coming soon.");
}

void DrawSmapsPanel(const gui::GuiSnapshot& snapshot) {
    if (snapshot.smaps.empty()) {
        ImGui::TextUnformatted("No smaps data.");
        return;
    }
    ImGui::Text("Sections: %zu", snapshot.smaps.size());
}

void DrawScreenshotPanel(const gui::GuiSnapshot& snapshot, sf::Texture& texture,
                         size_t& uploadedCount) {
    if (snapshot.screenshots.empty()) {
        ImGui::TextUnformatted("No screenshots captured.");
        uploadedCount = 0;
        return;
    }

    // Re-upload the texture only when a new screenshot appears.
    if (snapshot.screenshots.size() != uploadedCount) {
        const gui::ScreenshotSnapshot& latest = snapshot.screenshots.back();
        if (!latest.jpegBytes.empty() &&
            texture.loadFromMemory(latest.jpegBytes.data(), latest.jpegBytes.size())) {
            uploadedCount = snapshot.screenshots.size();
        } else {
            ImGui::TextUnformatted("Failed to decode latest screenshot.");
            return;
        }
    }

    const sf::Vector2u texSize = texture.getSize();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale =
        (texSize.x > 0 && texSize.y > 0)
            ? std::min(avail.x / static_cast<float>(texSize.x),
                       avail.y / static_cast<float>(texSize.y))
            : 0.0f;
    if (scale > 0.0f) {
        ImGui::Image(texture,
                     ImVec2(static_cast<float>(texSize.x) * scale,
                            static_cast<float>(texSize.y) * scale));
    }
}

void DrawConsolePanel(const gui::GuiSnapshot& snapshot, size_t& lastLineCount) {
    const bool autoScroll = snapshot.logLines.size() != lastLineCount;
    lastLineCount = snapshot.logLines.size();

    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("console_scroll", ImVec2(0, -footer), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(snapshot.logLines.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                ImGui::TextUnformatted(snapshot.logLines[i].c_str());
        }
    }
    const bool stickToBottom = autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    ImGui::EndChild();

    if (autoScroll || stickToBottom)
        ImGui::SetScrollY(ImGui::GetScrollMaxY());

    ImGui::Separator();
    ImGui::Text("%zu lines", snapshot.logLines.size());
}

} // namespace

int main() {
    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;

    sf::RenderWindow window(sf::VideoMode({1280, 720}), "LoliProfiler",
                            sf::Style::Default, sf::State::Windowed, settings);
    window.setVerticalSyncEnabled(true);

    if (!ImGui::SFML::Init(window))
        return -1;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Native file dialogs (NFD).
    FileDialogs::Init();

    // Theme: restore from QSettings, default to the first registered theme.
    QSettings qtSettings("Tencent", "LoliProfilerImGui");
    const std::string defaultTheme = GetDefaultImGuiThemeName();
    std::string currentTheme =
        qtSettings.value("theme", QString::fromStdString(defaultTheme)).toString().toStdString();
    ApplyImGuiThemeByName(currentTheme.c_str());

    // Core data bridge (owns ADB/stacktrace/screenshot processes; Qt signals
    // are pumped by the Qt event loop integration used elsewhere).
    gui::GuiDataBridge bridge;
    gui::GuiSnapshot snapshot;
    gui::RunLaunchDialog runLaunchDialog;

    std::string loadedRecordName;
    sf::Texture screenshotTexture;
    size_t uploadedScreenshotCount = 0;
    size_t lastConsoleLineCount = 0;

    sf::Clock deltaClock;
    while (window.isOpen()) {
        while (const auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        // Take a consistent copy of the bridge state for this frame.
        bridge.UpdateSnapshot(snapshot);

        // Root dockspace over the whole viewport
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Run/Launch...")) {
                    runLaunchDialog.Open(&bridge);
                }
                if (ImGui::MenuItem("Open Record...")) {
                    if (auto path = FileDialogs::OpenFile({{"Loli Record", "loli"}})) {
                        if (bridge.LoadRecord(QString::fromStdString(*path))) {
                            loadedRecordName = *path;
                            window.setTitle("LoliProfiler - " + loadedRecordName);
                        }
                    }
                }
                if (ImGui::BeginMenu("Themes")) {
                    int themeCount = 0;
                    const ImGuiTheme* themes = GetImGuiThemes(&themeCount);
                    for (int i = 0; i < themeCount; ++i) {
                        const bool isActive = currentTheme == themes[i].name;
                        if (ImGui::MenuItem(themes[i].name, nullptr, isActive)) {
                            ApplyImGuiThemeByName(themes[i].name);
                            currentTheme = themes[i].name;
                            qtSettings.setValue("theme", QString::fromStdString(currentTheme));
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    window.close();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Toolbar: Run/Launch + Stop Capture
        if (ImGui::Begin("Toolbar", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
            if (ImGui::Button("Run/Launch")) {
                runLaunchDialog.Open(&bridge);
            }
            ImGui::SameLine();
            if (!snapshot.capture.capturing)
                ImGui::BeginDisabled();
            if (ImGui::Button("Stop Capture")) {
                bridge.StopCapture();
            }
            if (!snapshot.capture.capturing)
                ImGui::EndDisabled();
        }
        ImGui::End();

        // Modal Run/Launch dialog (no-op unless open).
        runLaunchDialog.Render();

        // Dockable panels
        if (ImGui::Begin("Capture Status")) {
            DrawCaptureStatusPanel(snapshot);
        }
        ImGui::End();

        if (ImGui::Begin("Stacktrace")) {
            DrawStacktracePanel(snapshot);
        }
        ImGui::End();

        if (ImGui::Begin("Timeline")) {
            DrawTimelinePanel(snapshot);
        }
        ImGui::End();

        if (ImGui::Begin("Treemap")) {
            DrawTreemapPanel(snapshot);
        }
        ImGui::End();

        if (ImGui::Begin("Smaps")) {
            DrawSmapsPanel(snapshot);
        }
        ImGui::End();

        if (ImGui::Begin("Screenshot")) {
            DrawScreenshotPanel(snapshot, screenshotTexture, uploadedScreenshotCount);
        }
        ImGui::End();

        if (ImGui::Begin("Console")) {
            DrawConsolePanel(snapshot, lastConsoleLineCount);
        }
        ImGui::End();

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    FileDialogs::Shutdown();
    ImGui::SFML::Shutdown();
    return 0;
}
