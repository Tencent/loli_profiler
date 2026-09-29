// ProfileComparator differential test (remove-qt task 5.1 golden check).
//
// Loads real .loli captures through BOTH the Qt ProfileComparator
// (include/profilecomparator.h) and the Qt-free ProfileComparatorLite
// (include/profilecomparatorlite.h), runs Compare(0)/Compare(2) and
// DumpProfile/ExportDumpToText on identical inputs, and asserts:
//   - identical ComparisonStats field values
//   - byte-identical ExportDumpToText output files
//   - byte-identical ExportToText output files
//
// The .loli export itself is NOT byte-compared (record uuids are
// QUuid::createUuid() randoms on the Qt side by design); instead the lite
// export is re-loaded through loli::ReadSession and sanity-checked.
//
// Usage: ComparatorDiffTest [sample.loli ...]
// Defaults to the two known sample captures when no args are given.
// Skips (with a warning) any file that does not exist.
//
// Transition-only target (BUILD_LOLI_DIFFTEST); removed when the change
// lands.

#include "profilecomparator.h"
#include "profilecomparatorlite.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("PASS: %s\n", std::string(msg).c_str());           \
        } else {                                                      \
            printf("FAIL: %s (line %d)\n", std::string(msg).c_str(),  \
                   __LINE__);                                         \
            ++failures;                                               \
        }                                                             \
    } while (0)

static bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0)
        file.read(reinterpret_cast<char*>(out.data()),
                  static_cast<std::streamsize>(size));
    return !file.fail() || size == 0;
}

static bool FilesIdentical(const std::string& a, const std::string& b,
                           size_t* firstDiff) {
    std::vector<uint8_t> ba, bb;
    if (!ReadFileBytes(a, ba) || !ReadFileBytes(b, bb)) return false;
    if (ba.size() != bb.size()) {
        if (firstDiff) *firstDiff = ba.size() < bb.size() ? ba.size() : bb.size();
        return false;
    }
    for (size_t i = 0; i < ba.size(); ++i) {
        if (ba[i] != bb[i]) {
            if (firstDiff) *firstDiff = i;
            return false;
        }
    }
    if (firstDiff) *firstDiff = SIZE_MAX;
    return true;
}

static void PrintContext(const std::string& qa, const std::string& la,
                         size_t firstDiff) {
    std::vector<uint8_t> qBytes, lBytes;
    if (!ReadFileBytes(qa, qBytes) || !ReadFileBytes(la, lBytes)) return;
    // Walk back to the start of the line containing firstDiff and print a
    // few lines from each file.
    const size_t n = qBytes.size() < lBytes.size() ? qBytes.size() : lBytes.size();
    const size_t end = firstDiff < n ? firstDiff : n;
    size_t lineStart = 0;
    for (size_t i = end; i-- > 0;) {
        if (qBytes[i] == '\n') {
            lineStart = i + 1;
            break;
        }
    }
    printf("  qt: ");
    for (size_t i = lineStart; i < qBytes.size() && i < lineStart + 120; ++i) {
        if (qBytes[i] == '\r') continue;
        putchar(qBytes[i] == '\n' ? '\0' : static_cast<char>(qBytes[i]));
        if (qBytes[i] == '\n') break;
    }
    printf("\n  lite: ");
    for (size_t i = lineStart; i < lBytes.size() && i < lineStart + 120; ++i) {
        if (lBytes[i] == '\r') continue;
        putchar(lBytes[i] == '\n' ? '\0' : static_cast<char>(lBytes[i]));
        if (lBytes[i] == '\n') break;
    }
    printf("\n");
}

static void CompareStats(const ProfileComparator::ComparisonStats& qt,
                         const ProfileComparatorLite::ComparisonStats& lite,
                         const char* what) {
    CHECK(qt.baselineTotalSize == lite.baselineTotalSize,
          std::string(what) + ": baselineTotalSize");
    CHECK(qt.comparisonTotalSize == lite.comparisonTotalSize,
          std::string(what) + ": comparisonTotalSize");
    CHECK(qt.sizeDelta == lite.sizeDelta,
          std::string(what) + ": sizeDelta");
    CHECK(qt.baselineAllocCount == lite.baselineAllocCount,
          std::string(what) + ": baselineAllocCount");
    CHECK(qt.comparisonAllocCount == lite.comparisonAllocCount,
          std::string(what) + ": comparisonAllocCount");
    CHECK(qt.changedAllocations == lite.changedAllocations,
          std::string(what) + ": changedAllocations");
    CHECK(qt.newAllocationsCount == lite.newAllocationsCount,
          std::string(what) + ": newAllocationsCount");
}

static void RunDumpTest(const std::string& samplePath, int skipRootLevels) {
    printf("---- DumpProfile skip=%d: %s ----\n", skipRootLevels, samplePath.c_str());
    const QString qPath = QString::fromStdString(samplePath);
    const std::string tag =
        std::to_string(skipRootLevels) + "_" +
        samplePath.substr(samplePath.find_last_of("/\\") + 1);

    ProfileComparator qtComparator;
    ProfileComparatorLite liteComparator;

    if (!qtComparator.LoadProfile(qPath, true)) {
        CHECK(false, std::string("qt load: ") + samplePath +
                         " (" + qtComparator.GetErrorMessage().toStdString() + ")");
        return;
    }
    if (!liteComparator.LoadProfile(samplePath, true)) {
        CHECK(false, std::string("lite load: ") + samplePath +
                         " (" + liteComparator.GetErrorMessage() + ")");
        return;
    }

    if (!qtComparator.DumpProfile(skipRootLevels)) {
        CHECK(false, std::string("qt DumpProfile: ") +
                         qtComparator.GetErrorMessage().toStdString());
        return;
    }
    if (!liteComparator.DumpProfile(skipRootLevels)) {
        CHECK(false, std::string("lite DumpProfile: ") +
                         liteComparator.GetErrorMessage());
        return;
    }

    CompareStats(qtComparator.GetStats(), liteComparator.GetStats(),
                 "dump stats");

    const std::string qtOut = "cdt_dump_qt_" + tag + ".txt";
    const std::string liteOut = "cdt_dump_lite_" + tag + ".txt";
    CHECK(qtComparator.ExportDumpToText(QString::fromStdString(qtOut)),
          "qt ExportDumpToText");
    CHECK(liteComparator.ExportDumpToText(liteOut), "lite ExportDumpToText");

    size_t firstDiff = SIZE_MAX;
    if (FilesIdentical(qtOut, liteOut, &firstDiff)) {
        printf("PASS: dump text byte-identical (skip=%d)\n", skipRootLevels);
    } else {
        printf("FAIL: dump text differs (skip=%d)%s\n", skipRootLevels,
               firstDiff == SIZE_MAX ? "" : " - first diff at byte offset");
        if (firstDiff != SIZE_MAX) {
            printf("  at byte %zu\n", firstDiff);
            PrintContext(qtOut, liteOut, firstDiff);
        }
        ++failures;
    }
}

static void RunCompareTest(const std::string& baselinePath,
                           const std::string& comparisonPath,
                           int skipRootLevels) {
    printf("---- Compare skip=%d: %s vs %s ----\n", skipRootLevels,
           baselinePath.c_str(), comparisonPath.c_str());
    const std::string tag =
        std::to_string(skipRootLevels) + "_" +
        baselinePath.substr(baselinePath.find_last_of("/\\") + 1);

    ProfileComparator qtComparator;
    ProfileComparatorLite liteComparator;

    if (!qtComparator.LoadProfile(QString::fromStdString(baselinePath), true) ||
        !qtComparator.LoadProfile(QString::fromStdString(comparisonPath), false)) {
        CHECK(false, std::string("qt compare load: ") +
                         qtComparator.GetErrorMessage().toStdString());
        return;
    }
    if (!liteComparator.LoadProfile(baselinePath, true) ||
        !liteComparator.LoadProfile(comparisonPath, false)) {
        CHECK(false, std::string("lite compare load: ") +
                         liteComparator.GetErrorMessage());
        return;
    }

    if (!qtComparator.Compare(skipRootLevels)) {
        CHECK(false, std::string("qt Compare: ") +
                         qtComparator.GetErrorMessage().toStdString());
        return;
    }
    if (!liteComparator.Compare(skipRootLevels)) {
        CHECK(false, std::string("lite Compare: ") +
                         liteComparator.GetErrorMessage());
        return;
    }

    CompareStats(qtComparator.GetStats(), liteComparator.GetStats(),
                 "compare stats");

    const std::string qtOut = "cdt_cmp_qt_" + tag + ".txt";
    const std::string liteOut = "cdt_cmp_lite_" + tag + ".txt";
    CHECK(qtComparator.ExportToText(QString::fromStdString(qtOut)),
          "qt ExportToText");
    CHECK(liteComparator.ExportToText(liteOut), "lite ExportToText");

    size_t firstDiff = SIZE_MAX;
    if (FilesIdentical(qtOut, liteOut, &firstDiff)) {
        printf("PASS: compare text byte-identical (skip=%d)\n", skipRootLevels);
    } else {
        printf("FAIL: compare text differs (skip=%d)%s\n", skipRootLevels,
               firstDiff == SIZE_MAX ? "" : " - first diff at byte offset");
        if (firstDiff != SIZE_MAX) {
            printf("  at byte %zu\n", firstDiff);
            PrintContext(qtOut, liteOut, firstDiff);
        }
        ++failures;
    }

    // The lite delta .loli export: re-load through loli::ReadSession and
    // sanity-check. The Qt side's uuids are random, so no byte compare.
    const std::string liteLoli = "cdt_cmp_lite_" + tag + ".loli";
    if (liteComparator.ExportToLoli(liteLoli)) {
        std::vector<uint8_t> bytes;
        if (ReadFileBytes(liteLoli, bytes)) {
            loli::Session session;
            std::string err;
            if (loli::ReadSession(bytes.data(), bytes.size(), session, &err)) {
                CHECK(session.internTable.size() > 0 ||
                          session.records.empty(),
                      "lite ExportToLoli: re-readable session");
            } else {
                CHECK(false, "lite ExportToLoli: ReadSession failed: " + err);
            }
        } else {
            CHECK(false, "lite ExportToLoli: cannot read back file");
        }
    } else {
        CHECK(false, std::string("lite ExportToLoli: ") +
                         liteComparator.GetErrorMessage());
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    std::vector<std::string> samples;
    for (int i = 1; i < argc; ++i)
        samples.push_back(argv[i]);
    if (samples.empty()) {
        samples.push_back("G:/SampleData/Memory/6s_heap_0919.loli");
        samples.push_back("G:/SampleData/Memory/"
                          "com.tencent.mf.uam_1524_20260511113001-7949.loli");
    }

    // Pair up samples for compare tests: (baseline, comparison) = (0,1),
    // (2,3) ...; odd leftovers are dump-only.
    std::vector<std::string> available;
    for (const auto& s : samples) {
        std::ifstream f(s);
        if (f) {
            available.push_back(s);
        } else {
            printf("WARN: skipping missing sample %s\n", s.c_str());
        }
    }

    for (const auto& s : available) {
        RunDumpTest(s, 0);
        RunDumpTest(s, 2);
    }

    for (size_t i = 0; i + 1 < available.size(); i += 2) {
        RunCompareTest(available[i], available[i + 1], 0);
        RunCompareTest(available[i], available[i + 1], 2);
    }

    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
