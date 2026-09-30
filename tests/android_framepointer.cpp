#include "loli_utils.h"
#include <cstdio>

extern "C" uintptr_t loli_get_stackframepc(uintptr_t fp);
extern "C" bool loli_is_stackframe_valid(uintptr_t fp, uintptr_t prev_fp, uintptr_t stack_end);

int main() {
    uintptr_t frames[4] = {0, 0, 0, 0};
    const uintptr_t previous = reinterpret_cast<uintptr_t>(frames);
    const uintptr_t current = reinterpret_cast<uintptr_t>(frames + 2);
    const uintptr_t end = reinterpret_cast<uintptr_t>(frames + 4);
    // Signed null return PCs observed on the Xiaomi ARM64 device must not
    // become roots. Unsigned executable addresses must remain unchanged.
    const uintptr_t signedNulls[] = {0x71dc8000000000ULL, 0x20648000000000ULL};
    for (const auto pc : signedNulls) {
        frames[3] = pc;
        if (loli_get_stackframepc(current) != 0 ||
            loli_is_stackframe_valid(current, previous, end)) {
            std::fprintf(stderr, "Signed null PC was accepted: %llx\n",
                         static_cast<unsigned long long>(pc));
            return 1;
        }
    }
    frames[3] = 0x76cb009000ULL;
    if (loli_get_stackframepc(current) != frames[3] ||
        !loli_is_stackframe_valid(current, previous, end))
        return 1;
    std::puts("ARM64 signed-null rejection and unsigned-PC preservation passed");
    return 0;
}
