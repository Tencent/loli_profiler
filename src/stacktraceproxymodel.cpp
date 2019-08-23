#include "stacktraceproxymodel.h"

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

bool StackTraceProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    auto model = sourceModel();
    if (sizeFilter_ > 0) {
        auto index1 = model->index(sourceRow, 1, sourceParent); // size
        int size = model->data(index1, Qt::UserRole).toInt();
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
        auto index3 = model->index(sourceRow, 3, sourceParent); // lib
        if (model->data(index3).toString() != libraryFilter_)
            return false;
    }
    if (persistentFilter_) {
        auto index0 = model->index(sourceRow, 1, sourceParent); // time
        auto index2 = model->index(sourceRow, 2, sourceParent); // address
        auto addr = model->data(index2).toString();
        auto time = model->data(index0).toInt();
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
