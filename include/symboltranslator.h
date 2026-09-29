#ifndef SYMBOLTRANSLATOR_H
#define SYMBOLTRANSLATOR_H

#include <string>
#include <unordered_map>
#include <vector>

#include "threadpool.h"

// Qt-free symbol translation (task 4.5), replacing the nm-extract +
// binary-search flow of CliProfiler::LoadSymbolFile / MainWindow's symbol
// button, and the QtConcurrent per-record fan-out of
// CliProfiler::InterpretStacktraceData.
namespace loli {

// One parsed `nm -nCS` record.
struct SymbolRecord {
    uint64_t addr = 0;
    uint32_t size = 0;
    std::string name;
};

// Runs `nm -nCS <symbolPath>` (blocking; potentially slow - call on a
// worker), parses the `addr size type name` lines (lowercase hex, demangled
// names), and returns the records sorted by address (nm -n output order is
// preserved; a stable sort keeps it). Returns false when nm failed to run.
bool ExtractSymbolTable(const std::string& nmPath, const std::string& symbolPath,
                        std::vector<SymbolRecord>& outSorted);

// Binary-searches addr in a sorted symbol table (matching the Qt original:
// a symbol contains [addr, addr+size)). Returns the symbol name or "".
std::string LookupSymbol(const std::vector<SymbolRecord>& sorted, uint64_t addr);

// Parallel address->symbol translation for a library's callstack addresses
// (the addrMap the Qt build filled via nm). Input: the set of distinct
// addresses; output: addr(hex string, lowercase) -> symbol name ("" stays
// "" for unresolved - the Qt original left empty names in place).
// The nm extraction + per-address lookup runs on the thread pool in
// address-range chunks; results merge deterministically.
size_t TranslateAddresses(const std::string& nmPath, const std::string& symbolPath,
                          const std::vector<uint64_t>& addresses,
                          std::unordered_map<uint64_t, std::string>& outSymbols);

// Batch-translate addresses via llvm-symbolizer (DWARF-aware; handles DWARF
// v5 which nm/addr2line from this NDK cannot). More accurate than nm
// containment for inlined/merged functions — uses the deepest inline frame's
// function name. Feeds `addresses` (hex) to `llvm-symbolizer -e <symbolPath>`
// on stdin and parses the per-address function-name output. Returns the
// number of addresses resolved to a non-"??" name. symbolizerPath may be
// empty to use "llvm-symbolizer" on PATH.
size_t TranslateAddressesSymbolizer(const std::string& symbolizerPath,
                                    const std::string& symbolPath,
                                    const std::vector<uint64_t>& addresses,
                                    std::unordered_map<uint64_t, std::string>& outSymbols);

} // namespace loli

#endif // SYMBOLTRANSLATOR_H
