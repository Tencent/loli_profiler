#include "smaps/statsmapsdialog.h"
#include "smaps/smapssection.h"
#include "stacktracemodel.h"

#include <QTableWidget>
#include <QTextStream>
#include <QKeySequence>
#include <QTextStream>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QStatusBar>
#include <QVBoxLayout>

class MemoryTableWidgetItem : public QTableWidgetItem {
public:
    MemoryTableWidgetItem(quint32 size) : QTableWidgetItem(), size_(size) { setText(sizeToString(size)); }
    bool operator< (const QTableWidgetItem &other) const;
    quint32 size_ = 0;
};

bool MemoryTableWidgetItem::operator< (const QTableWidgetItem &other) const {
    return size_ < static_cast<const MemoryTableWidgetItem&>(other).size_;
}

class MemoryTableWidget : public QTableWidget {
public:
    MemoryTableWidget(int rows, int columns, QWidget* parent) : QTableWidget(rows, columns, parent) {}
    void keyPressEvent(QKeyEvent *event);
};

void MemoryTableWidget::keyPressEvent(QKeyEvent *event) {
    if (event == QKeySequence::Copy) {
        QString output;
        QTextStream stream(&output);
        stream << "Name, Virtual Memory, Rss, Pss, Private Clean, Private Dirty, Shared Clean, Shared Dirty" << endl;
        auto ranges = selectedRanges();
        for (auto& range : ranges) {
            int top = range.topRow();
            int bottom = range.bottomRow();
            for (int row = top; row <= bottom; row++) {
                stream << item(row, 0)->text() << ", " << item(row, 1)->text() << ", " << item(row, 2)->text() << ", " <<
                          item(row, 3)->text() << ", " << item(row, 4)->text() << ", " << item(row, 5)->text() << ", " <<
                          item(row, 6)->text() << ", " << item(row, 7)->text() << endl;
            }
        }
        stream.flush();
        QApplication::clipboard()->setText(output);
        event->accept();
        return;
    }
    QTableWidget::keyPressEvent(event);
}


StatSmapsDialog::StatSmapsDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{}

StatSmapsDialog::~StatSmapsDialog() {}

void StatSmapsDialog::ShowSmap(const QHash<QString, SMapsSection>& sMapsSections_)
{
    auto layout = new QVBoxLayout(this);
    this->setLayout(layout);
    auto tableWidget = new MemoryTableWidget(sMapsSections_.size(), 8, this);
    tableWidget->setEditTriggers(QTableWidget::EditTrigger::NoEditTriggers);
    tableWidget->setSelectionMode(QTableWidget::SelectionMode::ExtendedSelection);
    tableWidget->setSelectionBehavior(QTableWidget::SelectionBehavior::SelectRows);
    tableWidget->setWordWrap(false);
    tableWidget->setHorizontalHeaderLabels(
                QStringList() << "Name" << "Virtual Memory" << "Rss" << "Pss" <<
                "Private Clean" << "Private Dirty" << "Shared Clean" << "Shared Dirty");
    int row = 0;
    // smaps size data is in kilo-byte by default
    SMapsSection total;
    for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
        auto& name = it.key();
        auto& data = it.value();
        tableWidget->setItem(row, 0, new QTableWidgetItem(name));
        tableWidget->setItem(row, 1, new MemoryTableWidgetItem(data.virtual_ * 1024));
        tableWidget->setItem(row, 2, new MemoryTableWidgetItem(data.rss_ * 1024));
        tableWidget->setItem(row, 3, new MemoryTableWidgetItem(data.pss_ * 1024));
        tableWidget->setItem(row, 4, new MemoryTableWidgetItem(data.privateClean_ * 1024));
        tableWidget->setItem(row, 5, new MemoryTableWidgetItem(data.privateDirty_ * 1024));
        tableWidget->setItem(row, 6, new MemoryTableWidgetItem(data.sharedClean_ * 1024));
        tableWidget->setItem(row, 7, new MemoryTableWidgetItem(data.sharedDirty_ * 1024));
        total.virtual_ += data.virtual_;
        total.rss_ += data.rss_;
        total.pss_ += data.pss_;
        total.privateClean_ += data.privateClean_;
        total.privateDirty_ += data.privateDirty_;
        total.sharedClean_ += data.sharedClean_;
        total.sharedDirty_ += data.sharedDirty_;
        row++;
    }
    tableWidget->setSortingEnabled(true);
    tableWidget->setTextElideMode(Qt::TextElideMode::ElideLeft);
    tableWidget->sortByColumn(3, Qt::SortOrder::DescendingOrder);
    tableWidget->show();
    auto statusBar = new QStatusBar(this);
    statusBar->showMessage(QString("VM: %1, Rss: %2, Pss: %3, PC: %4, PD: %5, SC: %6, SD: %7")
                             .arg(sizeToString(total.virtual_ * 1024), sizeToString(total.rss_ * 1024), sizeToString(total.pss_ * 1024), sizeToString(total.privateClean_ * 1024),
                                  sizeToString(total.privateDirty_ * 1024), sizeToString(total.sharedClean_ * 1024), sizeToString(total.sharedDirty_ * 1024)));
    layout->addWidget(tableWidget);
    layout->addWidget(statusBar);
    layout->setMargin(0);
    this->setWindowTitle("Stat proc/pid/smaps");
    this->resize(900, 400);
    this->setMinimumSize(900, 400);
    this->exec();
}
