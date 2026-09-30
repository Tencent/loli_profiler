#include "profilecomparatorlite.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <functional>
#include <ostream>

using loli::CallStack;
using loli::Record;
using loli::Session;

namespace {

// Exact port of sizeToString (src/stacktracemodel.cpp). Formatting uses
// printf %.2f, which matches QString::number(double, 'f', 2) for all
// non-NaN doubles (both round-half-even via the CRT).
std::string SizeToString(uint64_t size) {
    char buf[64];
    if (size >= 1024ull * 1024ull * 1024ull) {
        std::snprintf(buf, sizeof(buf), "%.2f GB",
                      static_cast<double>(size) / 1024.0 / 1024.0 / 1024.0);
    } else if (size >= 1024ull * 1024ull) {
        std::snprintf(buf, sizeof(buf), "%.2f MB",
                      static_cast<double>(size) / 1024.0 / 1024.0);
    } else if (size > 1024) {
        std::snprintf(buf, sizeof(buf), "%.2f KB",
                      static_cast<double>(size) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu Bytes",
                      static_cast<unsigned long long>(size));
    }
    return buf;
}

// Case-sensitive string compare parity: QString::compare is by UTF-16 code
// units; std::string comparison is by UTF-8 bytes. The orders coincide for
// the ASCII library/symbol strings seen in practice, which the differential
// test validates on real captures.

// QString::arg(quint64, 0, 16) parity: lowercase hex, no padding.
std::string ToHex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace

ProfileComparatorLite::~ProfileComparatorLite() {
    for (CallTreeNode* root : deltaRoots_) DeleteTree(root);
}

void ProfileComparatorLite::DeleteTree(CallTreeNode* node) {
    if (!node) return;
    for (CallTreeNode* child : node->children) DeleteTree(child);
    delete node;
}

bool ProfileComparatorLite::LoadProfile(const std::string& filePath,
                                        bool isBaseline) {
    compared_ = signedComparison_ = false;
    (isBaseline ? baselineLoaded_ : comparisonLoaded_) = false;
    errorMessage_.clear();
    Session& session = isBaseline ? baselineSession_ : comparisonSession_;

    std::vector<uint8_t> bytes;
    {
        std::ifstream file(std::filesystem::u8path(filePath), std::ios::binary);
        if (!file) {
            errorMessage_ = "Cannot open file: " + filePath;
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) {
            errorMessage_ = "Cannot determine file size: " + filePath;
            return false;
        }
        file.seekg(0, std::ios::beg);
        bytes.resize(static_cast<std::size_t>(size));
        if (size > 0) {
            file.read(reinterpret_cast<char*>(bytes.data()),
                      static_cast<std::streamsize>(size));
            if (!file) {
                errorMessage_ = "Failed to read file: " + filePath;
                return false;
            }
        }
    }

    std::string err;
    if (!loli::ReadSession(bytes.data(), bytes.size(), session, &err)) {
        errorMessage_ = "Failed to load " +
                        std::string(isBaseline ? "baseline" : "comparison") +
                        ": " + filePath + " (" + err + ")";
        return false;
    }

    if (isBaseline) {
        baselineLoaded_ = true;
        baselinePath_ = filePath;
    } else {
        comparisonLoaded_ = true;
        comparisonPath_ = filePath;
    }

    compared_ = false; // Reset comparison state
    return true;
}

std::unordered_map<std::size_t, ProfileComparatorLite::CallTreeNode*>
ProfileComparatorLite::BuildCallTree(const TreeSource& source,
                                     std::vector<CallTreeNode*>& roots) {
    std::unordered_map<std::size_t, CallTreeNode*> nodes;
    struct StackTotals {
        const Record* record;
        int64_t bytes;
        int64_t count;
    };
    std::unordered_map<LoliUuid, std::size_t, LoliUuidHash> stackIndices;
    std::vector<StackTotals> stacks;
    stackIndices.reserve(source.callStackMap.size());
    stacks.reserve(source.callStackMap.size());
    // Resolve each shared UUID once, retaining first-arrival metadata/order.
    for (const auto& record : source.records) {
        const auto added = stackIndices.emplace(record.uuid, stacks.size());
        if (added.second)
            stacks.push_back({&record, record.size, 1});
        else {
            auto& total = stacks[added.first->second];
            total.bytes += record.size;
            ++total.count;
        }
    }
    for (const auto& total : stacks) {
        const auto found = source.callStackMap.find(total.record->uuid);
        if (found == source.callStackMap.end())
            continue;
        const auto& stack = found->second;
        const int end = static_cast<int>(stack.size()) - skipRootLevels_;
        if (end <= 0)
            continue;
        CallTreeNode* parent = nullptr;
        std::size_t parentId = 0;
        // Parent-before-child insertion guarantees an acyclic graph even
        // when the unordered_map hash function produces collisions.
        for (int index = end; index-- > 0;) {
            const auto& frame = stack[static_cast<std::size_t>(index)];
            std::string library;
            const auto lib = source.internTable.find(frame.first);
            if (lib != source.internTable.end())
                library = lib->second;
            std::string function;
            const auto symbols = source.symbolMap.find(library);
            if (symbols != source.symbolMap.end()) {
                const auto symbol = symbols->second.find(frame.second);
                if (symbol != symbols->second.end())
                    function = symbol->second;
            }
            if (function.empty())
                function = library + "!0x" + ToHex(frame.second);
            PathKey key{parentId, library, function};
            const auto identity = pathIds_.emplace(std::move(key), pathIds_.size() + 1);
            const std::size_t id = identity.first->second;
            auto item = nodes.find(id);
            CallTreeNode* node;
            if (item == nodes.end()) {
                node = new CallTreeNode();
                node->functionName = std::move(function);
                node->libraryName = std::move(library);
                node->functionAddress = frame.second;
                node->libraryHash = frame.first;
                node->parent = parent;
                nodes.emplace(id, node);
                if (parent)
                    parent->children.push_back(node);
                else
                    roots.push_back(node);
            } else {
                node = item->second;
            }
            node->size += total.bytes;
            node->count += total.count;
            parent = node;
            parentId = id;
        }
    }
    return nodes;
}

bool ProfileComparatorLite::Compare(int skipRootLevels) {
    compared_ = false;
    signedComparison_ = false;
    if (!baselineLoaded_ || !comparisonLoaded_) {
        errorMessage_ = "Both baseline and comparison profiles must be loaded first";
        return false;
    }
    if (!loli::CompareSessions(baselineSession_, comparisonSession_,
                              comparisonResult_, errorMessage_, skipRootLevels))
        return false;
    stats_ = {};
    stats_.baselineTotalSize = comparisonResult_.base.bytes;
    stats_.comparisonTotalSize = comparisonResult_.comparison.bytes;
    stats_.baselineAllocCount = comparisonResult_.base.count;
    stats_.comparisonAllocCount = comparisonResult_.comparison.count;
    stats_.sizeDelta = comparisonResult_.comparison.bytes - comparisonResult_.base.bytes;
    stats_.changedAllocations = comparisonResult_.changedStacks;
    stats_.newAllocationsCount = comparisonResult_.newStacks;
    stats_.removedAllocationsCount = comparisonResult_.removedStacks;
    comparisonResult_.basePath = baselinePath_;
    comparisonResult_.comparisonPath = comparisonPath_;
    signedComparison_ = compared_ = true;
    return true;
}

bool ProfileComparatorLite::DumpProfile(int skipRootLevels) {
    compared_ = false;
    signedComparison_ = false;
    if (skipRootLevels < 0) {
        errorMessage_ = "Root levels must be a non-negative integer";
        return false;
    }
    if (!baselineLoaded_) {
        errorMessage_ = "Profile must be loaded first (call LoadProfile with isBaseline=true)";
        return false;
    }

    skipRootLevels_ = skipRootLevels;

    for (CallTreeNode* root : deltaRoots_) DeleteTree(root);
    deltaRoots_.clear();

    // Filter out freed allocations to show only live memory (parity with
    // the Qt DumpProfile ProfileData copy; the Qt version used a
    // QHash<quint64, quint32> freeAddrMap lookup, so build the same lookup).
    std::unordered_map<uint64_t, uint32_t> freeAddrLookup;
    freeAddrLookup.reserve(baselineSession_.freeAddrMap.size() * 2);
    for (const auto& entry : baselineSession_.freeAddrMap) {
        freeAddrLookup.emplace(entry.first, entry.second);
    }
    std::vector<Record> filteredRecords;
    filteredRecords.reserve(baselineSession_.records.size());
    for (const auto& record : baselineSession_.records) {
        const auto it = freeAddrLookup.find(record.addr);
        if (it != freeAddrLookup.end() && record.seq < it->second) {
            continue; // This allocation was freed later, skip it
        }
        filteredRecords.push_back(record);
    }

    // Statistics from filtered (live) allocations
    stats_ = ComparisonStats();
    stats_.baselineAllocCount = static_cast<int64_t>(filteredRecords.size());
    stats_.comparisonAllocCount = 0;
    stats_.baselineTotalSize = 0;
    stats_.comparisonTotalSize = 0;
    stats_.sizeDelta = 0;
    stats_.changedAllocations = 0;
    stats_.newAllocationsCount = 0;
    for (const auto& record : filteredRecords) {
        stats_.baselineTotalSize += static_cast<uint64_t>(record.size);
    }

    TreeSource source{filteredRecords, baselineSession_.callStackMap,
                      baselineSession_.symbolMap, baselineSession_.internTable};
    pathIds_.clear();
    BuildCallTree(source, deltaRoots_);
    pathIds_.clear();

    compared_ = true;
    return true;
}

bool ProfileComparatorLite::ExportDumpToText(const std::string& outputPath) {
    if (!compared_ || signedComparison_) {
        errorMessage_ = "Must call DumpProfile() before exporting";
        return false;
    }

    // QIODevice::Text on Windows translates "\n" to "\r\n"; replicate with
    // std::ios::binary-less fstream default text mode (MSVC translates on
    // write identically).
    std::ofstream file(outputPath);
    if (!file) {
        errorMessage_ = "Cannot create output file: " + outputPath;
        return false;
    }

    file << "=== LoliProfiler Profile Report ===\n\n";
    file << "Total allocations: " << stats_.baselineAllocCount << "\n";
    file << "Total size: " << SizeToString(stats_.baselineTotalSize) << "\n\n";

    file << "=== Memory Allocations ===\n\n";

    // Sort root nodes by size (descending order) before writing
    std::vector<CallTreeNode*> sortedRoots = deltaRoots_;
    std::sort(sortedRoots.begin(), sortedRoots.end(),
              [](CallTreeNode* a, CallTreeNode* b) {
                  return a->size > b->size; // Descending order (largest first)
              });

    for (CallTreeNode* root : sortedRoots) {
        WriteCallTreeToTextAbsolute(file, root, 0);
    }

    // QTextStream << "\n" flushes; not needed for correctness here since the
    // destructor flushes, but keep the close-then-check parity.
    file.close();
    return file.good();
}

bool ProfileComparatorLite::ExportToText(const std::string& outputPath) {
    if (!compared_ || !signedComparison_) {
        errorMessage_ = "Must call Compare() before exporting";
        return false;
    }
    return loli::WriteComparisonReport(comparisonResult_, outputPath, errorMessage_);
}


void ProfileComparatorLite::WriteCallTreeToTextAbsolute(std::ostream& stream,
                                                        CallTreeNode* node,
                                                        int depth) {
    if (!node) return;

    for (int i = 0; i < depth; ++i) {
        stream << "    "; // 4 spaces for indentation
    }

    // Function name, absolute size, absolute count (no +/- prefix)
    stream << node->functionName << ", "
           << SizeToString(static_cast<uint64_t>(node->size)) << ", "
           << node->count << "\n";

    std::vector<CallTreeNode*> sortedChildren = node->children;
    std::sort(sortedChildren.begin(), sortedChildren.end(),
              [](CallTreeNode* a, CallTreeNode* b) {
                  return a->size > b->size;
              });

    for (CallTreeNode* child : sortedChildren) {
        WriteCallTreeToTextAbsolute(stream, child, depth + 1);
    }
}


bool ProfileComparatorLite::ExportToLoli(const std::string&) {
    errorMessage_ = "Signed comparison cannot be represented faithfully in capture .loli format; export .txt instead";
    return false;
}
