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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gui {

namespace {

struct Rect { float x, y, w, h; };
struct Cell { int32_t nodeIndex; int32_t depth; Rect rect; };

// Formats bytes into a human-readable string ("564.3 MB"), matching the
// original Qt treemap title ("<size>: <name>").
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

// Title strip height: font line plus a little padding, DPI-scaled.
float TitleHeight(float fontSize, float dpiScale) {
    return fontSize + 4.0f * dpiScale;
}

// Squarified treemap layout (Bruls-Huizing-van Wijk). Lays out `items` into
// `rect`, producing one Cell per item at `depth`, then recurses into each
// cell's children. Unlike a flat squarify, EVERY node (including parents)
// emits its own visible cell: the cell's rect is kept whole, a title strip is
// reserved at its top, and the children are laid out in the remaining body
// area below the strip — matching the original Qt nested treemap.
// `pad` insets children inside the parent's body so borders stay visible.
void Squarify(const std::vector<int32_t>& items, const StacktraceTree& tree,
              Rect rect, int depth, int maxDepth, float pad, float titleH,
              std::vector<Cell>& out) {
    if (items.empty())
        return;

    // Emit a cell for each item and recurse into children inside its body.
    auto layoutRow = [&](const std::vector<int32_t>& row, Rect rowRect) {
        const bool horizontal = rowRect.w >= rowRect.h;
        double rowSum = 0.0;
        for (int32_t idx : row) rowSum += (double)tree.NodeAt(idx).totalSize;
        if (rowSum <= 0.0) return;
        double offset = horizontal ? rowRect.y : rowRect.x;
        for (int32_t idx : row) {
            const double frac = (double)tree.NodeAt(idx).totalSize / rowSum;
            const double span = (horizontal ? rowRect.h : rowRect.w) * frac;
            Rect cell = horizontal
                ? Rect{ rowRect.x, (float)offset, (float)rowRect.w, (float)span }
                : Rect{ (float)offset, rowRect.y, (float)span, (float)rowRect.h };
            offset += span;

            // The cell rect is the node's OWN rect (frame + title drawn here).
            if (cell.w <= 1.0f || cell.h <= 1.0f)
                continue;
            out.push_back({ idx, depth, cell });

            // Children are laid out in the body area below the title strip.
            if (depth + 1 < maxDepth) {
                const auto& node = tree.NodeAt(idx);
                if (!node.children.empty()) {
                    Rect body{ cell.x + pad, cell.y + titleH,
                               cell.w - 2 * pad, cell.h - titleH - pad };
                    if (body.w > 2 * pad + 1 && body.h > 2 * pad + 1)
                        Squarify(node.children, tree, body, depth + 1, maxDepth,
                                 pad, titleH, out);
                }
            }
        }
    };

    // Work on a copy sorted by descending size (stable squarify input).
    std::vector<int32_t> sorted = items;
    std::sort(sorted.begin(), sorted.end(), [&](int32_t a, int32_t b) {
        return tree.NodeAt(a).totalSize > tree.NodeAt(b).totalSize;
    });

    Rect remaining = rect;
    size_t i = 0;
    while (i < sorted.size() && remaining.w > 2 && remaining.h > 2) {
        const bool horizontal = remaining.w >= remaining.h;
        const double length = horizontal ? remaining.h : remaining.w;
        double total = 0.0;
        for (size_t k = i; k < sorted.size(); k++)
            total += (double)tree.NodeAt(sorted[k]).totalSize;
        if (total <= 0.0) break;

        // Greedily pack a row while the worst aspect ratio keeps improving.
        std::vector<int32_t> row;
        std::vector<double> rowAreas;
        double rowSum = 0.0, worst = 1e300;
        size_t j = i;
        while (j < sorted.size()) {
            double area = (double)tree.NodeAt(sorted[j]).totalSize;
            double newWorst = WorstAspect(rowAreas, rowSum, area, length);
            if (newWorst > worst && !row.empty()) break;
            row.push_back(sorted[j]);
            rowAreas.push_back(area);
            rowSum += area;
            worst = newWorst;
            j++;
        }

        // Carve the row off the remaining rect.
        const double rowFrac = rowSum / total;
        Rect rowRect, rest;
        if (horizontal) {
            const float rowW = (float)(remaining.w * rowFrac);
            rowRect = { remaining.x, remaining.y, rowW, remaining.h };
            rest    = { remaining.x + rowW, remaining.y, remaining.w - rowW, remaining.h };
        } else {
            const float rowH = (float)(remaining.h * rowFrac);
            rowRect = { remaining.x, remaining.y, remaining.w, rowH };
            rest    = { remaining.x, remaining.y + rowH, remaining.w, remaining.h - rowH };
        }
        layoutRow(row, rowRect);
        remaining = rest;
        i = j;
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
                      uint64_t dataVersion, float dpiScale) {
    if (tree.Nodes().empty()) {
        ImGui::TextUnformatted("No data. Load a record or run a capture.");
        return;
    }

    // Reserve a bottom bar for the controls (Depth slider + Up button).
    const float controlBarH = ImGui::GetFrameHeightWithSpacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(8, (int)avail.x);
    const int h = std::max(8, (int)(avail.y - controlBarH));

    // Decide whether we must re-render the textures.
    const bool needRender =
        state.builtForVersion != dataVersion ||
        state.builtFocus != state.focusedNode ||
        state.builtDepth != state.maxDepth ||
        state.builtDpi != dpiScale ||
        state.texW != w || state.texH != h ||
        state.displayTex == nullptr || state.pickTex == nullptr;

    if (needRender) {
        sf::RenderTexture* disp = EnsureTexture(state.displayTex, w, h);
        sf::RenderTexture* pick = EnsureTexture(state.pickTex, w, h);
        if (disp && pick) {
            state.texW = w;
            state.texH = h;

            // Child padding + border scale with DPI so separation stays visible
            // on HiDPI displays.
            const float pad = 2.0f * dpiScale;
            const float fontSize = 12.0f * dpiScale;
            const float titleH = TitleHeight(fontSize, dpiScale);

            // Compute layout once (nested cells with title strips).
            std::vector<Cell> cells;
            std::vector<int32_t> top;
            if (state.focusedNode >= 0) top.push_back(state.focusedNode);
            else top = tree.Roots();
            Squarify(top, tree, { 0, 0, (float)w, (float)h }, 0, state.maxDepth,
                     pad, titleH, cells);

            // Load a font once for cell labels (Segoe UI on Windows, matching the
            // ImGui UI font). Static so it persists across renders.
            static sf::Font font;
            static bool fontLoaded = false;
            if (!fontLoaded) {
                fontLoaded = font.openFromFile("C:/Windows/Fonts/segoeui.ttf");
            }

            // Display layer.
            disp->clear(sf::Color(24, 24, 28));

            for (const auto& c : cells) {
                const auto& node = tree.NodeAt(c.nodeIndex);
                sf::Color base = DisplayColor(node.id, c.depth);

                // Body fill (slightly darkened so the frame/title read on top).
                sf::RectangleShape body(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                     std::max(0.0f, c.rect.h - 1)));
                body.setPosition({ c.rect.x, c.rect.y });
                sf::Color bodyCol = sf::Color((uint8_t)(base.r * 0.82f),
                                              (uint8_t)(base.g * 0.82f),
                                              (uint8_t)(base.b * 0.82f), 255);
                body.setFillColor(bodyCol);
                disp->draw(body);

                // Title strip at the top of the cell (darker band), matching the
                // original Qt treemap's nested look.
                if (c.rect.h > titleH + 2.0f && c.rect.w > 4.0f * dpiScale) {
                    sf::RectangleShape band(sf::Vector2f(c.rect.w - 1, titleH));
                    band.setPosition({ c.rect.x, c.rect.y });
                    sf::Color bandCol = sf::Color((uint8_t)(base.r * 0.55f),
                                                  (uint8_t)(base.g * 0.55f),
                                                  (uint8_t)(base.b * 0.55f), 255);
                    band.setFillColor(bandCol);
                    disp->draw(band);
                }

                // Border so nesting is clearly visible.
                sf::RectangleShape frame(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                      std::max(0.0f, c.rect.h - 1)));
                frame.setPosition({ c.rect.x, c.rect.y });
                frame.setFillColor(sf::Color::Transparent);
                frame.setOutlineThickness(1.0f);
                frame.setOutlineColor(sf::Color(15, 15, 18));
                disp->draw(frame);

                // Title text "<size>: <name>", truncated with "..." to fit.
                if (fontLoaded && c.rect.h > titleH + 2.0f && c.rect.w > 4.0f * dpiScale) {
                    std::string label = FormatBytes(node.totalSize) + ": " +
                                        (tree.PoolStr(node.funcName) ? tree.PoolStr(node.funcName) : "");
                    const float availW = c.rect.w - 6.0f * dpiScale;
                    const unsigned fsize = (unsigned)std::max(8.0f, fontSize);
                    auto textW = [&](const std::string& s) {
                        sf::Text t(font, sf::String::fromUtf8(s.begin(), s.end()), fsize);
                        return t.getLocalBounds().size.x;
                    };
                    if (!label.empty() && textW(label) > availW) {
                        const std::string ell = "...";
                        while (label.size() > 1 && textW(label + ell) > availW)
                            label.pop_back();
                        label += ell;
                    }
                    sf::Text text(font, sf::String::fromUtf8(label.begin(), label.end()), fsize);
                    text.setPosition({ c.rect.x + 3.0f * dpiScale, c.rect.y + 1.0f * dpiScale });
                    text.setFillColor(sf::Color(255, 255, 255, 240));
                    disp->draw(text);
                }
            }
            disp->display();

            // Picking layer (cell -> nodeIndex color), matching display rects.
            // Parents are drawn first, then children on top, so a pixel resolves
            // to the deepest (innermost) cell under the cursor.
            pick->clear(sf::Color::Black);
            for (const auto& c : cells) {
                sf::RectangleShape r(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                  std::max(0.0f, c.rect.h - 1)));
                r.setPosition({ c.rect.x, c.rect.y });
                r.setFillColor(PickColor(c.nodeIndex));
                pick->draw(r);
            }
            pick->display();

            state.builtForVersion = dataVersion;
            state.builtFocus = state.focusedNode;
            state.builtDepth = state.maxDepth;
            state.builtDpi = dpiScale;
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
        ImGui::Text("%s, %u allocs",
                    FormatBytes(node.totalSize).c_str(), node.allocCount);
        if (!node.children.empty())
            ImGui::TextDisabled("(click to zoom, right-click to go up)");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !node.children.empty())
            state.focusedNode = hoveredNode;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && state.focusedNode >= 0)
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;

    // Bottom control bar: Depth slider stretched full width, Up button at right.
    ImGui::Separator();
    const float upW = ImGui::CalcTextSize("Up").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x - upW - ImGui::GetStyle().ItemSpacing.x));
    ImGui::SliderInt("Depth", &state.maxDepth, 1, 12);
    ImGui::SameLine();
    if (state.focusedNode < 0)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Up")) {
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;
    }
    if (state.focusedNode < 0)
        ImGui::EndDisabled();
}

void FreeTreemapState(TreemapState& state) {
    delete state.displayTex; state.displayTex = nullptr;
    delete state.pickTex;    state.pickTex = nullptr;
}

} // namespace gui
