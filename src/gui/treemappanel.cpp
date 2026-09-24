#include "treemappanel.h"

#include "imgui.h"
#include "stacktracetree.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gui {

namespace {

struct Rect { float x, y, w, h; };

struct Cell {
    int32_t nodeIndex;
    int32_t depth;
    Rect    rect;
};

// Deterministic color from a node id (golden-ratio hue walk), shaded by depth.
ImU32 ColorForNode(uint64_t id, int depth) {
    float hue = std::fmod(static_cast<float>(id) * 0.618034f, 1.0f);
    float sat = 0.55f;
    float val = std::max(0.35f, 0.85f - depth * 0.12f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

// Squarified treemap layout (ported from the Qt TreeMap::Tessellate).
double WorstAspect(const std::vector<double>& row, double groupSum, double proposed,
                   double length, double currentWorst) {
    double sum = groupSum + proposed;
    double maxA = proposed, minA = proposed;
    for (double a : row) { maxA = std::max(maxA, a); minA = std::min(minA, a); }
    double len2 = length * length;
    double sum2 = sum * sum;
    double worstMax = (len2 * maxA) / sum2;
    double worstMin = sum2 / (len2 * minA);
    return std::max(worstMax, worstMin);
}

void Squarify(const std::vector<int32_t>& items, const StacktraceTree& tree,
              Rect rect, int depth, int maxDepth, std::vector<Cell>& out) {
    if (items.empty() || rect.w <= 1.0f || rect.h <= 1.0f)
        return;

    // areas proportional to totalSize
    std::vector<double> areas;
    areas.reserve(items.size());
    double total = 0.0;
    for (int32_t idx : items) {
        double a = static_cast<double>(tree.NodeAt(idx).totalSize);
        areas.push_back(a);
        total += a;
    }
    if (total <= 0.0)
        return;

    const bool horizontal = rect.w >= rect.h;
    const double length = horizontal ? rect.h : rect.w;

    // greedily accept items into a row while aspect ratio improves
    std::vector<double> rowAreas;
    std::vector<int32_t> rowItems;
    double groupSum = 0.0, worst = 1e300;
    size_t i = 0;
    std::vector<int32_t> remaining = items;

    while (!remaining.empty()) {
        int32_t idx = remaining.front();
        double area = static_cast<double>(tree.NodeAt(idx).totalSize);
        double newWorst = WorstAspect(rowAreas, groupSum, area, length, worst);
        if (newWorst > worst && !rowAreas.empty())
            break;
        rowAreas.push_back(area);
        rowItems.push_back(idx);
        groupSum += area;
        worst = newWorst;
        remaining.erase(remaining.begin());
    }

    // lay out the accepted row
    double rowWidth = groupSum / length;
    double offset = horizontal ? rect.y : rect.x;
    for (size_t k = 0; k < rowItems.size(); k++) {
        double h = rowAreas[k] / rowWidth;
        Rect cell;
        if (horizontal) cell = { rect.x, (float)offset, (float)rowWidth, (float)h };
        else            cell = { (float)offset, rect.y, (float)h, (float)rowWidth };
        offset += h;

        out.push_back({ rowItems[k], depth, cell });

        // recurse into children
        if (depth + 1 < maxDepth) {
            const auto& node = tree.NodeAt(rowItems[k]);
            if (!node.children.empty()) {
                Rect inner = { cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4 };
                Squarify(node.children, tree, inner, depth + 1, maxDepth, out);
            }
        }
    }

    // recurse on the remaining strip
    Rect rest;
    if (horizontal) rest = { rect.x + (float)rowWidth, rect.y, rect.w - (float)rowWidth, rect.h };
    else            rest = { rect.x, rect.y + (float)rowWidth, rect.w, rect.h - (float)rowWidth };
    Squarify(remaining, tree, rest, depth, maxDepth, out);
}

} // namespace

void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state) {
    if (tree.Nodes().empty()) {
        ImGui::TextUnformatted("No data. Load a record or run a capture.");
        return;
    }

    ImGui::SliderInt("Depth", &state.maxDepth, 1, 8);
    if (state.focusedNode >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            state.focusedNode = tree.NodeAt(state.focusedNode).parent;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", tree.PoolStr(tree.NodeAt(state.focusedNode).funcName));
    }

    // choose top-level items: roots, or children of the focused node
    std::vector<int32_t> top;
    if (state.focusedNode >= 0) {
        top.push_back(state.focusedNode);
    } else {
        top = tree.Roots();
    }
    if (top.empty()) {
        ImGui::TextUnformatted("Nothing to display at this level.");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##treemap_canvas", avail);
    const bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::vector<Cell> cells;
    Rect bounds{ origin.x, origin.y, avail.x, avail.y };
    Squarify(top, tree, bounds, 0, state.maxDepth, cells);

    state.hoveredNode = -1;
    for (const auto& cell : cells) {
        const auto& node = tree.NodeAt(cell.nodeIndex);
        ImU32 col = ColorForNode(node.id, cell.depth);
        ImVec2 p0(cell.rect.x, cell.rect.y);
        ImVec2 p1(cell.rect.x + cell.rect.w, cell.rect.y + cell.rect.h);
        dl->AddRectFilled(p0, p1, col);
        dl->AddRect(p0, p1, IM_COL32(20, 20, 20, 255), 0.0f, 0, 1.0f);

        const bool cellHover = hovered &&
            mouse.x >= p0.x && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y;
        if (cellHover)
            state.hoveredNode = cell.nodeIndex;

        // label if the cell is big enough
        if (cell.rect.w > 50.0f && cell.rect.h > 18.0f) {
            dl->PushClipRect(p0, p1, true);
            dl->AddText(ImVec2(p0.x + 3, p0.y + 2), IM_COL32(255, 255, 255, 230),
                        tree.PoolStr(node.funcName));
            dl->PopClipRect();
        }
    }

    // tooltip + click-to-drill
    if (state.hoveredNode >= 0) {
        const auto& node = tree.NodeAt(state.hoveredNode);
        ImGui::BeginTooltip();
        ImGui::Text("%s", tree.PoolStr(node.funcName));
        if (node.library >= 0)
            ImGui::TextDisabled("%s", tree.PoolStr(node.library));
        ImGui::Text("%llu bytes, %u allocs",
                    (unsigned long long)node.totalSize, node.allocCount);
        if (!node.children.empty())
            ImGui::TextDisabled("(click to zoom)");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !node.children.empty())
            state.focusedNode = state.hoveredNode;
    }
    // right-click to go up one level
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && state.focusedNode >= 0)
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;
}

} // namespace gui
