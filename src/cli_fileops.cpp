// CLI file-processing modes (--dump / --compare / --symbolize) on the
// Qt-free core (task 5.1/5.2). The capture mode lives in main_cli2.cpp.
#include "profilecomparatorlite.h"
#include "lolirecord.h"
#include "lolistream.h"
#include "symboltranslator.h"
#include "pathutilslite.h"
#include "captureconfig.h"
#include "appsettings.h"
#include "lolilogger.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Keep the CLI's human-readable stderr wording while recording the same error
// through the shared logger when --log-file is enabled.
void FileErrorAt(const char* source, int sourceLine, const char* function,
                 const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::vfprintf(stderr, format, args);
    va_end(args);
    if (length > 0) {
        std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
        va_start(args, format);
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);
        std::string line(buffer.data());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        loli::LoliLogger::Instance().Write(loli::LogLevel::Error, "file",
                                            line, source, sourceLine, function);
    }
}

#define FileError(...) FileErrorAt(__FILE__, __LINE__, __func__, __VA_ARGS__)

// Size formatting (sizeToString port) for the console stats output.
std::string SizeToString(uint64_t size) {
    char buf[64];
    if (size >= 1024ull * 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(size) / 1024.0 / 1024.0 / 1024.0);
    else if (size >= 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(size) / 1024.0 / 1024.0);
    else if (size > 1024)
        std::snprintf(buf, sizeof(buf), "%.2f KB", static_cast<double>(size) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu Bytes", static_cast<unsigned long long>(size));
    return buf;
}

void PrintUsage() {
    std::printf(
        "  LoliProfilerCLI --compare <baseline.loli> <comparison.loli> --out <output> [options]\n"
        "  LoliProfilerCLI --dump <profile.loli> --out <output.txt> [options]\n");
}

std::string GetOptionValue(const std::vector<std::string>& args,
                           const std::string& name) {
    const std::string prefix = "--" + name + "=";
    const std::string flag = "--" + name;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == flag && i + 1 < args.size())
            return args[i + 1];
        if (args[i].rfind(prefix, 0) == 0)
            return args[i].substr(prefix.size());
    }
    return std::string();
}

// Options that take a separate value argument. Boolean flags (--dump,
// --compare) do NOT, so the positional .loli paths following them are kept.
bool OptionTakesValue(const std::string& arg) {
    static const char* kValueOpts[] = {"--out",      "--skip-root-levels", "--app",
                                       "--symbol",   "--subprocess",       "--device",
                                       "--duration", "--log-file",        "--log-level"};
    for (const char* opt : kValueOpts)
        if (arg == opt)
            return true;
    return false;
}

std::vector<std::string> PositionalArgs(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) {
            if (args[i].find('=') == std::string::npos && OptionTakesValue(args[i]))
                ++i; // skip the option's value
            continue;
        }
        out.push_back(args[i]);
    }
    return out;
}

int RunCompare(const std::vector<std::string>& args) {
    const auto start = loli::LoliLogger::Clock::now();
    std::vector<std::string> files;
    std::string output, skip;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string option = args[i];
        if (option == "--compare" || option == "--verbose") continue;
        if (option.rfind("--", 0) != 0) { files.push_back(option); continue; }
        std::string value;
        const auto equal = option.find('=');
        if (equal != std::string::npos) { value = option.substr(equal + 1); option.resize(equal); }
        if (option != "--out" && option != "--skip-root-levels" && option != "--log-file" && option != "--log-level") {
            FileError("Error: unknown comparison option: %s\n", option.c_str());
            return 1;
        }
        if (equal == std::string::npos) {
            if (++i == args.size() || args[i].rfind("--", 0) == 0) {
                FileError("Error: missing value for %s\n", option.c_str());
                return 1;
            }
            value = args[i];
        }
        if (value.empty()) {
            FileError("Error: missing value for %s\n", option.c_str());
            return 1;
        }
        if (option == "--out") output = value;
        if (option == "--skip-root-levels") skip = value;
    }
    int levels = 0;
    if (!skip.empty()) {
        const auto parsed = std::from_chars(skip.data(), skip.data() + skip.size(), levels);
        if (parsed.ec != std::errc() || parsed.ptr != skip.data() + skip.size() || levels < 0) {
            FileError("Error: --skip-root-levels must be a non-negative integer\n");
            return 1;
        }
    }
    if (files.size() != 2 || output.empty()) {
        FileError("Error: --compare requires two .loli files and --out <diff.txt>\n");
        return 1;
    }
    loli::ComparisonResult result;
    std::string error;
    if (!loli::CompareFiles(files[0], files[1], result, error, levels,
        [](const std::string& progress) { std::printf("%s\n", progress.c_str()); })) {
        FileError("Error: %s\n", error.c_str());
        return 1;
    }
    if (!loli::WriteComparisonReport(result, output, error)) {
        FileError("Error: %s\n", error.c_str());
        return 1;
    }
    std::printf("\n=== Comparison Results (live allocations) ===\n"
                "Baseline allocations: %lld\nComparison allocations: %lld\n"
                "Baseline total bytes: %lld\nComparison total bytes: %lld\n"
                "Size delta bytes: %+lld\nCount delta: %+lld\n"
                "Changed allocation stacks: %lld\nNew allocation stacks: %lld\n"
                "Removed allocation stacks: %lld\nReport: %s\n",
                (long long)result.base.count, (long long)result.comparison.count,
                (long long)result.base.bytes, (long long)result.comparison.bytes,
                (long long)(result.comparison.bytes - result.base.bytes),
                (long long)(result.comparison.count - result.base.count),
                (long long)result.changedStacks, (long long)result.newStacks,
                (long long)result.removedStacks, output.c_str());
    loli::LoliLogger::Instance().LogStage("file", "compare_complete", start,
                                       "output=" + output);
    return 0;
}

int RunDump(const std::vector<std::string>& args) {
    const auto start = loli::LoliLogger::Clock::now();
    const std::vector<std::string> files = PositionalArgs(args);
    const std::string outFile = GetOptionValue(args, "out");

    if (files.size() != 1) {
        FileError( "Error: --dump requires exactly one .loli file\n");
        return 1;
    }
    if (outFile.empty()) {
        FileError( "Error: --out is required to specify output file\n");
        return 1;
    }

    std::string suffix = outFile.size() >= 3 ? outFile.substr(outFile.size() - 3) : "";
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool asSqlite = suffix == ".db";
    LOLI_INFO("file") << "dump input=" << files[0] << " output=" << outFile
                      << " format=" << (asSqlite ? "sqlite" : "text");

    ProfileComparatorLite comparator;
    std::printf("Loading profile: %s...\n", files[0].c_str());
    if (!comparator.LoadProfile(files[0], true)) {
        FileError( "Error: %s\n", comparator.GetErrorMessage().c_str());
        return 1;
    }
    const std::string dumpSkipStr = GetOptionValue(args, "skip-root-levels");
    const int dumpSkip = dumpSkipStr.empty() ? 0 : atoi(dumpSkipStr.c_str());
    if (!comparator.DumpProfile(dumpSkip)) {
        FileError( "Error: %s\n", comparator.GetErrorMessage().c_str());
        return 1;
    }
    if (!(asSqlite ? comparator.ExportDumpToSqlite(outFile)
                   : comparator.ExportDumpToText(outFile))) {
        FileError( "Error: failed to write %s (%s)\n", outFile.c_str(),
                     comparator.GetErrorMessage().c_str());
        return 1;
    }
    std::printf("Dump written to %s\n", outFile.c_str());
    loli::LoliLogger::Instance().LogStage("file", "dump_complete", start,
        "output=" + outFile);
    return 0;
}

} // namespace

// Re-run nm symbol translation on an already-captured .loli and write the
// result back (or to --out). Useful when the capture-time translation failed
// (e.g. NDK path unset or nm timeout) but the session's smaps-resolved frame
// addresses are intact. Usage:
//   LoliProfilerCLI --symbolize <profile.loli> --symbol <lib.so> [--out <file>]
int RunSymbolize(const std::vector<std::string>& args) {
    const auto start = loli::LoliLogger::Clock::now();
    const std::vector<std::string> files = PositionalArgs(args);
    if (files.size() != 1) {
        FileError( "Error: --symbolize requires exactly one .loli file\n");
        return 1;
    }
    const std::string symPath = GetOptionValue(args, "symbol");
    if (symPath.empty()) {
        FileError( "Error: --symbolize requires --symbol <lib.so>\n");
        return 1;
    }
    std::string outFile = GetOptionValue(args, "out");
    if (outFile.empty())
        outFile = files[0]; // in-place
    LOLI_INFO("file") << "symbolize input=" << files[0]
                      << " symbol=" << symPath << " output=" << outFile;

    std::printf("Loading profile: %s...\n", files[0].c_str());
    std::ifstream in(files[0].c_str(), std::ios::binary);
    if (!in.good()) {
        FileError( "Error: cannot open %s\n", files[0].c_str());
        return 1;
    }
    in.seekg(0, std::ios::end);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty())
        in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());

    loli::Session session;
    std::string err;
    if (!loli::ReadSession(bytes.data(), bytes.size(), session, &err)) {
        FileError( "Error: %s\n", err.c_str());
        return 1;
    }

    std::string symLibName = symPath;
    const std::size_t slash = symLibName.find_last_of("/\\");
    if (slash != std::string::npos)
        symLibName = symLibName.substr(slash + 1);

    // Only addresses from this library belong in its symbol table. Addresses
    // from libc, libart, or unknown mappings can overlap its virtual range.
    std::vector<uint64_t> addrs;
    for (const auto& kv : session.callStackMap)
        for (const auto& frame : kv.second) {
            const auto lib = session.internTable.find(frame.first);
            if (lib != session.internTable.end() && lib->second == symLibName)
                addrs.push_back(frame.second);
        }
    std::sort(addrs.begin(), addrs.end());
    addrs.erase(std::unique(addrs.begin(), addrs.end()), addrs.end());
    std::printf("Translating %zu distinct frame addresses...\n", addrs.size());
    LOLI_INFO("file") << "distinct frame addresses=" << addrs.size()
                      << " library=" << symLibName;
    if (addrs.empty()) {
        FileError(
            "Error: no frames mapped to %s. Check the library basename and capture smaps.\n",
            symLibName.c_str());
        return 1;
    }

    // Resolve the NDK nm + llvm-symbolizer for the capture arch. The
    // symbolizer is preferred: it reads DWARF v5 (which this NDK's
    // nm/addr2line cannot). Choose the outermost inline caller for one
    // name per PC, matching the old Qt nearest-ELF-symbol tree.
    {
        AppSettings settings;
        PathUtilsLite::SetNDKPath(settings.Get("AndroidNDK"));
    }
    const bool armv7 = loli::LoadCaptureConfig().arch == "armeabi-v7a";
    // llvm-symbolizer lives at <ndk>/toolchains/llvm/prebuilt/<host>/bin/.
    // GetNDKToolPath would double the "llvm-" prefix, so build it directly.
    std::string symbolizerPath;
    {
        const std::string ndk = PathUtilsLite::GetNDKPath();
        if (!ndk.empty()) {
#ifdef _WIN32
            const std::string p = ndk + "/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe";
#elif defined(__APPLE__)
            const std::string p = ndk + "/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-symbolizer";
#else
            const std::string p = ndk + "/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-symbolizer";
#endif
            std::ifstream probe(p.c_str());
            if (probe.good())
                symbolizerPath = p;
        }
    }
    const std::string nmPath = PathUtilsLite::GetNDKToolPath("nm", armv7);

    auto& map = session.symbolMap[symLibName];

    size_t translated = 0;
    if (!symbolizerPath.empty()) {
        std::unordered_map<uint64_t, std::string> resolved;
        translated = loli::TranslateAddressesSymbolizer(symbolizerPath, symPath,
                                                        addrs, resolved);
        for (const auto& kv : resolved)
            map[kv.first] = kv.second;
        std::printf("Symbolizer translated %zu / %zu addresses into %s.\n",
                    translated, addrs.size(), symLibName.c_str());
    } else {
        std::vector<loli::SymbolRecord> table;
        if (loli::ExtractSymbolTable(nmPath.empty() ? "nm" : nmPath, symPath, table)) {
            std::printf("nm table: %zu symbols\n", table.size());
            for (const uint64_t addr : addrs) {
                const std::string name = loli::LookupSymbol(table, addr);
                if (!name.empty()) {
                    map[addr] = name;
                    ++translated;
                }
            }
            std::printf("nm translated %zu / %zu addresses into %s.\n",
                        translated, addrs.size(), symLibName.c_str());
        } else {
            FileError( "Error: failed to extract symbols from %s\n",
                         symPath.c_str());
            return 1;
        }
    }
    if (translated == 0) {
        FileError( "Error: no symbols resolved from %s. Check that it matches the captured library build.\n",
                     symPath.c_str());
        return 1;
    }

    std::vector<uint8_t> outBytes;
    if (!loli::WriteSession(session, outBytes)) {
        FileError( "Error: failed to serialize session\n");
        return 1;
    }
    std::ofstream out(outFile.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        FileError( "Error: cannot write %s\n", outFile.c_str());
        return 1;
    }
    out.write(reinterpret_cast<const char*>(outBytes.data()),
              static_cast<std::streamsize>(outBytes.size()));
    if (!out.good()) {
        FileError( "Error: failed to write %s\n", outFile.c_str());
        return 1;
    }
    std::printf("Wrote %s\n", outFile.c_str());
    loli::LoliLogger::Instance().LogStage("file", "symbolize_complete", start,
        "output=" + outFile + " translated=" + std::to_string(translated));
    return 0;
}

int RunFileModes(const std::vector<std::string>& args) {
    for (const auto& a : args) {
        if (a == "--compare")
            return RunCompare(args);
        if (a == "--symbolize")
            return RunSymbolize(args);
    }
    return RunDump(args);
}
