#ifndef STACKTRACEPROXYMODEL_H
#define STACKTRACEPROXYMODEL_H

#include <QSortFilterProxyModel>

class StackTraceProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    StackTraceProxyModel(QHash<QString, int>& freeAddrMap, QAbstractItemModel* srcModel, QObject *parent = nullptr);

    void setSizeFilter(int value);
    void setPersistentFilter(bool value);
    void setLibraryFilter(const QString& value);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    const QHash<QString, int>& freeAddrMap_;
    int sizeFilter_ = 0;
    bool persistentFilter_ = false;
    QString libraryFilter_ = {};
};

#endif // STACKTRACEPROXYMODEL_H
