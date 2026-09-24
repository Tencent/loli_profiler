// LoliProfilerImGui - Dear ImGui + SFML application shell.
// This is the entry point for the experimental ImGui-based GUI.
// Qt5::Core is used for QSettings (theme persistence) and QString/QFile via
// GuiDataBridge; no Qt Widgets/Gui.

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui-SFML.h"

#include "chartwidgets.h"
#include "themes.h"
#include "filedialogs.h"
#include "guidatabridge.h"
#include "guisnapshot.h"
#include "runlaunchdialog.h"
#include "stacktracetree.h"
#include "treemappanel.h"

#include <QSettings>
#include <QString>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

// System DPI scale factor (1.0 = 96 DPI). On Windows the process is not
// auto-scaled by the OS for ImGui content, so we scale the UI explicitly.
// On macOS/Linux the OS already handles Retina scaling; return 1.0 there.
float GetSystemDpiScale() {
#ifdef _WIN32
    return static_cast<float>(GetDpiForSystem()) / 96.0f;
#else
    return 1.0f;
#endif
}


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

// Builds a sensible default dock layout on first run so panels aren't stacked:
//   left: Capture Status / Console (bottom)
//   center: Stacktrace (top) + Timeline (bottom)
//   right: Treemap / Smaps / Screenshot (tabbed)
void BuildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID dockMain = dockspaceId;
    // left column
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
    // right column
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.32f, nullptr, &dockMain);
    // center split: stacktrace (top) / timeline (bottom)
    ImGuiID dockCenterBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.40f, nullptr, &dockMain);
    ImGuiID dockCenterTop = dockMain;
    // left split: status (top) / console (bottom)
    ImGuiID dockLeftBottom = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.55f, nullptr, &dockLeft);

    ImGui::DockBuilderDockWindow("Capture Status", dockLeft);
    ImGui::DockBuilderDockWindow("Console", dockLeftBottom);
    ImGui::DockBuilderDockWindow("Stacktrace", dockCenterTop);
    ImGui::DockBuilderDockWindow("Timeline", dockCenterBottom);
    ImGui::DockBuilderDockWindow("Treemap", dockRight);
    ImGui::DockBuilderDockWindow("Smaps", dockRight);
    ImGui::DockBuilderDockWindow("Screenshot", dockRight);
    ImGui::DockBuilderFinish(dockspaceId);
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

void DrawStacktracePanel(const gui::GuiSnapshot& snapshot, gui::StacktraceTree& tree,
                         size_t& builtRecordCount, char* filterBuf, size_t filterBufSize) {
    if (snapshot.records.empty()) {
        ImGui::TextUnformatted("No allocation records loaded.");
        return;
    }

    // Rebuild the aggregated tree only when the record set grows (new data).
    if (snapshot.records.size() != builtRecordCount) {
        tree.Rebuild(snapshot.records, snapshot.recordFrames);
        builtRecordCount = snapshot.records.size();
    }

    // Toolbar: filter + expand/collapse + stats.
    if (ImGui::InputTextWithHint("##treefilter", "filter function/library...",
                                 filterBuf, filterBufSize)) {
        tree.SetFilter(filterBuf);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Expand All"))   tree.ExpandAll();
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse All")) tree.CollapseAll();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu nodes)", tree.Nodes().size());

    // Tree table: flat visible rows rendered via clipper for large datasets.
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("stacktrace_tree", 3, flags)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    const auto& rows = tree.VisibleRows();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const auto& row = rows[r];
            const auto& node = tree.NodeAt(row.nodeIndex);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Indent + expander. Use Selectable spanning for row interaction.
            ImGui::PushID(row.nodeIndex);
            if (row.hasChildren) {
                const char* arrow = row.expanded ? "[-]" : "[+]";
                ImGui::TextUnformatted(arrow);
                if (ImGui::IsItemClicked())
                    tree.SetExpanded(row.nodeIndex, !row.expanded);
                ImGui::SameLine();
            } else {
                ImGui::TextUnformatted(" ");
                ImGui::SameLine();
            }
            // indent by depth
            for (int d = 0; d < row.depth; ++d) {
                ImGui::Indent(14.0f);
            }
            ImGui::TextUnformatted(node.funcName.c_str());
            if (ImGui::IsItemHovered() && !node.library.empty())
                ImGui::SetTooltip("%s", node.library.c_str());
            for (int d = 0; d < row.depth; ++d) {
                ImGui::Unindent(14.0f);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(node.totalSize).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", node.allocCount);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
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

int main(int argc, char** argv) {
    // Optional: path to a .loli record to open on startup (for quick inspection).
    const char* openRecordArg = nullptr;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if ((std::strcmp(a, "--open") == 0 || std::strcmp(a, "-o") == 0) && i + 1 < argc) {
            openRecordArg = argv[++i];
        } else if (std::strstr(a, ".loli") != nullptr) {
            openRecordArg = a;  // bare path ending in .loli
        }
    }

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

    // DPI scaling (Fury3D pattern): scale widget sizes AND font density by the
    // system DPI so the UI isn't tiny on HiDPI displays. ScaleAllSizes is
    // applied after the theme so the theme's base sizes get scaled too.
    const float dpiScale = GetSystemDpiScale();
    if (dpiScale != 1.0f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FontScaleMain = dpiScale;
        style.ScaleAllSizes(dpiScale);
    }

    // Core data bridge (owns ADB/stacktrace/screenshot processes; Qt signals
    // are pumped by the Qt event loop integration used elsewhere).
    gui::GuiDataBridge bridge;
    gui::GuiSnapshot snapshot;
    gui::RunLaunchDialog runLaunchDialog;

    std::string loadedRecordName;
    sf::Texture screenshotTexture;
    size_t uploadedScreenshotCount = 0;
    size_t lastConsoleLineCount = 0;
    gui::StacktraceTree stacktraceTree;
    size_t stacktraceBuiltRecords = 0;
    char stacktraceFilter[256] = {0};
    gui::TimelineView timelineView;
    gui::TreemapState treemapState;
    bool firstFrame = true;

    // Auto-open a record passed on the command line.
    std::string pendingOpen;
    if (openRecordArg)
        pendingOpen = openRecordArg;

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

        // Root dockspace over the whole viewport. On the first frame we lay out
        // a sensible default arrangement so panels aren't all stacked.
        const ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (firstFrame) {
            BuildDefaultDockLayout(dockspaceId);
            firstFrame = false;
        }

        // Deferred auto-open: load the record after the first frame so the
        // bridge and dock layout are fully initialized.
        if (!pendingOpen.empty()) {
            if (bridge.LoadRecord(QString::fromStdString(pendingOpen))) {
                loadedRecordName = pendingOpen;
                window.setTitle("LoliProfiler - " + loadedRecordName);
            }
            pendingOpen.clear();
        }

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
            DrawStacktracePanel(snapshot, stacktraceTree, stacktraceBuiltRecords,
                                stacktraceFilter, sizeof(stacktraceFilter));
        }
        ImGui::End();

        if (ImGui::Begin("Timeline")) {
            gui::DrawMemoryTimelineChart(snapshot, timelineView);
        }
        ImGui::End();

        if (ImGui::Begin("Treemap")) {
            gui::DrawTreemapPanel(stacktraceTree, treemapState);
        }
        ImGui::End();

        if (ImGui::Begin("Smaps")) {
            gui::DrawSmapsPanel(snapshot);
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
