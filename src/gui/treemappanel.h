#ifndef LOLI_PROFILER_GUI_TREEMAPPANEL_H
#define LOLI_PROFILER_GUI_TREEMAPPANEL_H

// TreemapPanel — squarified treemap of the aggregated call tree, rendered with
// ImDrawList. Replaces the Qt TreeMapGraphicsView. Pure C++17 + ImGui, no Qt.

namespace gui {
class StacktraceTree;
}

namespace gui {

// Persistent view state for the treemap panel (zoom/drill-down).
struct TreemapState {
    int  focusedNode = -1;   // node index the treemap is drilled into (-1 = roots)
    int  hoveredNode = -1;   // for tooltip (transient)
    int  maxDepth = 4;       // levels to render below focus
};

// Draws the treemap for the given (already-built) stacktrace tree.
// Call between ImGui::Begin/End of the Treemap window.
void DrawTreemapPanel(const StacktraceTree& tree, TreemapState& state);

} // namespace gui

#endif // LOLI_PROFILER_GUI_TREEMAPPANEL_H
