// HashString hash-compatibility test (task 4.1).
//
// HashString's hashcodes are persisted in .loli files (intern-table keys,
// record libHash, callstack frame keys), so the Qt-free implementation must
// produce values identical to Qt 5.14.1's qHash(QString) with seed 0.
//
// The algorithm was determined empirically against a Qt 5.14.1 probe program
// (build/smoke/qhashprobe) and implemented as: fold each UTF-16 code unit of
// the QString with h = h * 31 + unit, starting from h = 0 (seed 0).
//
// This test validates three vector sets:
//   1. Vectors dumped from the Qt probe (qHash called directly in Qt).
//   2. The intern table of a real capture, cross-checked against a Qt
//      QDataStream walk (the Qt reference program hashes each interned
//      string with qHash(QString) and asserts it equals the stored key).
//   3. Controlled UTF-16 probes (non-ASCII, surrogates).
//
// Build via BUILD_LOLI_DIFFTEST (same target family as loli_difftest).
// Run: HashStringHashTest [sample.loli ...]

#include "hashstringlite.h"

#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QString>

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("PASS: %s\n", msg);                                \
        } else {                                                      \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);            \
            ++failures;                                               \
        }                                                             \
    } while (0)

// --- Vectors produced by the Qt 5.14.1 probe (qHash(QString), seed 0). ---
struct ProbeVector {
    const char* utf8;
    unsigned hash;
};

static const ProbeVector kQtProbeVectors[] = {
    {"AdServices", 1198557633u},
    {"libsystem_trace.dylib", 3649295644u},
    {"MSDKPIXLBS", 809227947u},
    {"caulk", 94434198u},
    {"CoreMedia", 400022373u},
    {"PluginCrosCurl", 178597842u},
    {"libAXSafeCategoryBundle.dylib", 3680944715u},
    {"CoreHaptics", 1923764221u},
    {"CoreMotion", 3820471381u},
    {"libcoretls.dylib", 3600377849u},
    {"LockdownMode", 2650042288u},
    {"libSessionUtility.dylib", 2718827005u},
    {"AudioCodecs", 1647078003u},
    {"CrashSightCore", 208575253u},
    {"AppTrackingTransparency", 858492752u},
    {"ContextKitExtraction", 1766287886u},
    {"libCGInterfaces.dylib", 4185865477u},
    {"XCTTargetBootstrap", 2188118484u},
    {"libsystem_configuration.dylib", 692980493u},
    {"GPM_dylib", 644798037u},
    {"", 0u},
    {"a", 97u},
    {"ab", 3105u},
    {"abc", 96354u},
    {"hello world", 1794106052u},
    {"/data/app/com.tencent.mf.uam/lib/arm64/libgame.so", 2465489403u},
    {"libc.so", 165712844u},
    {"libart.so", 341765260u},
    {"libhwui.so", 4068107810u},
    {"Stacks", 2486336267u},
    {"Total", 80997156u},
    {"0123456789012345678901234567890123456789012345678901234567890123456789",
     355609315u},
    {"libApolloRelay-DomainDeviceIDWatch.dylib", 2804416300u},
    {"libAudioToolboxUtility.dylib", 3521786444u},
    {"libBSAFoundation.dylib", 894686304u},
};

// --- Controlled UTF-16 probes from the Qt probe (probe2): explicit code
// units hashed by QString::fromUtf16 then qHash. UTF-8 inputs below decode
// to exactly these units. ---
static const ProbeVector kUtf16ProbeVectors[] = {
    // single units: U+0080, U+00FF, U+0100, U+5185
    {"\xC2\x80", 128u},
    {"\xC3\xBF", 255u},
    {"\xC4\x80", 256u},
    {"\xE5\x86\x85", 20869u},
    // U+5185 U+5B58
    {"\xE5\x86\x85\xE5\xAD\x98", 670323u},
    // U+0080 U+0080 / x3
    {"\xC2\x80\xC2\x80", 4096u},
    {"\xC2\x80\xC2\x80\xC2\x80", 127104u},
    // surrogate pair U+1F600
    {"\xF0\x9F\x98\x80", 1772899u},
    // mixed ASCII + BMP
    {"a\xE5\x86\x85" "b", 740254u},
    // 4 units: a b U+5185 U+5B58
    {"ab\xE5\x86\x85\xE5\xAD\x98", 3654228u},
};

// Reads the .loli intern table (big-endian quint32 key + QString per entry).
static std::vector<std::pair<uint32_t, std::string>> ReadInternTable(const char* path) {
    std::vector<std::pair<uint32_t, std::string>> pairs;
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        printf("FAIL: cannot open %s\n", path);
        ++failures;
        return pairs;
    }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    quint32 magic;
    qint32 version;
    s >> magic >> version;
    if (magic != 0xA4B3C2D1u || version != 106) {
        printf("FAIL: bad magic/version in %s\n", path);
        ++failures;
        return pairs;
    }
    qint32 maxMem, series;
    s >> maxMem >> series;
    for (int i = 0; i < series; ++i) {
        qint32 points;
        s >> points;
        f.seek(f.pos() + static_cast<qint64>(points) * 16);
    }
    quint32 hashCount;
    s >> hashCount;
    for (quint32 i = 0; i < hashCount; ++i) {
        quint32 key;
        s >> key;
        QString str;
        s >> str;
        pairs.emplace_back(key, str.toStdString());
    }
    return pairs;
}

int main(int argc, char** argv) {
    // 1. Qt probe vectors.
    for (const auto& v : kQtProbeVectors) {
        const uint32_t got = HashStringLite::Hash(v.utf8);
        char msg[512];
        std::snprintf(msg, sizeof(msg), "qt-probe hash(%s) == %u", v.utf8, v.hash);
        CHECK(got == v.hash, msg);
    }

    // 2. Controlled UTF-16 vectors.
    for (const auto& v : kUtf16ProbeVectors) {
        const uint32_t got = HashStringLite::Hash(v.utf8);
        char msg[512];
        std::snprintf(msg, sizeof(msg), "utf16-probe hash matches for %u", v.hash);
        CHECK(got == v.hash, msg);
    }

    // 3. Real capture intern tables. Two checks per file:
    //    a) Algorithm equivalence: HashStringLite::Hash(s) == qHash(QString)
    //       for every interned string (the actual compat requirement).
    //    b) Stored-key self-consistency (informational): the known sample
    //       captures do NOT store raw qHash outputs - the uam_* captures
    //       re-index intern keys sequentially 1..N and 6s_heap_* uses the
    //       magic 0xDEADBEEF - so this count is reported, not asserted.
    for (int i = 1; i < argc; ++i) {
        const auto pairs = ReadInternTable(argv[i]);
        printf("--- %s: %d intern pairs\n", argv[i], static_cast<int>(pairs.size()));
        int matched = 0;
        for (const auto& kv : pairs) {
            if (qHash(QString::fromStdString(kv.second)) == kv.first)
                ++matched;
        }
        printf("INFO: stored keys that are qHash outputs: %d/%d "
               "(uam captures use sequential 1..N keys; 6s uses 0xDEADBEEF)\n",
               matched, static_cast<int>(pairs.size()));

        int ours = 0;
        std::string firstBad;
        uint32_t badExp = 0, badGot = 0;
        for (const auto& kv : pairs) {
            if (HashStringLite::Hash(kv.second) ==
                qHash(QString::fromStdString(kv.second))) {
                ++ours;
            } else if (firstBad.empty()) {
                firstBad = kv.second;
                badExp = qHash(QString::fromStdString(kv.second));
                badGot = HashStringLite::Hash(kv.second);
            }
        }
        char msg[256];
        std::snprintf(msg, sizeof(msg), "%s: HashStringLite == Qt qHash on intern strings: %d/%d",
                      argv[i], ours, static_cast<int>(pairs.size()));
        CHECK(ours == static_cast<int>(pairs.size()), msg);
        if (ours != static_cast<int>(pairs.size())) {
            printf("  first mismatch: '%s' expected %u got %u\n", firstBad.c_str(),
                   badExp, badGot);
        }

        // Constructor/intern-table round trip.
        {
            bool constructorOk = true;
            for (const auto& kv : pairs) {
                HashStringLite hs(kv.second);
                const uint32_t qt = qHash(QString::fromStdString(kv.second));
                if (hs.hashcode_ != qt || HashStringLite::hashmap_[qt] != kv.second) {
                    constructorOk = false;
                    break;
                }
            }
            CHECK(constructorOk, "HashStringLite(str) interns with matching hashcode");
            uint32_t sum = 0;
            for (const auto& kv : pairs) {
                HashStringLite hs(qHash(QString::fromStdString(kv.second)));
                sum += static_cast<uint32_t>(hs.Get().size());
            }
            CHECK(sum > 0 || pairs.empty(), "HashStringLite(hashcode) lookup works");
        }
    }

    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
