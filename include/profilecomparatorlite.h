#ifndef PROFILECOMPARATORLITE_H
#define PROFILECOMPARATORLITE_H

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "lolirecord.h"
#include "profilecomparison.h"

// Compatibility facade for CLI snapshot dump and comparison callers.
// Compare delegates to the exact signed live-allocation API in
// profilecomparison.h. DumpProfile retains the legacy snapshot tree and
// SQLite schema. New file comparison consumers should call CompareFiles
// directly, which releases each parsed session before loading the next.
class ProfileComparatorLite {
public:
    ProfileComparatorLite() = default;
    ~ProfileComparatorLite();

    ProfileComparatorLite(const ProfileComparatorLite&) = delete;
    ProfileComparatorLite& operator=(const ProfileComparatorLite&) = delete;

    // Load a .loli profile file via loli::ReadSession.
    // isBaseline: true for baseline/file1, false for comparison/file2.
    bool LoadProfile(const std::string& filePath, bool isBaseline);

    // Compare the two loaded profiles (both must be loaded first).
    // skipRootLevels: number of root stack frames to skip (default 0).
    bool Compare(int skipRootLevels = 0);

    // Build call tree from a single loaded profile (baseline only),
    // filtering freed allocations via the freeaddr map.
    bool DumpProfile(int skipRootLevels = 0);

    // Comparison report (delta format with +/- prefix). Requires Compare().
    bool ExportToText(const std::string& outputPath);

    // Single profile dump, absolute values (no +/- prefix). Requires
    // DumpProfile().
    bool ExportDumpToText(const std::string& outputPath);
    bool ExportDumpToSqlite(const std::string& outputPath);

    // Legacy capture export. Signed comparison returns a clear error because
    // .loli cannot faithfully represent signed allocation counts/self metrics.
    bool ExportToLoli(const std::string& outputPath);

    struct ComparisonStats {
        uint64_t baselineTotalSize = 0;
        uint64_t comparisonTotalSize = 0;
        int64_t sizeDelta = 0;         // positive = growth, negative = reduction
        int64_t baselineAllocCount = 0;
        int64_t comparisonAllocCount = 0;
        int64_t changedAllocations = 0; // changed allocation stack paths
        int64_t newAllocationsCount = 0;
        int64_t removedAllocationsCount = 0;
    };

    const ComparisonStats& GetStats() const { return stats_; }
    const std::string& GetErrorMessage() const { return errorMessage_; }

private:
    // Delta/snapshot tree node. Keeps (library name, function address) per
    // frame; library names are resolved strings (no interning needed).
    // libraryHash retains the original intern-table key from the loaded
    // session so ExportToLoli can re-emit the frame hash deterministically.
    struct CallTreeNode {
        std::string functionName;      // resolved symbol or "library!0xaddress"
        std::string libraryName;       // original library name
        uint64_t functionAddress = 0;  // original function address
        uint32_t libraryHash = 0;      // original libHash (0 when unresolved)
        int64_t size = 0;              // signed to support negative deltas
        int64_t count = 0;
        std::vector<CallTreeNode*> children;
        CallTreeNode* parent = nullptr;
    };

    // Recursive delete (children first), used instead of relying on a node
    // destructor recursing per-node on very deep trees.
    static void DeleteTree(CallTreeNode* node);

    // Inputs for tree building: a record list plus the shared session maps.
    // DumpProfile passes a filtered record list (freed allocations removed),
    // mirroring the Qt ProfileData copy.
    struct TreeSource {
        const std::vector<loli::Record>& records;
        const std::unordered_map<LoliUuid, loli::CallStack, LoliUuidHash>& callStackMap;
        const std::unordered_map<std::string,
            std::unordered_map<uint64_t, std::string>>& symbolMap;
        const std::unordered_map<uint32_t, std::string>& internTable;
    };

    // Hash-based call tree building, matching the Qt implementation (and
    // MainWindow::GetMergedCallstacks): the node map key is the qHashRange
    // value of the callstack suffix from the current frame to the end, so
    // merge decisions are identical to the Qt build (same hash, same key
    // equality semantics on the uint32 value).
    std::unordered_map<uint32_t, CallTreeNode*> BuildCallTreeWithHashMap(
        const TreeSource& source, std::vector<CallTreeNode*>& roots);

    // qHashRange(...) over the per-frame string hashes: Qt's
    // QHashCombine fold, seed 0.
    static uint32_t SuffixHash(const std::vector<uint32_t>& nameHashes,
                               std::size_t idx);

    void WriteCallTreeToTextAbsolute(std::ostream& stream, CallTreeNode* node, int depth);

    loli::Session baselineSession_;
    loli::Session comparisonSession_;
    std::string baselinePath_, comparisonPath_;
    ComparisonStats stats_;
    std::string errorMessage_;
    bool baselineLoaded_ = false;
    bool comparisonLoaded_ = false;
    bool compared_ = false;
    bool signedComparison_ = false;
    loli::ComparisonResult comparisonResult_;
    int skipRootLevels_ = 0;

    // Delta tree roots for export (owns the nodes).
    std::vector<CallTreeNode*> deltaRoots_;
};

#endif // PROFILECOMPARATORLITE_H
