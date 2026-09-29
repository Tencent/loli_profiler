#ifndef LOLI_PROFILER_GUI_TREEMAPPANEL_H
#define LOLI_PROFILER_GUI_TREEMAPPANEL_H

// TreemapPanel - squarified treemap of the aggregated call tree.
//
// Rendering strategy (perf for large trees): the treemap is drawn ONCE into an
// offscreen SFML RenderTexture (display layer) plus a parallel picking layer
// (each cell a unique color = node index). We then present the display texture
// as a single ImGui::Image. Mouse position is mapped to a node by reading the
// picking layer's pixel. The textures re-render ONLY when the view changes
// (data version, focus node, depth, or size) - not per frame.
//
// Pure C++17 + ImGui + SFML. No Qt.

#include <cstdint>
#include <vector>

namespace sf { class RenderTexture; class Image; }

namespace gui {
class StacktraceTree;

// Persistent state for the treemap panel.
struct TreemapState {
    int      focusedNode = -1;   // node drilled into (-1 = roots)
    int      maxDepth = 6;       // levels rendered below focus

    // --- search ---
    // Search input is editable; only Enter copies it to committedSearch and
    // triggers the potentially expensive full-tree match scan.
    char     search[128] = "";
    char     committedSearch[128] = "";
    int      searchMatchIdx = -1;            // current match (for next/prev cycling)
    std::vector<int32_t> searchMatches;      // cached matched node indices
    uint64_t searchBuiltForVersion = ~0ull;  // data version the cache was built from
    char     searchBuiltText[128] = "\x01";  // committed query the cache was built from
    double   searchNoticeUntil = 0.0;
    int      searchNoticeCount = 0;

    // --- texture cache (owned by the panel implementation) ---
    sf::RenderTexture* displayTex = nullptr;
    sf::RenderTexture* pickTex = nullptr;
    sf::Image* pickImage = nullptr; // cached CPU picking pixels
    int      texW = 0, texH = 0;
    uint64_t builtForVersion = 0;   // data version the texture was built from
    int      builtFocus = -2;       // focus the texture was built for
    int      builtDepth = -1;       // depth the texture was built for
    float    builtDpi = 0.0f;       // dpi scale the texture was built for
    uint64_t builtSearchVersion = ~0ull;  // search version the texture was built from
    int32_t  builtSelectedMatch = -1;

    // --- resize throttle ---
    // Re-rendering the offscreen texture is expensive (Squarify + 2 draw passes
    // over thousands of cells). During a dock-separator drag the size changes
    // every frame; we debounce so we re-render only after the size has been
    // stable for `resizeDebounceMs`, showing the stale texture stretched in the
    // meantime. `lastResizeTimeSec` is ImGui::GetTime() of the last size change.
    double   lastResizeTimeSec = 0.0;
    int      pendingW = 0, pendingH = 0;  // size waiting to be rendered
    static constexpr double kResizeDebounceSec = 0.12;  // 120 ms
};

// Draws the treemap for the given (already-built) stacktrace tree. `dataVersion`
// should be the bridge snapshot version so the texture refreshes on new data.
// `dpiScale` scales cell separation/borders/text for HiDPI. Call between
// ImGui::Begin/End of the Treemap window. The sf::RenderTexture pointers in
// `state` are lazily created; free them at shutdown via FreeTreemapState().
void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state,
                      uint64_t dataVersion, float dpiScale = 1.0f,
                      bool liveView = false);

// Frees the cached textures. Call once at shutdown.
void FreeTreemapState(TreemapState& state);

} // namespace gui

#endif // LOLI_PROFILER_GUI_TREEMAPPANEL_H
