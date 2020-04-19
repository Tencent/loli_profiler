#include "stacktracemodel.h"

QString sizeToString(quint64 size) {
    if (size >= 1024 * 1024 * 1024) {
        return QString::number(static_cast<double>(size) / 1024 / 1024 / 1024, 'f', 2) + " GB";
    } else if (size >= 1024 * 1024) {
        return QString::number(static_cast<double>(size) / 1024 / 1024, 'f', 2) + " MB";
    } else if (size > 1024) {
        return QString::number(static_cast<double>(size) / 1024, 'f', 2) + " KB";
    } else {
        return QString::number(size) + " Bytes";
    }
}

QString timeToString(int ms) {
    int seconds = ms / 1000;
    int minutes = seconds / 60;
    int hours = minutes / 60;
    if (seconds < 60) {
        return QString::number(seconds);
    } else if (seconds < 3600) {
        return QString("%1:%2").arg(int(minutes % 60)).arg(int(seconds % 60));
    } else {
        return QString("%1:%2:%3").arg(int(hours)).arg(int(minutes % 60)).arg(int(seconds % 60));
    }
}

StackTraceModel::StackTraceModel(QObject* parent)
    : QAbstractTableModel(parent) {

}

int StackTraceModel::rowCount(const QModelIndex &) const {
    return records_.size();
}

int StackTraceModel::columnCount(const QModelIndex &) const {
    return 5;
}

QVariant StackTraceModel::data(const QModelIndex &index, int role) const {
    int row = index.row();
    int column = index.column();
    if (row >= 0 && row < records_.size()) {
        if (role == Qt::DisplayRole) {
            auto record = records_[row];
            switch(column) {
                case 0:
                    return timeToString(record.time_);
                case 1:
                    return sizeToString(static_cast<quint64>(record.size_));
                case 2:
                    return record.addr_;
                case 3:
                    return record.library_;
                case 4:
                    return record.funcAddr_;
            }
        } else if (role == Qt::UserRole) {
            auto record = records_[row];
            switch(column) {
                case 0:
                    return record.time_;
                case 1:
                    return record.size_;
                case 2:
                    return record.addr_;
                case 3:
                    return record.library_;
                case 4:
                    return record.funcAddr_;
            }
        }
    }
    return QVariant();
}

QVariant StackTraceModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
                case 0:
                    return QString("Time");
                case 1:
                    return QString("Size");
                case 2:
                    return QString("Address");
                case 3:
                    return QString("Library");
                case 4:
                    return QString("Function");
            }
        }
    }
    return QVariant();
}

void StackTraceModel::clear() {
//    auto size = records_.size();
//    if (size == 0)
//        return;
    beginResetModel();
    records_.clear();
    endResetModel();
//    beginRemoveRows({}, 0, size);
//    records_.clear();
//    endRemoveRows();
}

void StackTraceModel::append(const QVector<StackRecord>& records) {
    auto size = records.size();
    if (size == 0)
        return;
    beginInsertRows({}, records_.size(), records_.size() + size - 1);
    records_.append(records);
    endInsertRows();
}
