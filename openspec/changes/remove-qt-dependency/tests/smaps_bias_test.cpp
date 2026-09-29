#include "smaps/smapssectionlite.h"

#include <cstdint>

int main() {
    SMapsSectionLite libc;
    // Two independent libc.so mappings share one basename in /proc/pid/smaps.
    libc.addrs_.emplace_back(0x100000, 0x110000, 0);
    libc.addrs_.emplace_back(0x110000, 0x120000, 0x10000);
    libc.addrs_.emplace_back(0x700000, 0x710000, 0);
    libc.addrs_.emplace_back(0x710000, 0x720000, 0x10000);

    uint64_t vaddr = 0;
    if (!libc.Contains(0x115678, 0, vaddr) || vaddr != 0x15678)
        return 1;
    if (!libc.Contains(0x715678, 0, vaddr) || vaddr != 0x15678)
        return 2;
    if (libc.Contains(0x500000, 0, vaddr))
        return 3;

    std::unordered_map<std::string, SMapsSectionLite> sections;
    sections.emplace("libc.so", libc);
    SMapsAddressIndex index(sections);
    const std::string* library = nullptr;
    if (!index.Resolve(0x115678, library, vaddr) ||
        *library != "libc.so" || vaddr != 0x15678)
        return 4;
    if (!index.Resolve(0x715678, library, vaddr) ||
        *library != "libc.so" || vaddr != 0x15678)
        return 5;
    if (index.Resolve(0x500000, library, vaddr))
        return 6;
}
