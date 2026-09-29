#include "leakdetect.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "lolirecord.h"
#include "stacktracetree.h"

namespace gui {
namespace {

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string SymbolName(const loli::Session& session, const std::string& lib,
                       uint64_t addr) {
    auto find = [&](const std::string& key) -> std::string {
        auto byLib = session.symbolMap.find(key);
        if (byLib == session.symbolMap.end()) return {};
        auto byAddr = byLib->second.find(addr);
        return byAddr == byLib->second.end() ? std::string() : byAddr->second;
    };
    std::string name = find(lib);
    if (name.empty()) name = find(BaseName(lib));
    if (name.empty()) {
        char hex[32];
        std::snprintf(hex, sizeof(hex), "0x%llx",
                      static_cast<unsigned long long>(addr));
        name = hex;
    }
    return name;
}

} // namespace

std::shared_ptr<StacktraceTree> BuildPossibleLeaksTree(
    const loli::Session& session, int32_t startMs, int32_t endMs,
    bool persistentOnly) {
    auto tree = std::make_shared<StacktraceTree>();
    if (startMs < 0 || endMs <= startMs) return tree;

    std::unordered_map<uint64_t, uint32_t> freeSequence;
    if (persistentOnly) {
        freeSequence.reserve(session.freeAddrMap.size());
        for (const auto& [addr, seq] : session.freeAddrMap)
            freeSequence[addr] = std::max(freeSequence[addr], seq);
    }

    std::vector<uint32_t> sizes;
    std::vector<uint8_t> baseline;
    std::vector<std::vector<StacktraceTree::RawFrameIdx>> frames;
    std::vector<std::string> funcNames;
    std::vector<std::string> libNames;
    std::unordered_map<std::string, uint32_t> funcIndex;
    std::unordered_map<std::string, uint32_t> libIndex;
    auto intern = [](const std::string& name, std::vector<std::string>& names,
                     std::unordered_map<std::string, uint32_t>& indices) {
        auto it = indices.find(name);
        if (it != indices.end()) return it->second;
        const uint32_t index = static_cast<uint32_t>(names.size());
        names.push_back(name);
        indices.emplace(names.back(), index);
        return index;
    };
    for (const auto& record : session.records) {
        if (record.time > endMs || record.size <= 0) continue;
        if (persistentOnly) {
            auto it = freeSequence.find(record.addr);
            if (it != freeSequence.end() && record.seq < it->second) continue;
        }
        auto callstack = session.callStackMap.find(record.uuid);
        if (callstack == session.callStackMap.end() || callstack->second.empty())
            continue;
        sizes.push_back(static_cast<uint32_t>(record.size));
        baseline.push_back(record.time <= startMs ? 1 : 0);
        auto& row = frames.emplace_back();
        row.reserve(callstack->second.size());
        for (auto it = callstack->second.rbegin(); it != callstack->second.rend(); ++it) {
            auto libIt = session.internTable.find(it->first);
            const std::string lib = libIt == session.internTable.end() ?
                "unknown" : libIt->second;
            const std::string func = SymbolName(session, lib, it->second);
            row.push_back({intern(func, funcNames, funcIndex),
                           intern(lib, libNames, libIndex)});
        }
    }
    tree->BuildLeakDiffFromRecords(sizes, frames, funcNames, libNames, baseline);
    return tree;
}

} // namespace gui
