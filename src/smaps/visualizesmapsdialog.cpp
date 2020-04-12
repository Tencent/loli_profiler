#include "smaps/visualizesmapsdialog.h"
#include "stacktracemodel.h"
#include "memgraphicsview.h"

#include <QStatusBar>
#include <QComboBox>
#include <QVBoxLayout>
#include <QSet>
#include <QGLWidget>

VisualizeSmapsDialog::VisualizeSmapsDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint) {}

VisualizeSmapsDialog::~VisualizeSmapsDialog() {}

void VisualizeSmapsDialog::VisualizeSmap(const QHash<QString, SMapsSection>& sMapsSections_, QAbstractItemModel *curModel)
{
   // QDialog fragDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    this->setLayout(layout);
    auto statusBar = new QStatusBar(this);
    auto fragView = new MemGraphicsView(this);
    auto fragScene = new QGraphicsScene(this);
    auto sectionComboBox = new QComboBox(this);
    //auto curModel = ui->stackTableView->model();
    connect(sectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index){
        fragScene->clear();
        auto sectionit = sMapsSections_.find(sectionComboBox->itemText(index));
        if (sectionit == sMapsSections_.end()) {
            return;
        }
        auto& section = *sectionit;
        auto sectionsCount = section.addrs_.size();
        auto recordsCount = 0;
        auto totalUsedSize = 0ul;
        auto totalSize = 0ul;
        auto curY = 0.0;
        for (int i = 0; i < sectionsCount; i++) {
            auto& addr = section.addrs_[i];
            auto start = addr.start_ - addr.offset_;
            auto end = addr.end_ - addr.offset_;
            auto size = end - start;
            auto sectionItem = new MemSectionItem(1024, static_cast<double>(size) / 1024);
            sectionItem->setY(curY);
            auto rowCount = curModel->rowCount();
            auto usedSize = 0ul;
            for (int i = 0; i < rowCount; i++) {
                auto recSize = curModel->data(curModel->index(i, 1), Qt::UserRole).toUInt();
                auto recAddr = curModel->data(curModel->index(i, 2)).toString().toULongLong(nullptr, 0);
                if (recAddr >= start && recAddr < end && recAddr + recSize <= end) {
                    sectionItem->addAllocation((recAddr - start) / 1024, recSize / 1024);
                    recordsCount++;
                    usedSize += recSize;
                }
            }
            fragScene->addItem(sectionItem);
            curY += sectionItem->boundingRect().height() + 16;
            totalUsedSize += usedSize;
            totalSize += size;
        }
        statusBar->showMessage(QString("%1 sections with %2 allocation records, total: %3 used: %4 (%5%)")
                               .arg(QString::number(sectionsCount), QString::number(recordsCount), sizeToString(totalSize), sizeToString(totalUsedSize),
                                    QString::number((static_cast<double>(totalUsedSize) / totalSize) * 100.0)));
    });
    QSet<QString> visibleSections;
    auto recordCount = curModel->rowCount();
    for (int i = 0; i < recordCount; i++) {
        auto recAddr = curModel->data(curModel->index(i, 2)).toString().toULongLong(nullptr, 0);
        for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
            auto& section = *it;
            if (visibleSections.contains(it.key()))
                continue;
            for (int i = 0; i < section.addrs_.size(); i++) {
                auto& addr = section.addrs_[i];
                auto start = addr.start_ - addr.offset_;
                auto end = addr.end_ - addr.offset_;
                if (recAddr >= start && recAddr < end) {
                    visibleSections.insert(it.key());
                    break;
                }
            }
        }
        if (visibleSections.count() == sMapsSections_.count())
            break;
    }
    auto sectionNames = visibleSections.values();
    sectionNames.sort(Qt::CaseSensitivity::CaseInsensitive);
    sectionComboBox->addItems(sectionNames);
    fragView->setScene(fragScene);
    fragView->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
    fragView->setInteractive(false);
    fragView->show();
    layout->addWidget(sectionComboBox);
    layout->addWidget(fragView);
    layout->addWidget(statusBar);
    layout->setMargin(0);
    fragView->setFocus();
    this->setWindowTitle("Visualize proc/pid/smaps");
    this->resize(900, 400);
    this->setMinimumSize(900, 400);
    this->exec();
}
