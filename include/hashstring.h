#ifndef HASHSTRING_H
#define HASHSTRING_H

#include <QHash>
#include <QString>

struct HashString {
    static QHash<quint32, QString> hashmap_;

    quint32 hashcode_ = 0;

    HashString() = default;
    HashString(quint32 hashcode)
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
