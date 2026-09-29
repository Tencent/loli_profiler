#ifndef LOLI_PROFILER_GUI_STACKTRACETREE_H
#define LOLI_PROFILER_GUI_STACKTRACETREE_H

// StacktraceTree — builds a merged, aggregated call-tree and exposes a FLAT,
// clipper-ready list of visible rows for ImGui.
//
// Performance design (this is the hot path for 300-400MB records):
//  - The tree is built ONCE on a WORKER THREAD from raw record/callstack data,
//    with funcName/library stored in an interned string pool (indices, not
//    copies) so we never materialize millions of duplicate strings.
//  - The visible-row list is rebuilt only on expansion/filter change.
//  - ImGui renders VisibleRows() via ImGuiListClipper (only on-screen rows).
//
// Pure C++17, no Qt in this header (the raw Qt containers are only touched in
// the .cpp build path, behind templates/callbacks).

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gui {

class StacktraceTree {
public:
    StacktraceTree() = default;
    // Movable (built on a worker thread, then swapped into the UI-side tree).
    StacktraceTree(StacktraceTree&&) = default;
    StacktraceTree& operator=(StacktraceTree&&) = default;
    StacktraceTree(const StacktraceTree&) = delete;
    StacktraceTree& operator=(const StacktraceTree&) = delete;

    // Adopt another tree's built data (nodes/pool/roots), preserving this
    // tree's expansion state where node ids still match.
    void Adopt(StacktraceTree&& other);

    // One aggregated node. funcName/library are indices into the string pool.
    struct Node {
        int32_t     funcName = -1;   // index into Pool()
        int32_t     library  = -1;   // index into Pool()
        uint64_t    totalSize = 0;
        uint32_t    allocCount = 0;
        int32_t     parent = -1;
        std::vector<int32_t> children;
        uint64_t    id = 0;          // stable id for expansion state
    };

    struct VisibleRow {
        int32_t  nodeIndex = -1;
        int32_t  depth = 0;
        bool     hasChildren = false;
        bool     expanded = false;
    };

    // A single resolved frame fed to the builder (root-first), as string-pool
    // indices into the vectors passed to BuildFromRecords (no per-frame copies).
    struct RawFrameIdx {
        uint32_t funcName;  // index into funcNames[]
        uint32_t library;   // index into libNames[]
    };

    // Build the aggregated tree from per-record root-first frame index lists.
    // funcNames[]/libNames[] are the caller-owned string tables that the frame
    // indices reference; the tree interns them into its own pool. recordSizes[i]
    // is the byte size of record i. Runs off the UI thread; no ImGui calls.
    void BuildFromRecords(
        const std::vector<uint32_t>& recordSizes,
        const std::vector<std::vector<RawFrameIdx>>& recordFrames,
        const std::vector<std::string>& funcNames,
        const std::vector<std::string>& libNames);

    // Qt's "Possible Memory Leaks": compare cumulative allocations at two
    // timeline marks. baseline[i] is true when record i is at/before the first
    // mark; all records supplied are at/before the second mark.
    void BuildLeakDiffFromRecords(
        const std::vector<uint32_t>& recordSizes,
        const std::vector<std::vector<RawFrameIdx>>& recordFrames,
        const std::vector<std::string>& funcNames,
        const std::vector<std::string>& libNames,
        const std::vector<uint8_t>& baseline);

    void Clear();

    void SetExpanded(int32_t nodeIndex, bool expanded);
    void ExpandAll();
    void ExpandAtLeast(uint64_t bytes);
    void CollapseAll();
    void RevealNode(int32_t nodeIndex);

    void SetFilter(const std::string& text);
    const std::string& Filter() const { return filter_; }

    // Sort each node's children (and the roots) by the chosen key — column 0 =
    // Size (totalSize), column 1 = Count (allocCount) — ascending/descending,
    // then rebuild the visible rows. Default: Size, descending.
    void SetSort(int column, bool descending);
    int SortColumn() const { return sortColumn_; }
    bool SortDescending() const { return sortDescending_; }

    const std::vector<VisibleRow>& VisibleRows() const { return visibleRows_; }
    const Node& NodeAt(int32_t index) const { return nodes_[index]; }
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<int32_t>& Roots() const { return roots_; }

    // Interned display-string pool (func names + libraries).
    const std::vector<std::string>& Pool() const { return pool_; }
    const char* PoolStr(int32_t idx) const {
        return (idx >= 0 && idx < (int32_t)pool_.size()) ? pool_[idx].c_str() : "";
    }

private:
    void BuildFromRecordsInternal(
        const std::vector<uint32_t>& recordSizes,
        const std::vector<std::vector<RawFrameIdx>>& recordFrames,
        const std::vector<std::string>& funcNames,
        const std::vector<std::string>& libNames,
        const std::vector<uint8_t>* baseline);
    void RebuildVisible();
    void AppendVisible(int32_t nodeIndex, int32_t depth);
    bool MatchesFilterRecursive(int32_t nodeIndex) const;
    int32_t Intern(const std::string& s);
    void ApplySort();  // re-sort children/roots by sortColumn_/sortDescending_

private:
    std::vector<Node>        nodes_;
    std::vector<int32_t>     roots_;
    std::vector<VisibleRow>  visibleRows_;
    std::unordered_set<uint64_t> expandedIds_;
    std::string              filter_;
    uint64_t                 nextId_ = 1;
    int                      sortColumn_ = 0;      // 0 = Size, 1 = Count
    bool                     sortDescending_ = true;

    // string interning
    std::vector<std::string> pool_;
    std::unordered_map<std::string, int32_t> poolIndex_;
};

} // namespace gui

#endif // LOLI_PROFILER_GUI_STACKTRACETREE_H
