#include "lolistream.h"

#include <cstring>

namespace {

// --- UTF-8 <-> UTF-16 conversion (UTF-16BE on the wire) ---

void AppendUtf16AsUtf8(std::string& out, uint16_t unit) {
    if (unit < 0x80) {
        out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
        out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
    }
}

// Returns the UTF-16 code units for a UTF-8 string. Invalid sequences
// become U+FFFD. Returns byte length of the UTF-16 encoding.
std::vector<uint16_t> Utf8ToUtf16(const std::string& utf8) {
    std::vector<uint16_t> units;
    units.reserve(utf8.size());
    std::size_t i = 0;
    const auto err = [&units]() { units.push_back(0xFFFD); };
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            units.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size() &&
                   (utf8[i + 1] & 0xC0) == 0x80) {
            units.push_back(static_cast<uint16_t>(((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F)));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size() &&
                   (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80) {
            units.push_back(static_cast<uint16_t>(((c & 0x0F) << 12) |
                                                  ((utf8[i + 1] & 0x3F) << 6) |
                                                  (utf8[i + 2] & 0x3F)));
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size() &&
                   (utf8[i + 1] & 0xC0) == 0x80 && (utf8[i + 2] & 0xC0) == 0x80 &&
                   (utf8[i + 3] & 0xC0) == 0x80) {
            const uint32_t cp = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) |
                                ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F);
            const uint32_t v = cp - 0x10000;
            units.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
            units.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
            i += 4;
        } else {
            err();
            i += 1;
        }
    }
    return units;
}

void Utf16ToUtf8(std::string& out, const std::vector<uint16_t>& units) {
    std::size_t i = 0;
    while (i < units.size()) {
        const uint16_t u = units[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
                const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(u) - 0xD800) << 10) +
                                    (units[i + 1] - 0xDC00);
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                i += 2;
            } else {
                out += "\xEF\xBF\xBD"; // U+FFFD: lone high surrogate
                i += 1;
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            out += "\xEF\xBF\xBD"; // lone low surrogate
            i += 1;
        } else {
            AppendUtf16AsUtf8(out, u);
            i += 1;
        }
    }
}

} // namespace

// --- LoliWriter ---

void LoliWriter::WriteUInt8(uint8_t v) {
    buffer_.push_back(v);
}

void LoliWriter::WriteUInt32(uint32_t v) {
    buffer_.push_back(static_cast<uint8_t>(v >> 24));
    buffer_.push_back(static_cast<uint8_t>(v >> 16));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
    buffer_.push_back(static_cast<uint8_t>(v));
}

void LoliWriter::WriteUInt64(uint64_t v) {
    WriteUInt32(static_cast<uint32_t>(v >> 32));
    WriteUInt32(static_cast<uint32_t>(v));
}

void LoliWriter::WriteDouble(double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    WriteUInt64(bits);
}

void LoliWriter::WriteQString(const std::string& utf8, bool isNull) {
    if (isNull) {
        WriteUInt32(0xFFFFFFFFu);
        return;
    }
    const std::vector<uint16_t> units = Utf8ToUtf16(utf8);
    WriteUInt32(static_cast<uint32_t>(units.size() * 2));
    for (uint16_t unit : units) {
        buffer_.push_back(static_cast<uint8_t>(unit >> 8));
        buffer_.push_back(static_cast<uint8_t>(unit));
    }
}

void LoliWriter::WriteQByteArray(const std::vector<uint8_t>& bytes, bool isNull) {
    if (isNull) {
        WriteUInt32(0xFFFFFFFFu);
        return;
    }
    WriteUInt32(static_cast<uint32_t>(bytes.size()));
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

// --- LoliReader ---

bool LoliReader::ReadBytes(uint8_t* out, std::size_t n) {
    if (n > size_ - pos_)
        return false;
    std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
}

bool LoliReader::ReadUInt8(uint8_t& v) {
    return ReadBytes(&v, 1);
}

bool LoliReader::ReadUInt32(uint32_t& v) {
    uint8_t b[4];
    if (!ReadBytes(b, 4))
        return false;
    v = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
        (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
    return true;
}

bool LoliReader::ReadUInt64(uint64_t& v) {
    uint32_t hi, lo;
    if (!ReadUInt32(hi) || !ReadUInt32(lo))
        return false;
    v = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

bool LoliReader::ReadDouble(double& v) {
    uint64_t bits;
    if (!ReadUInt64(bits))
        return false;
    std::memcpy(&v, &bits, sizeof(v));
    return true;
}

bool LoliReader::ReadQString(std::string& utf8, bool* isNull) {
    uint32_t byteLength;
    if (!ReadUInt32(byteLength))
        return false;
    if (isNull)
        *isNull = false;
    if (byteLength == 0xFFFFFFFFu) {
        utf8.clear();
        if (isNull)
            *isNull = true;
        return true;
    }
    if (byteLength % 2 != 0 || byteLength > Remaining() * 2)
        return false;
    if (byteLength / 2 > Remaining())
        return false;
    const std::size_t unitCount = byteLength / 2;
    std::vector<uint16_t> units(unitCount);
    for (std::size_t i = 0; i < unitCount; ++i) {
        uint8_t b[2];
        if (!ReadBytes(b, 2))
            return false;
        units[i] = static_cast<uint16_t>((b[0] << 8) | b[1]);
    }
    utf8.clear();
    Utf16ToUtf8(utf8, units);
    return true;
}

bool LoliReader::ReadQByteArray(std::vector<uint8_t>& bytes, bool* isNull) {
    uint32_t byteLength;
    if (!ReadUInt32(byteLength))
        return false;
    if (isNull)
        *isNull = false;
    if (byteLength == 0xFFFFFFFFu) {
        bytes.clear();
        if (isNull)
            *isNull = true;
        return true;
    }
    if (byteLength > Remaining())
        return false;
    bytes.resize(byteLength);
    if (!ReadBytes(bytes.data(), byteLength))
        return false;
    return true;
}
