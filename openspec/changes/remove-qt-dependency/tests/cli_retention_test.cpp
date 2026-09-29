#include "clicapture.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace loli {

struct CliCaptureTestAccess {
    static void Arm(CliCaptureSession& capture, bool optimized) {
        capture.options_.useCache = optimized;
        capture.isConnected_ = true;
        capture.isCapturing_ = true;
    }
    static void Feed(CliCaptureSession& capture,
                     std::initializer_list<StacktraceChannel::RawStackInfo> allocations,
                     std::initializer_list<StacktraceChannel::FreeInfo> frees) {
        capture.OnStackData(
            std::vector<StacktraceChannel::RawStackInfo>(allocations),
            std::vector<StacktraceChannel::FreeInfo>(frees));
    }
};

} // namespace loli

namespace {

StacktraceChannel::RawStackInfo Alloc(uint32_t seq, uint64_t addr) {
    StacktraceChannel::RawStackInfo value;
    value.seq = seq;
    value.size = 64;
    value.addr = addr;
    value.recType = 1;
    value.stacktraces = {0x1000};
    return value;
}

bool HasAddress(const loli::CliCaptureSession& capture, uint64_t addr) {
    const auto& records = capture.GetSession().records;
    return std::any_of(records.begin(), records.end(), [addr](const loli::Record& r) {
        return r.addr == addr;
    });
}

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    constexpr uint64_t a = 0x100, b = 0x200, c = 0x300;
    loli::CliCaptureSession optimized;
    loli::CliCaptureTestAccess::Arm(optimized, true);
    loli::CliCaptureTestAccess::Feed(optimized, {Alloc(1, a), Alloc(2, b)}, {});
    loli::CliCaptureTestAccess::Feed(optimized, {}, {{3, a}});
    Check(optimized.GetSession().records.size() == 1 && HasAddress(optimized, b),
          "free must remove the old allocation");

    // Moved realloc: the old pointer is freed and the new one stays live.
    loli::CliCaptureTestAccess::Feed(optimized, {Alloc(4, a)}, {});
    loli::CliCaptureTestAccess::Feed(optimized, {Alloc(6, c)}, {{5, a}});
    Check(optimized.GetSession().records.size() == 2 &&
          !HasAddress(optimized, a) && HasAddress(optimized, c),
          "moved realloc must keep the new address");

    // In-place realloc: a free preceding the replacement allocation cannot
    // erase the newer record even when both events arrive in one packet.
    loli::CliCaptureTestAccess::Feed(optimized, {Alloc(8, c)}, {{7, c}});
    Check(optimized.GetSession().records.size() == 2 && HasAddress(optimized, c),
          "in-place realloc must keep the replacement record");

    // A free received before its older allocation must still win.
    loli::CliCaptureTestAccess::Feed(optimized, {}, {{11, b}});
    loli::CliCaptureTestAccess::Feed(optimized, {Alloc(10, b)}, {});
    Check(optimized.GetSession().records.size() == 1 && HasAddress(optimized, c),
          "late allocation must not resurrect an already-freed address");

    loli::CliCaptureSession allRecords;
    loli::CliCaptureTestAccess::Arm(allRecords, false);
    loli::CliCaptureTestAccess::Feed(allRecords, {Alloc(1, a)}, {{2, a}});
    Check(allRecords.GetSession().records.size() == 1,
          "default mode must retain freed allocation records");
    std::cout << "CLI retention: passed\n";
}
