#ifndef STACKTRACEMODEL_H
#define STACKTRACEMODEL_H

#include <QAbstractTableModel>
#include <QUuid>

struct StackRecord {
    QUuid uuid_;
    int time_;
    int size_;
    QString addr_;
    QString library_;
    QString funcAddr_;
};

QString sizeToString(quint64 size);
QString timeToString(int time);

class StackTraceModel : public QAbstractTableModel {
    Q_OBJECT
public:
    StackTraceModel(QObject* parent);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void clear();
    void append(const QVector<StackRecord>& records);
    const StackRecord& recordAt(int index) const {
        return records_[index];
    }
private:
    QVector<StackRecord> records_;
};

#endif // STACKTRACEMODEL_H
