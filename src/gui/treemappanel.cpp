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

// Squarified treemap layout (Bruls-Huizing-van Wijk). Lays out `items` into
// `rect`, producing one Cell per item at `depth`, then recurses into each cell's
// children at depth+1. `gap` insets every cell so cells never touch (separation).
void Squarify(const std::vector<int32_t>& items, const StacktraceTree& tree,
              Rect rect, int depth, int maxDepth, float gap, std::vector<Cell>& out) {
    if (items.empty())
        return;

    // Emit a cell for each item and recurse into children.
    auto layoutRow = [&](const std::vector<int32_t>& row, Rect rowRect) {
        const bool horizontal = rowRect.w >= rowRect.h;
        double rowSum = 0.0;
        for (int32_t idx : row) rowSum += (double)tree.NodeAt(idx).totalSize;
        if (rowSum <= 0.0) return;
        const double cross = horizontal ? rowRect.w : rowRect.h;  // fixed dimension
        double offset = horizontal ? rowRect.y : rowRect.x;
        for (int32_t idx : row) {
            const double frac = (double)tree.NodeAt(idx).totalSize / rowSum;
            const double span = (horizontal ? rowRect.h : rowRect.w) * frac;
            Rect cell = horizontal
                ? Rect{ rowRect.x, (float)offset, (float)rowRect.w, (float)span }
                : Rect{ (float)offset, rowRect.y, (float)span, (float)rowRect.h };
            offset += span;
            // Inset by gap for separation.
            Rect g{ cell.x + gap, cell.y + gap, cell.w - 2 * gap, cell.h - 2 * gap };
            if (g.w > 1.0f && g.h > 1.0f) {
                out.push_back({ idx, depth, g });
                if (depth + 1 < maxDepth) {
                    const auto& node = tree.NodeAt(idx);
                    if (!node.children.empty())
                        Squarify(node.children, tree, g, depth + 1, maxDepth, gap, out);
                }
            }
        }
        (void)cross;
    };

    // Work on a copy sorted by descending size (stable squarify input).
    std::vector<int32_t> sorted = items;
    std::sort(sorted.begin(), sorted.end(), [&](int32_t a, int32_t b) {
        return tree.NodeAt(a).totalSize > tree.NodeAt(b).totalSize;
    });

    Rect remaining = rect;
    size_t i = 0;
    while (i < sorted.size() && remaining.w > 2 * gap + 1 && remaining.h > 2 * gap + 1) {
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

            // Inter-cell gap + border scale with DPI so separation stays visible
            // on HiDPI displays.
            const float gap = 2.0f * dpiScale;
            const float fontSize = 12.0f * dpiScale;

            // Compute layout once.
            std::vector<Cell> cells;
            std::vector<int32_t> top;
            if (state.focusedNode >= 0) top.push_back(state.focusedNode);
            else top = tree.Roots();
            Squarify(top, tree, { 0, 0, (float)w, (float)h }, 0, state.maxDepth, gap, cells);
            std::fprintf(stderr, "[treemap] cells=%zu top=%zu focus=%d depth=%d w=%d h=%d gap=%.1f\n",
                         cells.size(), top.size(), state.focusedNode, state.maxDepth, w, h, gap);
            std::fflush(stderr);

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
                sf::RectangleShape r(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                  std::max(0.0f, c.rect.h - 1)));
                r.setPosition({ c.rect.x, c.rect.y });
                r.setFillColor(DisplayColor(node.id, c.depth));
                r.setOutlineThickness(1.0f);
                r.setOutlineColor(sf::Color(10, 10, 12));
                disp->draw(r);

                // Title band: every cell that can fit text gets a title. A darker
                // band sits behind the label (like the reference treemap), and the
                // text is truncated with an ellipsis to fit the cell width — never
                // silently dropped.
                const float titleH = fontSize + 4.0f * dpiScale;
                if (fontLoaded && c.rect.h > titleH + 2.0f && c.rect.w > 4.0f * dpiScale) {
                    // Darker title strip at the top of the cell.
                    sf::RectangleShape band(sf::Vector2f(c.rect.w - 1, titleH));
                    band.setPosition({ c.rect.x, c.rect.y });
                    sf::Color bandCol = DisplayColor(node.id, c.depth);
                    bandCol = sf::Color((uint8_t)(bandCol.r * 0.55f), (uint8_t)(bandCol.g * 0.55f),
                                        (uint8_t)(bandCol.b * 0.55f), 255);
                    band.setFillColor(bandCol);
                    disp->draw(band);

                    // Truncate the label with an ellipsis to fit the cell width.
                    const char* name = tree.PoolStr(node.funcName);
                    std::string label = name ? name : "";
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

    // Bottom control bar: Depth slider + Up button.
    ImGui::Separator();
    ImGui::SetNextItemWidth(160.0f * dpiScale);
    if (ImGui::SliderInt("Depth", &state.maxDepth, 1, 12)) {
        // depth change forces a re-render (tracked via builtDepth).
    }
    ImGui::SameLine();
    if (state.focusedNode < 0)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Up")) {
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;
    }
    if (state.focusedNode < 0)
        ImGui::EndDisabled();
    if (state.focusedNode >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", tree.PoolStr(tree.NodeAt(state.focusedNode).funcName));
    }
}

void FreeTreemapState(TreemapState& state) {
    delete state.displayTex; state.displayTex = nullptr;
    delete state.pickTex;    state.pickTex = nullptr;
}

} // namespace gui
