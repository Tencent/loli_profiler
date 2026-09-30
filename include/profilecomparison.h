#ifndef LOLI_PROFILECOMPARISON_H
#define LOLI_PROFILECOMPARISON_H

#include "lolirecord.h"
#include <functional>
#include <string>
#include <vector>

namespace loli {

enum class ComparisonView { Base, Comparison, Diff };

struct AllocationMetrics {
    int64_t bytes = 0;
    int64_t count = 0;
};

struct ComparisonNode {
    std::string function;
    std::string library;
    int32_t parent = -1;
    std::vector<int32_t> children;
    AllocationMetrics base, comparison, baseSelf, comparisonSelf;
    bool changed = false; // includes changed descendants, even with zero net delta

    AllocationMetrics Metrics(ComparisonView view, bool self = false) const;
    bool Present(ComparisonView view) const;
};

struct ComparisonResult {
    std::vector<ComparisonNode> nodes; // parents precede children
    std::vector<int32_t> roots;
    AllocationMetrics base, comparison;
    int64_t changedStacks = 0, newStacks = 0, removedStacks = 0;
    std::string basePath, comparisonPath;
    int skipRootLevels = 0;
};

// Exact library-qualified symbol paths; unresolved frames use library+address.
// Saved free events exclude records older than the latest free at that address.
// Each record contributes to self metrics at its allocation site and inclusive
// metrics along the entire path. No size/count threshold or heuristic pairing.
bool CompareSessions(const Session& base, const Session& comparison,
                     ComparisonResult& result, std::string& error,
                     int skipRootLevels = 0);

// Parses and aggregates one file at a time to bound peak memory. Progress runs
// on the calling thread. On failure result is cleared (no partially usable tree).
bool CompareFiles(const std::string& base, const std::string& comparison,
                  ComparisonResult& result, std::string& error,
                  int skipRootLevels = 0,
                  const std::function<void(const std::string&)>& progress = {});

bool WriteComparisonReport(const ComparisonResult& result,
                           const std::string& path, std::string& error);
std::string FormatComparisonBytes(int64_t bytes, bool signedValue = false);

} // namespace loli
#endif
