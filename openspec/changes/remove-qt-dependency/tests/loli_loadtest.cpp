// Load-verification test (task 3.4): parse real .loli captures with the
// Qt-free loli::ReadSession and a Qt QDataStream reference walk; record
// counts, aggregates (total size, addr sums, intern-table, symbol counts)
// must match exactly.
//
// Build via BUILD_LOLI_DIFFTEST (same target family as loli_difftest).
// Run: LoliLoadTest <file.loli> [more.loli ...]

#include "lolirecord.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QUuid>

#include <cstdio>
#include <cstdlib>
#include <memory>
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

// Qt reference walk: mirrors src/gui/guidatabridge.cpp's load path.
struct QtStats {
    int seriesCount = 0;
    long long totalPoints = 0;
    int internCount = 0;
    long long recordCount = 0;
    unsigned long long sizeSum = 0;
    unsigned long long addrSum = 0;
    unsigned long long funcSum = 0;
    unsigned long long libHashSum = 0;
    long long callstackCount = 0;
    long long frameSum = 0;
    int symbolLibCount = 0;
    long long symbolCount = 0;
    long long freeCount = 0;
    int screenshotCount = 0;
    long long screenshotBytes = 0;
    int smapsCount = 0;
    bool ok = false;
    std::string error;
};

static QtStats QtWalk(const char* path) {
    QtStats st;
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        st.error = "open failed";
        return st;
    }
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    quint32 magic;
    qint32 version;
    s >> magic >> version;
    if (magic != 0xA4B3C2D1u || version != 106) {
        st.error = "bad magic/version";
        return st;
    }
    qint32 maxMem;
    s >> maxMem;
    qint32 series;
    s >> series;
    st.seriesCount = series;
    for (int i = 0; i < series && s.status() == QDataStream::Ok; ++i) {
        qint32 points;
        s >> points;
        st.totalPoints += points;
        for (int j = 0; j < points; ++j) {
            QPointF p;
            s >> p;
        }
    }
    QHash<quint32, QString> hashmap;
    s >> hashmap;
    st.internCount = hashmap.size();
    qint32 records;
    s >> records;
    st.recordCount = records;
    for (int i = 0; i < records; ++i) {
        QString uuid;
        quint32 seq;
        qint32 time, size;
        quint64 addr, func;
        quint32 libHash;
        s >> uuid >> seq >> time >> size >> addr >> func >> libHash;
        st.sizeSum += static_cast<unsigned long long>(size);
        st.addrSum += addr;
        st.funcSum += func;
        st.libHashSum += libHash;
    }
    qint32 callstacks;
    s >> callstacks;
    st.callstackCount = callstacks;
    for (int i = 0; i < callstacks; ++i) {
        QString uuid;
        qint32 frames;
        s >> uuid >> frames;
        st.frameSum += frames;
        for (int j = 0; j < frames; ++j) {
            quint32 h;
            quint64 a;
            s >> h >> a;
        }
    }
    qint32 symbolLibs;
    s >> symbolLibs;
    st.symbolLibCount = symbolLibs;
    for (int i = 0; i < symbolLibs; ++i) {
        QString lib;
        qint32 symbols;
        s >> lib >> symbols;
        st.symbolCount += symbols;
        for (int j = 0; j < symbols; ++j) {
            quint64 a;
            QString n;
            s >> a >> n;
        }
    }
    qint32 frees;
    s >> frees;
    st.freeCount = frees;
    for (int i = 0; i < frees; ++i) {
        quint64 a;
        quint32 q;
        s >> a >> q;
    }
    qint32 shots;
    s >> shots;
    st.screenshotCount = shots;
    for (int i = 0; i < shots; ++i) {
        qint32 t;
        QByteArray ba;
        s >> t >> ba;
        st.screenshotBytes += ba.size();
    }
    qint32 smaps;
    s >> smaps;
    st.smapsCount = smaps;
    for (int i = 0; i < smaps; ++i) {
        QString name;
        qint32 addrs;
        s >> name >> addrs;
        for (int j = 0; j < addrs; ++j) {
            quint64 a, b, c;
            s >> a >> b >> c;
        }
        qint32 m1, m2, m3, m4, m5, m6, m7;
        s >> m1 >> m2 >> m3 >> m4 >> m5 >> m6 >> m7;
    }
    st.ok = (s.status() == QDataStream::Ok);
    if (!st.ok)
        st.error = "stream error";
    return st;
}

static bool Verify(const char* path) {
    printf("--- %s\n", path);

    const QtStats qt = QtWalk(path);
    if (!qt.ok) {
        printf("FAIL: Qt reference walk failed: %s\n", qt.error.c_str());
        ++failures;
        return false;
    }

    // Qt-free read (memory-mapped-free: read the whole file).
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("FAIL: open for loli read\n");
        ++failures;
        return false;
    }
    std::vector<uint8_t> bytes;
    {
        // Heap buffer: a stack buffer here would overflow the 1MB default
        // stack (Verify's frame is probed by __chkstk before any code runs).
        std::vector<char> buf(1 << 20);
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
            bytes.insert(bytes.end(), buf.data(), buf.data() + n);
        fclose(f);
    }

    auto sessionPtr = std::make_unique<loli::Session>();
    loli::Session& session = *sessionPtr;
    std::string err;
    if (!loli::ReadSession(bytes.data(), bytes.size(), session, &err)) {
        printf("FAIL: loli::ReadSession: %s\n", err.c_str());
        ++failures;
        return false;
    }

    unsigned long long sizeSum = 0, addrSum = 0, funcSum = 0, libHashSum = 0;
    for (const auto& r : session.records) {
        sizeSum += static_cast<unsigned long long>(r.size);
        addrSum += r.addr;
        funcSum += r.funcAddr;
        libHashSum += r.libHash;
    }
    long long frameSum = 0;
    for (const auto& kv : session.callStackMap)
        frameSum += static_cast<long long>(kv.second.size());
    long long symbolCount = 0;
    for (const auto& kv : session.symbolMap)
        symbolCount += static_cast<long long>(kv.second.size());
    long long screenshotBytes = 0;
    for (const auto& shot : session.screenshots)
        screenshotBytes += static_cast<long long>(shot.jpeg.size());

    char msg[256];
    bool allOk = true;
#define CMP(field, ours, ref)                                         \
    do {                                                              \
        snprintf(msg, sizeof(msg), "%s: %lld == %lld", field,         \
                 static_cast<long long>(ours), static_cast<long long>(ref)); \
        const bool ok_ = (ours) == (ref);                             \
        CHECK(ok_, msg);                                              \
        allOk = allOk && ok_;                                         \
    } while (0)

    CMP("seriesCount", session.memSeries.size(), qt.seriesCount);
    long long totalPoints = 0;
    for (const auto& s : session.memSeries) totalPoints += static_cast<long long>(s.size());
    CMP("totalPoints", totalPoints, qt.totalPoints);
    CMP("internCount", session.internTable.size(), qt.internCount);
    CMP("recordCount", session.records.size(), qt.recordCount);
    CMP("sizeSum", sizeSum, qt.sizeSum);
    CMP("addrSum", addrSum, qt.addrSum);
    CMP("funcSum", funcSum, qt.funcSum);
    CMP("libHashSum", libHashSum, qt.libHashSum);
    CMP("callstackCount", session.callStackMap.size(), qt.callstackCount);
    CMP("frameSum", frameSum, qt.frameSum);
    CMP("symbolLibCount", session.symbolMap.size(), qt.symbolLibCount);
    CMP("symbolCount", symbolCount, qt.symbolCount);
    CMP("freeCount", session.freeAddrMap.size(), qt.freeCount);
    CMP("screenshotCount", session.screenshots.size(), qt.screenshotCount);
    CMP("screenshotBytes", screenshotBytes, qt.screenshotBytes);
    CMP("smapsCount", session.smapsSections.size(), qt.smapsCount);
    return allOk;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: LoliLoadTest <file.loli> [more.loli ...]\n");
        return 2;
    }
    for (int i = 1; i < argc; ++i)
        Verify(argv[i]);
    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
