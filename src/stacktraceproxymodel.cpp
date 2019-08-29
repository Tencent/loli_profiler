#include "stacktraceproxymodel.h"
#include "stacktracemodel.h"

StackTraceProxyModel::StackTraceProxyModel(QHash<QString, int>& freeAddrMap, QAbstractItemModel* srcModel, QObject *parent)
    : QSortFilterProxyModel(parent), freeAddrMap_(freeAddrMap) {
    setSourceModel(srcModel);
}

void StackTraceProxyModel::setSizeFilter(int value) {
    sizeFilter_ = value;
    invalidateFilter();
}

void StackTraceProxyModel::setPersistentFilter(bool value) {
    persistentFilter_ = value;
    invalidateFilter();
}

void StackTraceProxyModel::setLibraryFilter(const QString& value) {
    libraryFilter_ = value;
    invalidateFilter();
}

bool StackTraceProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const {
    const auto model = static_cast<StackTraceModel*>(sourceModel());
    const auto record = model->recordAt(sourceRow);
    if (sizeFilter_ > 0) {
        auto size = record.size_;
        switch(sizeFilter_) {
        case 1: // large
            if (size < 1048576)
                return false;
            break;
        case 2: // medium
            if (size >= 1048576 || size <= 1024)
                return false;
            break;
        case 3: // small
            if (size > 1024)
                return false;
            break;
        }
    }
    if (libraryFilter_.size() > 0) {
        if (record.library_ != libraryFilter_)
            return false;
    }
    if (persistentFilter_) {
        auto addr = record.addr_;
        auto time = record.time_;
        auto it = freeAddrMap_.find(addr);
        if (it != freeAddrMap_.end()) {
            if (time < it.value()) {
                return false;
            }
        }
    }
    return true;
}

bool StackTraceProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    auto leftData = sourceModel()->data(left, Qt::UserRole);
    auto rightData = sourceModel()->data(right, Qt::UserRole);
    if (leftData.type() == QVariant::Int) {
        return leftData.toInt() < rightData.toInt();
    } else {
        return leftData.toString() < rightData.toString();
    }
}
