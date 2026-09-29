#include "treemappanel.h"

#include "imgui.h"
#include "imgui-SFML.h"
#include "imgui_internal.h"
#include "stacktracetree.h"
#include "lolilogger.h"

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
    if (nodeIndex == -1000)
        return sf::Color(255, 255, 255);
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

// Sentinel nodeIndex for the file-browser-style ".." go-up cell, prepended to
// the layout when the treemap is drilled into a subtree. Distinct from any
// real node (which are >= 0) and from -1 ("no cell" in picking).
constexpr int32_t kGoUpCell = -1000;

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

// Version counter for the active search, which changes on Enter, not typing.
uint64_t SearchVersion(const TreemapState& state, uint64_t dataVersion) {
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    for (const char* p = state.committedSearch; *p; ++p) {
        h ^= (uint64_t)(unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h ^ dataVersion;
}

// Recompute the cached search matches (function names, matching Qt column 0).
// Only runs when the committed query or the
// data version changed since the last build.
void RebuildSearchMatches(const StacktraceTree& tree, TreemapState& state,
                          uint64_t dataVersion) {
    if (state.searchBuiltForVersion == dataVersion &&
        std::strcmp(state.searchBuiltText, state.committedSearch) == 0)
        return;
    const auto searchStart = loli::LoliLogger::Clock::now();
    state.searchMatches.clear();
    state.searchMatchIdx = -1;
    const std::string needle = ToLower(state.committedSearch);
    if (!needle.empty()) {
        const auto& nodes = tree.Nodes();
        state.searchMatches.reserve(nodes.size() / 8 + 1);
        for (int32_t i = 0; i < (int32_t)nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (ToLower(tree.PoolStr(node.funcName)).find(needle) != std::string::npos)
                state.searchMatches.push_back(i);
        }
    }
    state.searchBuiltForVersion = dataVersion;
    std::snprintf(state.searchBuiltText, sizeof(state.searchBuiltText), "%s",
                  state.committedSearch);
    if (!needle.empty())
        loli::LoliLogger::Instance().LogStage("treemap", "search_matches",
            searchStart, "matches=" + std::to_string(state.searchMatches.size()) +
                " tree_nodes=" + std::to_string(tree.Nodes().size()));
}

} // namespace

void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state,
                      uint64_t dataVersion, float dpiScale, bool liveView) {
    if (tree.Nodes().empty()) {
        ImGui::TextUnformatted("No data. Load a record or run a capture.");
        return;
    }

    // Reserve the bottom bar for the controls (search + Depth combo + Go Up
    // button). Its true height is: ItemSpacing + Separator + ItemSpacing +
    // one frame row. Keep one pixel of slack for rounding while placing the
    // controls close to the bottom padding of the docked window.
    const ImGuiStyle& barStyle = ImGui::GetStyle();
    const float controlBarH = ImGui::GetFrameHeight() +
                              2.0f * barStyle.ItemSpacing.y +
                              barStyle.SeparatorSize + 1.0f;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float usableW = std::max(8.0f, avail.x);
    const int w = std::max(8, (int)usableW);
    const int h = std::max(8, (int)(avail.y - controlBarH));

    // Search only the last query committed with Enter. Unsubmitted edits do
    // not scan nodes or invalidate the cached treemap texture.
    RebuildSearchMatches(tree, state, dataVersion);
    const uint64_t searchVersion = SearchVersion(state, dataVersion);
    const int32_t selectedMatch =
        (state.searchMatchIdx >= 0 &&
         state.searchMatchIdx < (int)state.searchMatches.size())
            ? state.searchMatches[state.searchMatchIdx]
            : -1;

    // Decide whether we must re-render the textures.
    const bool dataChanged =
        state.builtForVersion != dataVersion ||
        state.builtSearchVersion != searchVersion ||
        state.builtSelectedMatch != selectedMatch ||
        state.builtFocus != state.focusedNode ||
        state.builtDepth != state.maxDepth ||
        state.builtDpi != dpiScale ||
        state.displayTex == nullptr || state.pickTex == nullptr;
    const bool sizeChanged = (state.texW != w || state.texH != h);

    // Resize throttle: remember when the requested size last changed, then
    // render once that same size has remained stable for the debounce window.
    const double now = ImGui::GetTime();
    bool needRender = dataChanged;
    if (sizeChanged && (state.pendingW != w || state.pendingH != h)) {
        state.pendingW = w;
        state.pendingH = h;
        state.lastResizeTimeSec = now;
    }
    if (sizeChanged &&
        (state.texW == 0 || state.texH == 0 ||
         (now - state.lastResizeTimeSec) >= TreemapState::kResizeDebounceSec)) {
        needRender = true;
    }
    if (!sizeChanged) {
        state.pendingW = state.pendingH = 0;
    }

    if (needRender) {
        const auto renderStart = loli::LoliLogger::Clock::now();
        const int rw = w;
        const int rh = h;
        state.pendingW = state.pendingH = 0;
        sf::RenderTexture* disp = EnsureTexture(state.displayTex, rw, rh);
        sf::RenderTexture* pick = EnsureTexture(state.pickTex, rw, rh);
        if (disp && pick) {
            state.texW = rw;
            state.texH = rh;

            // Child padding + border scale with DPI so separation stays visible
            // on HiDPI displays.
            const float pad = 2.0f * dpiScale;
            const float fontSize = 12.0f * dpiScale;
            const float titleH = TitleHeight(fontSize, dpiScale);

            // The current parent is the full graph cell, matching the Qt view.
            // Its title is clickable to go up; children fill its content area.
            std::vector<Cell> cells;
            std::vector<int32_t> top;
            if (state.focusedNode >= 0) top = tree.NodeAt(state.focusedNode).children;
            else top = tree.Roots();
            Rect contentRect{ 0, 0, (float)rw, (float)rh };
            cells.push_back({ kGoUpCell, 0, contentRect });
            contentRect = { pad, titleH + pad,
                            contentRect.w - 2.0f * pad,
                            contentRect.h - titleH - 2.0f * pad };
            Squarify(top, tree, contentRect, 1, state.maxDepth,
                     pad, titleH, cells);
            // A tiny match can be omitted by the one-pixel treemap cutoff.
            // Zoom into that node so the selected target remains visible as
            // the highlighted graph root instead of losing its selection.
            if (selectedMatch >= 0 && state.focusedNode != selectedMatch &&
                std::none_of(cells.begin(), cells.end(), [&](const Cell& cell) {
                    return cell.nodeIndex == selectedMatch;
                })) {
                state.focusedNode = selectedMatch;
                cells.resize(1);
                Squarify(tree.NodeAt(selectedMatch).children, tree, contentRect,
                         1, state.maxDepth, pad, titleH, cells);
            }
            loli::LoliLogger::Instance().LogStage("treemap", "layout",
                renderStart, "cells=" + std::to_string(cells.size()) +
                    " tree_nodes=" + std::to_string(tree.Nodes().size()) +
                    " width=" + std::to_string(rw) +
                    " height=" + std::to_string(rh));

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
            const auto displayStart = loli::LoliLogger::Clock::now();
            double textFitMs = 0.0;
            std::size_t truncatedLabels = 0;
            disp->clear(sf::Color(24, 24, 28));

            for (const auto& c : cells) {
                // Full-size fake root, with its own title and child content.
                if (c.nodeIndex == kGoUpCell) {
                    const bool selectedRoot = selectedMatch >= 0 &&
                                              state.focusedNode == selectedMatch;
                    sf::RectangleShape body(sf::Vector2f(std::max(0.0f, c.rect.w - 1),
                                                         std::max(0.0f, c.rect.h - 1)));
                    body.setPosition({ c.rect.x, c.rect.y });
                    body.setFillColor(sf::Color(46, 50, 56));
                    body.setOutlineThickness(selectedRoot ? -2.0f * dpiScale : -1.0f);
                    body.setOutlineColor(selectedRoot ? sf::Color(255, 220, 40)
                                                      : sf::Color(15, 15, 18));
                    disp->draw(body);
                    if (fontLoaded) {
                        const std::string label = state.focusedNode >= 0
                            ? FormatBytes(tree.NodeAt(state.focusedNode).totalSize) +
                              ": " + tree.PoolStr(tree.NodeAt(state.focusedNode).funcName)
                            : (liveView ? "Live allocations" : "Cumulative allocations");
                        const unsigned fsize = (unsigned)std::max(8.0f, fontSize);
                        sf::Text text(font, sf::String::fromUtf8(label.begin(), label.end()), fsize);
                        text.setPosition({ c.rect.x + 4.0f * dpiScale,
                                           c.rect.y + 1.0f * dpiScale });
                        text.setFillColor(selectedRoot ? sf::Color(255, 235, 130)
                                                       : sf::Color(210, 214, 220, 240));
                        disp->draw(text);
                    }
                    continue;
                }
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
                    sf::Text measured(font, sf::String(), fsize);
                    auto textW = [&](const std::string& s) {
                        measured.setString(sf::String::fromUtf8(s.begin(), s.end()));
                        return measured.getLocalBounds().size.x;
                    };
                    const auto fitStart = loli::LoliLogger::Clock::now();
                    if (!label.empty() && textW(label) > availW) {
                        ++truncatedLabels;
                        const std::string ell = "...";
                        if (textW(ell) > availW) {
                            label.clear();
                        } else {
                            // Search code-point boundaries instead of trimming
                            // one byte at a time. Long UE function names used
                            // to spend >100 ms in this loop per treemap frame.
                            std::vector<std::size_t> cuts{0};
                            for (std::size_t pos = 0; pos < label.size();) {
                                const unsigned char byte =
                                    static_cast<unsigned char>(label[pos]);
                                const std::size_t width = byte < 0x80 ? 1 :
                                    (byte & 0xE0) == 0xC0 ? 2 :
                                    (byte & 0xF0) == 0xE0 ? 3 :
                                    (byte & 0xF8) == 0xF0 ? 4 : 1;
                                pos = std::min(label.size(), pos + width);
                                cuts.push_back(pos);
                            }
                            std::size_t low = 0;
                            std::size_t high = cuts.size() - 1;
                            while (low < high) {
                                const std::size_t mid = low + (high - low + 1) / 2;
                                if (textW(label.substr(0, cuts[mid]) + ell) <= availW)
                                    low = mid;
                                else
                                    high = mid - 1;
                            }
                            label.resize(cuts[low]);
                            label += ell;
                        }
                    }
                    textFitMs += std::chrono::duration<double, std::milli>(
                        loli::LoliLogger::Clock::now() - fitStart).count();
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
            loli::LoliLogger::Instance().LogStage("treemap", "display_pass",
                displayStart, "cells=" + std::to_string(cells.size()) +
                    " text_fit_ms=" + std::to_string(textFitMs) +
                    " truncated_labels=" + std::to_string(truncatedLabels));

            // Picking layer (cell -> nodeIndex color), matching display rects.
            const auto pickStart = loli::LoliLogger::Clock::now();
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
            loli::LoliLogger::Instance().LogStage("treemap", "pick_pass",
                pickStart, "cells=" + std::to_string(cells.size()));
            const auto readbackStart = loli::LoliLogger::Clock::now();
            if (!state.pickImage)
                state.pickImage = new sf::Image();
            *state.pickImage = pick->getTexture().copyToImage();
            loli::LoliLogger::Instance().LogStage("treemap", "pick_readback",
                readbackStart, "pixels=" + std::to_string(rw * rh));

            state.builtForVersion = dataVersion;
            state.builtSearchVersion = searchVersion;
            state.builtSelectedMatch = selectedMatch;
            state.builtFocus = state.focusedNode;
            state.builtDepth = state.maxDepth;
            state.builtDpi = dpiScale;
            loli::LoliLogger::Instance().LogStage("treemap", "render_complete",
                renderStart, "cells=" + std::to_string(cells.size()) +
                    " width=" + std::to_string(rw) +
                    " height=" + std::to_string(rh));
        }
    }

    if (!state.displayTex)
        return;

    // Present the cached display texture as a single ImGui image.
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(*state.displayTex, ImVec2((float)w, (float)h));

    // Picking: map mouse to a node via the pick texture pixel. The texture may be
    // a different size than the displayed image while a resize is debounced, so
    // scale the pixel coords into texture space.
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsItemHovered();
    int32_t hoveredNode = -1;
    if (hovered && state.pickImage) {
        const float sx = (float)state.texW / (float)w;
        const float sy = (float)state.texH / (float)h;
        const int px = (int)((mouse.x - origin.x) * sx);
        const int py = (int)((mouse.y - origin.y) * sy);
        if (px >= 0 && py >= 0 && px < state.texW && py < state.texH) {
            sf::Color c = state.pickImage->getPixel({ (unsigned)px, (unsigned)py });
            uint32_t v = (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
            if (v == 0xFFFFFF)
                hoveredNode = kGoUpCell;
            else if (v > 0)
                hoveredNode = (int32_t)(v - 1);
        }
    }

    // The current parent is part of the graph. Clicking its exposed title
    // navigates to the parent, just like the old Qt treemap.
    if (hoveredNode == kGoUpCell) {
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextDisabled(state.focusedNode >= 0 ? "Click to go up"
                                                      : (liveView ? "Live allocations" : "Cumulative allocations"));
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && state.focusedNode >= 0)
            state.focusedNode = tree.NodeAt(state.focusedNode).parent;
        hoveredNode = -1; // not a real node; skip the node tooltip below
    }

    // Tooltip + interactions.
    if (hoveredNode >= 0 && hoveredNode < (int32_t)tree.Nodes().size()) {
        const auto& node = tree.NodeAt(hoveredNode);
        ImGui::BeginTooltip();
        ImGui::Text("%s", tree.PoolStr(node.funcName));
        if (node.library >= 0)
            ImGui::TextDisabled("%s", tree.PoolStr(node.library));
        ImGui::Text("%s %s, %u allocs",
                    FormatBytes(node.totalSize).c_str(),
                    liveView ? "live" : "allocated total", node.allocCount);
        if (!node.children.empty())
            ImGui::TextDisabled("(click to zoom, right-click to go up)");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !node.children.empty())
            state.focusedNode = hoveredNode;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && state.focusedNode >= 0)
        state.focusedNode = tree.NodeAt(state.focusedNode).parent;

    // Bottom control bar: search and depth.
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
        state.focusedNode = tree.NodeAt(match).parent;
    };

    ImGui::SetNextItemWidth(
        std::max(80.0f, usableW - comboW - 2.0f * navBtnW - 2.0f * spacing));
    const bool enter = ImGui::InputTextWithHint("##search", "search nodes...", state.search,
                                                sizeof(state.search), ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) {
        ImGui::SetKeyboardFocusHere(-1);
        std::snprintf(state.committedSearch, sizeof(state.committedSearch), "%s",
                      state.search);
        RebuildSearchMatches(tree, state, dataVersion);
        state.searchNoticeCount = static_cast<int>(state.searchMatches.size());
        state.searchNoticeUntil = ImGui::GetTime() + 3.0;
        cycleMatch(1);
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
    if (ImGui::GetTime() < state.searchNoticeUntil) {
        const float alpha = std::min(1.0f, static_cast<float>(state.searchNoticeUntil - ImGui::GetTime()));
        const std::string notice = std::to_string(state.searchNoticeCount) + " matches";
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 pos(origin.x + 20.0f, origin.y + 55.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(notice.c_str());
        ImGui::GetForegroundDrawList()->AddRectFilled(pos,
            ImVec2(pos.x + textSize.x + 20.0f, pos.y + textSize.y + 12.0f),
            ImGui::GetColorU32(ImVec4(0.05f, 0.08f, 0.08f, 0.78f * alpha)), 5.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f),
            ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), notice.c_str());
    }
}

void FreeTreemapState(TreemapState& state) {
    delete state.displayTex; state.displayTex = nullptr;
    delete state.pickTex;    state.pickTex = nullptr;
    delete state.pickImage;  state.pickImage = nullptr;
}

} // namespace gui
