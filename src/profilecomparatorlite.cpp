#include "profilecomparatorlite.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
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
    BuildCallTreeWithHashMap(source, deltaRoots_);

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
