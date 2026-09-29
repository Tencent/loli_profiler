#include "profilecomparatorlite.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <ostream>

#include "hashstringlite.h"

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

// qHash(uint) is identity on 32-bit platforms and on 64-bit MSVC/Unix LP64
// (qHash(quint64) folds, but the key here is quint32). QHashCombine fold,
// seed 0 - mirrors qHashRange over a container of QStrings where each
// string contributes its qHash value.
inline uint32_t HashCombine(uint32_t seed, uint32_t value) {
    return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
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
    Session& session = isBaseline ? baselineSession_ : comparisonSession_;

    std::vector<uint8_t> bytes;
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            errorMessage_ = "Cannot open file: " + filePath;
            return false;
        }
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
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
    } else {
        comparisonLoaded_ = true;
    }

    compared_ = false; // Reset comparison state
    return true;
}

uint32_t ProfileComparatorLite::SuffixHash(
    const std::vector<uint32_t>& nameHashes, std::size_t idx) {
    uint32_t seed = 0;
    for (std::size_t i = idx; i < nameHashes.size(); ++i)
        seed = HashCombine(seed, nameHashes[i]);
    return seed;
}

std::unordered_map<uint32_t, ProfileComparatorLite::CallTreeNode*>
ProfileComparatorLite::BuildCallTreeWithHashMap(const TreeSource& source,
                                                std::vector<CallTreeNode*>& roots) {
    // Port of the Qt BuildCallTreeWithHashMap: node map keyed by the
    // qHashRange hash of the callstack suffix (leaf-first), so identical
    // suffixes merge exactly as they did in the Qt build.
    std::unordered_map<uint32_t, CallTreeNode*> nodeMap;

    // Scratch buffers reused across records.
    std::vector<std::string> callstackNames;
    std::vector<std::pair<std::string, uint64_t>> callstackMetadata;
    std::vector<uint32_t> callstackHashes;
    std::vector<uint32_t> suffixHashes;

    for (const auto& record : source.records) {
        const auto stackIt = source.callStackMap.find(record.uuid);
        if (stackIt == source.callStackMap.end()) {
            continue;
        }

        const CallStack& callStack = stackIt->second;

        // Skip if call stack is too short after skipRootLevels_
        if (static_cast<int>(callStack.size()) <= skipRootLevels_) {
            continue;
        }

        // Build the frame list, skipping root levels. Call stacks are
        // stored leaf-first (index 0 = allocation site, last index = root);
        // iterate 0 .. size - skipRootLevels - matching the Qt code.
        callstackNames.clear();
        callstackMetadata.clear();
        callstackHashes.clear();
        const int endIndex = static_cast<int>(callStack.size()) - skipRootLevels_;
        callstackNames.reserve(static_cast<std::size_t>(endIndex));
        callstackMetadata.reserve(static_cast<std::size_t>(endIndex));
        callstackHashes.reserve(static_cast<std::size_t>(endIndex));
        for (int i = 0; i < endIndex; ++i) {
            const auto& frame = callStack[static_cast<std::size_t>(i)];

            // Resolve library name through the session intern table
            // (HashString::hashmap_ parity). Missing hash codes resolve to
            // an empty name, matching QHash operator[] defaulting.
            std::string libraryName;
            const auto libIt = source.internTable.find(frame.first);
            if (libIt != source.internTable.end()) {
                libraryName = libIt->second;
            }
            const uint64_t funcAddr = frame.second;

            // Resolve function name from the symbol map.
            std::string funcName;
            const auto symLibIt = source.symbolMap.find(libraryName);
            if (symLibIt != source.symbolMap.end()) {
                const auto symIt = symLibIt->second.find(funcAddr);
                if (symIt != symLibIt->second.end()) {
                    funcName = symIt->second;
                } else {
                    funcName = libraryName + "!0x" + ToHex(funcAddr);
                }
            } else {
                funcName = libraryName + "!0x" + ToHex(funcAddr);
            }

            callstackNames.push_back(std::move(funcName));
            callstackMetadata.emplace_back(std::move(libraryName), funcAddr);
            // qHash(QString) seed-0 fold over UTF-16 units
            // (HashStringLite::Hash parity - same algorithm).
            callstackHashes.push_back(HashStringLite::Hash(
                callstackNames.back()));
        }

        // Precompute every suffix hash once (matches qHashRange over
        // callstackNames.begin() + idx .. end()).
        suffixHashes.assign(callstackHashes.size(), 0);
        {
            uint32_t seed = 0;
            for (std::size_t i = callstackHashes.size(); i-- > 0;) {
                seed = HashCombine(seed, callstackHashes[i]);
                suffixHashes[i] = seed;
            }
        }

        // Build the tree using hash-based merging.
        CallTreeNode* child = nullptr;
        for (std::size_t idx = 0; idx < callstackNames.size(); ++idx) {
            const uint32_t curHash = suffixHashes[idx];
            auto itemIt = nodeMap.find(curHash);
            CallTreeNode* node = nullptr;

            if (itemIt != nodeMap.end()) {
                // Node exists - update size/count for the entire parent chain
                node = itemIt->second;
                CallTreeNode* parent = node;
                while (parent != nullptr) {
                    parent->size += record.size;
                    parent->count += 1;
                    parent = parent->parent;
                }

                // Attach child if we created one
                if (child != nullptr) {
                    node->children.push_back(child);
                    child->parent = node;
                }
                break; // Stop - rest of the chain already exists
            }

            // Create new node
            node = new CallTreeNode();
            node->functionName = callstackNames[idx];
            node->libraryName = callstackMetadata[idx].first;
            node->functionAddress = callstackMetadata[idx].second;
            // Original intern-table key for this frame (ExportToLoli re-emits
            // it as the frame's libHash).
            node->libraryHash = callStack[static_cast<std::size_t>(idx)].first;
            node->size = record.size;
            node->count = 1;
            node->parent = nullptr;
            nodeMap.emplace(curHash, node);

            // Attach child
            if (child != nullptr) {
                node->children.push_back(child);
                child->parent = node;
            }

            child = node;
        }

        // Add to roots if this is a top-level node
        if (child != nullptr && child->parent == nullptr) {
            bool alreadyInRoots = false;
            for (CallTreeNode* root : roots) {
                if (root == child) {
                    alreadyInRoots = true;
                    break;
                }
            }
            if (!alreadyInRoots) {
                roots.push_back(child);
            }
        }
    }

    return nodeMap;
}

bool ProfileComparatorLite::Compare(int skipRootLevels) {
    if (!baselineLoaded_ || !comparisonLoaded_) {
        errorMessage_ = "Both baseline and comparison profiles must be loaded first";
        return false;
    }

    skipRootLevels_ = skipRootLevels;

    for (CallTreeNode* root : deltaRoots_) DeleteTree(root);
    deltaRoots_.clear();

    // Basic statistics
    stats_ = ComparisonStats();
    stats_.baselineAllocCount = static_cast<int>(baselineSession_.records.size());
    stats_.comparisonAllocCount = static_cast<int>(comparisonSession_.records.size());

    for (const auto& record : baselineSession_.records) {
        stats_.baselineTotalSize += static_cast<uint64_t>(record.size);
    }
    for (const auto& record : comparisonSession_.records) {
        stats_.comparisonTotalSize += static_cast<uint64_t>(record.size);
    }

    stats_.sizeDelta = static_cast<int64_t>(stats_.comparisonTotalSize) -
                       static_cast<int64_t>(stats_.baselineTotalSize);

    // Build call trees (O(1) lookup by suffix hash)
    std::vector<CallTreeNode*> baselineRoots;
    std::vector<CallTreeNode*> comparisonRoots;

    TreeSource baselineSource{baselineSession_.records, baselineSession_.callStackMap,
                              baselineSession_.symbolMap, baselineSession_.internTable};
    TreeSource comparisonSource{comparisonSession_.records,
                                comparisonSession_.callStackMap,
                                comparisonSession_.symbolMap,
                                comparisonSession_.internTable};

    auto baselineHashmap = BuildCallTreeWithHashMap(baselineSource, baselineRoots);
    auto comparisonHashmap = BuildCallTreeWithHashMap(comparisonSource, comparisonRoots);

    // Size limiter - ignore small differences (1 KiB)
    const uint64_t sizeLimiter = 1024;

    // Leaf nodes for the bottom-up propagation
    std::vector<CallTreeNode*> leafItems;
    int newAllocationsCounter = 0;

    // Diff leaf nodes only (matching the Qt on_actionShow_Leaks_triggered logic)
    for (auto& kv : comparisonHashmap) {
        const uint32_t key = kv.first;
        CallTreeNode* compNode = kv.second;
        const auto baselineIt = baselineHashmap.find(key);

        // Skip non-leaf nodes
        if (compNode->children.size() != 0) {
            compNode->size = 0;
            compNode->count = 0;
            continue;
        }

        leafItems.push_back(compNode);

        if (baselineIt == baselineHashmap.end()) {
            // NEW ALLOCATION - exists in comparison only
            if (compNode->size < static_cast<int64_t>(sizeLimiter)) {
                compNode->size = 0;
                compNode->count = 0;
            } else {
                // Keep original values as delta, count as new allocation
                newAllocationsCounter++;
            }
        } else {
            // EXISTING ALLOCATION - calculate delta
            CallTreeNode* baseNode = baselineIt->second;
            const int64_t newSize = compNode->size - baseNode->size;
            const int64_t newCount = compNode->count - baseNode->count;

            // Filter small differences
            if (newSize < static_cast<int64_t>(sizeLimiter) || newCount <= 0) {
                compNode->size = 0;
                compNode->count = 0;
            } else {
                compNode->size = newSize;
                compNode->count = newCount;
            }
        }
    }

    // Release baseline tree - no longer needed
    for (CallTreeNode* root : baselineRoots) DeleteTree(root);
    baselineRoots.clear();
    baselineHashmap.clear();

    // Recalculate parent size & count from leaf nodes (bottom-up).
    // First reset all non-leaf nodes to zero.
    for (auto& kv : comparisonHashmap) {
        CallTreeNode* node = kv.second;
        if (node->children.size() > 0) {
            node->size = 0;
            node->count = 0;
        }
    }

    // Then propagate leaf values up to parents
    for (CallTreeNode* leaf : leafItems) {
        CallTreeNode* parent = leaf->parent;
        while (parent != nullptr) {
            parent->size += leaf->size;
            parent->count += leaf->count;
            parent = parent->parent;
        }
    }
    leafItems.clear();

    // Count statistics and remove zero-count nodes
    stats_.changedAllocations = 0;
    stats_.newAllocationsCount = newAllocationsCounter;

    for (auto& kv : comparisonHashmap) {
        CallTreeNode* node = kv.second;
        if (node->count > 0) {
            stats_.changedAllocations++;
        } else {
            // Remove from parent's children list
            if (node->parent) {
                auto& siblings = node->parent->children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), node),
                               siblings.end());
            } else {
                comparisonRoots.erase(
                    std::remove(comparisonRoots.begin(), comparisonRoots.end(), node),
                    comparisonRoots.end());
            }
        }
    }
    comparisonHashmap.clear();

    // Store delta roots for export (transfer ownership)
    deltaRoots_ = std::move(comparisonRoots);

    compared_ = true;
    return true;
}

bool ProfileComparatorLite::DumpProfile(int skipRootLevels) {
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
    stats_.baselineAllocCount = static_cast<int>(filteredRecords.size());
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
    BuildCallTreeWithHashMap(source, deltaRoots_);

    compared_ = true;
    return true;
}

bool ProfileComparatorLite::ExportDumpToText(const std::string& outputPath) {
    if (!compared_) {
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
    if (!compared_) {
        errorMessage_ = "Must call Compare() before exporting";
        return false;
    }

    std::ofstream file(outputPath);
    if (!file) {
        errorMessage_ = "Cannot create output file: " + outputPath;
        return false;
    }

    file << "=== LoliProfiler Comparison Report ===\n\n";
    file << "Baseline allocations: " << stats_.baselineAllocCount << "\n";
    file << "Comparison allocations: " << stats_.comparisonAllocCount << "\n";
    file << "Baseline total size: " << SizeToString(stats_.baselineTotalSize) << "\n";
    file << "Comparison total size: " << SizeToString(stats_.comparisonTotalSize) << "\n";
    file << "Size delta: ";
    if (stats_.sizeDelta >= 0) {
        file << "+" << SizeToString(static_cast<uint64_t>(stats_.sizeDelta));
    } else {
        file << "-" << SizeToString(static_cast<uint64_t>(-stats_.sizeDelta));
    }
    file << "\n\n";

    file << "Changed allocations (>1KB growth): " << stats_.changedAllocations << "\n";
    file << "New allocations (not in baseline): " << stats_.newAllocationsCount << "\n\n";

    file << "=== Memory Growth (Delta: Comparison - Baseline) ===\n\n";

    // Sort root nodes by size (descending order) before writing
    std::vector<CallTreeNode*> sortedRoots = deltaRoots_;
    std::sort(sortedRoots.begin(), sortedRoots.end(),
              [](CallTreeNode* a, CallTreeNode* b) {
                  return a->size > b->size;
              });

    for (CallTreeNode* root : sortedRoots) {
        WriteCallTreeToText(file, root, 0);
    }

    file.close();
    return file.good();
}

void ProfileComparatorLite::WriteCallTreeToText(std::ostream& stream,
                                                CallTreeNode* node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; ++i) {
        stream << "    "; // 4 spaces for indentation
    }

    stream << node->functionName << ", ";

    // Size with +/- prefix for deltas
    if (node->size > 0) {
        stream << "+" << SizeToString(static_cast<uint64_t>(node->size));
    } else if (node->size < 0) {
        stream << "-" << SizeToString(static_cast<uint64_t>(-node->size));
    } else {
        stream << SizeToString(0);
    }

    stream << ", ";

    // Count with +/- prefix for deltas
    if (node->count > 0) {
        stream << "+" << node->count;
    } else if (node->count < 0) {
        stream << node->count; // Already has negative sign
    } else {
        stream << "0";
    }

    stream << "\n";

    // Sort children by size (descending order) before writing
    std::vector<CallTreeNode*> sortedChildren = node->children;
    std::sort(sortedChildren.begin(), sortedChildren.end(),
              [](CallTreeNode* a, CallTreeNode* b) {
                  return a->size > b->size;
              });

    for (CallTreeNode* child : sortedChildren) {
        WriteCallTreeToText(stream, child, depth + 1);
    }
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

void ProfileComparatorLite::ConvertDeltaTreeToRecords(
    std::vector<Record>& stackRecords,
    std::unordered_map<LoliUuid, CallStack, LoliUuidHash>& callStackMap) {
    // Delta tree -> StackRecords + callstack map. Each leaf with a non-zero
    // delta becomes one record; the callstack path is stored leaf-first
    // (allocation site first, root last).
    stackRecords.clear();
    callStackMap.clear();

    uint32_t seqCounter = 0;

    // Library-name -> hashcode resolution (HashString::hashmap_ reverse
    // lookup parity): use the tree node's retained original libHash, which
    // is the intern-table key the loaded session carried.
    std::function<void(CallTreeNode*, std::vector<CallTreeNode*>&)> traverseTree;
    traverseTree = [&](CallTreeNode* node, std::vector<CallTreeNode*>& callStackPath) {
        if (!node) return;

        callStackPath.push_back(node);

        if (node->children.empty() && (node->size != 0 || node->count != 0)) {
            Record record;
            record.uuid = LoliUuid::CreateUuid();
            record.seq = seqCounter++;
            record.time = 0; // No meaningful timestamp for a delta
            record.size = static_cast<int32_t>(node->size);
            record.addr = 0;
            record.funcAddr = 0;
            record.libHash = 0;

            // Build callstack leaf-first: reverse the root-to-leaf path.
            CallStack callstack;
            callstack.reserve(callStackPath.size());
            for (auto it = callStackPath.rbegin(); it != callStackPath.rend(); ++it) {
                callstack.emplace_back((*it)->libraryHash, (*it)->functionAddress);
            }

            if (!callstack.empty()) {
                record.libHash = callstack[0].first;
                record.funcAddr = callstack[0].second;
            }

            stackRecords.push_back(record);
            callStackMap.emplace(record.uuid, std::move(callstack));
        }

        for (CallTreeNode* child : node->children) {
            traverseTree(child, callStackPath);
        }

        callStackPath.pop_back();
    };

    for (CallTreeNode* root : deltaRoots_) {
        std::vector<CallTreeNode*> path;
        traverseTree(root, path);
    }
}

bool ProfileComparatorLite::ExportToLoli(const std::string& outputPath) {
    if (!compared_) {
        errorMessage_ = "Must call Compare() before exporting";
        return false;
    }

    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        errorMessage_ = "Cannot create output file: " + outputPath;
        return false;
    }

    std::vector<Record> stackRecords;
    std::unordered_map<LoliUuid, CallStack, LoliUuidHash> callStackMap;
    ConvertDeltaTreeToRecords(stackRecords, callStackMap);

    // Delta comparison session: empty meminfo/screenshots/smaps/freeaddr.
    Session outSession;
    outSession.records = std::move(stackRecords);
    outSession.callStackMap = std::move(callStackMap);

    // Intern table: keep the intern table of the comparison session (the
    // delta nodes carry original hash codes resolved from it). The Qt
    // version wrote HashString::hashmap_, which both loads had merged into
    // the same global map - union both loaded sessions' tables.
    outSession.internTable = comparisonSession_.internTable;
    for (const auto& kv : baselineSession_.internTable) {
        outSession.internTable.insert(kv);
    }

    // Symbol map from comparison data, merged with baseline symbols not
    // already present (std::map iteration order is deterministic, matching
    // what the format needs; both readers accept any order).
    outSession.symbolMap = comparisonSession_.symbolMap;
    for (const auto& lib : baselineSession_.symbolMap) {
        auto& targetSymbols = outSession.symbolMap[lib.first];
        for (const auto& sym : lib.second) {
            if (targetSymbols.find(sym.first) == targetSymbols.end()) {
                targetSymbols[sym.first] = sym.second;
            }
        }
    }

    std::vector<uint8_t> bytes;
    if (!loli::WriteSession(outSession, bytes)) {
        errorMessage_ = "Failed to serialize delta session";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file.good()) {
        errorMessage_ = "Failed to write output file: " + outputPath;
        return false;
    }
    return true;
}
