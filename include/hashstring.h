#ifndef HASHSTRING_H
#define HASHSTRING_H

#include <QHash>
#include <QString>

//#include <mutex>

struct HashString {
    static QHash<quint64, QString> hashmap_;

    // multi-thread begin
//    static thread_local QHash<quint64, QString> localhashmap_;
//    static QHash<quint64, QString> hashmapcache_;
//    static std::mutex hashmapCacheMutex_;
//    static void CacheThreadLocalStorage() {
//        std::lock_guard<std::mutex> lock(hashmapCacheMutex_);
//        for (auto it = localhashmap_.begin(); it != localhashmap_.end(); ++it) {
//            hashmapcache_.insert(it.key(), it.value());
//        }
//    }
//    static void FlushCache() {
//        for (auto it = hashmapcache_.begin(); it != hashmapcache_.end(); ++it) {
//            hashmap_.insert(it.key(), it.value());
//        }
//    }
    // multi-thread end

    quint64 hashcode_ = 0;

    HashString() = default;
    HashString(quint64 hashcode)
        : hashcode_(hashcode) {}
    HashString(const QString& str)
        : hashcode_(qHash(str)) {
        if (!hashmap_.contains(hashcode_)) {
            hashmap_.insert(hashcode_, str);
        }
    }
    QString Get() const {
        return hashmap_[hashcode_];
    }
};

#endif
