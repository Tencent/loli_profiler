#ifndef LOLI_PROFILER_GUI_STACKTRACETREE_H
#define LOLI_PROFILER_GUI_STACKTRACETREE_H

// StacktraceTree — builds a merged, aggregated call-tree from flat allocation
// records and exposes a FLAT, clipper-ready list of visible rows for ImGui.
//
// Performance design (mirrors Qt model/view virtualization, without Qt):
//  - The aggregated tree is built ONCE from the snapshot records (on demand via
//    Rebuild), not per frame.
//  - The visible-row list is a flat std::vector<VisibleRow> rebuilt only when
//    expansion state or filter changes — never per frame.
//  - ImGui renders it with ImGuiListClipper over VisibleRows(), so only
//    on-screen rows submit widgets.
//
// Pure C++17, no Qt.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "guisnapshot.h"

namespace gui {

class StacktraceTree {
public:
    // One aggregated node in the merged call tree.
    struct Node {
        std::string funcName;
        std::string library;
        uint64_t    totalSize = 0;   // bytes attributed at/below this node
        uint32_t    allocCount = 0;  // allocations attributed at/below this node
        int32_t     parent = -1;
        std::vector<int32_t> children;
        uint64_t    id = 0;          // stable id for expansion state
    };

    // One row in the flattened, expansion-aware visible list.
    struct VisibleRow {
        int32_t  nodeIndex = -1; // index into nodes_
        int32_t  depth = 0;
        bool     hasChildren = false;
        bool     expanded = false;
    };

    // Build/refresh the aggregated tree from flat records + their callstacks.
    // `records` are the flat allocation records; `framesFor` returns the resolved
    // call frames (root-first) for records[i], or empty. Called on data change.
    void Rebuild(const std::vector<RecordSnapshot>& records,
                 const std::vector<std::vector<StackFrameSnapshot>>& framesFor);

    void Clear();

    // Expansion control (keyed by stable node id; survives rebuilds).
    void SetExpanded(int32_t nodeIndex, bool expanded);
    void ExpandAll();
    void CollapseAll();

    // Text filter; empty clears. Rebuilds the visible list.
    void SetFilter(const std::string& text);
    const std::string& Filter() const { return filter_; }

    // Flat visible rows (respecting expansion + filter). Render with clipper.
    const std::vector<VisibleRow>& VisibleRows() const { return visibleRows_; }
    const Node& NodeAt(int32_t index) const { return nodes_[index]; }
    const std::vector<Node>& Nodes() const { return nodes_; }
    int RootCount() const { return static_cast<int>(roots_.size()); }
    const std::vector<int32_t>& Roots() const { return roots_; }

private:
    void RebuildVisible();
    void AppendVisible(int32_t nodeIndex, int32_t depth);
    bool MatchesFilterRecursive(int32_t nodeIndex) const;

private:
    std::vector<Node>        nodes_;
    std::vector<int32_t>     roots_;
    std::vector<VisibleRow>  visibleRows_;
    std::unordered_set<uint64_t> expandedIds_;
    std::string              filter_;
    uint64_t                 nextId_ = 1;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_STACKTRACETREE_H
