#include "stacktraceproxymodel.h"
#include "stacktracemodel.h"

StackTraceProxyModel::StackTraceProxyModel(QAbstractItemModel* srcModel, QObject *parent)
    : QSortFilterProxyModel(parent) {
    setSourceModel(srcModel);
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
