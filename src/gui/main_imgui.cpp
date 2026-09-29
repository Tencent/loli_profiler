// LoliProfilerImGui - Dear ImGui + SFML application shell.
// This is the entry point for the experimental ImGui-based GUI.
// Qt-free since the remove-qt change: theme persistence via AppSettings
// (loli_settings.json), capture via the LoliCore-backed GuiDataBridge.

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui-SFML.h"

#include "chartwidgets.h"
#include "themes.h"
#include "filedialogs.h"
#include "guidatabridge.h"
#include "lolilogger.h"
#include "guisnapshot.h"
#include "runlaunchdialog.h"
#include "captureconfigdialog.h"
#include "stacktracetree.h"
#include "treemappanel.h"
#include "pathutilslite.h"
#include "appsettings.h"
#include "processrunner.h"


#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <cstdio>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <future>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

std::filesystem::path GuiExecutablePath() {
#ifdef _WIN32
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    return length ? std::filesystem::path(path) : std::filesystem::path();
#elif defined(__APPLE__)
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, '\0');
    if (_NSGetExecutablePath(path.data(), &length) != 0)
        return {};
    return std::filesystem::weakly_canonical(path.c_str());
#else
    char path[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    return length > 0 ? std::filesystem::path(std::string(path, length))
                      : std::filesystem::path();
#endif
}

std::filesystem::path FindCliExecutable() {
    const auto guiPath = GuiExecutablePath();
    if (guiPath.empty())
        return {};
    const auto dir = guiPath.parent_path();
#ifdef _WIN32
    const char* name = "LoliProfilerCLI.exe";
#else
    const char* name = "LoliProfilerCLI";
#endif
    const auto sibling = dir / name;
    if (std::filesystem::is_regular_file(sibling))
        return sibling;
#ifdef __APPLE__
    // Packaged CLI is next to LoliProfilerImGui.app in the release archive.
    const auto besideBundle = dir.parent_path().parent_path().parent_path() / name;
    if (std::filesystem::is_regular_file(besideBundle))
        return besideBundle;
#endif
    return {};
}

std::filesystem::path SymbolizedOutputPath(const std::filesystem::path& input) {
    const auto dir = input.parent_path();
    const std::string stem = input.stem().string();
    for (unsigned n = 0; ; ++n) {
        const auto candidate = dir / (stem + ".symbolized" +
            (n ? "-" + std::to_string(n + 1) : "") + ".loli");
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
}

struct SymbolizeResult {
    bool ok = false;
    std::string output;
    std::string message;
};

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
    const ImVec2 dockSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

    // Target layout (reversed so the stretch-friendly panels are on top):
    //   TOP    : Stacktrace (left) + Treemap/Smaps tabbed (right)  [stretch]
    //   BOTTOM : Timeline (left, wide) + Screenshot (right, its own unit)
    ImGuiID dockMain = dockspaceId;
    // Give the bottom band a fixed initial height. The opposite (top) node is
    // central, so it receives subsequent viewport height changes.
    const float bottomHeight = std::min(360.0f, dockSize.y * 0.5f);
    ImGuiID dockBottom = 0;
    ImGuiID dockTop = 0;
    ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down,
                                bottomHeight / dockSize.y, &dockBottom, &dockTop);
    // Bottom band: split Screenshot off to the right (its own unit).
    ImGuiID dockBottomRight = ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Right, 0.20f, nullptr, &dockBottom);
    ImGuiID dockBottomLeft = dockBottom;  // Timeline
    // Top band split: Stacktrace (left) / Treemap+Smaps (right tabbed).
    ImGuiID dockTopRight = ImGui::DockBuilderSplitNode(dockTop, ImGuiDir_Right, 0.42f, nullptr, &dockTop);
    ImGuiID dockTopLeft = dockTop;  // Stacktrace

    ImGui::DockBuilderDockWindow("Stacktrace", dockTopLeft);
    ImGui::DockBuilderDockWindow("Treemap", dockTopRight);
    ImGui::DockBuilderDockWindow("Smaps", dockTopRight);
    ImGui::DockBuilderDockWindow("Timeline", dockBottomLeft);
    ImGui::DockBuilderDockWindow("Console", dockBottomLeft);
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
                         bool liveView, bool& showPersistent, bool hasPersistentView,
                         char* filterBuf, size_t filterBufSize, int32_t& contextNode) {
    static std::string previousSearch;
    static std::vector<int32_t> matches;
    static int matchIndex = -1;
    static int32_t selectedNode = -1;
    static bool scrollToSelection = false;
    static double searchNoticeUntil = 0.0;
    static int searchNoticeCount = 0;
    static const gui::StacktraceTree::Node* searchedNodes = nullptr;
    static size_t searchedNodeCount = 0;
    if (searchedNodes != tree.Nodes().data() || searchedNodeCount != tree.Nodes().size()) {
        searchedNodes = tree.Nodes().data();
        searchedNodeCount = tree.Nodes().size();
        matches.clear();
        matchIndex = -1;
        selectedNode = -1;
    }
    if (snapshot.records.empty()) {
        ImGui::TextUnformatted("No allocation records loaded.");
        return;
    }

    // --- Item 1: single vertical scrollbar.
    // The table gets ScrollY/ScrollX and scrolls internally. We reserve the
    // bottom filter bar's height up front by giving the table an explicit outer
    // size of (avail - filterBarH); the filter then sits at the bottom with no
    // overflow, so the host window never grows a second scrollbar.
    const ImGuiStyle& barStyle = ImGui::GetStyle();
    const float filterBarH = ImGui::GetFrameHeight() +
                             2.0f * barStyle.ItemSpacing.y +
                             barStyle.SeparatorSize + 1.0f;
    const ImVec2 tableSize(-FLT_MIN, ImMax(1.0f, ImGui::GetContentRegionAvail().y - filterBarH));

    // Tree table: flat visible rows rendered via clipper for large datasets.
    // Sortable: clicking Size / Count re-sorts each node's children by that key.
    // Fixed columns let the Function/Size separator expand the table's inner
    // width, with ScrollX exposing names beyond the viewport.
    // The old saved stretch-column layout can restore a swapped visual order
    // after changing Function to fixed width. Keep manual resizing for this
    // session, but start each launch with the intended column order.
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("stacktrace_tree_v3", 4, flags, tableSize)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    const ImGuiStyle& tableStyle = ImGui::GetStyle();
    const float otherColumnsWidth = 140.0f + 80.0f + 120.0f;
    float functionWidth = std::max(180.0f,
        ImGui::GetContentRegionAvail().x - otherColumnsWidth -
        tableStyle.ScrollbarSize - 8.0f * tableStyle.CellPadding.x - 20.0f);
    if (selectedNode >= 0 && selectedNode < static_cast<int32_t>(tree.Nodes().size())) {
        for (const auto& row : tree.VisibleRows()) {
            if (row.nodeIndex == selectedNode) {
                functionWidth = std::max(functionWidth,
                    row.depth * ImGui::GetTreeNodeToLabelSpacing() + ImGui::GetFrameHeight() +
                    ImGui::CalcTextSize(tree.PoolStr(tree.NodeAt(selectedNode).funcName)).x + 24.0f);
                break;
            }
        }
    }
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_NoSort |
                                            ImGuiTableColumnFlags_NoHeaderLabel,
                            functionWidth, 0);
    // Default to descending (largest first) for both sortable columns.
    ImGui::TableSetupColumn(liveView ? "Live bytes" : "Allocated total", ImGuiTableColumnFlags_WidthFixed |
                                                   ImGuiTableColumnFlags_PreferSortDescending, 140.0f, 1);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed |
                                         ImGuiTableColumnFlags_PreferSortDescending, 80.0f, 2);
    ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthFixed |
                                           ImGuiTableColumnFlags_NoSort, 120.0f, 3);
    ImGui::TableHeadersRow();

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

    const auto& rows = tree.VisibleRows();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    // Force the selected row through the clipper. Its exact table position is
    // only known once submitted; an estimated scroll offset misses deep rows
    // because table cell padding is part of each row's height.
    if (scrollToSelection) {
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (rows[i].nodeIndex == selectedNode) {
                clipper.IncludeItemByIndex(i);
                break;
            }
        }
    }
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const auto& row = rows[r];
            const auto& node = tree.NodeAt(row.nodeIndex);

            ImGui::TableNextRow();
            if (row.nodeIndex == selectedNode)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImGuiCol_HeaderActive));
            ImGui::TableNextColumn();

            ImGui::PushID(row.nodeIndex);

            // --- Single-line tree row: [indent guides][arrow-or-space][name].
            // We lay out explicitly so the arrow and name share one line and
            // leaves align under the parent label (same X as if they had an arrow).
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float lineHeight = ImGui::GetFrameHeight();  // matches ArrowButton
            const float stepX = ImGui::GetTreeNodeToLabelSpacing();
            const float baseX = ImGui::GetCursorPosX();
            const float rowScreenX = ImGui::GetCursorScreenPos().x;
            const float rowScreenY = ImGui::GetCursorScreenPos().y;

            // Indent guide lines: one faint vertical line per ancestor depth, at the
            // X where that ancestor's arrow sits. Drawn BEFORE the row widgets so
            // they sit behind the text (no line crossing through the arrow).
            const ImU32 guideCol = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);
            for (int d = 1; d <= row.depth; ++d) {
                const float sx = rowScreenX + (float)(d - 1) * stepX +
                                 lineHeight * 0.5f;
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

            // Function name on the SAME line as the arrow. Clip to the FULL
            // Function-column width (not the post-indent text start) so deep
            // nodes' names aren't cut off by the cell clip rect. This is the
            // fix for label clipping on deep trees - see ImGui #3823 analysis.
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            const float nameScreenX = ImGui::GetCursorScreenPos().x;
            // The Function column's right edge in screen coords.
            const ImGuiTable* tbl = ImGui::GetCurrentTable();
            const ImRect cellRect = ImGui::TableGetCellBgRect(tbl, tbl->CurrentColumn);
            ImGui::PushClipRect(ImVec2(nameScreenX, rowScreenY),
                                ImVec2(cellRect.Max.x, rowScreenY + lineHeight),
                                true);
            ImGui::TextUnformatted(tree.PoolStr(node.funcName));
            ImGui::PopClipRect();

            // Row interaction: invisible button over the whole Function cell text
            // region - left-click toggles expansion for parents, right-click opens
            // the context menu (records the nodeIndex; rows are clipper-transient).
            ImGui::SetCursorScreenPos(ImVec2(nameScreenX, rowScreenY));
            const float rowW = cellRect.Max.x - ImGui::GetCursorScreenPos().x;
            ImGui::InvisibleButton("##rowhit", ImVec2(std::max(rowW, 1.0f), lineHeight));
            if (scrollToSelection && row.nodeIndex == selectedNode) {
                ImGui::SetScrollHereY(0.5f);
                const float nameX = row.depth * stepX + lineHeight +
                                    ImGui::GetStyle().ItemInnerSpacing.x;
                const float nameW = ImGui::CalcTextSize(tree.PoolStr(node.funcName)).x;
                const float visibleW = ImGui::GetWindowWidth() - otherColumnsWidth -
                                       ImGui::GetStyle().ScrollbarSize;
                ImGui::SetScrollX(std::max(0.0f, nameX + nameW - visibleW + 24.0f));
                scrollToSelection = false;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tree.PoolStr(node.funcName));
            if (ImGui::IsItemClicked()) {
                selectedNode = row.nodeIndex;
                if (row.hasChildren) tree.SetExpanded(row.nodeIndex, !row.expanded);
            }
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

    // Search the complete tree. Enter advances and wraps, as in the Qt view.
    ImGui::Separator();
    const float navW = ImGui::GetFrameHeight();
    const float selectorW = 170.0f;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x -
        2.0f * navW - selectorW - 3.0f * ImGui::GetStyle().ItemSpacing.x));
    const bool enter = ImGui::InputTextWithHint("##treefilter", "search nodes...",
                                                filterBuf, filterBufSize,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (previousSearch != filterBuf) {
        previousSearch = filterBuf;
        matches.clear();
        matchIndex = -1;
        selectedNode = -1;
    }
    if (enter && !previousSearch.empty()) {
        ImGui::SetKeyboardFocusHere(-1);
        if (matches.empty()) {
            std::vector<int32_t> pending(tree.Roots().rbegin(), tree.Roots().rend());
            std::string needle = previousSearch;
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            while (!pending.empty()) {
                const int32_t index = pending.back();
                pending.pop_back();
                const auto& node = tree.NodeAt(index);
                std::string name = tree.PoolStr(node.funcName);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name.find(needle) != std::string::npos)
                    matches.push_back(index);
                pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
            }
        }
        searchNoticeCount = static_cast<int>(matches.size());
        searchNoticeUntil = ImGui::GetTime() + 3.0;
    }
    auto jumpMatch = [&](int direction) {
        if (matches.empty()) return;
        matchIndex = ((matchIndex + direction) % static_cast<int>(matches.size()) +
                      static_cast<int>(matches.size())) % static_cast<int>(matches.size());
        selectedNode = matches[matchIndex];
        tree.RevealNode(selectedNode);
        scrollToSelection = true;
    };
    if (enter) jumpMatch(1);
    ImGui::SameLine();
    if (matches.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("<##stacksearch", ImVec2(navW, 0))) jumpMatch(-1);
    ImGui::SameLine();
    if (ImGui::Button(">##stacksearch", ImVec2(navW, 0))) jumpMatch(1);
    if (matches.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(selectorW);
    if (!hasPersistentView) ImGui::BeginDisabled();
    const char* currentView = hasPersistentView && showPersistent
        ? "Persistent" : "All Allocations";
    if (ImGui::BeginCombo("##allocation_view", currentView)) {
        if (ImGui::Selectable("All Allocations", !showPersistent))
            showPersistent = false;
        if (ImGui::Selectable("Persistent", showPersistent))
            showPersistent = true;
        ImGui::EndCombo();
    }
    if (!hasPersistentView) ImGui::EndDisabled();
    if (ImGui::GetTime() < searchNoticeUntil) {
        const float alpha = std::min(1.0f, static_cast<float>(searchNoticeUntil - ImGui::GetTime()));
        const std::string notice = std::to_string(searchNoticeCount) + " matches";
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 pos(origin.x + 20.0f, origin.y + 55.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(notice.c_str());
        const ImVec2 size(textSize.x + 20.0f, textSize.y + 12.0f);
        ImGui::GetForegroundDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            ImGui::GetColorU32(ImVec4(0.05f, 0.08f, 0.08f, 0.78f * alpha)), 5.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f),
            ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), notice.c_str());
    }
}

// Separate from DrawStacktracePanel: its search/selection statics belong to
// the main record view and must not be changed by the leak result window.
void DrawLeakTreePanel(gui::StacktraceTree& tree, int32_t& selectedNode) {
    if (tree.Roots().empty()) {
        ImGui::TextUnformatted("No growing callstacks met the 1 KiB threshold.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##leak_tree", 4, flags, ImVec2(-FLT_MIN, -FLT_MIN)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed, 520);
    ImGui::TableSetupColumn("Growth", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthFixed, 130);
    ImGui::TableHeadersRow();
    const auto& rows = tree.VisibleRows();
    int32_t toggle = -1;
    bool expand = false;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& row = rows[i];
            const auto& node = tree.NodeAt(row.nodeIndex);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (row.nodeIndex == selectedNode)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImGuiCol_HeaderActive));
            ImGui::PushID(row.nodeIndex);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 row.depth * ImGui::GetTreeNodeToLabelSpacing());
            const char* arrow = row.hasChildren ? (row.expanded ? "v " : "> ") : "  ";
            const std::string label = std::string(arrow) + tree.PoolStr(node.funcName);
            if (ImGui::Selectable(label.c_str(), row.nodeIndex == selectedNode,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectedNode = row.nodeIndex;
                if (row.hasChildren) {
                    toggle = row.nodeIndex;
                    expand = !row.expanded;
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatBytes(node.totalSize).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", node.allocCount);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tree.PoolStr(node.library));
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    if (toggle >= 0) tree.SetExpanded(toggle, expand);
}

ImVec4 LogColor(loli::LogLevel level) {
    switch (level) {
    case loli::LogLevel::Error: return ImVec4(1.0f, 0.42f, 0.42f, 1.0f);
    case loli::LogLevel::Warn: return ImVec4(1.0f, 0.76f, 0.38f, 1.0f);
    case loli::LogLevel::Debug: return ImVec4(0.65f, 0.69f, 0.72f, 1.0f);
    case loli::LogLevel::Info: return ImVec4(0.83f, 0.88f, 0.89f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

void DrawConsolePanel(std::vector<loli::LogEntry>& lines, uint64_t& cursor) {
    auto incoming = loli::LoliLogger::Instance().ReadSince(cursor);
    if (!incoming.empty()) {
        lines.insert(lines.end(), std::make_move_iterator(incoming.begin()),
                     std::make_move_iterator(incoming.end()));
        if (lines.size() > 5000)
            lines.erase(lines.begin(), lines.begin() + (lines.size() - 5000));
    }
    if (ImGui::BeginChild("##console_lines", ImVec2(0, 0),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Follow new records only while the user was already at the bottom.
        // Scrolling up to inspect older entries pauses this automatically.
        const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::PushStyleColor(ImGuiCol_Text, LogColor(lines[i].level));
                ImGui::TextUnformatted(lines[i].line.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (ImGui::BeginPopupContextWindow("##console_context")) {
            if (ImGui::MenuItem("Copy all")) {
                std::string text;
                for (const auto& entry : lines) {
                    text += entry.line;
                    text += '\n';
                }
                ImGui::SetClipboardText(text.c_str());
            }
            if (ImGui::MenuItem("Clear view"))
                lines.clear();
            ImGui::EndPopup();
        }
        if (wasAtBottom && !incoming.empty())
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
    }
    ImGui::EndChild();
}

// Renders screenshots[viewIdx] (clamped to the last one). Textures are
// uploaded lazily per index into the cache so timeline hover-scrubbing can
// flip between captures without re-uploading every frame.
void DrawScreenshotPanel(const gui::GuiSnapshot& snapshot,
                         std::unordered_map<int, sf::Texture>& textureCache,
                         int viewIdx) {
    if (snapshot.screenshots.empty()) {
        ImGui::TextUnformatted("No screenshots captured.");
        return;
    }
    const int count = static_cast<int>(snapshot.screenshots.size());
    if (viewIdx < 0 || viewIdx >= count)
        viewIdx = count - 1;

    auto it = textureCache.find(viewIdx);
    if (it == textureCache.end()) {
        const gui::ScreenshotSnapshot& shot = snapshot.screenshots[viewIdx];
        sf::Texture tex;
        if (shot.jpegBytes.empty() ||
            !tex.loadFromMemory(shot.jpegBytes.data(), shot.jpegBytes.size())) {
            ImGui::TextUnformatted("Failed to decode screenshot.");
            return;
        }
        it = textureCache.emplace(viewIdx, std::move(tex)).first;
    }
    const sf::Texture& texture = it->second;

    ImGui::Text("%.1fs", snapshot.screenshots[viewIdx].timeMs / 1000.0f);
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
    bool selfTestCumulative = false;
    bool selfTestRangeLeaks = false;
    bool selfTestCaptureClock = false;
    std::string selfTestCaptureDevice;
    std::string selfTestCaptureApp;
    std::string selfTestCaptureOut;
    int selfTestCaptureSeconds = 8;
    std::string selfTestSavePath;
    // Screenshot harness: after the selftest frame budget, dump the window to a
    // PNG so we can visually verify rendering from scripts.
    std::string screenshotPath;
    std::string selfTestDialog;
    std::string diagnosticLogPath;
    std::string diagnosticLevel;
    bool defaultDiagnosticLogPath = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--selftest") == 0) {
            selfTest = true;
        } else if (std::strcmp(a, "--selftest-range-leaks") == 0) {
            selfTest = true;
            selfTestRangeLeaks = true;
        } else if (std::strcmp(a, "--selftest-capture-clock") == 0 && i + 4 < argc) {
            selfTest = true;
            selfTestCaptureClock = true;
            selfTestCaptureDevice = argv[++i];
            selfTestCaptureApp = argv[++i];
            selfTestCaptureSeconds = std::max(3, std::atoi(argv[++i]));
            selfTestCaptureOut = argv[++i];
        } else if (std::strcmp(a, "--selftest-save") == 0 && i + 1 < argc) {
            selfTest = true;
            selfTestSavePath = argv[++i];
        } else if (std::strcmp(a, "--selftest-cumulative") == 0) {
            selfTest = true;
            selfTestCumulative = true;
        } else if (std::strcmp(a, "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
            selfTest = true;  // screenshot implies selftest (bounded frames + exit)
        } else if (std::strcmp(a, "--selftest-dialog") == 0 && i + 1 < argc) {
            selfTestDialog = argv[++i];
            selfTest = true;
        } else if (std::strcmp(a, "--log-file") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-')
                diagnosticLogPath = argv[++i];
            else
                defaultDiagnosticLogPath = true;
        } else if (std::strncmp(a, "--log-file=", 11) == 0) {
            diagnosticLogPath = a + 11;
        } else if (std::strcmp(a, "--log-level") == 0 && i + 1 < argc) {
            diagnosticLevel = argv[++i];
        } else if (std::strncmp(a, "--log-level=", 12) == 0) {
            diagnosticLevel = a + 12;
        } else if ((std::strcmp(a, "--open") == 0 || std::strcmp(a, "-o") == 0) && i + 1 < argc) {
            openRecordArg = argv[++i];
        } else if (std::strstr(a, ".loli") != nullptr) {
            openRecordArg = a;  // bare path ending in .loli
        }
    }
    const std::string defaultLogPath =
        (GuiExecutablePath().parent_path() / "loli_gui.log").string();
    const bool commandLineLogging = defaultDiagnosticLogPath ||
                                    !diagnosticLogPath.empty();
    if (defaultDiagnosticLogPath && diagnosticLogPath.empty())
        diagnosticLogPath = defaultLogPath;
    if (!commandLineLogging) {
        AppSettings startupSettings;
        if (startupSettings.Get("DiagnosticLogEnabled", "false") == "true")
            diagnosticLogPath = startupSettings.Get("DiagnosticLogPath", defaultLogPath);
    }
    auto& logger = loli::LoliLogger::Instance();
    logger.SetConsoleEnabled(false);
    if (diagnosticLevel == "debug") logger.SetMinLevel(loli::LogLevel::Debug);
    else if (diagnosticLevel == "warn") logger.SetMinLevel(loli::LogLevel::Warn);
    else if (diagnosticLevel == "error") logger.SetMinLevel(loli::LogLevel::Error);
    else if (!diagnosticLevel.empty() && diagnosticLevel != "info")
        LOLI_WARN("startup") << "Unknown --log-level: " << diagnosticLevel;
    if (!diagnosticLogPath.empty()) {
        if (!logger.StartFile(diagnosticLogPath))
            LOLI_ERROR("startup") << "Failed to open log file: " << diagnosticLogPath;
    }
    loli::LoliLogger::Instance().Log("startup",
        "GUI start; file_log=" + (diagnosticLogPath.empty() ? std::string("disabled")
                                                           : diagnosticLogPath));

    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;

    sf::RenderWindow window(sf::VideoMode({1440, 810}), "LoliProfiler",
                            sf::Style::Default, sf::State::Windowed, settings);
    std::filesystem::path iconDir;
#ifdef _WIN32
    wchar_t executablePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) != 0)
        iconDir = std::filesystem::path(executablePath).parent_path();
#else
    iconDir = std::filesystem::absolute(argv[0]).parent_path();
#endif
    sf::Image windowIcon;
    if (!iconDir.empty() &&
        windowIcon.loadFromFile(iconDir / "res" / "loli_cat_icon.png"))
        window.setIcon(windowIcon);
    window.setVerticalSyncEnabled(true);

    if (!ImGui::SFML::Init(window))
        return -1;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Native file dialogs (NFD).
    FileDialogs::Init();

    // Theme: restore from the Qt-free settings store, default to the first
    // registered theme.
    AppSettings appSettings;
    const std::string defaultTheme = GetDefaultImGuiThemeName();
    std::string currentTheme = appSettings.Get("theme", defaultTheme);
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
            font = fonts->AddFontDefault(); // [[nodiscard]]: keep the returned font
        io.FontDefault = font;
        (void)ImGui::SFML::UpdateFontTexture(); // returns false only on atlas upload failure
    }

    // Core data bridge (owns the LoliCore capture services; driven by
    // bridge.Tick() each frame below).
    gui::GuiDataBridge bridge;
    gui::RunLaunchDialog runLaunchDialog;
    gui::CaptureConfigDialog captureConfigDialog;
    runLaunchDialog.SetConfigDialog(&captureConfigDialog);
    // Auto-detect Android SDK/NDK on startup (Qt CLI Initialize parity): load
    // persisted paths, then fall back to ANDROID_HOME / the standard install
    // locations. SetSDKPath("")/SetNDKPath("") trigger the search; a found
    // path is persisted back to loli_settings.json.
    {
        AppSettings pathSettings;
        PathUtilsLite::SetSDKPath(pathSettings.Get("AndroidSDK"));
        PathUtilsLite::SetNDKPath(pathSettings.Get("AndroidNDK"));
        PathUtilsLite::LoadPythonPathSettings();
    }
    loli::LoliLogger::Instance().Log("startup",
        "sdk=" + PathUtilsLite::GetSDKPath() +
        " ndk=" + PathUtilsLite::GetNDKPath() +
        " adb=" + PathUtilsLite::GetADBExecutablePath() +
        " python=" + PathUtilsLite::GetPythonExecutablePath());
    if (selfTestDialog == "run")
        runLaunchDialog.Open(&bridge);
    else if (selfTestDialog == "config")
        captureConfigDialog.Open(&bridge);

    std::string loadedRecordName;
    std::string recordPath;
    std::string pendingSymbolizeSave;
    std::string pendingSymbolLibrary;
    std::future<SymbolizeResult> symbolizeFuture;
    std::shared_ptr<ProcessRunner> symbolizeProcess;
    bool symbolizeRunning = false;
    loli::LoliLogger::Clock::time_point symbolizeStartedAt{};
    std::string symbolizeError;
    bool showSymbolizeError = false;
    bool offerSymbolizeAfterStop = false;
    bool wasCapturing = false;
    // Screenshot panel: lazily-uploaded texture per screenshot index so the
    // timeline hover-scrub can flip between captures cheaply.
    std::unordered_map<int, sf::Texture> screenshotTextures;
    int screenshotHoverIdx = -1;  // set by the timeline when hovered
    int screenshotViewIdx = -1;   // scrub target; -1 = latest
    gui::StacktraceTree stacktraceTree;
    gui::StacktraceTree liveStacktraceTree;
    bool hasLiveTree = false;
    bool liveTreeAliasesAll = false;
    bool showPersistent = !selfTestCumulative;
    uint64_t stacktraceBuiltVersion = 0;  // bridge.SnapshotVersion() the tree was built from
    char stacktraceFilter[256] = {0};
    int32_t stacktraceContextNode = -1;   // nodeIndex right-clicked in the Stacktrace panel
    gui::TimelineView timelineView;
    gui::StacktraceTree rangeStacktraceTree;
    gui::StacktraceTree rangeLiveStacktraceTree;
    uint64_t timelineSessionEpoch = bridge.SessionEpoch();
    bool rangeRequested = false;
    bool rangeReady = false;
    bool hasRangeLiveTree = false;
    bool rangeLiveAliasesAll = false;
    double requestedRangeStart = 0.0;
    double requestedRangeEnd = 0.0;
    std::size_t rangeRecordCount = 0;
    uint64_t rangeTreeVersion = 0;
    gui::TreemapState treemapState;
    gui::TreemapState leakTreemapState;
    std::shared_ptr<gui::StacktraceTree> leakTree;
    bool leakWindowOpen = false;
    bool showLeakViewChoice = false;
    bool leakTreeView = true;
    bool leakAnalysisRunning = false;
    std::string leakAnalysisError;
    int32_t leakSelectedNode = -1;
    uint64_t leakResultVersion = 0;
    int32_t leakStartMs = 0;
    int32_t leakEndMs = 0;
    bool leakPersistentOnly = false;
    // Dockable panel visibility, toggled from the Window menu.
    bool showStacktrace = true;
    bool showTimeline = true;
    bool showTreemap = true;
    bool showSmaps = true;
    bool showScreenshot = true;
    bool showConsole = true;
    uint64_t consoleCursor = 0;
    std::vector<loli::LogEntry> consoleLines;
    bool showSettingsDialog = selfTestDialog == "settings";
    bool showAboutDialog = selfTestDialog == "about";
    bool firstFrame = true;
    bool activateTimelineAfterLayout = selfTestDialog.empty();
    int framesRendered = 0;
    int loadingFrames = 0;
    double lastTreeBuildMs = 0.0;
    double worstFrameMs = 0.0;
    bool wasLoading = false;
    std::string launchErrorModal;
    int framesAfterLoad = 0;
    int rangeLeakTestStage = 0;
    int selfTestExitCode = 0;
    bool selfTestRangeLeaksDone = false;
    bool selfTestSaveRequested = false;
    int captureClockTestStage = 0;
    loli::LoliLogger::Clock::time_point captureClockTestConnectedAt{};
    std::size_t rangeLeakOriginalNodes = 0;

    auto chooseSymbolLibrary = [&]() -> bool {
        auto path = FileDialogs::OpenFile({{"Shared library", "so,sym,dylib,debug"}});
        ImGui::GetIO().ClearInputKeys();
        if (!path)
            return false;
        pendingSymbolLibrary = *path;
        return true;
    };
    auto queueSymbolize = [&](const std::string& input) {
        const auto cli = FindCliExecutable();
        if (cli.empty()) {
            symbolizeError = "LoliProfilerCLI was not found beside the GUI. Rebuild or extract the complete release archive.";
            showSymbolizeError = true;
            return;
        }
        const std::string output = SymbolizedOutputPath(input).string();
        const std::string library = pendingSymbolLibrary;
        auto process = std::make_shared<ProcessRunner>();
        process->SetProgram(cli.string());
        process->SetArguments({"--symbolize", input, "--symbol", library,
                               "--out", output});
        if (!process->Start()) {
            symbolizeError = "Could not start " + cli.string();
            showSymbolizeError = true;
            return;
        }
        symbolizeProcess = process;
        symbolizeRunning = true;
        symbolizeStartedAt = loli::LoliLogger::Clock::now();
        loli::LoliLogger::Instance().Log("symbolize",
            "start input=" + input + " library=" + library +
            " output=" + output + " cli=" + cli.string());
        symbolizeFuture = std::async(std::launch::async, [process, output]() {
            SymbolizeResult result;
            result.output = output;
            if (!process->WaitForFinished(3600000)) {
                process->Kill();
                result.message = "Symbolization timed out after one hour.";
                return result;
            }
            const std::string stdoutText = process->ReadAllStdout();
            const std::string stderrText = process->ReadAllStderr();
            result.ok = process->GetExitCode() == 0 &&
                        std::filesystem::is_regular_file(output);
            result.message = result.ok ? stdoutText :
                (stderrText.empty() ? stdoutText : stderrText);
            if (result.message.empty() && !result.ok)
                result.message = "LoliProfilerCLI exited without a symbolized record.";
            return result;
        });
    };
    auto startSymbolize = [&]() {
        if (!chooseSymbolLibrary())
            return;
        if (!recordPath.empty()) {
            queueSymbolize(recordPath);
            return;
        }
        auto path = FileDialogs::SaveFile({{"Loli Record", "loli"}});
        ImGui::GetIO().ClearInputKeys();
        if (!path)
            return;
        if (std::filesystem::path(*path).extension().empty())
            *path += ".loli";
        if (bridge.SaveRecord(*path))
            pendingSymbolizeSave = *path;
        else {
            symbolizeError = "The current record could not be saved.";
            showSymbolizeError = true;
        }
    };

    // Adopt the worker-built aggregated tree when the snapshot version changes.
    // The tree arrives pre-built (interned strings) inside the snapshot, so this
    // is a cheap move - no UI-thread rebuild.
    auto rebuildTreeIfNeeded = [&]() {
        const uint64_t v = bridge.SnapshotVersion();
        if (v != stacktraceBuiltVersion) {
            const gui::GuiSnapshot& snap = *bridge.AcquireSnapshot();
            const auto t0 = std::chrono::steady_clock::now();
            if (snap.stackTree && !snap.stackTree->Nodes().empty()) {
                stacktraceTree.Adopt(std::move(*snap.stackTree));
                liveStacktraceTree.Clear();
                hasLiveTree = false;
                liveTreeAliasesAll = false;
            }
            if (snap.liveTreeUsesAll && !stacktraceTree.Nodes().empty()) {
                hasLiveTree = true;
                liveTreeAliasesAll = true;
            } else if (snap.liveStackTree && !snap.liveStackTree->Nodes().empty()) {
                liveStacktraceTree.Adopt(std::move(*snap.liveStackTree));
                hasLiveTree = true;
            }
            if (snap.records.empty()) {
                stacktraceTree.Clear();
                liveStacktraceTree.Clear();
                hasLiveTree = false;
                liveTreeAliasesAll = false;
            }
            const auto t1 = std::chrono::steady_clock::now();
            lastTreeBuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            loli::LoliLogger::Instance().LogStage("ui", "adopt_tree", t0,
                "nodes=" + std::to_string(stacktraceTree.Nodes().size()));
            stacktraceBuiltVersion = v;
        }
    };

    // Auto-open a record passed on the command line.
    std::string pendingOpen;
    if (openRecordArg)
        pendingOpen = openRecordArg;

    sf::Clock deltaClock;
    sf::Clock frameClock;
    sf::Clock selfTestClock;
    auto lastSlowFrameLog = loli::LoliLogger::Clock::now() - std::chrono::seconds(2);
#ifdef __APPLE__
    constexpr const char* kRunShortcut = "Cmd+R";
    constexpr const char* kOpenShortcut = "Cmd+O";
    constexpr const char* kSaveShortcut = "Cmd+S";
    constexpr const char* kSettingsShortcut = "Cmd+,";
    constexpr const char* kExitShortcut = "Cmd+Q";
#else
    constexpr const char* kRunShortcut = "Ctrl+R";
    constexpr const char* kOpenShortcut = "Ctrl+O";
    constexpr const char* kSaveShortcut = "Ctrl+S";
    constexpr const char* kSettingsShortcut = "Ctrl+,";
    constexpr const char* kExitShortcut = "Ctrl+Q";
#endif
    while (window.isOpen()) {
        frameClock.restart();

        // Drive the capture engine (meminfo/screenshot polling, stacktrace
        // channel pump, launch completion) once per frame.
        bridge.Tick();
        if (selfTestCaptureClock) {
            if (captureClockTestStage == 0 && framesRendered > 2) {
                const auto config = bridge.GetCaptureConfig();
                if (!bridge.StartCapture(selfTestCaptureDevice, selfTestCaptureApp,
                                         "", config, false, true)) {
                    LOLI_ERROR("selftest") << "GUI capture clock: start failed";
                    selfTestExitCode = 1;
                    window.close();
                } else {
                    captureClockTestStage = 1;
                    LOLI_INFO("selftest") << "GUI capture clock: started app="
                        << selfTestCaptureApp << " duration_s=" << selfTestCaptureSeconds;
                }
            } else if (captureClockTestStage == 1) {
                if (!bridge.IsCapturing()) {
                    LOLI_ERROR("selftest") << "GUI capture clock: capture ended early";
                    selfTestExitCode = 1;
                    window.close();
                } else if (bridge.IsConnected()) {
                    if (captureClockTestConnectedAt == loli::LoliLogger::Clock::time_point{})
                        captureClockTestConnectedAt = loli::LoliLogger::Clock::now();
                    const auto elapsed = loli::LoliLogger::Clock::now() -
                                         captureClockTestConnectedAt;
                    if (elapsed >= std::chrono::seconds(selfTestCaptureSeconds)) {
                        bridge.StopCapture();
                        const auto stopped = bridge.AcquireSnapshot();
                        const std::size_t samples = stopped ?
                            stopped->memTimeline.size() : 0;
                        const int32_t lastMs = samples ?
                            stopped->memTimeline.back().timeMs : -1;
                        LOLI_INFO("selftest") << "GUI capture clock: stopped samples="
                            << samples << " last_sample_ms=" << lastMs;
                        if (samples < 3 || lastMs < 2000 ||
                            lastMs > (selfTestCaptureSeconds + 3) * 1000) {
                            LOLI_ERROR("selftest") << "GUI capture clock: invalid sample timeline";
                            selfTestExitCode = 1;
                        }
                        captureClockTestStage = 2;
                    }
                }
            } else if (captureClockTestStage == 2 && !bridge.IsFinalizing()) {
                if (bridge.SaveRecord(selfTestCaptureOut)) {
                    captureClockTestStage = 3;
                } else {
                    LOLI_ERROR("selftest") << "GUI capture clock: save rejected";
                    selfTestExitCode = 1;
                    window.close();
                }
            } else if (captureClockTestStage == 3 && !bridge.IsSaving()) {
                if (!bridge.LastSaveResult()) {
                    LOLI_ERROR("selftest") << "GUI capture clock: save failed";
                    selfTestExitCode = 1;
                } else {
                    LOLI_INFO("selftest") << "GUI capture clock: saved "
                        << selfTestCaptureOut;
                }
                captureClockTestStage = 4;
                window.close();
            }
            if (selfTestClock.getElapsedTime().asSeconds() > 180.0f &&
                captureClockTestStage < 4) {
                LOLI_ERROR("selftest") << "GUI capture clock: timeout stage="
                    << captureClockTestStage;
                selfTestExitCode = 1;
                window.close();
            }
        }
        if (bridge.SessionEpoch() != timelineSessionEpoch) {
            timelineSessionEpoch = bridge.SessionEpoch();
            timelineView = gui::TimelineView{};
            rangeRequested = false;
            rangeReady = false;
            rangeStacktraceTree.Clear();
            rangeLiveStacktraceTree.Clear();
            rangeLiveAliasesAll = false;
            treemapState.focusedNode = -1;
            leakTree.reset();
            leakWindowOpen = false;
            leakAnalysisRunning = false;
            leakAnalysisError.clear();
        }
        if (leakAnalysisRunning) {
            gui::GuiDataBridge::LeakAnalysisResult result;
            if (bridge.TryTakePossibleLeaks(result)) {
                leakAnalysisRunning = false;
                leakAnalysisError = std::move(result.error);
                leakTree = std::move(result.tree);
                leakSelectedNode = -1;
                leakTreemapState.focusedNode = -1;
                ++leakResultVersion;
            }
        }
        if (rangeRequested && !rangeReady) {
            gui::GuiDataBridge::TimeRangeTrees filtered;
            if (bridge.TryTakeTimeRangeTrees(filtered)) {
                rangeLiveAliasesAll = filtered.live &&
                    filtered.live.get() == filtered.all.get();
                rangeStacktraceTree.Adopt(std::move(*filtered.all));
                hasRangeLiveTree = filtered.live != nullptr;
                if (hasRangeLiveTree && !rangeLiveAliasesAll)
                    rangeLiveStacktraceTree.Adopt(std::move(*filtered.live));
                rangeRecordCount = filtered.recordCount;
                rangeReady = true;
                ++rangeTreeVersion;
                treemapState.focusedNode = -1;
            }
        }
        if (bridge.IsCapturing() && !wasCapturing) {
            recordPath.clear();
            loadedRecordName.clear();
        }
        wasCapturing = bridge.IsCapturing();
        if (!pendingSymbolizeSave.empty() && !bridge.IsSaving()) {
            const std::string savedPath = std::move(pendingSymbolizeSave);
            pendingSymbolizeSave.clear();
            if (bridge.LastSaveResult()) {
                recordPath = savedPath;
                loadedRecordName = savedPath;
                queueSymbolize(savedPath);
            } else {
                symbolizeError = "Failed to save the record before symbolization.";
                showSymbolizeError = true;
            }
        }
        if (symbolizeRunning && symbolizeFuture.valid() &&
            symbolizeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const SymbolizeResult result = symbolizeFuture.get();
            symbolizeRunning = false;
            symbolizeProcess.reset();
            loli::LoliLogger::Instance().LogStage("symbolize", "complete",
                symbolizeStartedAt, "ok=" + std::string(result.ok ? "true" : "false") +
                    " output=" + result.output);
            if (!result.message.empty())
                loli::LoliLogger::Instance().Log("symbolize", result.message);
            if (result.ok) {
                bridge.LoadRecord(result.output);
                recordPath = result.output;
                loadedRecordName = result.output;
                window.setTitle("LoliProfiler - " + loadedRecordName);
            } else {
                symbolizeError = result.message;
                showSymbolizeError = true;
            }
        }

        while (const auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
        }

        ImGui::SFML::Update(window, deltaClock.restart());
        if (showSymbolizeError) {
            ImGui::OpenPopup("Symbolization failed");
            showSymbolizeError = false;
        }
        if (std::string error = bridge.TakeLaunchError(); !error.empty()) {
            launchErrorModal = std::move(error);
            ImGui::OpenPopup("Capture launch failed");
        }

        // Use the latest published bridge snapshot for this frame (immutable,
        // safe to hold across the frame).
        std::shared_ptr<const gui::GuiSnapshot> frameSnapshot = bridge.AcquireSnapshot();
        static const std::shared_ptr<const gui::GuiSnapshot> emptySnapshot(
            new gui::GuiSnapshot());
        if (!frameSnapshot)
            frameSnapshot = emptySnapshot;
        const gui::GuiSnapshot& snapshot = *frameSnapshot;
        rebuildTreeIfNeeded();
        const bool canStartCapture = !bridge.IsCapturing() &&
                                     !bridge.IsLoading() && !bridge.IsSaving() &&
                                     !bridge.IsFinalizing() && !symbolizeRunning;
        const bool canOpenRecord = !bridge.IsCapturing() &&
                                   !bridge.IsLoading() && !bridge.IsSaving() &&
                                   !bridge.IsFinalizing() && !symbolizeRunning;
        const bool canSaveRecord = !bridge.IsCapturing() &&
                                   !bridge.IsLoading() && !bridge.IsSaving() &&
                                   !bridge.IsFinalizing() && !symbolizeRunning &&
                                   !snapshot.records.empty();
        const bool canAnalyzeLeaks = canSaveRecord && timelineView.hasSelection &&
            rangeReady &&
            timelineView.selEndMs > timelineView.selStartMs && !leakAnalysisRunning;

        auto openRecordDialog = [&]() {
            auto path = FileDialogs::OpenFile({{"Loli Record", "loli"}});
            // NFD blocks the SFML frame loop, so the key-up for Ctrl+O may be
            // consumed by the native dialog. A stale Ctrl suppresses wheel
            // scrolling in ImGui until the window loses and regains focus.
            ImGui::GetIO().ClearInputKeys();
            return path;
        };

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
            bridge.LoadRecord(pendingOpen);
            loadedRecordName = pendingOpen;
            recordPath = pendingOpen;
            window.setTitle("LoliProfiler - " + loadedRecordName);
            pendingOpen.clear();
        }

        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (bridge.ShouldMaskCaptureView())
                ImGui::BeginDisabled();
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Run", kRunShortcut, false, canStartCapture)) {
                    runLaunchDialog.Open(&bridge);
                }
                if (ImGui::MenuItem("Open Record...", kOpenShortcut, false, canOpenRecord)) {
                    if (auto path = openRecordDialog()) {
                        bridge.LoadRecord(*path);
                        loadedRecordName = *path;
                        recordPath = *path;
                        window.setTitle("LoliProfiler - " + loadedRecordName);
                    }
                }
                if (ImGui::MenuItem("Save Record As...", kSaveShortcut, false, canSaveRecord)) {
                    auto path = FileDialogs::SaveFile({{"Loli Record", "loli"}});
                    ImGui::GetIO().ClearInputKeys();
                    if (path) {
                        if (!path->empty() && path->find('.') == std::string::npos)
                            *path += ".loli";
                        if (bridge.SaveRecord(*path)) {
                            recordPath = *path;
                            window.setTitle("LoliProfiler - " + *path);
                        }
                    }
                }
                if (ImGui::MenuItem("Symbolize Record...", nullptr, false,
                                    canSaveRecord))
                    startSymbolize();
                ImGui::Separator();
                if (ImGui::MenuItem("Settings...", kSettingsShortcut)) {
                    showSettingsDialog = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", kExitShortcut)) {
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
                ImGui::MenuItem("Console", nullptr, &showConsole);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Docs")) {
                    if (auto openURL = ImGui::GetPlatformIO().Platform_OpenInShellFn)
                        openURL(ImGui::GetCurrentContext(),
                                "https://github.com/Tencent/loli_profiler/tree/master/docs");
                }
                if (ImGui::MenuItem("About..."))
                    showAboutDialog = true;
                ImGui::EndMenu();
            }
            if (bridge.ShouldMaskCaptureView())
                ImGui::EndDisabled();
            ImGui::EndMainMenuBar();
        }

        // Global shortcuts use Command on macOS and Control elsewhere.
        {
            ImGuiIO& io = ImGui::GetIO();
#ifdef __APPLE__
            const bool mod = io.KeySuper;
#else
            const bool mod = io.KeyCtrl;
#endif
            if (mod && canStartCapture && ImGui::IsKeyPressed(ImGuiKey_R, false) &&
                !runLaunchDialog.IsOpen() && !captureConfigDialog.IsOpen())
                runLaunchDialog.Open(&bridge);
            if (mod && canOpenRecord && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                if (auto path = openRecordDialog()) {
                    bridge.LoadRecord(*path);
                    loadedRecordName = *path;
                    recordPath = *path;
                    window.setTitle("LoliProfiler - " + loadedRecordName);
                }
            }
            if (mod && canSaveRecord && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                auto path = FileDialogs::SaveFile({{"Loli Record", "loli"}});
                ImGui::GetIO().ClearInputKeys();
                if (path) {
                    if (!path->empty() && path->find('.') == std::string::npos)
                        *path += ".loli";
                    if (bridge.SaveRecord(*path)) {
                        recordPath = *path;
                        window.setTitle("LoliProfiler - " + *path);
                    }
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
            const bool capturing = bridge.IsCapturing();
            const float captureActionW = ImGui::CalcTextSize(
                capturing ? "Stop Capture" : "Run").x +
                ImGui::GetStyle().FramePadding.x * 2.0f;
            const float leaksW = ImGui::CalcTextSize("Leaks").x +
                                 ImGui::GetStyle().FramePadding.x * 2.0f;
            const float buttonsWidth = captureActionW + leaksW +
                                       ImGui::GetStyle().ItemSpacing.x;
            const float toolbarWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((toolbarWidth - buttonsWidth) * 0.5f);
            if (capturing) {
                if (ImGui::Button("Stop Capture")) {
                    bridge.StopCapture();
                    const auto stopped = bridge.AcquireSnapshot();
                    if (stopped && !stopped->records.empty())
                        offerSymbolizeAfterStop = true;
                }
            } else {
                if (!canStartCapture)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Run"))
                    runLaunchDialog.Open(&bridge);
                if (!canStartCapture)
                    ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!canAnalyzeLeaks) ImGui::BeginDisabled();
            if (ImGui::Button("Leaks"))
                showLeakViewChoice = true;
            if (!canAnalyzeLeaks) ImGui::EndDisabled();
            ImGui::SetItemTooltip("Select a timeline interval first. Compare callstack growth between its start and end marks.");
        }
        ImGui::End();

        if (showLeakViewChoice) {
            ImGui::OpenPopup("Leaks view");
            showLeakViewChoice = false;
        }
        ImGui::SetNextWindowSize(ImVec2(420, 170), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Leaks view", nullptr,
                                  ImGuiWindowFlags_NoResize)) {
            ImGui::Text("Compare allocations at %.1f s and %.1f s?",
                        timelineView.selStartMs / 1000.0,
                        timelineView.selEndMs / 1000.0);
            ImGui::TextWrapped("Only callstacks present at the first mark with at least 1 KiB growth are shown.");
            auto launchLeakView = [&](bool treeView) {
                leakStartMs = static_cast<int32_t>(timelineView.selStartMs);
                leakEndMs = static_cast<int32_t>(timelineView.selEndMs);
                leakPersistentOnly = showPersistent && hasRangeLiveTree;
                if (bridge.RequestPossibleLeaks(leakStartMs, leakEndMs,
                                                leakPersistentOnly)) {
                    leakTreeView = treeView;
                    leakWindowOpen = true;
                    leakTree.reset();
                    leakAnalysisError.clear();
                    leakAnalysisRunning = true;
                } else {
                    leakAnalysisError = "The record is unavailable or another leak analysis is running.";
                    leakWindowOpen = true;
                }
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::Button("Tree View", ImVec2(115, 0))) launchLeakView(true);
            ImGui::SameLine();
            if (ImGui::Button("Treemap", ImVec2(115, 0))) launchLeakView(false);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(115, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Modal dialogs (no-op unless open).
        runLaunchDialog.Render();
        captureConfigDialog.Render();

        // Selftest-only preview, after the dock layout and optional record
        // have loaded. No capture or file write is performed.
        if (selfTest && selfTestDialog == "stopped" && framesAfterLoad == 4)
            offerSymbolizeAfterStop = true;
        if (offerSymbolizeAfterStop) {
            ImGui::OpenPopup("Capture stopped");
            offerSymbolizeAfterStop = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520.0f * dpiScale, 190.0f * dpiScale),
                                 ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Capture stopped", nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextWrapped("Capture stopped. Save the record and symbolize it with a matching library?");
            if (bridge.IsFinalizing()) {
                ImGui::TextDisabled("Resolving device memory mappings...");
                ImGui::BeginDisabled();
            }
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
                ImGui::GetWindowHeight() - ImGui::GetFrameHeight() -
                ImGui::GetStyle().WindowPadding.y));
            if (ImGui::Button("Save and Symbolize")) {
                ImGui::CloseCurrentPopup();
                startSymbolize();
            }
            if (bridge.IsFinalizing())
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Later"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (symbolizeRunning || !pendingSymbolizeSave.empty())
            ImGui::OpenPopup("Symbolizing Record");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520.0f * dpiScale, 170.0f * dpiScale),
                                 ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Symbolizing Record", nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar)) {
            if (!symbolizeRunning && pendingSymbolizeSave.empty())
                ImGui::CloseCurrentPopup();
            else {
                ImGui::TextUnformatted(bridge.IsSaving() ?
                    "Saving record..." : "Resolving symbols with LoliProfilerCLI...");
                ImGui::Spacing();
                ImGui::TextDisabled("The symbolized record will open when finished.");
            }
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Symbolization failed", nullptr,
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoMove)) {
            ImGui::PushTextWrapPos(520.0f);
            ImGui::TextUnformatted(symbolizeError.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showAboutDialog) {
            ImGui::OpenPopup("About LoliProfiler");
            showAboutDialog = false;
        }
        if (ImGui::IsPopupOpen("About LoliProfiler")) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(430.0f, 0.0f),
                                                ImVec2(430.0f, 100000.0f));
            if (ImGui::BeginPopupModal("About LoliProfiler", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoScrollbar)) {
                ImGui::TextUnformatted("LoliProfiler");
                ImGui::TextDisabled("Android native memory profiler");
                ImGui::Separator();
                ImGui::TextWrapped("Copyright (C) 2021 THL A29 Limited, a Tencent company.");
                ImGui::TextLinkOpenURL("Source on GitHub",
                                       "https://github.com/Tencent/loli_profiler");
                ImGui::TextLinkOpenURL("Latest releases",
                                       "https://github.com/Tencent/loli_profiler/releases");
                ImGui::Separator();
                if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        // Settings dialog: fit its fields and footer without an outer scroll.
        if (showSettingsDialog) {
            ImGui::OpenPopup("Settings");
            showSettingsDialog = false;
        }
        const float settingsW = std::min(560.0f, ImGui::GetIO().DisplaySize.x - 32.0f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(settingsW, 0.0f),
                                            ImVec2(settingsW, 100000.0f));
        if (ImGui::BeginPopupModal("Settings", nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SeparatorText("Theme");
            int themeCount = 0;
            const ImGuiTheme* themes = GetImGuiThemes(&themeCount);
            if (ImGui::BeginCombo("##theme", currentTheme.c_str())) {
                for (int i = 0; i < themeCount; ++i) {
                    const bool isActive = currentTheme == themes[i].name;
                    if (ImGui::Selectable(themes[i].name, isActive)) {
                        ApplyImGuiThemeByName(themes[i].name);
                        currentTheme = themes[i].name;
                        appSettings.Set("theme", currentTheme);
                        appSettings.Sync();
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
                const std::string sdkExample = selfTestDialog == "settings"
                    ? "C:/Android/Sdk" : PathUtilsLite::GetSDKPath();
                const std::string ndkExample = selfTestDialog == "settings"
                    ? "C:/Android/Sdk/ndk/current" : PathUtilsLite::GetNDKPath();
                std::strncpy(sdkPath, sdkExample.c_str(), sizeof(sdkPath) - 1);
                std::strncpy(ndkPath, ndkExample.c_str(), sizeof(ndkPath) - 1);
                sdkPath[sizeof(sdkPath) - 1] = '\0';
                ndkPath[sizeof(ndkPath) - 1] = '\0';
                pathsLoaded = true;
            }
            ImGui::Text("Android SDK:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-110);
            ImGui::InputText("##sdk", sdkPath, sizeof(sdkPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##sdk")) {
                if (auto p = FileDialogs::PickFolder()) {
                    std::strncpy(sdkPath, p->c_str(), sizeof(sdkPath) - 1);
                    sdkPath[sizeof(sdkPath) - 1] = '\0';
                }
            }
            ImGui::Text("Android NDK:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-110);
            ImGui::InputText("##ndk", ndkPath, sizeof(ndkPath) - 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##ndk")) {
                if (auto p = FileDialogs::PickFolder()) {
                    std::strncpy(ndkPath, p->c_str(), sizeof(ndkPath) - 1);
                    ndkPath[sizeof(ndkPath) - 1] = '\0';
                }
            }

            ImGui::SeparatorText("Diagnostics");
            static bool writeLogFile = false;
            static char guiLogPath[512] = {};
            static bool logSettingsLoaded = false;
            if (!logSettingsLoaded) {
                const std::string activePath = logger.FilePath();
                const std::string configuredPath = activePath.empty()
                    ? appSettings.Get("DiagnosticLogPath", defaultLogPath)
                    : activePath;
                std::strncpy(guiLogPath, configuredPath.c_str(),
                             sizeof(guiLogPath) - 1);
                writeLogFile = !activePath.empty();
                logSettingsLoaded = true;
            }
            ImGui::Checkbox("Write diagnostics to file", &writeLogFile);
            ImGui::TextUnformatted("Log file:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-110);
            ImGui::InputText("##gui_log_path", guiLogPath, sizeof(guiLogPath));
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##gui_log")) {
                if (auto p = FileDialogs::SaveFile({{"Log file", "log"}})) {
                    ImGui::GetIO().ClearInputKeys();
                    std::strncpy(guiLogPath, p->c_str(), sizeof(guiLogPath) - 1);
                    guiLogPath[sizeof(guiLogPath) - 1] = '\0';
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                PathUtilsLite::SetSDKPath(sdkPath);
                PathUtilsLite::SetNDKPath(ndkPath);
                const std::string selectedPath = guiLogPath[0]
                    ? std::string(guiLogPath) : defaultLogPath;
                AppSettings logSettings;
                logSettings.Set("DiagnosticLogEnabled",
                                writeLogFile ? "true" : "false");
                logSettings.Set("DiagnosticLogPath", selectedPath);
                logSettings.Sync();
                if (writeLogFile) {
                    if (logger.StartFile(selectedPath))
                        LOLI_INFO("settings") << "File logging enabled: " << selectedPath;
                    else
                        LOLI_ERROR("settings") << "Cannot open log file: " << selectedPath;
                } else {
                    LOLI_INFO("settings") << "File logging disabled";
                    logger.StopFile();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // The capture toolbar remains active; inspection panels wait for a
        // completed capture. Disabling items also blocks keyboard navigation.
        const bool maskCaptureView = bridge.ShouldMaskCaptureView();
        if (maskCaptureView)
            ImGui::BeginDisabled();

        // Dockable panels
        if (showStacktrace && ImGui::Begin("Stacktrace", nullptr,
                                         ImGuiWindowFlags_NoScrollbar)) {
            if (!timelineView.hasSelection || rangeReady) {
                const bool rangeView = timelineView.hasSelection;
                const bool liveView = showPersistent &&
                    (rangeView ? hasRangeLiveTree : hasLiveTree);
                gui::StacktraceTree& visibleTree = rangeView
                    ? (liveView && !rangeLiveAliasesAll ? rangeLiveStacktraceTree : rangeStacktraceTree)
                    : (liveView && !liveTreeAliasesAll ? liveStacktraceTree : stacktraceTree);
                const bool previousPersistent = showPersistent;
                DrawStacktracePanel(snapshot, visibleTree, liveView,
                                    showPersistent,
                                    rangeView ? hasRangeLiveTree : hasLiveTree,
                                    stacktraceFilter, sizeof(stacktraceFilter),
                                    stacktraceContextNode);
                if (previousPersistent != showPersistent)
                    treemapState.focusedNode = -1;
            }
        }
        ImGui::End();

        // Timeline + Screenshot live in the bottom band (fixed height by the dock
        // split); Stacktrace + Treemap occupy the top band and stretch on resize.
        if (showTimeline) {
            if (ImGui::Begin("Timeline")) {
                screenshotHoverIdx = -1;
                gui::DrawMemoryTimelineChart(snapshot, timelineView, &screenshotHoverIdx);
                if (timelineView.hasSelection &&
                    (!rangeRequested || requestedRangeStart != timelineView.selStartMs ||
                     requestedRangeEnd != timelineView.selEndMs)) {
                    requestedRangeStart = timelineView.selStartMs;
                    requestedRangeEnd = timelineView.selEndMs;
                    rangeRequested = bridge.RequestTimeRangeTrees(
                        requestedRangeStart, requestedRangeEnd);
                    rangeReady = false;
                    treemapState.focusedNode = -1;
                } else if (!timelineView.hasSelection && rangeRequested) {
                    rangeRequested = false;
                    rangeReady = false;
                    rangeStacktraceTree.Clear();
                    rangeLiveStacktraceTree.Clear();
                    rangeLiveAliasesAll = false;
                    treemapState.focusedNode = -1;
                }
            }
            ImGui::End();
        }

        if (showTreemap && ImGui::Begin("Treemap", nullptr,
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse)) {
            if (!timelineView.hasSelection || rangeReady) {
                const bool rangeView = timelineView.hasSelection;
                const bool liveView = showPersistent &&
                    (rangeView ? hasRangeLiveTree : hasLiveTree);
                gui::StacktraceTree& visibleTree = rangeView
                    ? (liveView && !rangeLiveAliasesAll ? rangeLiveStacktraceTree : rangeStacktraceTree)
                    : (liveView && !liveTreeAliasesAll ? liveStacktraceTree : stacktraceTree);
                gui::DrawTreemapPanel(visibleTree, treemapState,
                                      bridge.SnapshotVersion() * 4 +
                                          (rangeView ? 2 : 0) + (liveView ? 1 : 0) +
                                          (rangeTreeVersion << 32),
                                      dpiScale, liveView);
            }
        }
        ImGui::End();

        if (leakWindowOpen) {
            ImGui::SetNextWindowSize(ImVec2(980, 650), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Leaks", &leakWindowOpen)) {
                ImGui::Text("%.1f-%.1fs | %s", leakStartMs / 1000.0,
                            leakEndMs / 1000.0,
                            leakPersistentOnly ? "Persistent" : "All allocations");
                ImGui::SameLine();
                if (ImGui::SmallButton(leakTreeView ? "Show Treemap" : "Show Tree"))
                    leakTreeView = !leakTreeView;
                ImGui::Separator();
                if (leakAnalysisRunning) {
                    ImGui::TextUnformatted("Analyzing callstack growth...");
                } else if (!leakAnalysisError.empty()) {
                    ImGui::TextWrapped("%s", leakAnalysisError.c_str());
                } else if (leakTree) {
                    if (leakTreeView)
                        DrawLeakTreePanel(*leakTree, leakSelectedNode);
                    else
                        gui::DrawTreemapPanel(*leakTree, leakTreemapState,
                                              leakResultVersion, dpiScale, false);
                }
            }
            ImGui::End();
        }

        if (showSmaps && ImGui::Begin("Smaps")) {
            gui::DrawSmapsPanel(snapshot);
        }
        ImGui::End();

        if (showScreenshot) {
            if (ImGui::Begin("Screenshot")) {
                // Hovering the timeline scrubs to the nearest capture; once
                // set, the view sticks until the next hover (Qt parity).
                if (screenshotHoverIdx >= 0)
                    screenshotViewIdx = screenshotHoverIdx;
                // If a new session replaced the screenshots, drop stale
                // textures and fall back to the latest capture.
                if (screenshotViewIdx >= static_cast<int>(snapshot.screenshots.size())) {
                    screenshotTextures.clear();
                    screenshotViewIdx = -1;
                }
                DrawScreenshotPanel(snapshot, screenshotTextures, screenshotViewIdx);
            }
            ImGui::End();
        }

        if (showConsole) {
            if (ImGui::Begin("Console"))
                DrawConsolePanel(consoleLines, consoleCursor);
            ImGui::End();
        }
        if (activateTimelineAfterLayout) {
            ImGui::SetWindowFocus("Timeline");
            activateTimelineAfterLayout = false;
        }
        if (maskCaptureView)
            ImGui::EndDisabled();
        if (maskCaptureView) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
            if (ImGui::Begin("##CaptureWorkspaceMask", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoBackground)) {
                const ImVec2 top = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    top, ImVec2(top.x + size.x, top.y + size.y),
                    IM_COL32(0, 0, 0, 115));
            }
            ImGui::End();
        }

        // Loading overlay: modal progress while a record parses on the worker
        // thread, so the UI never appears frozen. We open it once on the loading
        // rising edge and close it once on the falling edge - calling OpenPopup
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
                ImGui::EndPopup();
            }
        }
        if (ImGui::BeginPopupModal("Capture launch failed", nullptr,
                                  ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(520.0f);
            ImGui::TextUnformatted(launchErrorModal.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TextDisabled("Check the device and close other programs using ADB before retrying.");
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
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
            if (!selfTestSavePath.empty() && !bridge.IsLoading()) {
                if (!selfTestSaveRequested && !snapshot.records.empty()) {
                    selfTestSaveRequested = true;
                    if (!bridge.SaveRecord(selfTestSavePath)) {
                        std::fprintf(stderr, "[selftest-save] FAIL: save rejected\n");
                        selfTestExitCode = 1;
                        selfTestRangeLeaksDone = true;
                    }
                } else if (selfTestSaveRequested && !bridge.IsSaving()) {
                    if (bridge.LastSaveResult()) {
                        std::printf("[selftest-save] PASS: %s\n",
                                    selfTestSavePath.c_str());
                    } else {
                        std::fprintf(stderr, "[selftest-save] FAIL: write failed\n");
                        selfTestExitCode = 1;
                    }
                    selfTestRangeLeaksDone = true;
                }
                if (selfTestClock.getElapsedTime().asSeconds() > 180.0f) {
                    std::fprintf(stderr, "[selftest-save] FAIL: timeout\n");
                    selfTestExitCode = 1;
                    selfTestRangeLeaksDone = true;
                }
                std::fflush(stdout);
                std::fflush(stderr);
            }
            if (selfTestRangeLeaks && !bridge.IsLoading()) {
                if (rangeLeakTestStage == 0 && !snapshot.records.empty() &&
                    snapshot.memTimeline.size() >= 10 &&
                    !stacktraceTree.Nodes().empty()) {
                    const auto& samples = snapshot.memTimeline;
                    timelineView.selStartMs = samples[samples.size() / 10].timeMs;
                    timelineView.selEndMs = samples[samples.size() * 9 / 10].timeMs;
                    timelineView.hasSelection = true;
                    rangeLeakOriginalNodes = stacktraceTree.Nodes().size();
                    rangeLeakTestStage = 1;
                    std::printf("[selftest-range-leaks] selecting %.1f-%.1fs\n",
                                timelineView.selStartMs / 1000.0,
                                timelineView.selEndMs / 1000.0);
                } else if (rangeLeakTestStage == 1 && rangeReady) {
                    if (rangeRecordCount == 0 || rangeStacktraceTree.Nodes().empty()) {
                        std::fprintf(stderr, "[selftest-range-leaks] FAIL: empty range result\n");
                        selfTestExitCode = 1;
                        selfTestRangeLeaksDone = true;
                    } else if (bridge.RequestPossibleLeaks(
                                   static_cast<int32_t>(timelineView.selStartMs),
                                   static_cast<int32_t>(timelineView.selEndMs), false)) {
                        leakAnalysisRunning = true;
                        rangeLeakTestStage = 2;
                        std::printf("[selftest-range-leaks] range ready: %zu records, %zu nodes\n",
                                    rangeRecordCount, rangeStacktraceTree.Nodes().size());
                    } else {
                        std::fprintf(stderr, "[selftest-range-leaks] FAIL: leak request rejected\n");
                        selfTestExitCode = 1;
                        selfTestRangeLeaksDone = true;
                    }
                } else if (rangeLeakTestStage == 2 && !leakAnalysisRunning) {
                    if (!leakAnalysisError.empty() || !leakTree || leakTree->Nodes().empty()) {
                        std::fprintf(stderr, "[selftest-range-leaks] FAIL: leak result empty or failed: %s\n",
                                     leakAnalysisError.c_str());
                        selfTestExitCode = 1;
                        selfTestRangeLeaksDone = true;
                    } else {
                        std::printf("[selftest-range-leaks] leak ready: %zu nodes\n",
                                    leakTree->Nodes().size());
                        timelineView.hasSelection = false;
                        rangeLeakTestStage = 3;
                    }
                } else if (rangeLeakTestStage == 3 && !rangeRequested &&
                           !rangeReady && stacktraceTree.Nodes().size() ==
                               rangeLeakOriginalNodes) {
                    std::printf("[selftest-range-leaks] PASS: clear restored %zu nodes in %.1fs\n",
                                rangeLeakOriginalNodes,
                                selfTestClock.getElapsedTime().asSeconds());
                    selfTestRangeLeaksDone = true;
                    rangeLeakTestStage = 4;
                }
                if (selfTestClock.getElapsedTime().asSeconds() > 180.0f &&
                    rangeLeakTestStage != 4) {
                    std::fprintf(stderr, "[selftest-range-leaks] FAIL: timeout at stage %d\n",
                                 rangeLeakTestStage);
                    selfTestExitCode = 1;
                    selfTestRangeLeaksDone = true;
                }
                std::fflush(stdout);
                std::fflush(stderr);
            }
            // Exit once we've rendered enough frames past load to capture the
            // fully-populated UI (a small count is enough for a screenshot).
            if (!selfTestRangeLeaks && selfTestSavePath.empty() &&
                !bridge.IsLoading() && framesAfterLoad > 40) {
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
        if (selfTestRangeLeaksDone)
            window.close();

        const double frameMs = frameClock.getElapsedTime().asSeconds() * 1000.0;
        if (selfTest && frameMs > worstFrameMs)
            worstFrameMs = frameMs;
        const auto frameNow = loli::LoliLogger::Clock::now();
        if (frameMs >= 100.0 && frameNow - lastSlowFrameLog >= std::chrono::seconds(1)) {
            lastSlowFrameLog = frameNow;
            loli::LoliLogger::Instance().Log("frame",
                "slow_frame_ms=" + std::to_string(frameMs) +
                " records=" + std::to_string(snapshot.records.size()) +
                " tree_nodes=" + std::to_string(stacktraceTree.Nodes().size()));
        }

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
        if (selfTest && !selfTestRangeLeaks && !selfTestCaptureClock &&
            selfTestSavePath.empty() &&
            screenshotPath.empty() &&
            !bridge.IsLoading() && framesAfterLoad > 40)
            window.close();
        // A fast offscreen window can render 20,000 frames while a large
        // record still loads. Bound the harness by elapsed time instead.
        if (selfTest && selfTestClock.getElapsedTime().asSeconds() > 600.0f)
            window.close();
    }

    if (symbolizeRunning && symbolizeProcess) {
        symbolizeProcess->Kill();
        if (symbolizeFuture.valid())
            symbolizeFuture.wait();
    }
    FileDialogs::Shutdown();
    gui::FreeTreemapState(treemapState);
    gui::FreeTreemapState(leakTreemapState);
    ImGui::SFML::Shutdown();
    return selfTestExitCode;
}
