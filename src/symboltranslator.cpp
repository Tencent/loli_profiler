#include "symboltranslator.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include <process.hpp>

#include "processrunner.h"

namespace loli {

namespace {

// llvm-symbolizer emits the inlined function at the PC first, followed by
// enclosing callers. A .loli stack has one frame per captured PC, so keep the
// outermost named function to match Qt's nearest ELF symbol for that PC.
std::string ParseSymbolizerBlock(const std::string& block) {
    std::istringstream in(block);
    std::string line;
    std::string outermost;
    bool expectFunc = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break; // blank line separates addresses
        if (expectFunc) {
            if (line != "??")
                outermost = line;
            expectFunc = false;
        } else {
            expectFunc = true; // next line was file:line
        }
    }
    return outermost;
}

} // namespace
namespace {

// Parses one `nm -nCS` output line: QRegExp "([0-9a-z]+)\s([0-9a-z]+)\s(\w)\s(.+)"
// - lowercase-hex address, lowercase-hex size, single type char, name.
bool ParseNmLine(const std::string& line, SymbolRecord& out) {
    std::size_t i = 0;
    auto hexField = [&line, &i](uint64_t& value) -> bool {
        const std::size_t start = i;
        uint64_t v = 0;
        while (i < line.size() && line[i] != ' ' &&
               std::isxdigit(static_cast<unsigned char>(line[i]))) {
            const char c = line[i];
            const int d = (c <= '9') ? c - '0'
                          : (c >= 'a') ? c - 'a' + 10 : -1; // lowercase only (Qt regex)
            if (d < 0)
                return false;
            v = v * 16 + static_cast<uint64_t>(d);
            ++i;
        }
        if (i == start)
            return false;
        value = v;
        return true;
    };

    uint64_t addr = 0;
    if (!hexField(addr))
        return false;
    if (i >= line.size() || line[i] != ' ')
        return false;
    ++i;

    uint64_t size = 0;
    if (!hexField(size))
        return false;
    if (i >= line.size() || line[i] != ' ')
        return false;
    ++i;

    if (i >= line.size() || line[i] == ' ')
        return false;
    ++i; // type char
    if (i >= line.size() || line[i] != ' ')
        return false;
    ++i;

    out.addr = addr;
    out.size = static_cast<uint32_t>(size);
    out.name = line.substr(i);
    return true;
}

} // namespace

bool ExtractSymbolTable(const std::string& nmPath, const std::string& symbolPath,
                        std::vector<SymbolRecord>& outSorted) {
    outSorted.clear();

    ProcessRunner runner;
    runner.SetProgram(nmPath);
    runner.SetArguments({"-nCS", symbolPath});
    if (!runner.Start())
        return false;
    // Large game .so files (multi-GB) can take ~60s for a full nm dump; give
    // generous headroom so the extraction doesn't get killed mid-table.
    if (!runner.WaitForFinished(300000))
        runner.Kill();

    const std::string output = runner.ReadAllStdout();
    std::size_t start = 0;
    for (std::size_t i = 0; i <= output.size(); ++i) {
        if (i == output.size() || output[i] == '\n') {
            std::string line = output.substr(start, i - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            SymbolRecord record;
            if (ParseNmLine(line, record))
                outSorted.push_back(std::move(record));
            start = i + 1;
        }
    }
    // nm -n sorts by address; a stable sort keeps that order if the tool
    // ever emitted unsorted data.
    std::stable_sort(outSorted.begin(), outSorted.end(),
                     [](const SymbolRecord& a, const SymbolRecord& b) {
                         return a.addr < b.addr;
                     });
    return true;
}

std::string LookupSymbol(const std::vector<SymbolRecord>& sorted, uint64_t addr) {
    // Find the nearest symbol whose start is at-or-below addr, then walk
    // backwards to pick the SMALLEST symbol that actually contains addr.
    // The previous binary search returned the FIRST containing record it
    // probed, so a zero-size or oversized .init_array/_GLOBAL__sub_I_* symbol
    // at a low address swallowed every frame above it (wrong names appearing
    // mid-callstack). Zero-size symbols never contain anything.
    if (sorted.empty())
        return std::string();

    // upper_bound: first symbol with .addr > addr.
    size_t hi = 0;
    {
        size_t lo = 0, n = sorted.size();
        hi = n;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (sorted[mid].addr <= addr)
                lo = mid + 1;
            else
                hi = mid;
        }
        hi = lo; // first index with addr > target
    }
    if (hi == 0)
        return std::string(); // addr below the first symbol

    // Walk back a bounded window; the true container is almost always the
    // immediately-preceding symbol, but overlapping sections (ICF / aliases)
    // mean a slightly earlier, larger symbol may be the real owner.
    const SymbolRecord* best = nullptr;
    const size_t start = hi - 1;
    const size_t limit = start > 64 ? start - 64 : 0;
    for (size_t i = start + 1; i-- > limit;) {
        const SymbolRecord& rec = sorted[i];
        if (rec.size == 0)
            continue;
        if (addr >= rec.addr && addr < rec.addr + static_cast<uint64_t>(rec.size)) {
            if (!best || rec.size < best->size)
                best = &rec;
        }
        // Symbols are sorted by addr; once rec.addr + rec.size can't reach
        // addr AND rec.size is shrinking past the gap, we can stop early.
        if (rec.addr + static_cast<uint64_t>(rec.size) <= addr &&
            best != nullptr)
            break;
    }
    return best ? best->name : std::string();
}

size_t TranslateAddresses(const std::string& nmPath, const std::string& symbolPath,
                          const std::vector<uint64_t>& addresses,
                          std::unordered_map<uint64_t, std::string>& outSymbols) {
    outSymbols.clear();
    if (addresses.empty())
        return 0;

    // nm extraction runs once (it is a single external process); the
    // per-address binary search is then fanned out over the pool for large
    // address sets.
    std::vector<SymbolRecord> table;
    if (!ExtractSymbolTable(nmPath, symbolPath, table))
        return 0;

    ThreadPool pool;
    std::mutex mergeMutex;
    std::vector<std::future<size_t>> futures;
    const std::size_t chunk = 4096;
    for (std::size_t start = 0; start < addresses.size(); start += chunk) {
        const std::size_t end = std::min(start + chunk, addresses.size());
        futures.push_back(pool.Submit([&table, &addresses, start, end, &mergeMutex,
                                       &outSymbols]() -> size_t {
            size_t resolved = 0;
            std::unordered_map<uint64_t, std::string> local;
            for (std::size_t i = start; i < end; ++i) {
                const std::string name = LookupSymbol(table, addresses[i]);
                local.emplace(addresses[i], name);
                if (!name.empty())
                    ++resolved;
            }
            std::lock_guard<std::mutex> lock(mergeMutex);
            outSymbols.insert(local.begin(), local.end());
            return resolved;
        }));
    }
    std::size_t total = 0;
    for (auto& f : futures)
        total += f.get();
    return total;
}

size_t TranslateAddressesSymbolizer(const std::string& symbolizerPath,
                                    const std::string& symbolPath,
                                    const std::vector<uint64_t>& addresses,
                                    std::unordered_map<uint64_t, std::string>& outSymbols) {
    outSymbols.clear();
    if (addresses.empty())
        return 0;
    const std::string prog = symbolizerPath.empty() ? "llvm-symbolizer" : symbolizerPath;

    // Build the stdin payload: one lowercase-hex address per line.
    std::string input;
    input.reserve(addresses.size() * 18);
    char hexbuf[24];
    for (const uint64_t a : addresses) {
        std::snprintf(hexbuf, sizeof(hexbuf), "0x%llx\n",
                      static_cast<unsigned long long>(a));
        input += hexbuf;
    }

    // llvm-symbolizer -e <sym>: reads addresses on stdin, emits per-address
    // "<func>\n<file:line>\n" blocks separated by blank lines.
    std::string output;
    {
        TinyProcessLib::Process proc(
            "\"" + prog + "\" -e \"" + symbolPath + "\"",
            "", // no working dir
            [&output](const char* bytes, size_t n) { output.append(bytes, n); },
            nullptr,   // stderr: discard
            true);     // open_stdin
        // Feed the payload, then signal EOF so the symbolizer finishes.
        size_t off = 0;
        while (off < input.size()) {
            const size_t chunk = std::min<size_t>(input.size() - off, 1 << 16);
            if (!proc.write(input.data() + off, chunk))
                break;
            off += chunk;
        }
        proc.close_stdin();
        proc.get_exit_status();
    }
    if (output.empty())
        return 0;

    // Split into per-address blocks on blank lines, in input order.
    std::vector<std::string> blocks;
    std::string cur;
    {
        std::istringstream in(output);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty()) {
                blocks.push_back(cur);
                cur.clear();
            } else {
                cur += line + "\n";
            }
        }
        if (!cur.empty())
            blocks.push_back(cur);
    }

    size_t resolved = 0;
    const size_t n = std::min(blocks.size(), addresses.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string name = ParseSymbolizerBlock(blocks[i]);
        if (!name.empty()) {
            outSymbols[addresses[i]] = name;
            ++resolved;
        }
    }
    return resolved;
}

} // namespace loli
