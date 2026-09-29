#ifndef LOLISTREAM_H
#define LOLISTREAM_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Endian-explicit binary reader/writer reproducing the QDataStream byte
// layout used by the .loli format (see
// openspec/changes/remove-qt-dependency/loli-format.md). All multi-byte
// values are big-endian. Qt type encodings:
//   qint32/quint32/quint64/double  fixed-width big-endian
//   QString     quint32 byteLength + UTF-16BE (byteLength = 2 x code units;
//               a null QString is 0xFFFFFFFF, an empty one is 0)
//   QByteArray  quint32 byteLength + raw bytes (null = 0xFFFFFFFF)
//   QPointF     two doubles
//   QHash<<     quint32 count + (key, value) pairs in iteration order
//
// UTF-8 <-> UTF-16 conversion is lossy-safe: lone surrogates in input
// UTF-8 (invalid UTF-8, shouldn't occur) are replaced with U+FFFD on write
// and re-emitted as-is on read.
class LoliWriter {
public:
    LoliWriter() = default;

    void WriteUInt8(uint8_t v);
    void WriteInt8(int8_t v) { WriteUInt8(static_cast<uint8_t>(v)); }
    void WriteUInt32(uint32_t v);
    void WriteInt32(int32_t v) { WriteUInt32(static_cast<uint32_t>(v)); }
    void WriteUInt64(uint64_t v);
    void WriteInt64(int64_t v) { WriteUInt64(static_cast<uint64_t>(v)); }
    void WriteDouble(double v);
    // Count prefix helper for the QHash<< container form (quint32).
    void WriteCount32(uint32_t count) { WriteUInt32(count); }
    void WriteCount32i(int32_t count) { WriteUInt32(static_cast<uint32_t>(count)); }

    // QString encoding. isNull reproduces QDataStream's null-string marker
    // (0xFFFFFFFF); the .loli writers never write null strings, so the
    // default (empty string) is what call sites use.
    void WriteQString(const std::string& utf8, bool isNull = false);
    void WriteQByteArray(const std::vector<uint8_t>& bytes, bool isNull = false);
    void WriteQPointF(double x, double y) {
        WriteDouble(x);
        WriteDouble(y);
    }

    const std::vector<uint8_t>& Buffer() const { return buffer_; }
    std::vector<uint8_t>& Buffer() { return buffer_; }

private:
    std::vector<uint8_t> buffer_;
};

class LoliReader {
public:
    // Non-owning view over the bytes to read.
    LoliReader(const uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}
    explicit LoliReader(const std::vector<uint8_t>& buffer)
        : LoliReader(buffer.data(), buffer.size()) {}

    bool AtEnd() const { return pos_ >= size_; }
    std::size_t Pos() const { return pos_; }
    std::size_t Remaining() const { return size_ - pos_; }
    void Seek(std::size_t pos) { pos_ = pos < size_ ? pos : size_; }

    // All readers return false (and leave position unchanged on failure
    // where practical) when the data is exhausted or malformed.
    bool ReadUInt8(uint8_t& v);
    bool ReadInt8(int8_t& v) {
        uint8_t u;
        if (!ReadUInt8(u))
            return false;
        v = static_cast<int8_t>(u);
        return true;
    }
    bool ReadUInt32(uint32_t& v);
    bool ReadInt32(int32_t& v) { return ReadUInt32(reinterpret_cast<uint32_t&>(v)); }
    bool ReadUInt64(uint64_t& v);
    bool ReadInt64(int64_t& v) { return ReadUInt64(reinterpret_cast<uint64_t&>(v)); }
    bool ReadDouble(double& v);
    bool ReadCount32(uint32_t& count) { return ReadUInt32(count); }
    bool ReadCount32i(int32_t& count) { return ReadInt32(count); }

    // QString. Returns false on malformed/truncated UTF-16. The null-string
    // marker (0xFFFFFFFF) reads as an empty string with *isNull = true.
    bool ReadQString(std::string& utf8, bool* isNull = nullptr);
    bool ReadQByteArray(std::vector<uint8_t>& bytes, bool* isNull = nullptr);
    bool ReadQPointF(double& x, double& y) {
        return ReadDouble(x) && ReadDouble(y);
    }

private:
    bool ReadBytes(uint8_t* out, std::size_t n);

    const uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

#endif // LOLISTREAM_H
