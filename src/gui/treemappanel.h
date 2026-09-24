#ifndef LOLI_PROFILER_GUI_TREEMAPPANEL_H
#define LOLI_PROFILER_GUI_TREEMAPPANEL_H

// TreemapPanel — squarified treemap of the aggregated call tree.
//
// Rendering strategy (perf for large trees): the treemap is drawn ONCE into an
// offscreen SFML RenderTexture (display layer) plus a parallel picking layer
// (each cell a unique color = node index). We then present the display texture
// as a single ImGui::Image. Mouse position is mapped to a node by reading the
// picking layer's pixel. The textures re-render ONLY when the view changes
// (data version, focus node, depth, or size) — not per frame.
//
// Pure C++17 + ImGui + SFML. No Qt.

#include <cstdint>

namespace sf { class RenderTexture; }

namespace gui {
class StacktraceTree;

// Persistent state for the treemap panel.
struct TreemapState {
    int      focusedNode = -1;   // node drilled into (-1 = roots)
    int      maxDepth = 6;       // levels rendered below focus
    // --- texture cache (owned by the panel implementation) ---
    sf::RenderTexture* displayTex = nullptr;
    sf::RenderTexture* pickTex = nullptr;
    int      texW = 0, texH = 0;
    uint64_t builtForVersion = 0;   // data version the texture was built from
    int      builtFocus = -2;       // focus the texture was built for
    int      builtDepth = -1;       // depth the texture was built for
};

// Draws the treemap for the given (already-built) stacktrace tree. `dataVersion`
// should be the bridge snapshot version so the texture refreshes on new data.
// Call between ImGui::Begin/End of the Treemap window. The sf::RenderTexture
// pointers in `state` are lazily created and must be freed by the caller at
// shutdown via FreeTreemapState().
void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state,
                      uint64_t dataVersion);

// Frees the cached textures. Call once at shutdown.
void FreeTreemapState(TreemapState& state);

} // namespace gui

#endif // LOLI_PROFILER_GUI_TREEMAPPANEL_H
