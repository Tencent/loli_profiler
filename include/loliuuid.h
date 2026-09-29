#ifndef LOLIUUID_H
#define LOLIUUID_H

#include <array>
#include <cstdint>
#include <string>

// Qt-free record identity (replaces QUuid for StackRecord::uuid_).
//
// The uuid is a pure association key between records and callstacks; it is
// never semantically meaningful. It IS persisted in .loli files in its
// string form ("{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" - QUuid::toString
// layout), so this class reproduces exactly that string encoding and
// parsing, keeping .loli byte compatibility (design D8).
class LoliUuid {
public:
    // All-zero uuid (QUuid default-construct parity).
    LoliUuid() : bytes_{} {}

    // Creates a random uuid (QUuid::createUuid parity: 128 random bits).
    // Not RFC 4122 version-tagged - the .loli format never depended on
    // QUuid embedding version/variant bits, only on the string shape.
    static LoliUuid CreateUuid();

    // "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" (QUuid::toString parity,
    // lowercase hex, braces included).
    std::string ToString() const;

    // Parses the QUuid::toString string form (braces optional, case
    // insensitive). Returns a null uuid on malformed input
    // (QUuid::fromString parity).
    static LoliUuid FromString(const std::string& str);

    bool IsNull() const;
    bool operator==(const LoliUuid& other) const { return bytes_ == other.bytes_; }
    bool operator!=(const LoliUuid& other) const { return bytes_ != other.bytes_; }
    bool operator<(const LoliUuid& other) const { return bytes_ < other.bytes_; }

private:
    std::array<uint8_t, 16> bytes_;
};

// Hash support for unordered_map/unordered_set keys.
struct LoliUuidHash {
    std::size_t operator()(const LoliUuid& uuid) const;
};

namespace std {
template <>
struct hash<LoliUuid> : LoliUuidHash {};
} // namespace std

#endif // LOLIUUID_H
