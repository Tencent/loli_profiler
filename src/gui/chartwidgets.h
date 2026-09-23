#ifndef LOLI_PROFILER_GUI_CHARTWIDGETS_H
#define LOLI_PROFILER_GUI_CHARTWIDGETS_H

// ImDrawList-based visualization widgets for the ImGui GUI.
// Pure C++17 + Dear ImGui — NO Qt, NO SFML.
//
// TimelineView: persistent zoom/pan state for DrawMemoryTimelineChart,
// owned by main() and passed in by reference.

#include "guisnapshot.h"

#include "imgui.h"

#include <cstdint>
#include <string>

namespace gui {

// ---------------------------------------------------------------------------
// Timeline chart
// ---------------------------------------------------------------------------

// Persistent view state for the timeline chart (zoom/pan range in ms).
// zoomMin/zoomMax are clamped to the data domain each frame; zoomMin < 0
// means "auto-fit the whole capture" until the user zooms or pans.
struct TimelineView {
    double zoomMin = -1.0; // view start (ms), negative = full range
    double zoomMax = -1.0; // view end (ms)
    bool   userZoomed = false;
};

// Renders snapshot.memTimeline as a multi-series line chart with axes,
// gridlines, legend, hover tooltip and wheel-zoom / drag-pan interaction.
// Call between ImGui::Begin/End; uses an InvisibleButton to capture input.
void DrawMemoryTimelineChart(const GuiSnapshot& snapshot, TimelineView& view);

// ---------------------------------------------------------------------------
// Smaps panel
// ---------------------------------------------------------------------------

// Renders snapshot.smaps: a horizontal stacked bar of the top-N sections by
// PSS followed by a sortable table of all sections.
void DrawSmapsPanel(const GuiSnapshot& snapshot);

} // namespace gui

#endif // LOLI_PROFILER_GUI_CHARTWIDGETS_H
