// Round-trip test (task 3.5): simulates the GuiDataBridge save path -
// a loli::Session (as the bridge builds it from a load or capture) is
// written with loli::WriteSession to a real file, then re-read with
// loli::ReadSession AND with a Qt QDataStream reference walk. Counts,
// aggregates and per-record values must match; the file must be loadable
// by the previous Qt-based tool (byte-compatible format).
//
// Build via BUILD_LOLI_DIFFTEST. Run: LoliRoundTripTest

#include "lolirecord.h"
#include "loliuuid.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QCoreApplication>

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

// Qt reference walk (same as loli_loadtest): returns record count and
// aggregate sums so we prove a Qt reader accepts the file identically.
struct QtSummary {
    bool ok = false;
    long long records = 0;
    long long callstacks = 0;
    unsigned long long sizeSum = 0;
    unsigned long long addrSum = 0;
};

static QtSummary QtWalk(const QString& path) {
    QtSummary st;
    QFile f(path);
    if (!f.open(QFile::ReadOnly))
        return st;
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_5_12);
    quint32 magic;
    qint32 version;
    s >> magic >> version;
    if (magic != 0xA4B3C2D1u || version != 106)
        return st;
    qint32 maxMem, seriesCount;
    s >> maxMem >> seriesCount;
    for (int i = 0; i < seriesCount; ++i) {
        qint32 points;
        s >> points;
        for (int j = 0; j < points; ++j) {
            QPointF p;
            s >> p;
        }
    }
    QHash<quint32, QString> hashmap;
    s >> hashmap;
    qint32 records;
    s >> records;
    st.records = records;
    for (int i = 0; i < records; ++i) {
        QString uuid;
        quint32 seq;
        qint32 time, size;
        quint64 addr, func;
        quint32 libHash;
        s >> uuid >> seq >> time >> size >> addr >> func >> libHash;
        st.sizeSum += static_cast<unsigned long long>(size);
        st.addrSum += addr;
    }
    qint32 callstacks;
    s >> callstacks;
    st.callstacks = callstacks;
    for (int i = 0; i < callstacks; ++i) {
        QString uuid;
        qint32 frames;
        s >> uuid >> frames;
        for (int j = 0; j < frames; ++j) {
            quint32 h;
            quint64 a;
            s >> h >> a;
        }
    }
    st.ok = (s.status() == QDataStream::Ok);
    return st;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const char* outPath = argc > 1 ? argv[1] : "roundtrip_test.loli";

    // Build a session shaped like the bridge's capture-path output
    // (uuid per record, callstack frames keyed by uuid, intern table).
    loli::Session session;
    session.maxMemInfoValue = 512;
    session.memSeries.resize(6);
    for (int t = 0; t < 50; ++t) {
        const double td = static_cast<double>(t * 1000);
        session.memSeries[0].push_back({td, 1000.0 + t});
        session.memSeries[1].push_back({td, 500.0 + t});
        session.memSeries[2].push_back({td, 250.0 + t});
        session.memSeries[3].push_back({td, 125.0 + t});
        session.memSeries[4].push_back({td, 62.0 + t});
        session.memSeries[5].push_back({td, 31.0 + t});
    }
    session.internTable[0xDEADBEEF] = "libUE4.so";
    session.internTable[0x11223344] = "libc.so";
    const int recordCount = 5000;
    std::vector<LoliUuid> uuids;
    uuids.reserve(recordCount);
    for (int i = 0; i < recordCount; ++i) {
        loli::Record r;
        r.uuid = LoliUuid::CreateUuid();
        r.seq = static_cast<uint32_t>(i);
        r.time = i * 7;
        r.size = 32 + (i % 1024);
        r.addr = 0x10000 + static_cast<uint64_t>(i) * 16;
        r.funcAddr = 0x20000 + static_cast<uint64_t>(i) * 8;
        r.libHash = 0xDEADBEEF;
        session.records.push_back(r);
        uuids.push_back(r.uuid);
        // every 5th record gets a callstack (mixed depths)
        if (i % 5 == 0) {
            loli::CallStack stack;
            const int depth = 1 + (i % 8);
            for (int d = 0; d < depth; ++d)
                stack.emplace_back(0x11223344u, 0x70000 + static_cast<uint64_t>(i * 16 + d));
            session.callStackMap.emplace(r.uuid, std::move(stack));
        }
    }
    session.symbolMap["libUE4.so"][0x20000] = "FMemory::Malloc";
    session.freeAddrMap.emplace_back(0x10000, 1u);
    loli::Screenshot shot;
    shot.timeMs = 4200;
    shot.jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0x02, 0xFF, 0xD9};
    session.screenshots.push_back(std::move(shot));
    loli::SMapsSection heap;
    heap.addrs.emplace_back(0x1000, 0x2000, 0);
    heap.virtual_ = 4096;
    heap.rss_ = 2048;
    heap.pss_ = 1024;
    heap.privateClean_ = 1;
    heap.privateDirty_ = 2;
    heap.sharedClean_ = 3;
    heap.sharedDirty_ = 4;
    session.smapsSections["[heap]"] = heap;

    // ---- Save (the bridge's SaveRecord path) ----
    std::vector<uint8_t> bytes;
    CHECK(loli::WriteSession(session, bytes), "write: WriteSession ok");
    {
        QFile f(QString::fromLocal8Bit(outPath));
        CHECK(f.open(QIODevice::WriteOnly), "write: open output file");
        CHECK(f.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<qint64>(bytes.size())) ==
                  static_cast<qint64>(bytes.size()),
              "write: all bytes written");
        f.close();
    }

    // ---- Reload with loli::ReadSession ----
    {
        QFile f(QString::fromLocal8Bit(outPath));
        CHECK(f.open(QFile::ReadOnly), "reload: open");
        const QByteArray all = f.readAll();
        loli::Session back;
        std::string err;
        CHECK(loli::ReadSession(reinterpret_cast<const uint8_t*>(all.constData()),
                                static_cast<std::size_t>(all.size()), back, &err),
              "reload: ReadSession parses own file");
        CHECK(back.records.size() == session.records.size(), "reload: record count");
        CHECK(back.callStackMap.size() == session.callStackMap.size(), "reload: callstack count");
        CHECK(back.internTable == session.internTable, "reload: intern table");
        CHECK(back.symbolMap == session.symbolMap, "reload: symbol map");
        CHECK(back.smapsSections == session.smapsSections, "reload: smaps");
        CHECK(back.screenshots.size() == 1 &&
                  back.screenshots[0].jpeg == session.screenshots[0].jpeg,
              "reload: screenshot bytes");
        bool perRecordOk = true;
        for (std::size_t i = 0; i < back.records.size(); ++i) {
            const auto& a = back.records[i];
            const auto& b = session.records[i];
            if (!(a.uuid == b.uuid) || a.seq != b.seq || a.time != b.time ||
                a.size != b.size || a.addr != b.addr || a.funcAddr != b.funcAddr ||
                a.libHash != b.libHash) {
                perRecordOk = false;
                break;
            }
        }
        CHECK(perRecordOk, "reload: every record field + uuid matches");
        bool framesOk = true;
        for (const auto& kv : back.callStackMap) {
            const auto it = session.callStackMap.find(kv.first);
            if (it == session.callStackMap.end() || it->second != kv.second) {
                framesOk = false;
                break;
            }
        }
        CHECK(framesOk, "reload: every callstack matches by uuid");
    }

    // ---- Verify a Qt reader accepts the file with identical values ----
    {
        const QtSummary qt = QtWalk(QString::fromLocal8Bit(outPath));
        CHECK(qt.ok, "qt-read: Qt QDataStream walk succeeds on our file");
        CHECK(qt.records == recordCount, "qt-read: record count matches");
        CHECK(qt.callstacks == static_cast<long long>(session.callStackMap.size()),
              "qt-read: callstack count matches");
        unsigned long long sizeSum = 0, addrSum = 0;
        for (const auto& r : session.records) {
            sizeSum += static_cast<unsigned long long>(r.size);
            addrSum += r.addr;
        }
        CHECK(qt.sizeSum == sizeSum, "qt-read: size aggregate matches");
        CHECK(qt.addrSum == addrSum, "qt-read: addr aggregate matches");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
