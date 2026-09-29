#include "hashstringlite.h"

std::unordered_map<uint32_t, std::string> HashStringLite::hashmap_;

uint32_t HashStringLite::HashUtf16(const uint16_t* units, std::size_t count) {
    // Qt 5.14 qHash(QString), seed 0: h = h * 31 + utf16Unit, over every
    // UTF-16 code unit (including surrogates). uint32 wraparound matches
    // Qt's uint arithmetic.
    uint32_t h = 0;
    for (std::size_t i = 0; i < count; ++i)
        h = h * 31u + units[i];
    return h;
}

uint32_t HashStringLite::Hash(const std::string& str) {
    // Decode UTF-8 to UTF-16 code units on the fly (mirrors the QString
    // backing store). Invalid sequences map to U+FFFD, matching the
    // replacement-char policy used elsewhere in the Qt-free serializer
    // (lolistream.cpp). QString constructed from malformed UTF-8 would also
    // contain replacement characters.
    uint32_t h = 0;
    std::size_t i = 0;
    const std::size_t n = str.size();
    while (i < n) {
        uint32_t cp;
        const unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n &&
                   (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80) {
            cp = ((c & 0x1Fu) << 6) |
                 (static_cast<unsigned char>(str[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n &&
                   (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80) {
            cp = ((c & 0x0Fu) << 12) |
                 ((static_cast<unsigned char>(str[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(str[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < n &&
                   (static_cast<unsigned char>(str[i + 1]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(str[i + 2]) & 0xC0) == 0x80 &&
                   (static_cast<unsigned char>(str[i + 3]) & 0xC0) == 0x80) {
            cp = ((c & 0x07u) << 18) |
                 ((static_cast<unsigned char>(str[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(str[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(str[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }

        if (cp < 0x10000) {
            h = h * 31u + static_cast<uint16_t>(cp);
        } else {
            // Surrogate pair, same as QString's internal representation.
            const uint32_t v = cp - 0x10000;
            h = h * 31u + static_cast<uint16_t>(0xD800 + (v >> 10));
            h = h * 31u + static_cast<uint16_t>(0xDC00 + (v & 0x3FF));
        }
    }
    return h;
}
