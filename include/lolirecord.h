#ifndef LOLIRECORD_H
#define LOLIRECORD_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "loliuuid.h"

// Qt-free in-memory model of a .loli capture session, mirroring the fields
// the Qt build's CliProfiler/mainwindow kept (see
// openspec/changes/remove-qt-dependency/loli-format.md for the layout).
namespace loli {

struct MemPoint {
    double time = 0.0;
    double value = 0.0;
};

struct Record {
    LoliUuid uuid;
    uint32_t seq = 0;
    int32_t time = 0;
    int32_t size = 0;
    uint64_t addr = 0;
    uint64_t funcAddr = 0;
    uint32_t libHash = 0; // key into Session::internTable
};

using CallFrame = std::pair<uint32_t, uint64_t>; // (libHash, addr)
using CallStack = std::vector<CallFrame>;

struct Screenshot {
    int32_t timeMs = 0; // historical field name; Qt .loli wire unit is seconds
    std::vector<uint8_t> jpeg;
};

struct SMapsAddr {
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t offset = 0;
    SMapsAddr() = default;
    SMapsAddr(uint64_t s, uint64_t e, uint64_t o) : start(s), end(e), offset(o) {}
    bool operator==(const SMapsAddr& other) const {
        return start == other.start && end == other.end && offset == other.offset;
    }
    bool operator!=(const SMapsAddr& other) const { return !(*this == other); }
};

struct SMapsSection {
    std::vector<SMapsAddr> addrs;
    int32_t virtual_ = 0;
    int32_t rss_ = 0;
    int32_t pss_ = 0;
    int32_t privateClean_ = 0;
    int32_t privateDirty_ = 0;
    int32_t sharedClean_ = 0;
    int32_t sharedDirty_ = 0;

    bool operator==(const SMapsSection& other) const {
        return addrs == other.addrs && virtual_ == other.virtual_ && rss_ == other.rss_ &&
               pss_ == other.pss_ && privateClean_ == other.privateClean_ &&
               privateDirty_ == other.privateDirty_ &&
               sharedClean_ == other.sharedClean_ && sharedDirty_ == other.sharedDirty_;
    }
    bool operator!=(const SMapsSection& other) const { return !(*this == other); }
};

struct Session {
    int32_t maxMemInfoValue = 128;
    // 6 series: Total, NativeHeap, GfxDev, EGLmtrack, GLmtrack, Unknown.
    std::vector<std::vector<MemPoint>> memSeries;

    // String intern table (HashString::hashmap_ equivalent).
    std::unordered_map<uint32_t, std::string> internTable;

    std::vector<Record> records;
    std::unordered_map<LoliUuid, CallStack, LoliUuidHash> callStackMap;

    // Per-library symbol tables: lib -> (addr -> name).
    std::unordered_map<std::string, std::unordered_map<uint64_t, std::string>> symbolMap;

    // freeaddr entries (quint64 addr, quint32 seq) - only used during
    // capture filtering; kept for format round-trip fidelity.
    std::vector<std::pair<uint64_t, uint32_t>> freeAddrMap;

    std::vector<Screenshot> screenshots;
    std::unordered_map<std::string, SMapsSection> smapsSections;

    void Clear() {
        maxMemInfoValue = 128;
        memSeries.clear();
        internTable.clear();
        records.clear();
        callStackMap.clear();
        symbolMap.clear();
        freeAddrMap.clear();
        screenshots.clear();
        smapsSections.clear();
    }
};

constexpr uint32_t kMagic = 0xA4B3C2D1u;
constexpr int32_t kVersion = 106;

// Serializes a session in the exact .loli byte layout (byte-identical to
// the Qt QDataStream writer). Returns false on serialization failure
// (e.g. non-finite doubles would still be written; currently always true,
// kept for API symmetry with the reader).
bool WriteSession(const Session& session, std::vector<uint8_t>& out);

// Parses .loli bytes into the session. Returns false on the first
// malformed section; partially-read sections are truncated consistently
// (no torn records). Accepts files with the standard magic/version only.
bool ReadSession(const uint8_t* data, std::size_t size, Session& session,
                 std::string* error = nullptr);

} // namespace loli

#endif // LOLIRECORD_H
