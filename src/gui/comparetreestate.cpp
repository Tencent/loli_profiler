#include "comparetreestate.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gui {
namespace {
std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
}
void CompareTreeState::Reset(const loli::ComparisonResult& result, loli::ComparisonView view) {
    *this = CompareTreeState();
    result_ = &result;
    view_ = view;
    children_.resize(result.nodes.size());
    expanded_.resize(result.nodes.size());
    for (size_t i = 0; i < result.nodes.size(); ++i)
        for (int32_t child : result.nodes[i].children)
            if (result.nodes[child].Present(view)) children_[i].push_back(child);
    for (int32_t root : result.roots) {
        if (!result.nodes[root].Present(view)) continue;
        roots_.push_back(root);
        expanded_[root] = 1;
    }
    Sort(1, true);
}
void CompareTreeState::Sort(int column, bool descending) {
    auto less = [&](int32_t a, int32_t b) {
        const auto& x = result_->nodes[a];
        const auto& y = result_->nodes[b];
        int order = 0;
        if (column == 1 || column == 2) {
            auto xm = x.Metrics(view_), ym = y.Metrics(view_);
            auto xv = column == 1 ? xm.bytes : xm.count;
            auto yv = column == 1 ? ym.bytes : ym.count;
            order = xv < yv ? -1 : xv > yv ? 1 : 0;
        } else order = (column == 3 ? x.library : x.function).compare(column == 3 ? y.library : y.function);
        if (order) return descending ? order > 0 : order < 0;
        if (x.function != y.function) return x.function < y.function;
        return x.library < y.library;
    };
    for (auto& children : children_) std::sort(children.begin(), children.end(), less);
    std::sort(roots_.begin(), roots_.end(), less);
    Rebuild();
}
void CompareTreeState::Rebuild() {
    rows_.clear();
    std::vector<Row> pending;
    for (auto i = roots_.rbegin(); i != roots_.rend(); ++i) pending.push_back({*i, 0, !children_[*i].empty()});
    while (!pending.empty()) {
        auto row = pending.back(); pending.pop_back();
        rows_.push_back(row);
        if (!expanded_[row.node]) continue;
        for (auto i = children_[row.node].rbegin(); i != children_[row.node].rend(); ++i)
            pending.push_back({*i, row.depth + 1, !children_[*i].empty()});
    }
}
void CompareTreeState::Toggle(int32_t node) {
    expanded_[node] = !expanded_[node];
    Rebuild();
}
void CompareTreeState::ExpandAll(bool expand) {
    std::fill(expanded_.begin(), expanded_.end(), expand ? 1 : 0);
    Rebuild();
}
void CompareTreeState::Search(const std::string& text) {
    std::snprintf(searchText, sizeof(searchText), "%s", text.c_str());
    matches_.clear();
    matchIndex_ = -1;
    selected = -1;
    scrollToSelection = false;
    if (text.empty()) return;
    const auto needle = Lower(text);
    for (size_t i = 0; i < result_->nodes.size(); ++i) {
        const auto& node = result_->nodes[i];
        if (node.Present(view_) && (Lower(node.function).find(needle) != std::string::npos ||
                                   Lower(node.library).find(needle) != std::string::npos))
            matches_.push_back(int32_t(i));
    }
}
void CompareTreeState::Reveal(int32_t node) {
    for (int32_t parent = result_->nodes[node].parent; parent >= 0; parent = result_->nodes[parent].parent)
        expanded_[parent] = 1;
    Rebuild();
    selected = node;
    scrollToSelection = true;
}
void CompareTreeState::NextMatch(int direction) {
    if (matches_.empty()) return;
    if (matchIndex_ < 0) matchIndex_ = direction > 0 ? 0 : int(matches_.size()) - 1;
    else matchIndex_ = (matchIndex_ + direction + int(matches_.size())) % int(matches_.size());
    Reveal(matches_[matchIndex_]);
}
} // namespace gui
