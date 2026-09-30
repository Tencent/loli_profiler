#ifndef LOLI_COMPARETREESTATE_H
#define LOLI_COMPARETREESTATE_H
#include "profilecomparison.h"
#include <vector>

namespace gui {

// UI-independent state, owned separately by each of the three dockable panels.
class CompareTreeState {
public:
    struct Row { int32_t node; int depth; bool hasChildren; };
    void Reset(const loli::ComparisonResult& result, loli::ComparisonView view);
    void Sort(int column, bool descending);
    void Toggle(int32_t node);
    void ExpandAll(bool expand);
    void Search(const std::string& text);
    void NextMatch(int direction);
    void Reveal(int32_t node);
    bool Expanded(int32_t node) const { return expanded_[node] != 0; }
    const std::vector<Row>& Rows() const { return rows_; }
    const std::vector<int32_t>& Matches() const { return matches_; }
    int MatchIndex() const { return matchIndex_; }
    int32_t selected = -1;
    bool scrollToSelection = false;
    char searchText[256] = {};
private:
    void Rebuild();
    const loli::ComparisonResult* result_ = nullptr;
    loli::ComparisonView view_ = loli::ComparisonView::Base;
    std::vector<std::vector<int32_t>> children_;
    std::vector<int32_t> roots_, matches_;
    std::vector<uint8_t> expanded_;
    std::vector<Row> rows_;
    int matchIndex_ = -1;
};

void DrawComparisonTree(const loli::ComparisonResult& result,
                        loli::ComparisonView view, CompareTreeState& state,
                        const char* emptyMessage = nullptr);
} // namespace gui
#endif
