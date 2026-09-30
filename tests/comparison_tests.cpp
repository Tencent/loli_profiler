#include "profilecomparison.h"
#include "profilecomparatorlite.h"
#include "comparetreestate.h"
#include "comparelaunch.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

using namespace loli;
namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
using Path = std::vector<std::pair<std::string, std::string>>;
struct Fixture {
    Session session;
    std::map<Path, AllocationMetrics> reference;
    uint32_t salt;
    explicit Fixture(uint32_t seed) : salt(seed) {}
    LoliUuid Add(const Path& path, int32_t size, int count = 1) {
        LoliUuid uuid = LoliUuid::CreateUuid();
        CallStack frames;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            uint32_t hash = uint32_t(std::hash<std::string>{}(it->first)) + salt;
            uint64_t address = std::hash<std::string>{}(it->second) + salt;
            session.internTable[hash] = it->first;
            session.symbolMap[it->first][address] = it->second;
            frames.push_back({hash, address});
        }
        session.callStackMap[uuid] = std::move(frames);
        for (int i = 0; i < count; ++i) {
            Record record;
            record.uuid = uuid; record.size = size;
            record.seq = uint32_t(session.records.size() + 1);
            record.addr = record.seq + uint64_t(salt) * 1000;
            session.records.push_back(record);
        }
        auto& value = reference[path]; value.bytes += int64_t(size) * count; value.count += count;
        return uuid;
    }
};
ComparisonResult Compare(const Session& a, const Session& b, int skip = 0) {
    ComparisonResult result; std::string error;
    Check(CompareSessions(a, b, result, error, skip), error.c_str()); return result;
}
Path NodePath(const ComparisonResult& result, int32_t index) {
    Path path;
    while (index >= 0) {
        const auto& node = result.nodes[index];
        path.push_back({node.library, node.function}); index = node.parent;
    }
    std::reverse(path.begin(), path.end()); return path;
}
void Reconcile(const ComparisonResult& result) {
    AllocationMetrics a, b;
    for (int32_t root : result.roots) {
        a.bytes += result.nodes[root].base.bytes; a.count += result.nodes[root].base.count;
        b.bytes += result.nodes[root].comparison.bytes; b.count += result.nodes[root].comparison.count;
    }
    Check(a.bytes == result.base.bytes && a.count == result.base.count, "base roots do not reconcile");
    Check(b.bytes == result.comparison.bytes && b.count == result.comparison.count, "comparison roots do not reconcile");
    for (const auto& node : result.nodes) {
        a = node.baseSelf; b = node.comparisonSelf;
        for (int32_t child : node.children) {
            a.bytes += result.nodes[child].base.bytes; a.count += result.nodes[child].base.count;
            b.bytes += result.nodes[child].comparison.bytes; b.count += result.nodes[child].comparison.count;
        }
        Check(a.bytes == node.base.bytes && a.count == node.base.count, "base inclusive != self + children");
        Check(b.bytes == node.comparison.bytes && b.count == node.comparison.count, "comparison inclusive != self + children");
    }
}
void KnownCases() {
    Fixture a(1), b(999);
    Path root{{"game.so", "root"}};
    auto child = [&](const char* name) { auto path = root; path.push_back({"game.so", name}); return path; };
    a.Add(root, 50); b.Add(root, 75); // allocations directly on an internal node
    a.Add(child("resized"), 100); b.Add(child("resized"), 200); // same count
    a.Add(child("removed"), 70);
    b.Add(child("new"), 45); // well below 1 KiB
    a.Add(child("count only"), 0); b.Add(child("count only"), 0, 2);
    a.Add(child("balanced minus"), 100);
    b.Add(child("balanced plus"), 100);
    // identical symbol text in two libraries must not merge
    a.Add({{"alpha.so", "same symbol"}}, 10);
    b.Add({{"beta.so", "same symbol"}}, 10);
    auto result = Compare(a.session, b.session); Reconcile(result);
    Check(result.changedStacks == 9, "changed stack count");
    for (size_t i = 0; i < result.nodes.size(); ++i) {
        const auto path = NodePath(result, int32_t(i));
        const auto expectedA = a.reference[path], expectedB = b.reference[path];
        const auto& node = result.nodes[i];
        Check(node.baseSelf.bytes == expectedA.bytes && node.baseSelf.count == expectedA.count, "wrong baseline self metrics");
        Check(node.comparisonSelf.bytes == expectedB.bytes && node.comparisonSelf.count == expectedB.count, "wrong comparison self metrics");
    }
    auto same = Compare(a.session, a.session);
    Check(!same.changedStacks, "identical comparison changed");
    for (const auto& node : same.nodes) Check(!node.changed, "identical node changed");
    auto reversed = Compare(b.session, a.session);
    Check(reversed.base.bytes == result.comparison.bytes && reversed.comparison.bytes == result.base.bytes, "reverse totals");
    auto skipped = Compare(a.session, b.session, 100); Reconcile(skipped);
    Check(skipped.base.bytes == result.base.bytes, "skipping roots lost allocations");

    Fixture balancedA(3), balancedB(4);
    balancedA.Add(child("minus"), 100); balancedB.Add(child("plus"), 100);
    auto balanced = Compare(balancedA.session, balancedB.session);
    const auto& parent = balanced.nodes[balanced.roots[0]];
    Check(parent.Metrics(ComparisonView::Diff).bytes == 0 && parent.changed, "balanced parent was hidden");

    gui::CompareTreeState baseTab, comparisonTab, diffTab;
    baseTab.Reset(result, ComparisonView::Base);
    comparisonTab.Reset(result, ComparisonView::Comparison);
    diffTab.Reset(result, ComparisonView::Diff);
    baseTab.Search("REMOVED"); baseTab.NextMatch(1);
    Check(baseTab.selected >= 0 && baseTab.Matches().size() == 1, "case insensitive search/reveal");
    Check(comparisonTab.selected < 0 && comparisonTab.Matches().empty(), "tab state leaked");
    comparisonTab.Search("removed"); Check(comparisonTab.Matches().empty(), "absent node searchable");
    diffTab.ExpandAll(false); diffTab.Search("count only"); diffTab.NextMatch(-1);
    Check(diffTab.selected >= 0 && diffTab.Rows().size() > result.roots.size(), "search did not reveal collapsed ancestors");
    const auto selected = baseTab.selected;
    diffTab.Sort(2, false); diffTab.ExpandAll(true);
    Check(baseTab.selected == selected, "sorting changed another tab");
}
void FreesAndFallback() {
    Fixture a(0); a.Add({{"a", "f"}}, 10, 2);
    a.session.records[0].addr = a.session.records[1].addr = 7;
    a.session.records[0].seq = 1; a.session.records[1].seq = 3;
    a.session.freeAddrMap = {{7, 2}, {7, 1}}; // use max, not final insertion
    auto result = Compare(a.session, Session{});
    Check(result.base.bytes == 10 && result.base.count == 1, "free/reuse filter");
    a.session.callStackMap.clear();
    result = Compare(a.session, Session{}); Reconcile(result);
    Check(result.nodes[0].function == "[missing call stack]", "missing stack dropped");
    a.session.records[1].size = -1;
    std::string error;
    Check(!CompareSessions(a.session, Session{}, result, error), "negative size accepted");
    Check(result.nodes.empty() && !error.empty(), "failure retained partial result");
    Check(!CompareSessions(Session{}, Session{}, result, error, -1), "negative root skip accepted");
    a.session.records[1].size = 2147483647;
    a.session.records.push_back(a.session.records[1]);
    result = Compare(a.session, Session{}); Reconcile(result);
    Check(result.base.bytes == 4294967294LL, "64-bit allocation totals truncated");
    Check(FormatComparisonBytes(INT64_MIN, true)[0] == '-', "minimum signed formatting overflow");
}
void RandomReference() {
    std::mt19937 random(0x9917);
    for (int trial = 0; trial < 100; ++trial) {
        Fixture a(100), b(10000);
        for (auto* fixture : {&a, &b}) for (int j = 0; j < 80; ++j) {
            Path path;
            for (unsigned depth = 1 + random() % 7; depth; --depth)
                path.push_back({"lib" + std::to_string(random() % 3), "function" + std::to_string(random() % 5)});
            fixture->Add(path, int32_t(random() % 2048), 1 + random() % 3);
        }
        auto result = Compare(a.session, b.session); Reconcile(result);
        std::map<Path, AllocationMetrics> actualA, actualB;
        for (size_t i = 0; i < result.nodes.size(); ++i) {
            const auto path = NodePath(result, int32_t(i));
            const auto& node = result.nodes[i];
            if (node.baseSelf.count) actualA[path] = node.baseSelf;
            if (node.comparisonSelf.count) actualB[path] = node.comparisonSelf;
        }
        Check(actualA.size() == a.reference.size() && actualB.size() == b.reference.size(), "random reference path set mismatch");
        for (const auto& entry : a.reference)
            Check(actualA[entry.first].bytes == entry.second.bytes && actualA[entry.first].count == entry.second.count, "random baseline mismatch");
        for (const auto& entry : b.reference)
            Check(actualB[entry.first].bytes == entry.second.bytes && actualB[entry.first].count == entry.second.count, "random comparison mismatch");
    }
}
void Files(const std::filesystem::path& directory) {
    Fixture a(1), b(2); a.Add({{"game", "root"}}, 25); b.Add({{"game", "root"}}, 50);
    auto write = [&](const std::filesystem::path& path, const Session& session) {
        std::vector<uint8_t> bytes; WriteSession(session, bytes);
        std::ofstream file(path, std::ios::binary); file.write((const char*)bytes.data(), bytes.size());
    };
    const auto base = directory / std::filesystem::u8path(u8"\u57fa\u51c6 with spaces.loli"), comparison = directory / "comparison.loli";
    write(base, a.session); write(comparison, b.session);
    ComparisonResult result; std::string error;
    Check(CompareFiles(base.u8string(), comparison.u8string(), result, error), error.c_str());
    Check(WriteComparisonReport(result, (directory / "diff.txt").u8string(), error), error.c_str());
    Check(!WriteComparisonReport(result, base.u8string(), error), "output overwrote capture");
    Check(!WriteComparisonReport(result, (directory / "diff.LOLI").u8string(), error), "lossy .loli accepted");
    Check(!std::filesystem::exists(directory / "diff.LOLI"), "failed export created output");
    ProfileComparatorLite facade;
    Check(facade.LoadProfile(base.u8string(), true) && facade.LoadProfile(comparison.u8string(), false) && facade.Compare(), "facade compare");
    Check(facade.GetStats().sizeDelta == 25, "facade differs from shared API");
    Check(facade.ExportToText((directory / "facade.txt").u8string()), "facade report");
    auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    Check(read(directory / "facade.txt") == read(directory / "diff.txt"), "facade report differs from shared API");
    Check(!facade.ExportDumpToText((directory / "wrong-dump.txt").u8string()), "comparison exported as snapshot");
    Check(!facade.ExportToLoli((directory / "signed.loli").u8string()), "facade exported signed capture");
    Check(facade.DumpProfile(), "snapshot dump failed after comparison");
    Check(!facade.ExportToText((directory / "wrong-diff.txt").u8string()), "snapshot exported as comparison");
    Check(!facade.DumpProfile(-1), "negative snapshot skip accepted");
    Check(!facade.ExportDumpToText((directory / "stale.txt").u8string()), "failed dump retained export state");
    Check(!facade.LoadProfile((directory / "missing").u8string(), true) && !facade.Compare(), "failed reload retained stale baseline");
    Check(!CompareFiles(base.u8string(), (directory / "missing").u8string(), result, error), "missing file accepted");
    const auto malformed = directory / "malformed.loli";
    std::ofstream(malformed, std::ios::binary) << "bad input";
    Check(!CompareFiles(malformed.u8string(), comparison.u8string(), result, error), "malformed input accepted");
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--launch-compare") {
        std::string error;
        if (!gui::LaunchComparison(std::filesystem::absolute(argv[0]), error)) {
            std::cerr << error << '\n'; return 1;
        }
        return 0; // comparison process must outlive this parent
    }
    const auto directory = std::filesystem::temp_directory_path() / ("loli-comparison-test-" + LoliUuid::CreateUuid().ToString());
    try {
        std::filesystem::create_directory(directory);
        KnownCases(); FreesAndFallback(); RandomReference(); Files(directory);
        std::filesystem::remove_all(directory);
        std::cout << "Comparison regressions passed (100 independent randomized references plus edge cases).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nFixtures: " << directory << '\n'; return 1;
    }
}
