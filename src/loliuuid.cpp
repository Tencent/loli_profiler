#include "loliuuid.h"

#include <cstdio>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <intrin.h>
#endif

namespace {

// QUuid::toString layout: "{%08x-%04x-%04x-%04x-%04x%08x}" over the 16
// bytes in big-endian field order (QUuid stores its fields that way).
// Field layout: 4 - 2 - 2 - 2 - 6 bytes.

int HexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

LoliUuid LoliUuid::CreateUuid() {
    // thread_local keeps generators independent across pool workers and
    // avoids the lock a shared generator would need.
    static thread_local std::mt19937_64 rng([]() {
#ifdef _WIN32
        return std::random_device{}() * (static_cast<uint64_t>(__rdtsc()) | 1);
#else
        return std::random_device{}();
#endif
    }());
    LoliUuid uuid;
    for (auto& b : uuid.bytes_)
        b = static_cast<uint8_t>(rng());
    return uuid;
}

std::string LoliUuid::ToString() const {
    char buf[39]; // 38 chars + NUL
    std::snprintf(buf, sizeof(buf),
                  "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  bytes_[0], bytes_[1], bytes_[2], bytes_[3],
                  bytes_[4], bytes_[5],
                  bytes_[6], bytes_[7],
                  bytes_[8], bytes_[9],
                  bytes_[10], bytes_[11], bytes_[12], bytes_[13], bytes_[14], bytes_[15]);
    return std::string(buf);
}

LoliUuid LoliUuid::FromString(const std::string& str) {
    // Collect hex digits, skipping '{', '}', and '-' separators. QUuid
    // accepts with/without braces and dashes; exactly 32 hex digits must
    // remain.
    char hex[33];
    int n = 0;
    for (char c : str) {
        if (c == '{' || c == '}' || c == '-')
            continue;
        if (HexValue(c) < 0)
            return LoliUuid(); // malformed - null uuid
        if (n >= 32)
            return LoliUuid();
        hex[n++] = c;
    }
    if (n != 32)
        return LoliUuid();

    LoliUuid uuid;
    for (int i = 0; i < 16; ++i)
        uuid.bytes_[static_cast<std::size_t>(i)] =
            static_cast<uint8_t>((HexValue(hex[i * 2]) << 4) | HexValue(hex[i * 2 + 1]));
    return uuid;
}

bool LoliUuid::IsNull() const {
    for (uint8_t b : bytes_) {
        if (b != 0)
            return false;
    }
    return true;
}

std::size_t LoliUuidHash::operator()(const LoliUuid& uuid) const {
    // FNV-1a over the 16 bytes - fast and well distributed for map keys.
    std::size_t h = 1469598103934665603ull;
    const char* data = reinterpret_cast<const char*>(&uuid);
    for (std::size_t i = 0; i < sizeof(LoliUuid); ++i) {
        h ^= static_cast<unsigned char>(data[i]);
        h *= 1099511628211ull;
    }
    return h;
}
