#ifndef STACKTRACEPROXYMODEL_H
#define STACKTRACEPROXYMODEL_H

#include <QSortFilterProxyModel>

class StackTraceProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    StackTraceProxyModel(QAbstractItemModel* srcModel, QObject *parent = nullptr);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

#endif // STACKTRACEPROXYMODEL_H
