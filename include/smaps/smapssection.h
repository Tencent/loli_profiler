#ifndef SMAPSSECTION_H
#define SMAPSSECTION_H

#include <QVector>

struct SMapsSectionAddr {
    quint64 start_ = 0, end_ = 0, offset_ = 0;
    SMapsSectionAddr() = default;
    SMapsSectionAddr(quint64 start, quint64 end, quint64 offset) :
        start_(start), end_(end), offset_(offset) {}
};

struct SMapsSection {
    QVector<SMapsSectionAddr> addrs_;
    quint32 virtual_ = 0;
    quint32 rss_ = 0;
    quint32 pss_ = 0;
    quint32 sharedClean_ = 0;
    quint32 sharedDirty_ = 0;
    quint32 privateClean_ = 0;
    quint32 privateDirty_ = 0;

    bool Contains(quint64 addr, qint32 size, quint64& symbolVAddr) const  {
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
        
        // Find the segment with offset=0 to calculate load_bias
        quint64 loadBias = 0;
        for (const auto& segment : addrs_) {
            if (segment.offset_ == 0) {
                loadBias = segment.start_;
                break;
            }
        }
        
        // If no segment with offset=0, use first segment (less reliable)
        if (loadBias == 0 && !addrs_.empty()) {
            // Assume vaddr approximately equals offset for first segment
            loadBias = addrs_[0].start_ - addrs_[0].offset_;
        }
        
        // Find which segment contains this address
        for (auto& sectionAddr : addrs_) {
            if (addr >= sectionAddr.start_ && addr < sectionAddr.end_) {
                // Convert runtime address to symbol virtual address
                symbolVAddr = addr - loadBias;
                return true;
            }
        }
        return false;
    }
};

#endif // SMAPSSECTION_H
