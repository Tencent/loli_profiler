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
#include <QCoreApplication>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <cstdio>
#include <cstring>
#include <chrono>
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

// Console log cache: InputTextMultiline holds a FIXED-size buffer, so the log
// must be capped. We keep only the tail of the log (both a byte and a line
// cap) so the widget never slows down on huge captures.
constexpr size_t kConsoleMaxBufferBytes = 64 * 1024;
constexpr size_t kConsoleMaxLines = 4000;

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
    // left column (~20% of total width)
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
    // right column (~32% of total width => 0.32 / 0.80 of what remains)
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.40f, nullptr, &dockMain);
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

// The tree is rebuilt OUTSIDE this function (version-gated on SnapshotVersion)
// so it never blocks the frame loop more than necessary.
void DrawStacktracePanel(const gui::GuiSnapshot& snapshot, gui::StacktraceTree& tree,
                         char* filterBuf, size_t filterBufSize) {
    if (snapshot.records.empty()) {
        ImGui::TextUnformatted("No allocation records loaded.");
        return;
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

            ImGui::PushID(row.nodeIndex);

            // --- Tree indentation geometry (used by the expander arrow and the
            // vertical indent guide lines so the hierarchy reads as a real tree).
            ImGuiWindow* drawWindow = ImGui::GetCurrentWindow();
            const float lineHeight = ImGui::GetTextLineHeight();
            const float stepX = ImGui::GetTreeNodeToLabelSpacing();  // ~indent per depth
            const float baseX = ImGui::GetCursorPosX();              // arrow column
            const float labelX = baseX + (float)(row.depth + 1) * stepX;

            // --- Expander arrow: an ArrowButton on the arrow column. Clicking
            // it toggles the node; a transparent label Selectable below covers
            // the whole row and ALSO toggles when the row has children.
            ImGui::SetCursorPosX(baseX + (float)row.depth * stepX);
            if (row.hasChildren) {
                if (ImGui::ArrowButtonEx("##arrow", row.expanded ? ImGuiDir_Down : ImGuiDir_Right,
                                         ImVec2(lineHeight, lineHeight), ImGuiButtonFlags_None)) {
                    tree.SetExpanded(row.nodeIndex, !row.expanded);
                }
                if (ImGui::IsItemHovered() && node.library >= 0)
                    ImGui::SetTooltip("%s", tree.PoolStr(node.library));
            }

            // --- Row label Selectable. Disabled (and SkipItem so it doesn't
            // draw/cost anything) when the row has no children.
            if (!row.hasChildren)
                ImGui::BeginDisabled();
            ImGui::SetCursorPosX(labelX);
            const bool selected = row.expanded;  // highlight expanded rows, like TreeNode
            ImGui::Selectable(tree.PoolStr(node.funcName), selected,
                              ImGuiSelectableFlags_SpanAllColumns);
            if (!row.hasChildren)
                ImGui::EndDisabled();
            if (ImGui::IsItemHovered() && node.library >= 0)
                ImGui::SetTooltip("%s", tree.PoolStr(node.library));
            if (row.hasChildren && ImGui::IsItemClicked())
                tree.SetExpanded(row.nodeIndex, !row.expanded);

            // --- Indent guide lines: one subtle vertical line per depth level,
            // matching where the child arrows sit, like a classic tree control.
            if (row.depth > 0) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_Text, 0.18f);
                const float x = drawWindow->Pos.x + baseX + (float)row.depth * stepX +
                                lineHeight * 0.5f;
                drawList->AddLine(ImVec2(x, drawWindow->DC.CursorPos.y),
                                  ImVec2(x, drawWindow->DC.CursorPos.y + lineHeight), lineCol);
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

// Renders the console as a single read-only multiline text field, so the log
// is selectable and copyable (Ctrl+C). The joined buffer is cached in
// `consoleBuf` and only rebuilt (from the tail of the log) when the line count
// changes; `lastLineCount` tracks that. New lines auto-scroll to the bottom
// until the user scrolls up.
void DrawConsolePanel(const gui::GuiSnapshot& snapshot, size_t& lastLineCount,
                      std::string& consoleBuf) {
    const size_t lineCount = snapshot.logLines.size();
    const bool autoScroll = lineCount != lastLineCount;
    lastLineCount = lineCount;

    if (lineCount == 0) {
        consoleBuf.clear();
        ImGui::TextUnformatted("(no log output)");
        return;
    }

    if (consoleBuf.empty()) {
        // Cap: InputTextMultiline holds a FIXED-size buffer, so keep only the
        // tail of the log (kConsoleMaxBufferBytes / kConsoleMaxLines) — the
        // widget slows down proportionally to the buffer size, and unbounded
        // growth would eventually blow past the fixed buffer.
        const size_t startLine =
            lineCount > kConsoleMaxLines ? lineCount - kConsoleMaxLines : 0;
        consoleBuf.reserve(std::min(kConsoleMaxBufferBytes + 64, (size_t)4096));
        for (size_t i = startLine; i < lineCount; ++i) {
            consoleBuf += snapshot.logLines[i];
            consoleBuf += '\n';
        }
        if (consoleBuf.size() > kConsoleMaxBufferBytes) {
            consoleBuf.erase(0, consoleBuf.size() - kConsoleMaxBufferBytes);
            consoleBuf.erase(0, consoleBuf.find('\n') + 1);  // drop partial first line
        }
    }

    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    // ReadOnly + NoUndoRedo gives selection + Ctrl+C copy for free. CallbackAlways
    // lets us force the caret (and thus the scroll) to the end on the frames new
    // lines arrive, without the widget needing keyboard focus.
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly |
                                ImGuiInputTextFlags_NoUndoRedo |
                                ImGuiInputTextFlags_CallbackAlways;

    bool scrollToBottom = autoScroll;  // sticky: user can scroll up to detach
    auto onEdit = [](ImGuiInputTextCallbackData* data) -> int {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways &&
            *static_cast<bool*>(data->UserData)) {
            data->SelectionStart = data->SelectionEnd = data->CursorPos = data->BufTextLen;
        }
        return 0;
    };

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##console", const_cast<char*>(consoleBuf.c_str()),
                              consoleBuf.size() + 1, ImVec2(0, -footer), flags, onEdit,
                              &scrollToBottom);

    ImGui::Separator();
    ImGui::Text("%zu lines", lineCount);
}

} // namespace

int main(int argc, char** argv) {
    // Optional: path to a .loli record to open on startup (for quick inspection).
    const char* openRecordArg = nullptr;
    // Headless perf harness: render a bounded number of frames and auto-exit so
    // we can measure load/interactivity from scripts without manual clicking.
    bool selfTest = false;
    // Screenshot harness: after the selftest frame budget, dump the window to a
    // PNG so we can visually verify rendering from scripts.
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--selftest") == 0) {
            selfTest = true;
        } else if (std::strcmp(a, "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
            selfTest = true;  // screenshot implies selftest (bounded frames + exit)
        } else if ((std::strcmp(a, "--open") == 0 || std::strcmp(a, "-o") == 0) && i + 1 < argc) {
            openRecordArg = argv[++i];
        } else if (std::strstr(a, ".loli") != nullptr) {
            openRecordArg = a;  // bare path ending in .loli
        }
    }

    // Qt application object. The core profiling processes (QProcess/QTcpSocket/
    // QTimer) and the async record loader deliver results via Qt signals, which
    // require a QCoreApplication event loop. We create it here and pump it each
    // frame (processEvents) instead of exec(), so the SFML/ImGui loop stays in
    // control of rendering.
    QCoreApplication qtApp(argc, argv);

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
        style.ScaleAllSizes(dpiScale);

        // Rasterize the font at the scaled pixel size instead of scaling the
        // small default atlas (which makes text blocky on HiDPI).
        ImFontAtlas* fonts = io.Fonts;
        fonts->Clear();
        ImFont* font = fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",
                                                 13.0f * dpiScale);
        if (!font)
            font = fonts->AddFontDefault();
        io.FontDefault = font;
        ImGui::SFML::UpdateFontTexture();
    }

    // Core data bridge (owns ADB/stacktrace/screenshot processes; Qt signals
    // are pumped by the Qt event loop integration used elsewhere).
    gui::GuiDataBridge bridge;
    gui::RunLaunchDialog runLaunchDialog;

    std::string loadedRecordName;
    sf::Texture screenshotTexture;
    size_t uploadedScreenshotCount = 0;
    size_t lastConsoleLineCount = 0;
    std::string consoleBuf;  // cached joined log text for DrawConsolePanel
    gui::StacktraceTree stacktraceTree;
    uint64_t stacktraceBuiltVersion = 0;  // bridge.SnapshotVersion() the tree was built from
    char stacktraceFilter[256] = {0};
    gui::TimelineView timelineView;
    gui::TreemapState treemapState;
    // Dockable panel visibility, toggled from the Window menu.
    bool showCaptureStatus = true;
    bool showStacktrace = true;
    bool showTimeline = true;
    bool showTreemap = true;
    bool showSmaps = true;
    bool showScreenshot = true;
    bool showConsole = true;
    bool firstFrame = true;
    int framesRendered = 0;
    int loadingFrames = 0;
    double lastTreeBuildMs = 0.0;
    double worstFrameMs = 0.0;
    bool wasLoading = false;
    int framesAfterLoad = 0;

    // Adopt the worker-built aggregated tree when the snapshot version changes.
    // The tree arrives pre-built (interned strings) inside the snapshot, so this
    // is a cheap move — no UI-thread rebuild.
    auto rebuildTreeIfNeeded = [&]() {
        const uint64_t v = bridge.SnapshotVersion();
        if (v != stacktraceBuiltVersion) {
            const gui::GuiSnapshot& snap = *bridge.AcquireSnapshot();
            const auto t0 = std::chrono::steady_clock::now();
            if (snap.stackTree)
                stacktraceTree.Adopt(std::move(*snap.stackTree));
            else
                stacktraceTree.Clear();
            const auto t1 = std::chrono::steady_clock::now();
            lastTreeBuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            stacktraceBuiltVersion = v;
        }
    };

    // Auto-open a record passed on the command line.
    std::string pendingOpen;
    if (openRecordArg)
        pendingOpen = openRecordArg;

    sf::Clock deltaClock;
    sf::Clock frameClock;
    while (window.isOpen()) {
        frameClock.restart();

        // Pump the Qt event loop so core-process and async-loader signals fire.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 8);

        while (const auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        // Use the latest published bridge snapshot for this frame (immutable,
        // safe to hold across the frame).
        std::shared_ptr<const gui::GuiSnapshot> frameSnapshot = bridge.AcquireSnapshot();
        static const std::shared_ptr<const gui::GuiSnapshot> emptySnapshot(
            new gui::GuiSnapshot());
        if (!frameSnapshot)
            frameSnapshot = emptySnapshot;
        const gui::GuiSnapshot& snapshot = *frameSnapshot;

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
            bridge.LoadRecord(QString::fromStdString(pendingOpen));
            loadedRecordName = pendingOpen;
            window.setTitle("LoliProfiler - " + loadedRecordName);
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
                        bridge.LoadRecord(QString::fromStdString(*path));
                        loadedRecordName = *path;
                        window.setTitle("LoliProfiler - " + loadedRecordName);
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
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Capture Status", nullptr, &showCaptureStatus);
                ImGui::MenuItem("Stacktrace", nullptr, &showStacktrace);
                ImGui::MenuItem("Timeline", nullptr, &showTimeline);
                ImGui::MenuItem("Treemap", nullptr, &showTreemap);
                ImGui::MenuItem("Smaps", nullptr, &showSmaps);
                ImGui::MenuItem("Screenshot", nullptr, &showScreenshot);
                ImGui::MenuItem("Console", nullptr, &showConsole);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Fixed toolbar strip at the top, directly under the menubar and above
        // the dockspace. Attached to the main viewport; it does not dock/float.
        // Height = button height + vertical padding so buttons aren't clipped.
        const float toolbarHeight = ImGui::GetFrameHeightWithSpacing() +
                                    ImGui::GetStyle().FramePadding.y * 2.0f +
                                    ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginViewportSideBar("##MainToolBar", ImGui::GetMainViewport(),
                                        ImGuiDir_Up, toolbarHeight,
                                        ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoSavedSettings)) {
            // Center the button group in the strip: vertical centering plus
            // horizontal centering of the combined button width.
            ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
            const float runLaunchW = ImGui::CalcTextSize("Run/Launch").x +
                                     ImGui::GetStyle().FramePadding.x * 2.0f;
            const float stopCaptureW = ImGui::CalcTextSize("Stop Capture").x +
                                       ImGui::GetStyle().FramePadding.x * 2.0f;
            const float buttonsWidth = runLaunchW + stopCaptureW +
                                       ImGui::GetStyle().ItemSpacing.x;
            const float toolbarWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((toolbarWidth - buttonsWidth) * 0.5f);
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
        if (showCaptureStatus && ImGui::Begin("Capture Status")) {
            DrawCaptureStatusPanel(snapshot);
        }
        ImGui::End();

        if (showStacktrace && ImGui::Begin("Stacktrace")) {
            rebuildTreeIfNeeded();
            DrawStacktracePanel(snapshot, stacktraceTree,
                                stacktraceFilter, sizeof(stacktraceFilter));
        }
        ImGui::End();

        if (showTimeline && ImGui::Begin("Timeline")) {
            gui::DrawMemoryTimelineChart(snapshot, timelineView);
        }
        ImGui::End();

        if (showTreemap && ImGui::Begin("Treemap")) {
            rebuildTreeIfNeeded();
            gui::DrawTreemapPanel(stacktraceTree, treemapState, bridge.SnapshotVersion(), dpiScale);
        }
        ImGui::End();

        if (showSmaps && ImGui::Begin("Smaps")) {
            gui::DrawSmapsPanel(snapshot);
        }
        ImGui::End();

        if (showScreenshot && ImGui::Begin("Screenshot")) {
            DrawScreenshotPanel(snapshot, screenshotTexture, uploadedScreenshotCount);
        }
        ImGui::End();

        if (showConsole && ImGui::Begin("Console")) {
            DrawConsolePanel(snapshot, lastConsoleLineCount, consoleBuf);
        }
        ImGui::End();

        // Loading overlay: modal progress while a record parses on the worker
        // thread, so the UI never appears frozen. We open it once on the loading
        // rising edge and close it once on the falling edge — calling OpenPopup
        // every frame would keep it open forever after loading finishes.
        const bool isLoading = bridge.IsLoading();
        if (isLoading && !wasLoading)
            ImGui::OpenPopup("Loading Record");
        wasLoading = isLoading;

        if (ImGui::BeginPopupModal("Loading Record", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
            if (!isLoading) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            } else {
                ImGui::TextUnformatted("Loading record, please wait...");
                const float progress = bridge.LoadProgress();
                ImGui::ProgressBar(progress, ImVec2(360, 0));
                ImGui::Text("%.0f%%", progress * 100.0f);
                ImGui::EndPopup();
            }
        }

        // Headless self-test bookkeeping (before display so we can capture the
        // final frame reliably from the back buffer).
        bool captureScreenshotNow = false;
        if (selfTest) {
            framesRendered++;
            if (bridge.IsLoading())
                loadingFrames++;
            // Count frames rendered AFTER loading finishes, so the screenshot /
            // summary reflects the fully-populated UI, not the loading state.
            if (!bridge.IsLoading())
                framesAfterLoad++;
            // Exit once we've rendered a healthy number of frames past load.
            if (!bridge.IsLoading() && framesAfterLoad > 180) {
                std::printf("[selftest] frames=%d loadingFrames=%d records=%zu treeNodes=%zu treeBuildMs=%.1f worstFrameMs=%.2f\n",
                            framesRendered, loadingFrames, snapshot.records.size(),
                            stacktraceTree.Nodes().size(), lastTreeBuildMs, worstFrameMs);
                std::fflush(stdout);
                captureScreenshotNow = !screenshotPath.empty();
            }
        }

        window.clear();
        ImGui::SFML::Render(window);
        window.display();

        const double frameMs = frameClock.getElapsedTime().asSeconds() * 1000.0;
        if (selfTest && frameMs > worstFrameMs)
            worstFrameMs = frameMs;

        // Capture the just-presented frame from the front buffer, then exit.
        if (captureScreenshotNow) {
            sf::Texture tex;
            if (tex.resize(window.getSize())) {
                tex.update(window);
                if (tex.copyToImage().saveToFile(screenshotPath))
                    std::printf("[selftest] screenshot saved: %s\n", screenshotPath.c_str());
                else
                    std::fprintf(stderr, "[selftest] FAILED to save screenshot %s\n", screenshotPath.c_str());
            }
            window.close();
        }
        // Safety cap: never hang the harness.
        if (selfTest && framesRendered > 20000)
            window.close();
    }

    FileDialogs::Shutdown();
    gui::FreeTreemapState(treemapState);
    ImGui::SFML::Shutdown();
    return 0;
}
