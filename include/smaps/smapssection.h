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
    bool Contains(quint64 addr, qint32 size, quint64& fileOffset) const  {
        (void)size;
        for (auto& sectionAddr : addrs_) {
            if (addr >= sectionAddr.start_ && addr < sectionAddr.end_) {
                // Convert runtime address to file offset directly
                // file_offset = (runtime_addr - section_start) + section_offset
                fileOffset = (addr - sectionAddr.start_) + sectionAddr.offset_;
                return true;
            }
        }
        return false;
    }
};

#endif // SMAPSSECTION_H
