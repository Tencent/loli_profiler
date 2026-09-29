#include "lolirecord.h"

#include "lolistream.h"

namespace loli {

bool WriteSession(const Session& session, std::vector<uint8_t>& out) {
    LoliWriter w;

    w.WriteUInt32(kMagic);
    w.WriteInt32(kVersion);

    // meminfo series
    w.WriteInt32(session.maxMemInfoValue);
    w.WriteInt32(static_cast<int32_t>(session.memSeries.size()));
    for (const auto& series : session.memSeries) {
        w.WriteInt32(static_cast<int32_t>(series.size()));
        for (const auto& point : series)
            w.WriteQPointF(point.time, point.value);
    }

    // string intern table (QHash<quint32,QString> operator<< form:
    // quint32 count + pairs in iteration order)
    w.WriteCount32(static_cast<uint32_t>(session.internTable.size()));
    for (const auto& kv : session.internTable) {
        w.WriteUInt32(kv.first);
        w.WriteQString(kv.second);
    }

    // records
    w.WriteInt32(static_cast<int32_t>(session.records.size()));
    for (const auto& record : session.records) {
        w.WriteQString(record.uuid.ToString());
        w.WriteUInt32(record.seq);
        w.WriteInt32(record.time);
        w.WriteInt32(record.size);
        w.WriteUInt64(record.addr);
        w.WriteUInt64(record.funcAddr);
        w.WriteUInt32(record.libHash);
    }

    // callstack map
    w.WriteInt32(static_cast<int32_t>(session.callStackMap.size()));
    for (const auto& kv : session.callStackMap) {
        w.WriteQString(kv.first.ToString());
        w.WriteInt32(static_cast<int32_t>(kv.second.size()));
        for (const auto& frame : kv.second) {
            w.WriteUInt32(frame.first);
            w.WriteUInt64(frame.second);
        }
    }

    // symbol map
    w.WriteInt32(static_cast<int32_t>(session.symbolMap.size()));
    for (const auto& lib : session.symbolMap) {
        w.WriteQString(lib.first);
        w.WriteInt32(static_cast<int32_t>(lib.second.size()));
        for (const auto& sym : lib.second) {
            w.WriteUInt64(sym.first);
            w.WriteQString(sym.second);
        }
    }

    // freeaddr map
    w.WriteInt32(static_cast<int32_t>(session.freeAddrMap.size()));
    for (const auto& entry : session.freeAddrMap) {
        w.WriteUInt64(entry.first);
        w.WriteUInt32(entry.second);
    }

    // screenshots
    w.WriteInt32(static_cast<int32_t>(session.screenshots.size()));
    for (const auto& shot : session.screenshots) {
        w.WriteInt32(shot.timeMs);
        w.WriteQByteArray(shot.jpeg);
    }

    // smaps
    w.WriteInt32(static_cast<int32_t>(session.smapsSections.size()));
    for (const auto& kv : session.smapsSections) {
        w.WriteQString(kv.first);
        const auto& section = kv.second;
        w.WriteInt32(static_cast<int32_t>(section.addrs.size()));
        for (const auto& addr : section.addrs) {
            w.WriteUInt64(addr.start);
            w.WriteUInt64(addr.end);
            w.WriteUInt64(addr.offset);
        }
        w.WriteInt32(section.virtual_);
        w.WriteInt32(section.rss_);
        w.WriteInt32(section.pss_);
        w.WriteInt32(section.privateClean_);
        w.WriteInt32(section.privateDirty_);
        w.WriteInt32(section.sharedClean_);
        w.WriteInt32(section.sharedDirty_);
    }

    out = std::move(w.Buffer());
    return true;
}

bool ReadSession(const uint8_t* data, std::size_t size, Session& session,
                 std::string* error) {
    LoliReader r(data, size);
    const auto fail = [&error](const char* what) {
        if (error)
            *error = what;
        return false;
    };

    session.Clear();

    uint32_t magic;
    if (!r.ReadUInt32(magic))
        return fail("truncated magic");
    if (magic != kMagic)
        return fail("bad magic");
    int32_t version;
    if (!r.ReadInt32(version))
        return fail("truncated version");
    if (version != kVersion)
        return fail("bad version");

    // meminfo series
    if (!r.ReadInt32(session.maxMemInfoValue))
        return fail("truncated maxMemInfoValue");
    int32_t seriesCount;
    if (!r.ReadInt32(seriesCount) || seriesCount < 0)
        return fail("bad seriesCount");
    session.memSeries.resize(static_cast<std::size_t>(seriesCount));
    for (int i = 0; i < seriesCount; ++i) {
        int32_t pointCount;
        if (!r.ReadInt32(pointCount) || pointCount < 0)
            return fail("bad pointsCount");
        auto& series = session.memSeries[static_cast<std::size_t>(i)];
        series.reserve(static_cast<std::size_t>(pointCount));
        for (int j = 0; j < pointCount; ++j) {
            MemPoint point;
            if (!r.ReadQPointF(point.time, point.value))
                return fail("truncated mem point");
            series.push_back(point);
        }
    }

    // string intern table
    uint32_t hashCount;
    if (!r.ReadCount32(hashCount))
        return fail("truncated intern table count");
    for (uint32_t i = 0; i < hashCount; ++i) {
        uint32_t hash;
        std::string str;
        if (!r.ReadUInt32(hash) || !r.ReadQString(str))
            return fail("truncated intern table entry");
        session.internTable.emplace(hash, std::move(str));
    }

    // records
    int32_t recordCount;
    if (!r.ReadInt32(recordCount) || recordCount < 0)
        return fail("bad recordCount");
    session.records.reserve(static_cast<std::size_t>(recordCount));
    for (int i = 0; i < recordCount; ++i) {
        Record record;
        std::string uuidStr;
        if (!r.ReadQString(uuidStr))
            return fail("truncated record uuid");
        record.uuid = LoliUuid::FromString(uuidStr);
        if (!r.ReadUInt32(record.seq) || !r.ReadInt32(record.time) ||
            !r.ReadInt32(record.size) || !r.ReadUInt64(record.addr) ||
            !r.ReadUInt64(record.funcAddr) || !r.ReadUInt32(record.libHash))
            return fail("truncated record fields");
        session.records.push_back(record);
    }

    // callstack map
    int32_t callstackCount;
    if (!r.ReadInt32(callstackCount) || callstackCount < 0)
        return fail("bad callstackCount");
    for (int i = 0; i < callstackCount; ++i) {
        std::string uuidStr;
        int32_t frameCount;
        if (!r.ReadQString(uuidStr) || !r.ReadInt32(frameCount) || frameCount < 0)
            return fail("bad callstack header");
        CallStack stack;
        stack.reserve(static_cast<std::size_t>(frameCount));
        for (int j = 0; j < frameCount; ++j) {
            uint32_t libHash;
            uint64_t addr;
            if (!r.ReadUInt32(libHash) || !r.ReadUInt64(addr))
                return fail("truncated callstack frame");
            stack.emplace_back(libHash, addr);
        }
        session.callStackMap.emplace(LoliUuid::FromString(uuidStr), std::move(stack));
    }

    // symbol map
    int32_t symbolLibCount;
    if (!r.ReadInt32(symbolLibCount) || symbolLibCount < 0)
        return fail("bad symbolLibCount");
    for (int i = 0; i < symbolLibCount; ++i) {
        std::string libName;
        int32_t symbolCount;
        if (!r.ReadQString(libName) || !r.ReadInt32(symbolCount) || symbolCount < 0)
            return fail("bad symbol lib header");
        auto& map = session.symbolMap[libName];
        map.reserve(static_cast<std::size_t>(symbolCount));
        for (int j = 0; j < symbolCount; ++j) {
            uint64_t addr;
            std::string name;
            if (!r.ReadUInt64(addr) || !r.ReadQString(name))
                return fail("truncated symbol entry");
            map.emplace(addr, std::move(name));
        }
    }

    // freeaddr map
    int32_t freeCount;
    if (!r.ReadInt32(freeCount) || freeCount < 0)
        return fail("bad freeaddrCount");
    session.freeAddrMap.reserve(static_cast<std::size_t>(freeCount));
    for (int i = 0; i < freeCount; ++i) {
        uint64_t addr;
        uint32_t seq;
        if (!r.ReadUInt64(addr) || !r.ReadUInt32(seq))
            return fail("truncated freeaddr entry");
        session.freeAddrMap.emplace_back(addr, seq);
    }

    // screenshots
    int32_t shotCount;
    if (!r.ReadInt32(shotCount) || shotCount < 0)
        return fail("bad screenshotCount");
    session.screenshots.reserve(static_cast<std::size_t>(shotCount));
    for (int i = 0; i < shotCount; ++i) {
        Screenshot shot;
        if (!r.ReadInt32(shot.timeMs) || !r.ReadQByteArray(shot.jpeg))
            return fail("truncated screenshot");
        session.screenshots.push_back(std::move(shot));
    }

    // smaps
    int32_t smapsCount;
    if (!r.ReadInt32(smapsCount) || smapsCount < 0)
        return fail("bad smapsCount");
    for (int i = 0; i < smapsCount; ++i) {
        std::string name;
        int32_t addrCount;
        if (!r.ReadQString(name) || !r.ReadInt32(addrCount) || addrCount < 0)
            return fail("bad smaps header");
        SMapsSection section;
        section.addrs.reserve(static_cast<std::size_t>(addrCount));
        for (int j = 0; j < addrCount; ++j) {
            uint64_t start, end, offset;
            if (!r.ReadUInt64(start) || !r.ReadUInt64(end) || !r.ReadUInt64(offset))
                return fail("truncated smaps addr");
            section.addrs.emplace_back(start, end, offset);
        }
        if (!r.ReadInt32(section.virtual_) || !r.ReadInt32(section.rss_) ||
            !r.ReadInt32(section.pss_) || !r.ReadInt32(section.privateClean_) ||
            !r.ReadInt32(section.privateDirty_) || !r.ReadInt32(section.sharedClean_) ||
            !r.ReadInt32(section.sharedDirty_))
            return fail("truncated smaps metrics");
        session.smapsSections.emplace(std::move(name), std::move(section));
    }

    return true; // trailing bytes are ignored (forward compatibility)
}

} // namespace loli
