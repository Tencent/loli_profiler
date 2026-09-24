#include "stacktracetree.h"

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
            walk = nodes_[walk].parent;
        }
    }
    std::fprintf(stderr, "[tree-dbg] afterLoop nodes=%zu roots=%zu pool=%zu recordCount=%zu\n",
                 nodes_.size(), roots_.size(), pool_.size(), recordCount);
    std::fflush(stderr);

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
