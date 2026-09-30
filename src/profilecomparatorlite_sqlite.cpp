#include "profilecomparatorlite.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

std::string DisplaySize(uint64_t size) {
    char buf[64];
    if (size >= 1024ull * 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.2f GB", double(size) / 1024.0 / 1024.0 / 1024.0);
    else if (size >= 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.2f MB", double(size) / 1024.0 / 1024.0);
    else if (size > 1024)
        std::snprintf(buf, sizeof(buf), "%.2f KB", double(size) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu Bytes", static_cast<unsigned long long>(size));
    return buf;
}

std::string UtcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

} // namespace

bool ProfileComparatorLite::ExportDumpToSqlite(const std::string& outputPath) {
    if (!compared_ || signedComparison_) {
        errorMessage_ = "Must call DumpProfile() before exporting";
        return false;
    }

    // The database is a regenerable cache. Build it beside the destination,
    // then replace the destination only after the transaction commits.
    const std::filesystem::path target(outputPath);
    const std::filesystem::path temporary(outputPath + ".tmp");
    std::error_code fsError;
    std::filesystem::remove(temporary, fsError);
    if (fsError) {
        errorMessage_ = "Cannot remove stale temporary database: " + fsError.message();
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(temporary.string().c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        errorMessage_ = std::string("Cannot open SQLite database: ") +
                        (db ? sqlite3_errmsg(db) : "out of memory");
        if (db) sqlite3_close(db);
        std::filesystem::remove(temporary, fsError);
        return false;
    }

    const bool written = [&]() -> bool {
        auto exec = [&](const char* sql) -> bool {
            char* message = nullptr;
            const int code = sqlite3_exec(db, sql, nullptr, nullptr, &message);
            if (code == SQLITE_OK)
                return true;
            errorMessage_ = std::string("SQL failed: ") +
                            (message ? message : sqlite3_errmsg(db));
            sqlite3_free(message);
            return false;
        };
        using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
        auto prepare = [&](const char* sql) -> Statement {
            sqlite3_stmt* raw = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
                errorMessage_ = std::string("SQL prepare failed: ") + sqlite3_errmsg(db);
            return Statement(raw, sqlite3_finalize);
        };
        auto finishRow = [&](sqlite3_stmt* stmt, const char* what) -> bool {
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                errorMessage_ = std::string(what) + ": " + sqlite3_errmsg(db);
                return false;
            }
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            return true;
        };
        auto bindText = [](sqlite3_stmt* stmt, int index, const std::string& value) {
            sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
        };

        if (!exec("PRAGMA journal_mode=OFF") ||
            !exec("PRAGMA synchronous=OFF") ||
            !exec("PRAGMA temp_store=MEMORY") ||
            !exec("PRAGMA page_size=4096") ||
            !exec("CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT)") ||
            !exec("CREATE TABLE libraries(id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL)") ||
            !exec("CREATE TABLE symbols(library_id INTEGER NOT NULL, address INTEGER NOT NULL, "
                  "name TEXT NOT NULL, PRIMARY KEY(library_id,address)) WITHOUT ROWID") ||
            !exec("CREATE TABLE nodes(id INTEGER PRIMARY KEY, parent_id INTEGER REFERENCES nodes(id), "
                  "depth INTEGER NOT NULL, function_name TEXT NOT NULL, "
                  "library_id INTEGER REFERENCES libraries(id), func_addr INTEGER NOT NULL, "
                  "size_bytes INTEGER NOT NULL, count INTEGER NOT NULL)") ||
            !exec("BEGIN")) return false;

        auto insertNode = prepare("INSERT INTO nodes(id,parent_id,depth,function_name,library_id,"
                                  "func_addr,size_bytes,count) VALUES(?,?,?,?,?,?,?,?)");
        if (!insertNode) return false;

        std::unordered_map<std::string, sqlite3_int64> libraryIds;
        sqlite3_int64 nextLibraryId = 1;
        auto internLibrary = [&](const std::string& name) -> sqlite3_int64 {
            if (name.empty()) return 0;
            auto it = libraryIds.find(name);
            if (it != libraryIds.end()) return it->second;
            const sqlite3_int64 id = nextLibraryId++;
            libraryIds.emplace(name, id);
            return id;
        };
        auto childOrder = [](const CallTreeNode* a, const CallTreeNode* b) {
            if (a->size != b->size) return a->size > b->size;
            if (a->functionName != b->functionName) return a->functionName < b->functionName;
            if (a->libraryName != b->libraryName) return a->libraryName < b->libraryName;
            return a->functionAddress < b->functionAddress;
        };
        struct Frame { CallTreeNode* node; sqlite3_int64 parentId; int depth; };
        std::vector<CallTreeNode*> roots = deltaRoots_;
        std::sort(roots.begin(), roots.end(), childOrder);
        std::vector<Frame> stack;
        stack.reserve(1024);
        for (auto it = roots.rbegin(); it != roots.rend(); ++it)
            stack.push_back({*it, -1, 0});

        std::unordered_set<std::string> uniqueNames;
        sqlite3_int64 nextNodeId = 0;
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const CallTreeNode* node = frame.node;
            const sqlite3_int64 id = nextNodeId++;
            sqlite3_bind_int64(insertNode.get(), 1, id);
            if (frame.parentId < 0) sqlite3_bind_null(insertNode.get(), 2);
            else sqlite3_bind_int64(insertNode.get(), 2, frame.parentId);
            sqlite3_bind_int(insertNode.get(), 3, frame.depth);
            bindText(insertNode.get(), 4, node->functionName);
            const sqlite3_int64 libraryId = internLibrary(node->libraryName);
            if (libraryId == 0) sqlite3_bind_null(insertNode.get(), 5);
            else sqlite3_bind_int64(insertNode.get(), 5, libraryId);
            sqlite3_bind_int64(insertNode.get(), 6,
                               static_cast<sqlite3_int64>(node->functionAddress));
            sqlite3_bind_int64(insertNode.get(), 7, node->size);
            sqlite3_bind_int64(insertNode.get(), 8, node->count);
            if (!finishRow(insertNode.get(), "insert node")) return false;
            uniqueNames.insert(node->functionName);

            std::vector<CallTreeNode*> children = node->children;
            std::sort(children.begin(), children.end(), childOrder);
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back({*it, id, frame.depth + 1});
        }

        auto insertLibrary = prepare("INSERT OR IGNORE INTO libraries(id,name) VALUES(?,?)");
        if (!insertLibrary) return false;
        auto writeLibrary = [&](sqlite3_int64 id, const std::string& name) -> bool {
            sqlite3_bind_int64(insertLibrary.get(), 1, id);
            bindText(insertLibrary.get(), 2, name);
            return finishRow(insertLibrary.get(), "insert library");
        };
        for (const auto& entry : libraryIds)
            if (!writeLibrary(entry.second, entry.first)) return false;

        auto insertSymbol = prepare("INSERT INTO symbols(library_id,address,name) VALUES(?,?,?)");
        if (!insertSymbol) return false;
        std::vector<std::string> symbolLibraries;
        symbolLibraries.reserve(baselineSession_.symbolMap.size());
        for (const auto& library : baselineSession_.symbolMap)
            symbolLibraries.push_back(library.first);
        std::sort(symbolLibraries.begin(), symbolLibraries.end());
        for (const std::string& libraryName : symbolLibraries) {
            const auto& symbols = baselineSession_.symbolMap.at(libraryName);
            sqlite3_int64 libraryId = internLibrary(libraryName);
            if (libraryId == 0) continue;
            // A library may occur only in the symbol map, not in a tree node.
            if (!writeLibrary(libraryId, libraryName)) return false;
            std::vector<uint64_t> addresses;
            addresses.reserve(symbols.size());
            for (const auto& symbol : symbols)
                addresses.push_back(symbol.first);
            std::sort(addresses.begin(), addresses.end());
            for (const uint64_t address : addresses) {
                sqlite3_bind_int64(insertSymbol.get(), 1, libraryId);
                sqlite3_bind_int64(insertSymbol.get(), 2,
                                   static_cast<sqlite3_int64>(address));
                bindText(insertSymbol.get(), 3, symbols.at(address));
                if (!finishRow(insertSymbol.get(), "insert symbol")) return false;
            }
        }

        if (!exec("CREATE INDEX idx_nodes_parent ON nodes(parent_id)") ||
            !exec("CREATE INDEX idx_nodes_size ON nodes(size_bytes DESC)")) return false;
        auto insertMeta = prepare("INSERT INTO metadata(key,value) VALUES(?,?)");
        if (!insertMeta) return false;
        auto stamp = [&](const std::string& key, const std::string& value) -> bool {
            bindText(insertMeta.get(), 1, key);
            bindText(insertMeta.get(), 2, value);
            return finishRow(insertMeta.get(), "insert metadata");
        };
        if (!stamp("magic", "loli") ||
            !stamp("schema_version", "1") ||
            !stamp("app_version", std::to_string(loli::kVersion)) ||
            !stamp("mode", "snapshot") ||
            !stamp("total_allocations", std::to_string(stats_.baselineAllocCount)) ||
            !stamp("total_size_bytes", std::to_string(stats_.baselineTotalSize)) ||
            !stamp("total_size_display", DisplaySize(stats_.baselineTotalSize)) ||
            !stamp("skip_root_levels", std::to_string(skipRootLevels_)) ||
            !stamp("node_count", std::to_string(nextNodeId)) ||
            !stamp("root_count", std::to_string(roots.size())) ||
            !stamp("unique_function_names", std::to_string(uniqueNames.size())) ||
            !stamp("traversal", "size_desc_v1") ||
            !stamp("created_at", UtcTimestamp())) return false;
        return exec("COMMIT");
    }();

    if (sqlite3_close(db) != SQLITE_OK) {
        if (written) errorMessage_ = "Failed to close SQLite database";
        std::filesystem::remove(temporary, fsError);
        return false;
    }
    if (!written) {
        std::filesystem::remove(temporary, fsError);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.wstring().c_str(), target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        errorMessage_ = "Failed to replace output database: " + outputPath;
        std::filesystem::remove(temporary, fsError);
        return false;
    }
#else
    std::filesystem::rename(temporary, target, fsError);
    if (fsError) {
        errorMessage_ = "Failed to replace output database: " + fsError.message();
        std::filesystem::remove(temporary, fsError);
        return false;
    }
#endif
    return true;
}
