#include "treemappanel.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include "imgui_internal.h"
#include "stacktracetree.h"

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
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

std::string ToLower(const char* s) {
    std::string out(s ? s : "");
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

// Version counter for the search state; bumped whenever the text changes so
// the texture re-renders (and the match cache recomputes) exactly once.
uint64_t SearchVersion(const TreemapState& state, uint64_t dataVersion) {
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    for (const char* p = state.search; *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h ^ dataVersion;
}

// Recompute the cached search matches (node indices whose funcName or library
// contains the search text, case-insensitive). Only runs when the text or the
// data version changed since the last build.
void RebuildSearchMatches(const StacktraceTree& tree, TreemapState& state,
                          uint64_t dataVersion) {
    if (state.searchBuiltForVersion == dataVersion &&
        std::strcmp(state.searchBuiltText, state.search) == 0)
        return;
    state.searchMatches.clear();
    state.searchMatchIdx = -1;
    const std::string needle = ToLower(state.search);
    if (!needle.empty()) {
        const auto& nodes = tree.Nodes();
        state.searchMatches.reserve(nodes.size() / 8 + 1);
        for (int32_t i = 0; i < (int32_t)nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (ToLower(tree.PoolStr(node.funcName)).find(needle) != std::string::npos ||
                ToLower(tree.PoolStr(node.library)).find(needle) != std::string::npos)
                state.searchMatches.push_back(i);
        }
    }
    state.searchBuiltForVersion = dataVersion;
    std::snprintf(state.searchBuiltText, sizeof(state.searchBuiltText), "%s", state.search);
}

} // namespace

void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state,
                      uint64_t dataVersion, float dpiScale) {
    if (tree.Nodes().empty()) {
        ImGui::TextUnformatted("No data. Load a record or run a capture.");
        return;
    }

    // Reserve the bottom bar for the controls (search + Depth combo + Go Up
    // button). Its true height is: ItemSpacing + Separator + ItemSpacing +
    // one frame row. Add a couple of px of slack for float->int truncation so
    // image + bar always fit the content region exactly (no v-scrollbar).
    const ImGuiStyle& barStyle = ImGui::GetStyle();
    const float controlBarH = ImGui::GetFrameHeightWithSpacing() +
                              2.0f * barStyle.ItemSpacing.y +
                              barStyle.SeparatorSize + 2.0f;

    // Size the image to the true client width. GetContentRegionAvail() can lag
    // a frame behind scrollbar visibility, which would let an image a few px
    // too wide force a horizontal scrollbar (which in turn steals vertical
    // space and clips the bar). Reserve the scrollbar width only while
    // scrolling is actually in effect (ScrollMax from the previous frame);
    // once the content fits, the image spans the full content width.
    ImGuiWindow* win = GImGui->CurrentWindow;
    const float styleScrollbarW = GImGui->Style.ScrollbarSize;
    const float padLeft = win->WindowPadding.x;
    const float padRight = win->WindowPadding.x;
    const bool reserveVScrollbar = (win->Flags & ImGuiWindowFlags_NoScrollbar) == 0 &&
                                   win->ScrollMax.y > 0.0f;
    const float usableW = std::max(8.0f, win->Size.x - padLeft - padRight -
                                         (reserveVScrollbar ? styleScrollbarW : 0.0f));
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(8, (int)usableW);
    const int h = std::max(8, (int)(avail.y - controlBarH));

    // Search: refresh the cached match list only when the text or the data
    // version changed, and remember which match (if any) is "selected" for
    // the highlight overlay.
    RebuildSearchMatches(tree, state, dataVersion);
    const uint64_t searchVersion = SearchVersion(state, dataVersion);
    const int32_t selectedMatch =
        (state.searchMatchIdx >= 0 &&
         state.searchMatchIdx < (int)state.searchMatches.size())
            ? state.searchMatches[state.searchMatchIdx]
            : -1;

    // Decide whether we must re-render the textures.
    const bool needRender =
        state.builtForVersion != dataVersion ||
        state.builtSearchVersion != searchVersion ||
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

            // Match lookup set for search highlighting (built only when there
            // is an active search; empty set = no overlay work at all).
            std::unordered_set<int32_t> matchSet;
            if (!state.searchMatches.empty()) {
                matchSet.reserve(state.searchMatches.size());
                for (int32_t idx : state.searchMatches)
                    matchSet.insert(idx);
            }

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
            // Search highlight overlay: draw matching cells with a bright
            // outline (parents first so a selected child stays visible on
            // top), and the currently selected match with a distinct thick
            // border. Drawn after the cell loop so overlays aren't covered.
            if (!matchSet.empty()) {
                for (const auto& c : cells) {
                    if (matchSet.find(c.nodeIndex) == matchSet.end())
                        continue;
                    const bool selected = (c.nodeIndex == selectedMatch);
                    sf::RectangleShape hl(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                       std::max(0.0f, c.rect.h - 1)));
                    hl.setPosition({ c.rect.x, c.rect.y });
                    hl.setFillColor(sf::Color::Transparent);
                    hl.setOutlineThickness(selected ? 2.0f * dpiScale
                                                    : 1.0f * dpiScale);
                    hl.setOutlineColor(selected ? sf::Color(255, 220, 40)
                                                : sf::Color(255, 255, 255, 210));
                    disp->draw(hl);
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
            state.builtSearchVersion = searchVersion;
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

    // Bottom control bar, left to right: [search input (flex width)]
    // [prev/next match buttons] [compact Depth combo] [Go Up button].
    // Clamp any out-of-range depth to the nearest preset so the combo's
    // preview always matches a selectable option.
    static const struct { const char* label; int depth; } kDepthOptions[] = {
        { "Normal (6)", 6 },
        { "Dense (9)", 9 },
        { "Extreme (12)", 12 },
    };
    int depthIdx = 0;
    int bestDist = 1 << 30;
    for (int i = 0; i < 3; ++i) {
        const int d = std::abs(state.maxDepth - kDepthOptions[i].depth);
        if (d < bestDist) { bestDist = d; depthIdx = i; }
    }
    state.maxDepth = kDepthOptions[depthIdx].depth;

    ImGui::Separator();
    ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float comboW = 110.0f * dpiScale;  // fits "Extreme (12)" + arrow
    const float upW = ImGui::CalcTextSize("Go Up").x + style.FramePadding.x * 2.0f;
    const float navBtnW = ImGui::GetFrameHeight(); // square "<" / ">" buttons

    // Cycle focus through the search matches, wrapping around. Focus is
    // drilled to the match's parent (unless the match is already inside the
    // focused subtree) so the highlighted cell is actually visible.
    auto cycleMatch = [&](int dir) {
        const int count = (int)state.searchMatches.size();
        if (count == 0)
            return;
        state.searchMatchIdx = ((state.searchMatchIdx + dir) % count + count) % count;
        const int32_t match = state.searchMatches[state.searchMatchIdx];
        bool visible = (state.focusedNode < 0);
        for (int32_t p = match; p >= 0 && !visible; p = tree.NodeAt(p).parent)
            visible = (p == state.focusedNode);
        if (!visible) {
            const int32_t parent = tree.NodeAt(match).parent;
            state.focusedNode = parent;
        }
    };

    ImGui::SetNextItemWidth(
        std::max(80.0f, usableW - comboW - upW - 2.0f * navBtnW - 3.0f * spacing));
    if (ImGui::InputTextWithHint("##search", "search nodes...", state.search,
                                 sizeof(state.search))) {
        state.searchMatchIdx = -1; // text changed; matches rebuild next frame
    }
    ImGui::SameLine();
    if (state.searchMatches.empty())
        ImGui::BeginDisabled();
    if (ImGui::Button("<", ImVec2(navBtnW, 0)))
        cycleMatch(-1);
    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(navBtnW, 0)))
        cycleMatch(1);
    if (state.searchMatches.empty())
        ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboW);
    if (ImGui::BeginCombo("##depth", kDepthOptions[depthIdx].label)) {
        for (int i = 0; i < 3; ++i) {
            const bool selected = (i == depthIdx);
            if (ImGui::Selectable(kDepthOptions[i].label, selected))
                state.maxDepth = kDepthOptions[i].depth;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (state.focusedNode < 0)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Go Up")) {
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
