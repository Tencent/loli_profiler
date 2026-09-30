#include "hashstringlite.h"
#include "lolirecord.h"
#include "profilecomparatorlite.h"
#include "sqlite3.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void Save(const loli::Session& session, const fs::path& path) {
    std::vector<uint8_t> bytes;
    Require(loli::WriteSession(session, bytes), "fixture serialization failed");
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Require(stream.good(), "fixture write failed");
}

int64_t Query(const fs::path& path, const char* sql) {
    sqlite3* db = nullptr;
    Require(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "SQLite open failed");
    sqlite3_stmt* stmt = nullptr;
    Require(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK, "SQLite query failed");
    Require(sqlite3_step(stmt) == SQLITE_ROW, "missing SQLite row");
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

int main() {
    const auto scratch = fs::temp_directory_path() / ("loli calltree " +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);
    int result = 0;
    try {
        Require(HashStringLite::Hash("Aa") == HashStringLite::Hash("BB"), "fixture must collide under the legacy hash");
        loli::Session baseline;
        baseline.internTable[1] = "fixture.so";
        baseline.symbolMap["fixture.so"] = {{1, "Aa"}, {2, "BB"}, {3, "Root"}};
        loli::Record first;
        first.uuid = LoliUuid::CreateUuid();
        first.seq = 1;
        first.size = 4096;
        first.addr = 0x1000;
        baseline.records.push_back(first);
        baseline.callStackMap[first.uuid] = {{1, 1}, {1, 3}};
        loli::Record second = first;
        second.uuid = LoliUuid::CreateUuid();
        second.seq = 2;
        second.size = 8192;
        second.addr = 0x2000;
        baseline.records.push_back(second);
        baseline.callStackMap[second.uuid] = {{1, 2}, {1, 3}};
        const auto input = scratch / "baseline.loli";
        const auto database = scratch / "baseline.db";
        Save(baseline, input);
        ProfileComparatorLite dump;
        Require(dump.LoadProfile(input.string(), true) && dump.DumpProfile() &&
                dump.ExportDumpToSqlite(database.string()), "collision dump failed");
        Require(Query(database, "SELECT COUNT(*) FROM nodes") == 3, "colliding names merged");
        Require(Query(database, "SELECT size_bytes FROM nodes WHERE function_name='Aa'") == 4096, "Aa accounting corrupted");
        Require(Query(database, "SELECT size_bytes FROM nodes WHERE function_name='BB'") == 8192, "BB accounting corrupted");
        Require(Query(database, "SELECT size_bytes FROM nodes WHERE parent_id IS NULL") == 12288, "root accounting corrupted");
        ProfileComparatorLite same;
        Require(same.LoadProfile(input.string(), true) && same.LoadProfile(input.string(), false) && same.Compare(), "self comparison failed");
        Require(same.GetStats().changedAllocations == 0 && same.GetStats().newAllocationsCount == 0, "self comparison differs");

        auto current = baseline;
        first.seq = 3;
        first.size = 6144;
        first.addr = 0x3000;
        current.records.push_back(first);
        const auto comparison = scratch / "current.loli";
        Save(current, comparison);
        ProfileComparatorLite diff;
        Require(diff.LoadProfile(input.string(), true) && diff.LoadProfile(comparison.string(), false) && diff.Compare(), "growth comparison failed");
        Require(diff.GetStats().changedAllocations == 1 && diff.GetStats().newAllocationsCount == 0 && diff.GetStats().sizeDelta == 6144, "shared signed paths did not match baseline");
        Require(diff.ExportToText((scratch / "diff.txt").string()), "signed delta export failed");
        Require(!diff.ExportToLoli((scratch / "diff.loli").string()), "lossy signed delta capture was accepted");

        // Repeated functions at different depths must remain distinct nodes.
        baseline.callStackMap[first.uuid] = {{1, 1}, {1, 1}, {1, 3}};
        Save(baseline, input);
        Require(dump.LoadProfile(input.string(), true) && dump.DumpProfile() &&
                dump.ExportDumpToSqlite(database.string()), "recursive path dump failed");
        Require(Query(database, "SELECT COUNT(*) FROM nodes WHERE function_name='Aa'") == 2, "recursive frames merged");
        Require(dump.DumpProfile(1) && dump.ExportDumpToSqlite(database.string()), "skip-root dump failed");
        Require(Query(database, "SELECT COUNT(*) FROM nodes WHERE function_name='Root'") == 0, "root skip failed");
        std::cout << "PASS distinct hash collisions, recursive paths, self/growth deltas, root skip\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(scratch);
    return result;
}
