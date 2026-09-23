#include "stacktracetree.h"

#include <algorithm>
#include <cctype>

namespace gui {

void StacktraceTree::Clear() {
    nodes_.clear();
    roots_.clear();
    visibleRows_.clear();
    // expandedIds_ intentionally retained so expansion survives a rebuild
    // against the same logical tree (ids are stable per funcName path).
}

void StacktraceTree::Rebuild(
    const std::vector<RecordSnapshot>& records,
    const std::vector<std::vector<StackFrameSnapshot>>& framesFor) {

    nodes_.clear();
    roots_.clear();
    nextId_ = 1;

    // Merge records into an aggregated tree keyed by root-first frame path.
    // A path-key map finds an existing node at each level.
    std::unordered_map<std::string, int32_t> pathMap;
    pathMap.reserve(records.size() * 2);

    const size_t count = records.size();
    for (size_t i = 0; i < count; i++) {
        const auto& rec = records[i];
        const auto& frames = (i < framesFor.size()) ? framesFor[i]
                                                    : std::vector<StackFrameSnapshot>{};
        if (frames.empty())
            continue;

        int32_t parent = -1;
        std::string path;
        path.reserve(256);
        for (const auto& frame : frames) {
            path += frame.funcName;
            path += '\x1f';  // unit separator, avoids collisions

            auto it = pathMap.find(path);
            int32_t nodeIndex;
            if (it != pathMap.end()) {
                nodeIndex = it->second;
            } else {
                nodeIndex = static_cast<int32_t>(nodes_.size());
                Node node;
                node.funcName = frame.funcName;
                node.library  = frame.library;
                node.parent   = parent;
                node.id       = nextId_++;
                nodes_.push_back(std::move(node));
                pathMap.emplace(path, nodeIndex);
                if (parent >= 0)
                    nodes_[parent].children.push_back(nodeIndex);
                else
                    roots_.push_back(nodeIndex);
            }
            parent = nodeIndex;
        }

        // Attribute the record size up the chain (every ancestor aggregates).
        const uint64_t sz = rec.size > 0 ? static_cast<uint64_t>(rec.size) : 0;
        int32_t walk = parent;
        while (walk >= 0) {
            nodes_[walk].totalSize += sz;
            nodes_[walk].allocCount += 1;
            walk = nodes_[walk].parent;
        }
    }

    // Sort children by total size descending for a stable, useful ordering.
    for (auto& node : nodes_) {
        std::sort(node.children.begin(), node.children.end(),
                  [&](int32_t a, int32_t b) {
                      return nodes_[a].totalSize > nodes_[b].totalSize;
                  });
    }
    std::sort(roots_.begin(), roots_.end(), [&](int32_t a, int32_t b) {
        return nodes_[a].totalSize > nodes_[b].totalSize;
    });

    RebuildVisible();
}

void StacktraceTree::SetExpanded(int32_t nodeIndex, bool expanded) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes_.size()))
        return;
    const uint64_t id = nodes_[nodeIndex].id;
    if (expanded)
        expandedIds_.insert(id);
    else
        expandedIds_.erase(id);
    RebuildVisible();
}

void StacktraceTree::ExpandAll() {
    for (const auto& n : nodes_)
        if (!n.children.empty())
            expandedIds_.insert(n.id);
    RebuildVisible();
}

void StacktraceTree::CollapseAll() {
    expandedIds_.clear();
    RebuildVisible();
}

void StacktraceTree::SetFilter(const std::string& text) {
    filter_ = text;
    RebuildVisible();
}

void StacktraceTree::RebuildVisible() {
    visibleRows_.clear();
    visibleRows_.reserve(nodes_.size() / 2 + 1);

    const bool filtering = !filter_.empty();
    for (int32_t root : roots_) {
        if (filtering && !MatchesFilterRecursive(root))
            continue;
        AppendVisible(root, 0);
    }
}

void StacktraceTree::AppendVisible(int32_t nodeIndex, int32_t depth) {
    const Node& node = nodes_[nodeIndex];

    VisibleRow row;
    row.nodeIndex = nodeIndex;
    row.depth = depth;
    row.hasChildren = !node.children.empty();
    row.expanded = expandedIds_.count(node.id) != 0;
    visibleRows_.push_back(row);

    if (!row.expanded)
        return;

    const bool filtering = !filter_.empty();
    for (int32_t child : node.children) {
        if (filtering && !MatchesFilterRecursive(child))
            continue;
        AppendVisible(child, depth + 1);
    }
}

bool StacktraceTree::MatchesFilterRecursive(int32_t nodeIndex) const {
    const Node& node = nodes_[nodeIndex];
    // Case-insensitive substring match on funcName or library.
    auto containsCI = [&](const std::string& hay) {
        auto it = std::search(hay.begin(), hay.end(), filter_.begin(), filter_.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            });
        return it != hay.end();
    };
    if (containsCI(node.funcName) || containsCI(node.library))
        return true;
    for (int32_t child : node.children) {
        if (MatchesFilterRecursive(child))
            return true;
    }
    return false;
}

} // namespace gui
