#include "stacktracetree.h"
#include "lolilogger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gui {

void StacktraceTree::Clear() {
    nodes_.clear();
    roots_.clear();
    visibleRows_.clear();
    // expandedIds_ retained so expansion survives a rebuild of the same tree.
}

void StacktraceTree::Adopt(StacktraceTree&& other) {
    nodes_  = std::move(other.nodes_);
    roots_  = std::move(other.roots_);
    pool_   = std::move(other.pool_);
    poolIndex_ = std::move(other.poolIndex_);
    nextId_ = other.nextId_;
    // Keep our expandedIds_/filter_; visible list is rebuilt below.
    RebuildVisible();
}

int32_t StacktraceTree::Intern(const std::string& s) {
    auto it = poolIndex_.find(s);
    if (it != poolIndex_.end())
        return it->second;
    int32_t idx = static_cast<int32_t>(pool_.size());
    pool_.push_back(s);
    poolIndex_.emplace(pool_.back(), idx);
    return idx;
}

void StacktraceTree::BuildFromRecords(
    const std::vector<uint32_t>& recordSizes,
    const std::vector<std::vector<RawFrameIdx>>& recordFrames,
    const std::vector<std::string>& funcNames,
    const std::vector<std::string>& libNames) {
    BuildFromRecordsInternal(recordSizes, recordFrames, funcNames, libNames, nullptr);
}

void StacktraceTree::BuildLeakDiffFromRecords(
    const std::vector<uint32_t>& recordSizes,
    const std::vector<std::vector<RawFrameIdx>>& recordFrames,
    const std::vector<std::string>& funcNames,
    const std::vector<std::string>& libNames,
    const std::vector<uint8_t>& baseline) {
    BuildFromRecordsInternal(recordSizes, recordFrames, funcNames, libNames, &baseline);
}

void StacktraceTree::BuildFromRecordsInternal(
    const std::vector<uint32_t>& recordSizes,
    const std::vector<std::vector<RawFrameIdx>>& recordFrames,
    const std::vector<std::string>& funcNames,
    const std::vector<std::string>& libNames,
    const std::vector<uint8_t>* baseline) {

    nodes_.clear();
    roots_.clear();
    pool_.clear();
    poolIndex_.clear();
    nextId_ = 1;

    const size_t recordCount = recordFrames.size();

    // Pre-intern the caller's string tables into our pool once, so per-frame we
    // only ever copy an index (never a string). Map caller-index -> pool-index.
    std::vector<int32_t> funcPoolIdx(funcNames.size());
    for (size_t k = 0; k < funcNames.size(); k++)
        funcPoolIdx[k] = Intern(funcNames[k]);
    std::vector<int32_t> libPoolIdx(libNames.size());
    for (size_t k = 0; k < libNames.size(); k++)
        libPoolIdx[k] = Intern(libNames[k]);

    // Merge records into an aggregated tree. Instead of building a full path
    // STRING per frame (snprintf + string hash over 100M+ frames), key each node
    // by the integer pair (parentNodeIndex, funcPoolIndex) -> childNodeIndex.
    // This is allocation-free in the hot loop and dramatically faster.
    struct PairHash {
        size_t operator()(const uint64_t v) const noexcept {
            return std::hash<uint64_t>()(v * 0x9E3779B97F4A7C15ULL);
        }
    };
    // key = (parent+1) << 32 | funcPoolIndex   (parent -1 -> 0)
    std::unordered_map<uint64_t, int32_t, PairHash> nodeMap;
    nodeMap.reserve(recordCount / 2 + 16);
    std::vector<uint64_t> baselineSizes;
    std::vector<uint32_t> baselineCounts;

    for (size_t i = 0; i < recordCount; i++) {
        const auto& frames = recordFrames[i];
        if (frames.empty())
            continue;
        const uint32_t size = i < recordSizes.size() ? recordSizes[i] : 0;

        int32_t parent = -1;
        for (const RawFrameIdx& fr : frames) {
            const int32_t fIdx = fr.funcName < funcPoolIdx.size() ? funcPoolIdx[fr.funcName] : -1;
            const int32_t lIdx = fr.library  < libPoolIdx.size()  ? libPoolIdx[fr.library]  : -1;

            const uint64_t key = (uint64_t)(uint32_t)(parent + 1) << 32 | (uint32_t)fIdx;
            auto it = nodeMap.find(key);
            int32_t nodeIndex;
            if (it != nodeMap.end()) {
                nodeIndex = it->second;
            } else {
                nodeIndex = static_cast<int32_t>(nodes_.size());
                Node node;
                node.funcName = fIdx;
                node.library  = lIdx;
                node.parent   = parent;
                node.id       = nextId_++;
                nodes_.push_back(node);
                if (baseline) {
                    baselineSizes.push_back(0);
                    baselineCounts.push_back(0);
                }
                nodeMap.emplace(key, nodeIndex);
                if (parent >= 0)
                    nodes_[parent].children.push_back(nodeIndex);
                else
                    roots_.push_back(nodeIndex);
            }
            parent = nodeIndex;
        }

        if (parent < 0)
            continue;

        int32_t walk = parent;
        while (walk >= 0) {
            nodes_[walk].totalSize += size;
            nodes_[walk].allocCount += 1;
            if (baseline && i < baseline->size() && (*baseline)[i]) {
                baselineSizes[walk] += size;
                baselineCounts[walk] += 1;
            }
            walk = nodes_[walk].parent;
        }
    }
    if (baseline) {
        // Old Qt behavior: only leaves which existed at the first mark can
        // count as leak candidates. A callstack introduced later is omitted.
        for (size_t i = 0; i < nodes_.size(); ++i) {
            Node& n = nodes_[i];
            if (!n.children.empty() || baselineCounts[i] == 0 ||
                n.totalSize < baselineSizes[i] ||
                n.totalSize - baselineSizes[i] < 1024 ||
                n.allocCount <= baselineCounts[i]) {
                n.totalSize = 0;
                n.allocCount = 0;
            } else {
                n.totalSize -= baselineSizes[i];
                n.allocCount -= baselineCounts[i];
            }
        }
        // Node indices are created parent-first, so reverse traversal sums
        // surviving leaf deltas into their parents.
        for (size_t i = nodes_.size(); i-- > 0;) {
            const int32_t p = nodes_[i].parent;
            if (p >= 0) {
                nodes_[p].totalSize += nodes_[i].totalSize;
                nodes_[p].allocCount += nodes_[i].allocCount;
            }
        }
        // Keep only reachable positive nodes so search and treemap both see
        // the same candidates as the Qt result tree.
        std::vector<int32_t> remap(nodes_.size(), -1);
        std::vector<Node> kept;
        kept.reserve(nodes_.size());
        for (size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].allocCount == 0) continue;
            remap[i] = static_cast<int32_t>(kept.size());
            kept.push_back(std::move(nodes_[i]));
        }
        for (Node& n : kept) {
            n.parent = n.parent >= 0 ? remap[n.parent] : -1;
            n.children.erase(std::remove_if(n.children.begin(), n.children.end(),
                [&](int32_t child) { return remap[child] < 0; }), n.children.end());
            for (int32_t& child : n.children) child = remap[child];
        }
        roots_.erase(std::remove_if(roots_.begin(), roots_.end(),
            [&](int32_t root) { return remap[root] < 0; }), roots_.end());
        for (int32_t& root : roots_) root = remap[root];
        nodes_ = std::move(kept);
    }
    LOLI_DEBUG("tree") << "after merge nodes=" << nodes_.size()
                       << " roots=" << roots_.size()
                       << " pool=" << pool_.size()
                       << " records=" << recordCount;

    for (auto& node : nodes_) {
        std::sort(node.children.begin(), node.children.end(),
                  [&](int32_t a, int32_t b) {
                      return nodes_[a].totalSize > nodes_[b].totalSize;
                  });
    }
    std::sort(roots_.begin(), roots_.end(), [&](int32_t a, int32_t b) {
        return nodes_[a].totalSize > nodes_[b].totalSize;
    });

    // Re-apply the current sort in case the user picked a different key.
    ApplySort();

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

void StacktraceTree::ExpandAtLeast(uint64_t bytes) {
    expandedIds_.clear();
    for (const auto& n : nodes_)
        if (!n.children.empty() && n.totalSize >= bytes)
            expandedIds_.insert(n.id);
    RebuildVisible();
}

void StacktraceTree::RevealNode(int32_t nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodes_.size()))
        return;
    for (int32_t p = nodes_[nodeIndex].parent; p >= 0; p = nodes_[p].parent)
        expandedIds_.insert(nodes_[p].id);
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

void StacktraceTree::SetSort(int column, bool descending) {
    sortColumn_ = column;
    sortDescending_ = descending;
    ApplySort();
    RebuildVisible();
}

void StacktraceTree::ApplySort() {
    auto keyLess = [&](int32_t a, int32_t b) {
        const Node& na = nodes_[a];
        const Node& nb = nodes_[b];
        if (sortColumn_ == 1) {
            if (na.allocCount != nb.allocCount)
                return na.allocCount < nb.allocCount;
        } else {
            if (na.totalSize != nb.totalSize)
                return na.totalSize < nb.totalSize;
        }
        // Stable, deterministic tie-break on the pool index so equal-key nodes
        // keep a consistent order.
        return na.funcName < nb.funcName;
    };
    // sortDescending_ == true means the LARGEST values come first.
    auto cmp = [&](int32_t a, int32_t b) {
        return sortDescending_ ? keyLess(b, a) : keyLess(a, b);
    };
    for (auto& node : nodes_)
        std::sort(node.children.begin(), node.children.end(), cmp);
    std::sort(roots_.begin(), roots_.end(), cmp);
}

bool StacktraceTree::MatchesFilterRecursive(int32_t nodeIndex) const {
    const Node& node = nodes_[nodeIndex];
    auto containsCI = [&](int32_t poolIdx) {
        if (poolIdx < 0 || poolIdx >= (int32_t)pool_.size())
            return false;
        const std::string& hay = pool_[poolIdx];
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
