#ifndef SMAPSSECTIONLITE_H
#define SMAPSSECTIONLITE_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Qt-free replacement for include/smaps/smapssection.h. Keeps the same fields
// while handling duplicate library basenames in separate linker namespaces.

struct SMapsSectionAddrLite {
    uint64_t start_ = 0, end_ = 0, offset_ = 0;
    SMapsSectionAddrLite() = default;
    SMapsSectionAddrLite(uint64_t start, uint64_t end, uint64_t offset)
        : start_(start), end_(end), offset_(offset) {}
};

struct SMapsSectionLite {
    std::vector<SMapsSectionAddrLite> addrs_;
    uint32_t virtual_ = 0;
    uint32_t rss_ = 0;
    uint32_t pss_ = 0;
    uint32_t sharedClean_ = 0;
    uint32_t sharedDirty_ = 0;
    uint32_t privateClean_ = 0;
    uint32_t privateDirty_ = 0;

    bool Contains(uint64_t addr, int32_t size, uint64_t& symbolVAddr) const {
        (void)size;

        // For each segment: runtime_start = load_bias + segment_vaddr
        // Where load_bias is constant across all segments.
        //
        // For PIE binaries, the first segment (ELF headers) has:
        //   - file_offset = 0
        //   - vaddr = 0
        //   - runtime_start = load_bias
        //
        // For subsequent segments with page alignment (NDK25):
        //   - file_offset might not equal vaddr due to padding
        //   - But: (runtime_start - load_bias) always equals vaddr
        //
        // Therefore: symbol_vaddr = runtime_addr - load_bias
        //
        // To calculate load_bias, we find the segment with file_offset=0:
        //   load_bias = that_segment's_runtime_start - 0

        // Smaps may contain several instances with the same basename (for
        // example libc.so in different linker namespaces). Choose the offset-0
        // mapping immediately preceding the containing segment, not the first
        // offset-0 mapping in the entire basename group.
        const SMapsSectionAddrLite* containing = nullptr;
        uint64_t loadBias = 0;
        for (const auto& segment : addrs_) {
            if (addr >= segment.start_ && addr < segment.end_) {
                containing = &segment;
                break;
            }
        }
        if (!containing)
            return false;
        for (const auto& segment : addrs_) {
            if (segment.offset_ == 0 && segment.start_ <= containing->start_ &&
                segment.start_ >= loadBias)
                loadBias = segment.start_;
        }
        if (loadBias == 0)
            loadBias = containing->start_ - containing->offset_;
        symbolVAddr = addr - loadBias;
        return true;
    }
};

// Sorted runtime ranges for resolving large captures without testing every
// frame against every smaps section. Basenames may occur in several linker
// namespaces; each offset-zero mapping starts a separate load-bias group.
class SMapsAddressIndex {
public:
    explicit SMapsAddressIndex(
        const std::unordered_map<std::string, SMapsSectionLite>& sections) {
        for (const auto& kv : sections) {
            const std::string& name = kv.first;
            if (name.size() < 3 || name.compare(name.size() - 3, 3, ".so") != 0)
                continue;
            std::vector<SMapsSectionAddrLite> segments = kv.second.addrs_;
            std::sort(segments.begin(), segments.end(),
                      [](const auto& a, const auto& b) { return a.start_ < b.start_; });
            uint64_t bias = 0;
            for (const auto& segment : segments) {
                if (segment.offset_ == 0 || bias == 0)
                    bias = segment.start_ - segment.offset_;
                ranges_.push_back({segment.start_, segment.end_, bias, name});
            }
        }
        std::sort(ranges_.begin(), ranges_.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
    }

    bool Resolve(uint64_t address, const std::string*& library,
                 uint64_t& virtualAddress) const {
        auto it = std::upper_bound(ranges_.begin(), ranges_.end(), address,
            [](uint64_t value, const Range& range) { return value < range.start; });
        if (it == ranges_.begin())
            return false;
        --it;
        if (address >= it->end || address < it->bias)
            return false;
        library = &it->library;
        virtualAddress = address - it->bias;
        return true;
    }

private:
    struct Range {
        uint64_t start;
        uint64_t end;
        uint64_t bias;
        std::string library;
    };
    std::vector<Range> ranges_;
};

// Qt-free port of CliProfiler::ReadSMapsFile's parser (src/cliprofiler.cpp):
// parses the text of a /proc/<pid>/smaps dump into per-library sections.
// The `total` argument accumulates the sum over all sections (the Qt
// original's `total` SMapsSection); pass nullptr to skip it.
// Matches the original's behavior: sections keyed by the trailing library
// name on a mapping line (basename unless it starts with '['), metric lines
// matched by exact "Key:" words, and unknown keys ignored.
void ParseSmapsText(const std::string& text,
                    std::unordered_map<std::string, SMapsSectionLite>& outSections,
                    SMapsSectionLite* total = nullptr);

#endif // SMAPSSECTIONLITE_H
