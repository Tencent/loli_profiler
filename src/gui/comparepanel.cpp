#include "comparetreestate.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

namespace gui {
namespace {
std::string Details(const loli::ComparisonNode& node) {
    auto a = node.Metrics(loli::ComparisonView::Base);
    auto b = node.Metrics(loli::ComparisonView::Comparison);
    auto d = node.Metrics(loli::ComparisonView::Diff);
    auto s = node.Metrics(loli::ComparisonView::Diff, true);
    return node.function + " [" + node.library + "]\nBase: " + std::to_string(a.bytes) + " bytes, " + std::to_string(a.count) +
        " allocations\nComparison: " + std::to_string(b.bytes) + " bytes, " + std::to_string(b.count) +
        " allocations\nDiff: " + std::to_string(d.bytes) + " bytes, " + std::to_string(d.count) +
        " allocations\nSelf diff: " + std::to_string(s.bytes) + " bytes, " + std::to_string(s.count) + " allocations";
}
}
static void DrawTreeTable(const loli::ComparisonResult& result, loli::ComparisonView view,
                          CompareTreeState& state, float height, const char* emptyMessage) {
    if (state.Rows().empty()) {
        ImGui::BeginChild("##emptytree", ImVec2(-FLT_MIN, height), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(emptyMessage ? emptyMessage :
            (view == loli::ComparisonView::Diff ? "No allocation differences." : "No live allocations."));
        ImGui::EndChild();
        return;
    }
    const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                       ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("tree", 4, flags, ImVec2(-FLT_MIN, height))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    float functionWidth = std::max(140.0f, ImGui::GetContentRegionAvail().x - 300.0f);
    // Widen to reveal long matching names, just as the main stacktrace view does.
    if (state.selected >= 0) {
        for (const auto& row : state.Rows()) if (row.node == state.selected)
            functionWidth = std::max(functionWidth, row.depth * ImGui::GetTreeNodeToLabelSpacing() +
                ImGui::CalcTextSize(result.nodes[row.node].function.c_str()).x + 40.0f);
    }
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthFixed, functionWidth);
    ImGui::TableSetupColumn(view == loli::ComparisonView::Diff ? "Delta bytes" : "Live bytes",
        ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 100);
    ImGui::TableSetupColumn(view == loli::ComparisonView::Diff ? "Delta count" : "Count", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthFixed, 100);
    ImGui::TableHeadersRow();
    if (auto* sort = ImGui::TableGetSortSpecs()) {
        if (sort->SpecsDirty && sort->SpecsCount) {
            state.Sort(sort->Specs[0].ColumnIndex, sort->Specs[0].SortDirection == ImGuiSortDirection_Descending);
            sort->SpecsDirty = false;
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(int(state.Rows().size()));
    if (state.scrollToSelection) {
        for (size_t i = 0; i < state.Rows().size(); ++i)
            if (state.Rows()[i].node == state.selected) clipper.IncludeItemByIndex(int(i));
    }
    int32_t toggle = -1;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto row = state.Rows()[i];
            const auto& node = result.nodes[row.node];
            const auto metrics = node.Metrics(view);
            ImGui::PushID(row.node);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row.depth * ImGui::GetTreeNodeToLabelSpacing());
            if (view == loli::ComparisonView::Diff && metrics.bytes)
                ImGui::PushStyleColor(ImGuiCol_Text, metrics.bytes > 0 ? ImVec4(0.96f, 0.35f, 0.30f, 1) : ImVec4(0.25f, 0.85f, 0.45f, 1));
            auto treeFlags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!row.hasChildren) treeFlags |= ImGuiTreeNodeFlags_Leaf;
            if (state.selected == row.node) treeFlags |= ImGuiTreeNodeFlags_Selected;
            ImGui::SetNextItemOpen(state.Expanded(row.node), ImGuiCond_Always);
            const bool open = ImGui::TreeNodeEx("##node", treeFlags, "%s", node.function.c_str());
            if (row.hasChildren && open != state.Expanded(row.node)) toggle = row.node;
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) state.selected = row.node;
            if (state.scrollToSelection && state.selected == row.node) {
                ImGui::SetScrollHereY(0.5f);
                ImGui::SetScrollHereX(0.5f);
                state.scrollToSelection = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Details(node).c_str());
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Copy node")) ImGui::SetClipboardText(Details(node).c_str());
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(loli::FormatComparisonBytes(metrics.bytes, view == loli::ComparisonView::Diff).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Details(node).c_str());
            ImGui::TableNextColumn();
            ImGui::Text(view == loli::ComparisonView::Diff ? "%+lld" : "%lld", (long long)metrics.count);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(node.library.c_str());
            if (view == loli::ComparisonView::Diff && metrics.bytes) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    if (toggle >= 0) state.Toggle(toggle);
}

void DrawComparisonTree(const loli::ComparisonResult& result, loli::ComparisonView view,
                        CompareTreeState& state, const char* emptyMessage) {
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() +
                               ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().SeparatorSize;
    DrawTreeTable(result, view, state, std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight), emptyMessage);
    ImGui::Separator();
    const std::string previous = state.searchText;
    const std::string count = std::to_string(state.MatchIndex() + 1) + " / " + std::to_string(state.Matches().size());
    const float navWidth = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x -
        2.0f * navWidth - ImGui::CalcTextSize(count.c_str()).x - 3.0f * ImGui::GetStyle().ItemSpacing.x));
    const bool enter = ImGui::InputTextWithHint("##search", "search nodes...", state.searchText, sizeof(state.searchText),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
    if (previous != state.searchText) state.Search(std::string(state.searchText));
    if (enter) { state.NextMatch(1); ImGui::SetKeyboardFocusHere(-1); }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.Matches().empty());
    if (ImGui::Button("<", ImVec2(navWidth, 0))) state.NextMatch(-1);
    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(navWidth, 0))) state.NextMatch(1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%d / %zu", state.MatchIndex() + 1, state.Matches().size());
}
} // namespace gui
