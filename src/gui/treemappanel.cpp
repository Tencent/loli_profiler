#include "treemappanel.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include "stacktracetree.h"

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace gui {

namespace {

struct Rect { float x, y, w, h; };
struct Cell { int32_t nodeIndex; int32_t depth; Rect rect; };

// Deterministic display color from node id, shaded by depth.
sf::Color DisplayColor(uint64_t id, int depth) {
    float hue = std::fmod(static_cast<float>(id) * 0.618034f, 1.0f);
    float sat = 0.55f;
    float val = std::max(0.35f, 0.85f - depth * 0.12f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, b);
    return sf::Color((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// Picking color: encode nodeIndex+1 as RGB (so 0 = background "no node").
sf::Color PickColor(int32_t nodeIndex) {
    const uint32_t v = (uint32_t)nodeIndex + 1;
    return sf::Color((uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                     (uint8_t)((v >> 16) & 0xFF));
}

double WorstAspect(const std::vector<double>& row, double groupSum, double proposed,
                   double length) {
    double sum = groupSum + proposed;
    double maxA = proposed, minA = proposed;
    for (double a : row) { maxA = std::max(maxA, a); minA = std::min(minA, a); }
    double len2 = length * length, sum2 = sum * sum;
    return std::max((len2 * maxA) / sum2, sum2 / (len2 * minA));
}

void Squarify(const std::vector<int32_t>& items, const StacktraceTree& tree,
              Rect rect, int depth, int maxDepth, std::vector<Cell>& out) {
    if (items.empty() || rect.w <= 1.0f || rect.h <= 1.0f)
        return;
    double total = 0.0;
    for (int32_t idx : items) total += (double)tree.NodeAt(idx).totalSize;
    if (total <= 0.0) return;

    const bool horizontal = rect.w >= rect.h;
    const double length = horizontal ? rect.h : rect.w;

    std::vector<int32_t> remaining = items;
    while (!remaining.empty()) {
        std::vector<double> rowAreas;
        std::vector<int32_t> rowItems;
        double groupSum = 0.0, worst = 1e300;
        while (!remaining.empty()) {
            int32_t idx = remaining.front();
            double area = (double)tree.NodeAt(idx).totalSize;
            double newWorst = WorstAspect(rowAreas, groupSum, area, length);
            if (newWorst > worst && !rowAreas.empty()) break;
            rowAreas.push_back(area);
            rowItems.push_back(idx);
            groupSum += area;
            worst = newWorst;
            remaining.erase(remaining.begin());
        }
        double rowWidth = groupSum / length;
        double offset = horizontal ? rect.y : rect.x;
        for (size_t k = 0; k < rowItems.size(); k++) {
            double h = rowAreas[k] / rowWidth;
            Rect cell = horizontal
                ? Rect{ rect.x, (float)offset, (float)rowWidth, (float)h }
                : Rect{ (float)offset, rect.y, (float)h, (float)rowWidth };
            offset += h;
            out.push_back({ rowItems[k], depth, cell });
            if (depth + 1 < maxDepth) {
                const auto& node = tree.NodeAt(rowItems[k]);
                if (!node.children.empty())
                    Squarify(node.children, tree, { cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4 },
                             depth + 1, maxDepth, out);
            }
        }
        Rect rest = horizontal
            ? Rect{ rect.x + (float)rowWidth, rect.y, rect.w - (float)rowWidth, rect.h }
            : Rect{ rect.x, rect.y + (float)rowWidth, rect.w, rect.h - (float)rowWidth };
        Squarify(remaining, tree, rest, depth, maxDepth, out);
        break;  // Squarify above consumed the rest; loop guard
    }
}

// Lazily (re)create a render texture at the given size.
sf::RenderTexture* EnsureTexture(sf::RenderTexture*& tex, int w, int h) {
    if (!tex) tex = new sf::RenderTexture();
    if ((int)tex->getSize().x != w || (int)tex->getSize().y != h) {
        if (!tex->resize({ (unsigned)w, (unsigned)h }))
            return nullptr;
    }
    return tex;
}

} // namespace

void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state,
                      uint64_t dataVersion) {
    if (tree.Nodes().empty()) {
        ImGui::TextUnformatted("No data. Load a record or run a capture.");
        return;
    }

    // Controls (cheap widgets above the image).
    bool controlsChanged = false;
    if (ImGui::SliderInt("Depth", &state.maxDepth, 1, 10))
        controlsChanged = true;
    if (state.focusedNode >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            state.focusedNode = tree.NodeAt(state.focusedNode).parent;
            controlsChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", tree.PoolStr(tree.NodeAt(state.focusedNode).funcName));
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(8, (int)avail.x);
    const int h = std::max(8, (int)avail.y);

    // Decide whether we must re-render the textures.
    const bool needRender =
        state.builtForVersion != dataVersion ||
        state.builtFocus != state.focusedNode ||
        state.builtDepth != state.maxDepth ||
        state.texW != w || state.texH != h ||
        state.displayTex == nullptr || state.pickTex == nullptr ||
        controlsChanged;

    if (needRender) {
        sf::RenderTexture* disp = EnsureTexture(state.displayTex, w, h);
        sf::RenderTexture* pick = EnsureTexture(state.pickTex, w, h);
        if (disp && pick) {
            state.texW = w;
            state.texH = h;

            // Compute layout once.
            std::vector<Cell> cells;
            std::vector<int32_t> top;
            if (state.focusedNode >= 0) top.push_back(state.focusedNode);
            else top = tree.Roots();
            Squarify(top, tree, { 0, 0, (float)w, (float)h }, 0, state.maxDepth, cells);

            // Display layer.
            disp->clear(sf::Color(24, 24, 28));

            // Load a font once for cell labels (Segoe UI on Windows, matching the
            // ImGui UI font). Static so it persists across renders.
            static sf::Font font;
            static bool fontLoaded = false;
            if (!fontLoaded) {
                fontLoaded = font.openFromFile("C:/Windows/Fonts/segoeui.ttf");
            }

            for (const auto& c : cells) {
                const auto& node = tree.NodeAt(c.nodeIndex);
                sf::RectangleShape r(sf::Vector2f(c.rect.w - 1, c.rect.h - 1));
                r.setPosition({ c.rect.x, c.rect.y });
                r.setFillColor(DisplayColor(node.id, c.depth));
                r.setOutlineThickness(1.0f);
                r.setOutlineColor(sf::Color(16, 16, 16));
                disp->draw(r);

                // Cell label (top depth levels, only where the cell is big enough).
                if (fontLoaded && c.rect.w > 40.0f && c.rect.h > 16.0f && c.depth <= 2) {
                    const char* name = tree.PoolStr(node.funcName);
                    sf::Text text(font, sf::String::fromUtf8(name, name + std::strlen(name)),
                                  12u);
                    text.setPosition({ c.rect.x + 3.0f, c.rect.y + 1.0f });
                    text.setFillColor(sf::Color(255, 255, 255, 235));
                    // Clip: skip drawing if the text is wider than the cell.
                    if (text.getLocalBounds().size.x < c.rect.w - 6.0f)
                        disp->draw(text);
                }
            }
            disp->display();

            // Picking layer (cell -> nodeIndex color).
            pick->clear(sf::Color::Black);
            for (const auto& c : cells) {
                sf::RectangleShape r(sf::Vector2f(c.rect.w - 1, c.rect.h - 1));
                r.setPosition({ c.rect.x, c.rect.y });
                r.setFillColor(PickColor(c.nodeIndex));
                pick->draw(r);
            }
            pick->display();

            state.builtForVersion = dataVersion;
            state.builtFocus = state.focusedNode;
            state.builtDepth = state.maxDepth;
        }
    }

    if (!state.displayTex)
        return;

    // Present the cached display texture as a single ImGui image.
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(*state.displayTex, ImVec2((float)w, (float)h));

    // Picking: map mouse to a node via the pick texture pixel.
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsItemHovered();
    int32_t hoveredNode = -1;
    if (hovered && state.pickTex) {
        const int px = (int)(mouse.x - origin.x);
        const int py = (int)(mouse.y - origin.y);
        if (px >= 0 && py >= 0 && px < w && py < h) {
            sf::Image img = state.pickTex->getTexture().copyToImage();
            sf::Color c = img.getPixel({ (unsigned)px, (unsigned)py });
            uint32_t v = (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
            if (v > 0)
                hoveredNode = (int32_t)(v - 1);
        }
    }

    // Tooltip + interactions.
    if (hoveredNode >= 0 && hoveredNode < (int32_t)tree.Nodes().size()) {
        const auto& node = tree.NodeAt(hoveredNode);
        ImGui::BeginTooltip();
        ImGui::Text("%s", tree.PoolStr(node.funcName));
        if (node.library >= 0)
            ImGui::TextDisabled("%s", tree.PoolStr(node.library));
        ImGui::Text("%llu bytes, %u allocs",
                    (unsigned long long)node.totalSize, node.allocCount);
        if (!node.children.empty())
            ImGui::TextDisabled("(click to zoom, right-click to go up)");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !node.children.empty())
            state.focusedNode = hoveredNode;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && state.focusedNode >= 0)
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;
}

void FreeTreemapState(TreemapState& state) {
    delete state.displayTex; state.displayTex = nullptr;
    delete state.pickTex;    state.pickTex = nullptr;
}

} // namespace gui
