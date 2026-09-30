#include "profilecomparison.h"
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace loli {

AllocationMetrics ComparisonNode::Metrics(ComparisonView view, bool self) const {
    const auto& a = self ? baseSelf : base;
    const auto& b = self ? comparisonSelf : comparison;
    if (view == ComparisonView::Base) return a;
    if (view == ComparisonView::Comparison) return b;
    return {b.bytes - a.bytes, b.count - a.count};
}

bool ComparisonNode::Present(ComparisonView view) const {
    return view == ComparisonView::Diff ? changed : Metrics(view).count != 0;
}

std::string FormatComparisonBytes(int64_t bytes, bool signedValue) {
    const uint64_t magnitude = bytes < 0 ? uint64_t(-(bytes + 1)) + 1 : uint64_t(bytes);
    double value = double(magnitude);
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    unsigned unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    char text[80];
    std::snprintf(text, sizeof(text), unit ? "%s%.2f %s" : "%s%.0f %s",
                  bytes < 0 ? "-" : (signedValue && bytes > 0 ? "+" : ""), value, units[unit]);
    return text;
}

namespace {
struct Key {
    int32_t parent;
    std::string library, function;
    bool operator==(const Key& b) const {
        return parent == b.parent && library == b.library && function == b.function;
    }
};
struct KeyHash {
    size_t operator()(const Key& key) const {
        size_t h = std::hash<std::string>{}(key.library);
        h ^= std::hash<std::string>{}(key.function) + (h << 6) + (h >> 2);
        return h ^ (std::hash<int32_t>{}(key.parent) + (h << 6) + (h >> 2));
    }
};

void Add(AllocationMetrics& target, AllocationMetrics value) {
    if (value.bytes > std::numeric_limits<int64_t>::max() - target.bytes ||
        value.count > std::numeric_limits<int64_t>::max() - target.count)
        throw std::runtime_error("Allocation totals exceed signed 64-bit range");
    target.bytes += value.bytes;
    target.count += value.count;
}

class Builder {
public:
    explicit Builder(ComparisonResult& result) : result_(result) {}

    void AddSession(const Session& session, bool baseline) {
        std::unordered_map<uint64_t, uint32_t> frees;
        for (const auto& free : session.freeAddrMap) {
            auto& seq = frees[free.first];
            seq = std::max(seq, free.second);
        }
        std::unordered_map<LoliUuid, int32_t, LoliUuidHash> leaves;
        leaves.reserve(session.callStackMap.size());
        for (const auto& record : session.records) {
            const auto freed = frees.find(record.addr);
            if (freed != frees.end() && record.seq < freed->second) continue;
            if (record.size < 0)
                throw std::runtime_error("Negative allocation size: input must be a capture, not a legacy delta file");
            int32_t leaf;
            const auto cached = leaves.find(record.uuid);
            if (cached != leaves.end()) leaf = cached->second;
            else {
                const auto stack = session.callStackMap.find(record.uuid);
                leaf = -1;
                if (stack != session.callStackMap.end() && !stack->second.empty()) {
                    const auto& frames = stack->second;
                    const size_t skip = std::min(size_t(result_.skipRootLevels), frames.size());
                    for (size_t i = frames.size() - skip; i-- > 0;) {
                        const auto& frame = frames[i];
                        const auto lib = session.internTable.find(frame.first);
                        // Keep unavailable library hashes distinct rather than merging unknown libraries.
                        std::string library = lib == session.internTable.end()
                            ? "[unknown library " + std::to_string(frame.first) + "]" : lib->second;
                        std::string function;
                        const auto symbols = session.symbolMap.find(library);
                        if (symbols != session.symbolMap.end()) {
                            const auto symbol = symbols->second.find(frame.second);
                            if (symbol != symbols->second.end()) function = symbol->second;
                        }
                        if (function.empty()) {
                            char address[32];
                            std::snprintf(address, sizeof(address), "0x%llx", (unsigned long long)frame.second);
                            function = address;
                        }
                        leaf = Node(leaf, library, function);
                    }
                    if (leaf < 0) leaf = Node(-1, "", "[stack omitted by root skipping]");
                } else leaf = Node(-1, "", "[missing call stack]");
                leaves.emplace(record.uuid, leaf);
            }
            auto& node = result_.nodes[leaf];
            Add(baseline ? node.baseSelf : node.comparisonSelf, {record.size, 1});
            Add(baseline ? result_.base : result_.comparison, {record.size, 1});
        }
    }

    void Finish() {
        for (auto& node : result_.nodes) {
            node.base = node.baseSelf;
            node.comparison = node.comparisonSelf;
            node.changed = node.baseSelf.bytes != node.comparisonSelf.bytes ||
                           node.baseSelf.count != node.comparisonSelf.count;
            if (node.changed) ++result_.changedStacks;
            if (!node.baseSelf.count && node.comparisonSelf.count) ++result_.newStacks;
            if (node.baseSelf.count && !node.comparisonSelf.count) ++result_.removedStacks;
        }
        // Reverse insertion order is a postorder: parents always precede children.
        for (size_t i = result_.nodes.size(); i-- > 0;) {
            const auto& node = result_.nodes[i];
            if (node.parent < 0) continue;
            auto& parent = result_.nodes[node.parent];
            Add(parent.base, node.base);
            Add(parent.comparison, node.comparison);
            parent.changed = parent.changed || node.changed;
        }
    }

private:
    int32_t Node(int32_t parent, const std::string& library, const std::string& function) {
        Key key{parent, library, function};
        auto found = lookup_.find(key);
        if (found != lookup_.end()) return found->second;
        if (result_.nodes.size() >= size_t(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("Too many comparison tree nodes");
        const int32_t index = int32_t(result_.nodes.size());
        ComparisonNode node;
        node.parent = parent;
        node.library = library;
        node.function = function;
        result_.nodes.push_back(std::move(node));
        lookup_.emplace(std::move(key), index);
        if (parent < 0) result_.roots.push_back(index);
        else result_.nodes[parent].children.push_back(index);
        return index;
    }
    ComparisonResult& result_;
    std::unordered_map<Key, int32_t, KeyHash> lookup_;
};

void ReadFile(const std::string& path, Session& session) {
    std::ifstream stream(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open file: " + path);
    const auto size = stream.tellg();
    if (size < 0 || uint64_t(size) > size_t(-1)) throw std::runtime_error("Cannot determine file size: " + path);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (size > 0 && !stream.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("Cannot read file: " + path);
    std::string error;
    if (!ReadSession(bytes.data(), bytes.size(), session, &error))
        throw std::runtime_error("Cannot load " + path + ": " + error);
}
} // namespace

bool CompareSessions(const Session& base, const Session& comparison,
                     ComparisonResult& result, std::string& error, int skipRootLevels) {
    result = {};
    error.clear();
    try {
        if (skipRootLevels < 0) throw std::runtime_error("Root levels must be a non-negative integer");
        result.skipRootLevels = skipRootLevels;
        Builder builder(result);
        builder.AddSession(base, true);
        builder.AddSession(comparison, false);
        builder.Finish();
        return true;
    } catch (const std::exception& e) { error = e.what(); result = {}; return false; }
}

bool CompareFiles(const std::string& base, const std::string& comparison,
                  ComparisonResult& result, std::string& error, int skipRootLevels,
                  const std::function<void(const std::string&)>& progress) {
    result = {};
    error.clear();
    try {
        if (skipRootLevels < 0) throw std::runtime_error("Root levels must be a non-negative integer");
        result.skipRootLevels = skipRootLevels;
        result.basePath = base;
        result.comparisonPath = comparison;
        Builder builder(result);
        for (int side = 0; side < 2; ++side) {
            const std::string& path = side == 0 ? base : comparison;
            if (progress) progress(side == 0 ? "Loading base..." : "Loading comparison...");
            Session session;
            ReadFile(path, session);
            if (progress) progress(side == 0 ? "Building base tree..." : "Building comparison tree...");
            builder.AddSession(session, side == 0);
        }
        if (progress) progress("Computing signed differences...");
        builder.Finish();
        return true;
    } catch (const std::exception& e) { error = e.what(); result = {}; return false; }
}

bool WriteComparisonReport(const ComparisonResult& result, const std::string& path, std::string& error) {
    error.clear();
    const auto target = std::filesystem::u8path(path);
    std::string extension = target.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension == ".loli") {
        error = "Signed comparison cannot be represented faithfully in capture .loli format; use --out diff.txt";
        return false;
    }
    std::error_code ec;
    for (const auto& input : {result.basePath, result.comparisonPath}) {
        if (input.empty()) continue;
        if (std::filesystem::equivalent(target, std::filesystem::u8path(input), ec)) {
            error = "Comparison output must not overwrite an input capture";
            return false;
        }
        ec.clear();
    }
    std::ofstream stream(target);
    if (!stream) { error = "Cannot create report: " + path; return false; }
    stream << "=== LoliProfiler Comparison Report ===\n\n"
           << "Baseline: " << result.basePath << "\nComparison: " << result.comparisonPath
           << "\nAllocation mode: live (saved free sequence filter)\n"
           << "Skipped root levels: " << result.skipRootLevels << '\n'
           << "Baseline allocations: " << result.base.count << '\n'
           << "Comparison allocations: " << result.comparison.count << '\n'
           << "Baseline total bytes: " << result.base.bytes << '\n'
           << "Comparison total bytes: " << result.comparison.bytes << '\n'
           << "Size delta bytes: " << result.comparison.bytes - result.base.bytes << '\n'
           << "Count delta: " << result.comparison.count - result.base.count << '\n'
           << "Changed allocation stacks: " << result.changedStacks << '\n'
           << "New allocation stacks: " << result.newStacks << '\n'
           << "Removed allocation stacks: " << result.removedStacks << "\n\n"
           << "=== Memory Diff (Comparison - Base) ===\n"
           << "Columns: function [library], delta bytes, delta count, self delta bytes, self delta count\n\n";
    struct Row { int32_t node; size_t depth; };
    std::vector<Row> pending;
    auto push = [&](const std::vector<int32_t>& nodes, size_t depth) {
        std::vector<int32_t> order;
        for (int32_t i : nodes) if (result.nodes[i].changed) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) {
            auto da = result.nodes[a].Metrics(ComparisonView::Diff).bytes;
            auto db = result.nodes[b].Metrics(ComparisonView::Diff).bytes;
            if (da != db) return da > db;
            if (result.nodes[a].function != result.nodes[b].function)
                return result.nodes[a].function < result.nodes[b].function;
            return result.nodes[a].library < result.nodes[b].library;
        });
        for (auto i = order.rbegin(); i != order.rend(); ++i) pending.push_back({*i, depth});
    };
    push(result.roots, 0);
    while (!pending.empty()) {
        const auto row = pending.back(); pending.pop_back();
        const auto& node = result.nodes[row.node];
        const auto delta = node.Metrics(ComparisonView::Diff);
        const auto self = node.Metrics(ComparisonView::Diff, true);
        stream << std::string(row.depth * 4, ' ') << node.function << " [" << node.library << "], "
               << std::showpos << delta.bytes << ", " << delta.count << ", " << self.bytes << ", " << self.count
               << std::noshowpos << '\n';
        push(node.children, row.depth + 1);
    }
    stream.close();
    if (!stream) { error = "Failed writing report: " + path; return false; }
    return true;
}
} // namespace loli
