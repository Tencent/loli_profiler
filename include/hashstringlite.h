#ifndef HASHSTRINGLITE_H
#define HASHSTRINGLITE_H

#include <cstdint>
#include <string>
#include <unordered_map>

// Qt-free replacement for include/hashstring.h (struct HashString).
//
// The hashcodes ARE persisted in .loli files (libHash record fields, callstack
// frame keys, and the intern table keys themselves), so they must be identical
// to what the Qt build produced. The Qt build used qHash(QString) from Qt
// 5.14.1 with the default seed (0): a deterministic fold over the string's
// UTF-16 code units, h = h * 31 + unit (verified empirically against a Qt
// 5.14.1 probe during the Qt removal).
//
// Note: the global QHash seed (qGlobalQHashSeed, randomized per process since
// Qt 5.6) only affects container-internal rehashing; direct qHash(QString)
// calls with the default seed parameter are always seeded with 0, which is
// what HashString used.
struct HashStringLite {
    static std::unordered_map<uint32_t, std::string> hashmap_;

    uint32_t hashcode_ = 0;

    HashStringLite() = default;
    explicit HashStringLite(uint32_t hashcode) : hashcode_(hashcode) {}
    explicit HashStringLite(const std::string& str) : hashcode_(Intern(str)) {}

    const std::string& Get() const {
        return hashmap_[hashcode_];
    }

    // Computes the hash for a UTF-8 string and interns it (QHash-compatible
    // code, matching the Qt HashString constructor).
    static uint32_t Intern(const std::string& str) {
        const uint32_t code = Hash(str);
        auto it = hashmap_.find(code);
        if (it == hashmap_.end())
            hashmap_.emplace(code, str);
        return code;
    }

    // qHash(QString) with seed 0 for a UTF-8 string.
    static uint32_t Hash(const std::string& str);

    // qHash(QString) with seed 0 over raw UTF-16 code units (host order).
    static uint32_t HashUtf16(const uint16_t* units, std::size_t count);

    // Empties the intern table (capture-launch reset parity).
    static void Clear() { hashmap_.clear(); }
};

#endif // HASHSTRINGLITE_H
