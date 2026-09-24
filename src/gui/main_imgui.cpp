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
#include "pathutils.h"

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
#include <cfloat>
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

// Clipboard text for the Stacktrace panel's "Copy" context-menu item:
// the node's function name plus its aggregated size/count (and library).
std::string FormatCallstackNode(const gui::StacktraceTree::Node& node,
                                const gui::StacktraceTree& tree) {
    std::string out = tree.PoolStr(node.funcName);
    out += " (size: ";
    out += FormatBytes(node.totalSize);
    out += ", count: ";
    char count[32];
    std::snprintf(count, sizeof(count), "%u", node.allocCount);
    out += count;
    out += ")";
    if (node.library >= 0) {
        const char* lib = tree.PoolStr(node.library);
        if (lib[0] != '\0') {
            out += " [";
            out += lib;
            out += "]";
        }
    }
    return out;
}

// Builds a sensible default dock layout on first run so panels aren't stacked:
//   left: Capture Status / Console (bottom)
//   center: Stacktrace (top) + Timeline (bottom)
//   right: Treemap / Smaps / Screenshot (tabbed)
void BuildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    // Target layout (reversed so the stretch-friendly panels are on top):
    //   TOP    : Stacktrace (left) + Treemap/Smaps tabbed (right)  [stretch]
    //   BOTTOM : Timeline (left, wide) + Screenshot (right, its own unit)
    ImGuiID dockMain = dockspaceId;
    // Top band (Stacktrace + Treemap/Smaps), full width.
    ImGuiID dockTop = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.62f, nullptr, &dockMain);
    // Bottom band: split Screenshot off to the right (its own unit).
    ImGuiID dockBottomRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.20f, nullptr, &dockMain);
    ImGuiID dockBottomLeft = dockMain;  // Timeline
    // Top band split: Stacktrace (left) / Treemap+Smaps (right tabbed).
    ImGuiID dockTopRight = ImGui::DockBuilderSplitNode(dockTop, ImGuiDir_Right, 0.42f, nullptr, &dockTop);
    ImGuiID dockTopLeft = dockTop;  // Stacktrace

    ImGui::DockBuilderDockWindow("Stacktrace", dockTopLeft);
    ImGui::DockBuilderDockWindow("Treemap", dockTopRight);
    ImGui::DockBuilderDockWindow("Smaps", dockTopRight);
    ImGui::DockBuilderDockWindow("Timeline", dockBottomLeft);
    ImGui::DockBuilderDockWindow("Screenshot", dockBottomRight);
    ImGui::DockBuilderFinish(dockspaceId);
}

// ---------------------------------------------------------------------------
// Panel renderers (each is called between ImGui::Begin/End by the caller)
// ---------------------------------------------------------------------------

// The tree is rebuilt OUTSIDE this function (version-gated on SnapshotVersion)
// so it never blocks the frame loop more than necessary.
// `contextNode` persists the nodeIndex of the right-clicked row across frames
// (rows are transient under the clipper, so the popup acts on this stored id).
void DrawStacktracePanel(const gui::GuiSnapshot& snapshot, gui::StacktraceTree& tree,
                         char* filterBuf, size_t filterBufSize, int32_t& contextNode) {
    if (snapshot.records.empty()) {
        ImGui::TextUnformatted("No allocation records loaded.");
        return;
    }

    // --- Item 1: single vertical scrollbar.
    // The table gets ScrollY/ScrollX and scrolls internally. We reserve the
    // bottom filter bar's height up front by giving the table an explicit outer
    // size of (avail - filterBarH); the filter then sits at the bottom with no
    // overflow, so the host window never grows a second scrollbar.
    const float filterBarH = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 tableSize(-FLT_MIN, ImMax(1.0f, ImGui::GetContentRegionAvail().y - filterBarH));

    // --- Item 2a: auto-size the Function column to fit the widest visible row.
    // Track the max content width (depth*indent + arrow + name) across ALL rows
    // and feed it as the Function column's stretch weight, so the Function column
    // is at least as wide as the deepest/longest visible name. Overflow is
    // reachable via the horizontal scrollbar. The max name width is cached per
    // node so the full O(N) scan only runs when expansion/filter/sort changes.
    const auto& rows = tree.VisibleRows();
    static ImGuiID lastVisibleSignature = 0;
    static float maxNameW = 0.0f;
    const float stepX = ImGui::GetTreeNodeToLabelSpacing();
    const float cellPadX = ImGui::GetStyle().CellPadding.x;
    const float frameH = ImGui::GetFrameHeight();
    const ImGuiID visibleSignature = ImHashStr("stacktrace_tree", 0, (ImGuiID)(intptr_t)&tree);
    if (visibleSignature != lastVisibleSignature) {
        maxNameW = 0.0f;
        for (size_t r = 0; r < rows.size(); ++r) {
            const auto& row = rows[r];
            const auto& node = tree.NodeAt(row.nodeIndex);
            const float nameW = ImGui::CalcTextSize(tree.PoolStr(node.funcName)).x;
            const float contentW = stepX + (float)row.depth * stepX +
                                   ImGui::GetStyle().ItemInnerSpacing.x + nameW;
            if (contentW > maxNameW)
                maxNameW = contentW;
        }
        lastVisibleSignature = visibleSignature;
    }
    // Function column minimum content width (stretch weight = min pixel width).
    const float funcMinW = maxNameW + cellPadX * 2.0f;

    // Tree table: flat visible rows rendered via clipper for large datasets.
    // Sortable: clicking Size / Count re-sorts each node's children by that key.
    // ScrollX so deeply-nested / long function names stay reachable horizontally.
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;
    if (!ImGui::BeginTable("stacktrace_tree", 4, flags, tableSize)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    // Function: WidthStretch so it ABSORBS any manual resize (the Size/Count/
    // Library columns are WidthFixed and keep their widths). The stretch weight
    // is our computed min content width, so the column is always at least wide
    // enough to show the full deepest/longest visible name.
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch |
                                            ImGuiTableColumnFlags_NoSort |
                                            ImGuiTableColumnFlags_NoHeaderLabel,
                            funcMinW, 0);
    // Default to descending (largest first) for both sortable columns.
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed |
                                        ImGuiTableColumnFlags_PreferSortDescending, 110.0f, 1);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed |
                                         ImGuiTableColumnFlags_PreferSortDescending, 80.0f, 2);
    // Library: trailing WidthFixed so the Function/Size separator drag widens
    // only Function (the fixed columns keep their widths; the stretch column
    // takes/absorbs the delta).
    ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthFixed |
                                           ImGuiTableColumnFlags_NoSort, 0.0f, 3);
    ImGui::TableHeadersRow();

    // --- Item 2a (cont.): keep the Function column wide enough for the content.
    // The user may drag the Function/Size separator; only enlarge (never shrink)
    // toward the measured content width so a manual narrow choice isn't yanked
    // back while it is active. We feed the request through the queued-resize
    // fields, which BeginTable applies before layout locks, so this never fights
    // a live resize interaction.
    if (ImGuiTable* table = ImGui::GetCurrentTable()) {
        const ImGuiTableColumn& funcCol = table->Columns[0];
        if (!(table->ResizedColumn == 0 && ImGui::IsMouseDragging(0))) {
            const float curW = funcCol.WidthRequest;
            if (funcMinW > curW + 0.5f) {
                table->ResizedColumn = 0;
                table->ResizedColumnNextWidth = funcMinW;
            }
        }
    }

    // Consume sort specs: Size (default, desc) and Count are sortable; the
    // Function column is marked NoSort so clicks there are ignored. On the very
    // first frame we force the tree's default (Size, descending) so a stale
    // persisted imgui.ini spec can't leave the view sorted ascending.
    static bool sortInitialized = false;
    if (!sortInitialized) {
        tree.SetSort(0, /*descending=*/true);
        sortInitialized = true;
    }
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty) {
            int column = tree.SortColumn();
            bool descending = tree.SortDescending();
            for (int n = 0; n < specs->SpecsCount; ++n) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[n];
                if (s.ColumnUserID == 1 || s.ColumnIndex == 1) {
                    column = 0;  // Size
                    descending = s.SortDirection == ImGuiSortDirection_Descending;
                } else if (s.ColumnUserID == 2 || s.ColumnIndex == 2) {
                    column = 1;  // Count
                    descending = s.SortDirection == ImGuiSortDirection_Descending;
                }
            }
            if (column != tree.SortColumn() || descending != tree.SortDescending())
                tree.SetSort(column, descending);
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const auto& row = rows[r];
            const auto& node = tree.NodeAt(row.nodeIndex);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushID(row.nodeIndex);

            // --- Single-line tree row: [indent guides][arrow-or-space][name].
            // We lay out explicitly so the arrow and name share one line and
            // leaves align under the parent label (same X as if they had an arrow).
            ImGuiWindow* drawWindow = ImGui::GetCurrentWindow();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float lineHeight = ImGui::GetFrameHeight();  // matches ArrowButton
            const float stepX = ImGui::GetTreeNodeToLabelSpacing();
            const float baseX = ImGui::GetCursorPosX();
            const float rowScreenY = ImGui::GetCursorScreenPos().y;

            // Indent guide lines: one faint vertical line per ancestor depth, at the
            // X where that ancestor's arrow sits. Drawn BEFORE the row widgets so
            // they sit behind the text (no line crossing through the arrow).
            const ImU32 guideCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
            for (int d = 1; d <= row.depth; ++d) {
                const float gx = baseX + (float)(d - 1) * stepX + lineHeight * 0.5f;
                const float sx = drawWindow->Pos.x + gx;
                drawList->AddLine(ImVec2(sx, rowScreenY), ImVec2(sx, rowScreenY + lineHeight),
                                  guideCol, 1.0f);
            }

            // Arrow column (always occupies lineHeight width so leaves align).
            const float arrowX = baseX + (float)row.depth * stepX;
            ImGui::SetCursorPosX(arrowX);
            if (row.hasChildren) {
                if (ImGui::ArrowButtonEx("##arrow", row.expanded ? ImGuiDir_Down : ImGuiDir_Right,
                                         ImVec2(lineHeight, lineHeight), ImGuiButtonFlags_None)) {
                    tree.SetExpanded(row.nodeIndex, !row.expanded);
                }
            } else {
                // Leaf: reserve the same space so the name aligns with sibling labels.
                ImGui::Dummy(ImVec2(lineHeight, lineHeight));
            }

            // Function name on the SAME line as the arrow.
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            const float nameX = ImGui::GetCursorPosX();
            ImGui::TextUnformatted(tree.PoolStr(node.funcName));

            // Row interaction: invisible button over the whole Function cell text
            // region — left-click toggles expansion for parents, right-click opens
            // the context menu (records the nodeIndex; rows are clipper-transient).
            ImGui::SetCursorScreenPos(ImVec2(drawWindow->Pos.x + nameX, rowScreenY));
            const float rowW = drawWindow->Pos.x + ImGui::GetContentRegionAvail().x +
                               ImGui::GetCursorPosX() - (drawWindow->Pos.x + nameX);
            ImGui::InvisibleButton("##rowhit", ImVec2(std::max(rowW, lineHeight), lineHeight));
            if (row.hasChildren && ImGui::IsItemClicked())
                tree.SetExpanded(row.nodeIndex, !row.expanded);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                contextNode = row.nodeIndex;
                ImGui::OpenPopup("##stacktrace_row_ctx");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(node.totalSize).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", node.allocCount);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tree.PoolStr(node.library));
            ImGui::PopID();
        }
    }

    // Right-click context menu for a tree row (shared; acts on contextNode).
    if (ImGui::BeginPopup("##stacktrace_row_ctx")) {
        if (contextNode >= 0 && contextNode < (int32_t)tree.Nodes().size()) {
            const auto& ctxNode = tree.NodeAt(contextNode);
            const bool hasChildren = !ctxNode.children.empty();
            if (ImGui::MenuItem("Copy", nullptr, false, true)) {
                ImGui::SetClipboardText(
                    FormatCallstackNode(ctxNode, tree).c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Expand Node", nullptr, false, hasChildren))
                tree.SetExpanded(contextNode, true);
            if (ImGui::MenuItem("Collapse Node", nullptr, false, hasChildren))
                tree.SetExpanded(contextNode, false);
        }
        ImGui::EndPopup();
    }

    ImGui::EndTable();

    // Filter bar pinned to the BOTTOM of the panel, full width.
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##treefilter", "filter function/library...",
                                 filterBuf, filterBufSize)) {
        tree.SetFilter(filterBuf);
    }
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
    gui::StacktraceTree stacktraceTree;
    uint64_t stacktraceBuiltVersion = 0;  // bridge.SnapshotVersion() the tree was built from
    char stacktraceFilter[256] = {0};
    int32_t stacktraceContextNode = -1;   // nodeIndex right-clicked in the Stacktrace panel
    gui::TimelineView timelineView;
    gui::TreemapState treemapState;
    // Dockable panel visibility, toggled from the Window menu.
    bool showStacktrace = true;
    bool showTimeline = true;
    bool showTreemap = true;
    bool showSmaps = true;
    bool showScreenshot = true;
    bool showSettingsDialog = false;
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
                if (ImGui::MenuItem("Open Record...", "Ctrl+O")) {
                    if (auto path = FileDialogs::OpenFile({{"Loli Record", "loli"}})) {
                        bridge.LoadRecord(QString::fromStdString(*path));
                        loadedRecordName = *path;
                        window.setTitle("LoliProfiler - " + loadedRecordName);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Settings...", "Ctrl+,")) {
                    showSettingsDialog = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Ctrl+Q")) {
                    window.close();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Stacktrace", nullptr, &showStacktrace);
                ImGui::MenuItem("Timeline", nullptr, &showTimeline);
                ImGui::MenuItem("Treemap", nullptr, &showTreemap);
                ImGui::MenuItem("Smaps", nullptr, &showSmaps);
                ImGui::MenuItem("Screenshot", nullptr, &showScreenshot);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Global shortcuts: Ctrl+O (open record), Ctrl+Q (exit). On macOS the
        // platform "Cmd" is io.KeySuper; treat Ctrl or Super as the modifier.
        {
            ImGuiIO& io = ImGui::GetIO();
            const bool mod = io.KeyCtrl || io.KeySuper;
            if (mod && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                if (auto path = FileDialogs::OpenFile({{"Loli Record", "loli"}})) {
                    bridge.LoadRecord(QString::fromStdString(*path));
                    loadedRecordName = *path;
                    window.setTitle("LoliProfiler - " + loadedRecordName);
                }
            }
            if (mod && ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                window.close();
            }
            if (mod && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) {
                showSettingsDialog = true;
            }
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
            // Center the button group: vertically center the button frame height
            // within the strip, and horizontally center the combined width.
            const float btnH = ImGui::GetFrameHeight();
            const float contentH = ImGui::GetContentRegionAvail().y;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (contentH - btnH) * 0.5f);
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

        // Settings dialog: theme + Android SDK/NDK paths.
        if (showSettingsDialog) {
            ImGui::OpenPopup("Settings");
            showSettingsDialog = false;
        }
        if (ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SeparatorText("Theme");
            int themeCount = 0;
            const ImGuiTheme* themes = GetImGuiThemes(&themeCount);
            if (ImGui::BeginCombo("##theme", currentTheme.c_str())) {
                for (int i = 0; i < themeCount; ++i) {
                    const bool isActive = currentTheme == themes[i].name;
                    if (ImGui::Selectable(themes[i].name, isActive)) {
                        ApplyImGuiThemeByName(themes[i].name);
                        currentTheme = themes[i].name;
                        qtSettings.setValue("theme", QString::fromStdString(currentTheme));
                    }
                    if (isActive) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SeparatorText("Android Paths");
            static char sdkPath[512] = {0};
            static char ndkPath[512] = {0};
            static bool pathsLoaded = false;
            if (!pathsLoaded) {
                std::strncpy(sdkPath, PathUtils::GetSDKPath().toStdString().c_str(), sizeof(sdkPath) - 1);
                std::strncpy(ndkPath, PathUtils::GetNDKPath().toStdString().c_str(), sizeof(ndkPath) - 1);
                pathsLoaded = true;
            }
            ImGui::Text("Android SDK:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-110);
            ImGui::InputText("##sdk", sdkPath, sizeof(sdkPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##sdk")) {
                if (auto p = FileDialogs::PickFolder())
                    std::strncpy(sdkPath, p->c_str(), sizeof(sdkPath) - 1);
            }
            ImGui::Text("Android NDK:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-110);
            ImGui::InputText("##ndk", ndkPath, sizeof(ndkPath) - 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##ndk")) {
                if (auto p = FileDialogs::PickFolder())
                    std::strncpy(ndkPath, p->c_str(), sizeof(ndkPath) - 1);
            }

            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                PathUtils::SetSDKPath(QString::fromStdString(sdkPath));
                PathUtils::SetNDKPath(QString::fromStdString(ndkPath));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Dockable panels
        if (showStacktrace && ImGui::Begin("Stacktrace")) {
            rebuildTreeIfNeeded();
            DrawStacktracePanel(snapshot, stacktraceTree,
                                stacktraceFilter, sizeof(stacktraceFilter),
                                stacktraceContextNode);
        }
        ImGui::End();

        // Timeline + Screenshot live in the bottom band (fixed height by the dock
        // split); Stacktrace + Treemap occupy the top band and stretch on resize.
        if (showTimeline) {
            if (ImGui::Begin("Timeline")) {
                gui::DrawMemoryTimelineChart(snapshot, timelineView);
            }
            ImGui::End();
        }

        if (showTreemap && ImGui::Begin("Treemap")) {
            rebuildTreeIfNeeded();
            gui::DrawTreemapPanel(stacktraceTree, treemapState, bridge.SnapshotVersion(), dpiScale);
        }
        ImGui::End();

        if (showSmaps && ImGui::Begin("Smaps")) {
            gui::DrawSmapsPanel(snapshot);
        }
        ImGui::End();

        if (showScreenshot) {
            if (ImGui::Begin("Screenshot")) {
                DrawScreenshotPanel(snapshot, screenshotTexture, uploadedScreenshotCount);
            }
            ImGui::End();
        }

        // Loading overlay: modal progress while a record parses on the worker
        // thread, so the UI never appears frozen. We open it once on the loading
        // rising edge and close it once on the falling edge — calling OpenPopup
        // every frame would keep it open forever after loading finishes.
        const bool isLoading = bridge.IsLoading();
        if (isLoading && !wasLoading) {
            // Center the modal in the main viewport before opening it.
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::OpenPopup("Loading Record");
        }
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
