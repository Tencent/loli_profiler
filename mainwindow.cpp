#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "timeprofiler.h"
#include "customgraphicsview.h"

#include <QClipboard>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QTextStream>
#include <QProgressDialog>
#include <QScrollBar>
#include <QGLWidget>

#include <algorithm>
#include <cmath>
#include <vector>

#define APP_MAGIC 0xA4B3C2D1
#define APP_VERSION 102

enum class IOErrorCode : qint32 {
    NONE = 0,
    MAGIC_NUMBER_MISSMATCH,
    VERSION_MISSMATCH,
    CORRUPTED_DATA,
};

QString SizeToString(int size) {
    if (size >= 1024 * 1024) {
        return QString::number(static_cast<double>(size) / 1024 / 1024, 'f', 2) + " MB";
    } else if (size > 1024) {
        return QString::number(static_cast<double>(size) / 1024, 'f', 2) + " KB";
    } else {
        return QString::number(size) + " Bytes";
    }
}

QString TimeToString(int ms) {
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

class SortableTreeWidgetItem : public QTreeWidgetItem {
public:
    SortableTreeWidgetItem(QUuid uuid, int time, int size, QTreeWidget* parent) : QTreeWidgetItem(parent), uuid_(uuid) {
        SetTime(time);
        SetSize(size);
    }
    SortableTreeWidgetItem(const StackRecord& record, QTreeWidget* parent) : QTreeWidgetItem(parent), uuid_(record.uuid_) {
        SetTime(record.time_);
        SetSize(record.size_);
        setText(2, record.addr_);
        setText(3, record.library_);
        setText(4, record.funcAddr_);
    }
    QUuid Uuid() const { return uuid_; }
    int Time() const { return time_; }
    void SetTime(int time) {
        time_ = time;
        setText(0, TimeToString(time));
    }
    int Size() const { return size_; }
    void SetSize(int size) {
        size_ = size;
        setText(1, SizeToString(size));
    }
private:
    bool operator<(const QTreeWidgetItem &other) const {
        auto casted = static_cast<const SortableTreeWidgetItem&>(other);
        int column = treeWidget()->sortColumn();
        switch (column) {
            case 0:
                return time_ < casted.Time();
            case 1:
                return size_ < casted.Size();
            default:
                return text(column) < other.text(column);
        }
    }
    QUuid uuid_;
    int time_;
    int size_;
};

using namespace QtCharts;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow) {
    ui->setupUi(this);
    ui->stackTreeWidget->setUniformRowHeights(true);

    progressDialog_ = new QProgressDialog(this, Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setAutoClose(true);
    progressDialog_->setCancelButton(nullptr);
    progressDialog_->close();

    // setup adb process
    startAppProcess_ = new StartAppProcess(this);
    connect(startAppProcess_, &StartAppProcess::ProcessFinished, this, &MainWindow::StartAppProcessFinished);
    connect(startAppProcess_, &StartAppProcess::ProcessErrorOccurred, this, &MainWindow::StartAppProcessErrorOccurred);

    screenshotProcess_ = new ScreenshotProcess(this);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessFinished, this, &MainWindow::ScreenshotProcessFinished);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessErrorOccurred, this, &MainWindow::ScreenshotProcessErrorOccurred);

    memInfoProcess_ = new MemInfoProcess(this);
    connect(memInfoProcess_, &MemInfoProcess::ProcessFinished, this, &MainWindow::MemInfoProcessFinished);
    connect(memInfoProcess_, &MemInfoProcess::ProcessErrorOccurred, this, &MainWindow::MemInfoProcessErrorOccurred);

    stacktraceProcess_ = new StackTraceProcess(this);
    connect(stacktraceProcess_, &StackTraceProcess::DataReceived, this, &MainWindow::StacktraceDataReceived);
    connect(stacktraceProcess_, &StackTraceProcess::ConnectionLost, this, &MainWindow::StacktraceConnectionLost);

    // setup screenshot view
    ui->screenshotGraphicsView->setScene(new QGraphicsScene());
    for (int i = 0; i < 5; i++) {
        auto item = new QGraphicsPixmapItem();
        screenshotItems_.push_back(item);
        ui->screenshotGraphicsView->scene()->addItem(item);
    }

    // setup meminfo chart
    memInfoChart_ = new QChart();
    memInfoChart_->setTitle("meminfo");
    memInfoChart_->legend()->show();
    memInfoChart_->legend()->setAlignment(Qt::AlignBottom);

    memInfoAxisX_ = new QValueAxis();
    memInfoAxisX_->setLabelFormat("%i");
    memInfoAxisX_->setTickCount(11);
    memInfoAxisX_->setRange(0, 100);
    memInfoChart_->addAxis(memInfoAxisX_, Qt::AlignBottom);

    memInfoAxisY_ = new QValueAxis();
    memInfoAxisY_->setLabelFormat("%i");
    memInfoChart_->addAxis(memInfoAxisY_, Qt::AlignLeft);
    UpdateMemInfoRange();

    QVector<QString> memInfoTitles = {"Total", "NativeHeap", "GfxDev", "EGLmtrack", "GLmtrack", "Unknown"};
    for (int i = 0; i < memInfoTitles.size(); i++) {
        auto series = new QLineSeries();
        series->setName(memInfoTitles[i]);
        memInfoSeries_.push_back(series);
        memInfoChart_->addSeries(series);
        series->attachAxis(memInfoAxisX_);
        series->attachAxis(memInfoAxisY_);
    }

    memInfoChartView_ = new InteractiveChartView(memInfoChart_);
    memInfoChartView_->setRenderHint(QPainter::Antialiasing);
    memInfoChartView_->setContentsMargins(0, 0, 0, 0);
    memInfoChartView_->setFixedHeight(250);

    connect(memInfoChartView_, &InteractiveChartView::OnSyncScroll, this, &MainWindow::OnSyncScroll);
    connect(memInfoChartView_, &InteractiveChartView::OnSelectionChange, this, &MainWindow::OnTimeSelectionChange);

    // steup chart scroll area
    scrollArea_ = new FixedScrollArea();
    scrollArea_->setContentsMargins(0, 0, 0, 0);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);
    scrollArea_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scrollArea_->setWidget(new QWidget());
    scrollArea_->widget()->setLayout(new QVBoxLayout());
    scrollArea_->widget()->setContentsMargins(0, 0, 0, 0);
    scrollArea_->widget()->layout()->setSpacing(0);
    scrollArea_->widget()->layout()->addWidget(memInfoChartView_);
    scrollArea_->widget()->setFixedSize(100, 250);
    connect(scrollArea_, &FixedScrollArea::ScaleTriggered, [this](int delta) {
        ui->chartScaleHSlider->setValue(
                    ui->chartScaleHSlider->value() + (delta > 0 ? 1 : -1) * ui->chartScaleHSlider->singleStep());
    });

    ui->chartWidget->layout()->setContentsMargins(0, 0, 0, 0);
    ui->chartWidget->layout()->setSpacing(0);
    ui->chartWidget->layout()->addWidget(scrollArea_);

    callStackModel_ = new QStandardItemModel();
    callStackModel_->setHorizontalHeaderLabels(QStringList() << "Library" << "Function");
    ui->callStackTableView->setModel(callStackModel_);
    ui->callStackTableView->horizontalHeader()->setStretchLastSection(true);
    ui->callStackTableView->verticalHeader()->setVisible(false);
    ui->callStackTableView->show();

    LoadSettings();

    connect(ui->stackTreeWidget, &QTreeWidget::customContextMenuRequested, this, &MainWindow::OnStackTreeWidgetContextMenu);

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
    mainTimer_->start(1000);
}

MainWindow::~MainWindow() {
    delete ui;
}

const QString SETTINGS_WINDOW_W = "WindowWidth";
const QString SETTINGS_WINDOW_H = "WindowHeight";
const QString SETTINGS_APPNAME = "AppName";
const QString SETTINGS_SDKPATH = "SdkPath";
const QString SETTINGS_SPLITER = "Spliter";
const QString SETTINGS_SPLITER_2 = "Spliter_2";
const QString SETTINGS_SPLITER_3 = "Spliter_3";
const QString SETTINGS_SCALEHSLIDER = "ChartScaleHSlider";
const QString SETTINGS_LASTOPENDIR = "lastopen_dir";
const QString SETTINGS_LASTSYMBOLDIR = "lastsymbol_dir";
const QString SETTINGS_ADDR2LINEPATH = "addr2line_path";
const QString SETTINGS_PYTHONPATH = "python_path";

void MainWindow::LoadSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    int windowWidth = settings.value(SETTINGS_WINDOW_W).toInt();
    int windowHeight = settings.value(SETTINGS_WINDOW_H).toInt();
    if (windowWidth > 0 && windowHeight > 0) {
        this->resize(windowWidth, windowHeight);
    }
    ui->appNameLineEdit->setText(settings.value(SETTINGS_APPNAME).toString());
    ui->sdkPathLineEdit->setText(settings.value(SETTINGS_SDKPATH).toString());
    ui->addr2LinePathLineEdit->setText(settings.value(SETTINGS_ADDR2LINEPATH).toString());
    ui->pythonPathLineEdit->setText(settings.value(SETTINGS_PYTHONPATH).toString());
    ui->splitter->restoreState(settings.value(SETTINGS_SPLITER).toByteArray());
    ui->splitter_2->restoreState(settings.value(SETTINGS_SPLITER_2).toByteArray());
    ui->splitter_3->restoreState(settings.value(SETTINGS_SPLITER_3).toByteArray());
    ui->chartScaleHSlider->setValue(settings.value(SETTINGS_SCALEHSLIDER, 10).toInt());
    auto lastOpenDir = settings.value(SETTINGS_LASTOPENDIR).toString();
    if (QDir(lastOpenDir).exists())
        lastOpenDir_ = lastOpenDir;
    auto lastSymbolDir = settings.value(SETTINGS_LASTSYMBOLDIR).toString();
    if (QDir(lastSymbolDir).exists())
        lastSymbolDir_ = lastSymbolDir;
}

void MainWindow::SaveSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    settings.setValue(SETTINGS_WINDOW_W, this->width());
    settings.setValue(SETTINGS_WINDOW_H, this->height());
    settings.setValue(SETTINGS_APPNAME, ui->appNameLineEdit->text());
    settings.setValue(SETTINGS_SDKPATH, ui->sdkPathLineEdit->text());
    settings.setValue(SETTINGS_ADDR2LINEPATH, ui->addr2LinePathLineEdit->text());
    settings.setValue(SETTINGS_PYTHONPATH, ui->pythonPathLineEdit->text());
    settings.setValue(SETTINGS_SPLITER, ui->splitter->saveState());
    settings.setValue(SETTINGS_SPLITER_2, ui->splitter_2->saveState());
    settings.setValue(SETTINGS_SPLITER_3, ui->splitter_3->saveState());
    settings.setValue(SETTINGS_SCALEHSLIDER, ui->chartScaleHSlider->value());
    if (QDir(lastOpenDir_).exists())
        settings.setValue(SETTINGS_LASTOPENDIR, lastOpenDir_);
    if (QDir(lastSymbolDir_).exists())
        settings.setValue(SETTINGS_LASTSYMBOLDIR, lastSymbolDir_);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    SaveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    HideToolTips();
    QMainWindow::resizeEvent(event);
}

void MainWindow::Print(const QString& str) {
    ui->consolePlainTextEdit->appendPlainText(str);
}

void MainWindow::SaveToFile(QFile *file) {
    QDataStream stream(file);
    stream << static_cast<quint32>(APP_MAGIC);
    stream << static_cast<qint32>(APP_VERSION);
    stream.setVersion(QDataStream::Qt_5_12);
    // meminfo charts
    stream << static_cast<qint32>(maxMemInfoValue_);
    stream << static_cast<qint32>(memInfoSeries_.size());
    for (auto series : memInfoSeries_) {
        auto count = static_cast<qint32>(series->count());
        stream << count;
        for (int i = 0; i < count; i++) {
            auto point = series->at(i);
            stream << point;
        }
    }
    // callstack tree view
    qint32 count = static_cast<qint32>(ui->stackTreeWidget->topLevelItemCount());
    stream << count;
    for (int i = 0; i < count; i++) {
        auto topItem = static_cast<SortableTreeWidgetItem*>(ui->stackTreeWidget->topLevelItem(i));
        stream << topItem->Uuid().toString();
        stream << static_cast<qint32>(topItem->Time());
        stream << static_cast<qint32>(topItem->Size());
        stream << topItem->text(2);
        stream << topItem->text(3);
        stream << topItem->text(4);
    }
    // callstack map
    stream << static_cast<qint32>(callStackMap_.size());
    for (auto it = callStackMap_.begin(); it != callStackMap_.end(); ++it) {
        stream << it.key().toString();
        stream << it.value();
    }
    // symbol map
    stream << static_cast<qint32>(symbloMap_.size());
    for (auto it = symbloMap_.begin(); it != symbloMap_.end(); ++it) {
        stream << it.key();
        stream << static_cast<qint32>(it.value().size());
        for (auto it1 = it.value().begin(); it1 != it.value().end(); ++it1) {
            stream << it1.key();
            stream << it1.value();
        }
    }
    // freeaddr map
    stream << static_cast<qint32>(freeAddrMap_.size());
    for (auto it = freeAddrMap_.begin(); it != freeAddrMap_.end(); ++it) {
        stream << it.key();
        stream << static_cast<qint32>(it.value());
    }
    // screen shots
    stream << static_cast<qint32>(screenshots_.size());
    for (auto& pair : screenshots_) {
        stream << static_cast<qint32>(pair.first);
        stream << pair.second;
    }
}

int MainWindow::LoadFromFile(QFile *file) {
    QDataStream stream(file);
    quint32 magic;
    stream >> magic;
    if (magic != APP_MAGIC)
        return static_cast<qint32>(IOErrorCode::MAGIC_NUMBER_MISSMATCH);
    qint32 version;
    stream >> version;
    if (version != APP_VERSION)
        return static_cast<qint32>(IOErrorCode::VERSION_MISSMATCH);
    callStackModel_->clear();
    // meminfo charts
    qint32 value;
    stream >> value;
    maxMemInfoValue_ = value;
    UpdateMemInfoRange();
    qint32 seriesCount;
    stream >> seriesCount;
    for (auto series : memInfoSeries_)
        series->clear();
    for (int i = 0 ;i < seriesCount; i++) {
        int pointsCount;
        stream >> pointsCount;
        for (int j = 0; j < pointsCount; j++) {
            QPointF point;
            stream >> point;
            memInfoSeries_[i]->append(point);
        }
    }
    // callstack tree view
    QSet<QString> libraries;
    ui->stackTreeWidget->clear();
    stream >> value;
    QList<QTreeWidgetItem*> treeItems;
    for (int i = 0; i < value; i++) {
        QString str;
        stream >> str;
        auto topItem = new SortableTreeWidgetItem(QUuid::fromString(str), 0, 0, ui->stackTreeWidget);
        qint32 size;
        stream >> size;
        topItem->SetTime(size);
        stream >> size;
        topItem->SetSize(size);
        stream >> str;
        topItem->setText(2, str);
        stream >> str;
        topItem->setText(3, str);
        if (!libraries.contains(str))
            libraries.insert(str);
        stream >> str;
        topItem->setText(4, str);
        treeItems.push_back(topItem);
    }
    ui->stackTreeWidget->addTopLevelItems(treeItems);
    ui->libraryComboBox->setCurrentIndex(0);
    for (int i = 1; i < ui->libraryComboBox->count(); i++)
        ui->libraryComboBox->removeItem(i);
    for (auto& library : libraries)
        ui->libraryComboBox->addItem(library);
    // callstack map
    callStackMap_.clear();
    stream >> value;
    QVector<QString> strs;
    for (int i = 0; i < value; i++) {
        QString str;
        strs.clear();
        stream >> str;
        stream >> strs;
        callStackMap_.insert(QUuid::fromString(str), strs);
    }
    // symbol map
    symbloMap_.clear();
    stream >> value;
    for (int i = 0; i < value; i++) {
        QString str;
        stream >> str;
        qint32 size;
        stream >> size;
        auto& map = symbloMap_[str];
        QString key, value;
        for (int j = 0; j < size; j++) {
            stream >> key;
            stream >> value;
            map[key] = value;
        }
    }
    // freeaddr map
    freeAddrMap_.clear();
    stream >> value;
    for (int i = 0; i < value; i++) {
        QString str;
        stream >> str;
        qint32 time;
        stream >> time;
        freeAddrMap_.insert(str, time);
    }
    FilterTreeWidget();
    // screen shots
    screenshots_.clear();
    stream >> value;
    screenshots_.reserve(value);
    for (int i = 0; i < value; i++) {
        qint32 time;
        stream >> time;
        QByteArray ba;
        stream >> ba;
        screenshots_.push_back(qMakePair(time, ba));
    }
    stackRecords_.clear();
    return static_cast<qint32>(IOErrorCode::NONE);
}

QString MainWindow::GetLastOpenDir() const {
    return QDir(lastOpenDir_).exists() ? lastOpenDir_ : QDir::homePath();
}

QString MainWindow::GetLastSymbolDir() const {
    return QDir(lastSymbolDir_).exists() ? lastSymbolDir_ : QDir::homePath();
}

void MainWindow::ConnectionFailed() {
    isConnected_ = false;
    isCapturing_ = false;
    if (screenshotProcess_->IsRunning())
        screenshotProcess_->Process()->kill();
    stacktraceProcess_->Disconnect();
    ui->appNameLineEdit->setEnabled(true);
    ui->launchPushButton->setText("Launch");
    ui->sdkPushButton->setEnabled(true);
    ui->actionOpen->setEnabled(true);
    ui->stackTreeWidget->setSortingEnabled(true);
}

int MainWindow::GetScreenshotIndex(const QPointF& pos) const { // TODO: optimize with binary search
    auto screenshotCount = screenshots_.size();
    if (screenshotCount == 0)
        return -1;
    else if (screenshotCount == 1)
        return 0;
    auto time = pos.x();
    if (time <= screenshots_[0].first)
        return 0;
    if (time >= screenshots_[screenshotCount - 1].first)
        return screenshotCount - 1;
    for (int i = 0, j = 1; j < screenshotCount; i++, j++) {
        int curTime = screenshots_[i].first;
        int nextTime = screenshots_[j].first;
        if (time >= curTime && time <= nextTime) {
            return i;
        }
    }
    return -1;
}

void MainWindow::ShowScreenshotAt(int index) {
    int screenshotCount = screenshots_.size();
    int count = screenshotItems_.size();
    int startIndex = index - static_cast<int>(std::floor(static_cast<float>(count) / 2));
    for (int i = 0; i < count; i++, startIndex++) {
        auto& item = screenshotItems_[i];
        if (startIndex < 0 || startIndex >= screenshotCount) {
            item->setVisible(false);
            continue;
        }
        QPixmap pixmap;
        pixmap.loadFromData(screenshots_[startIndex].second, "JPG");
        item->setPixmap(pixmap);
        item->setVisible(true);
        item->setScale(startIndex == index ? 1.2 : 1.0);
        item->setZValue(startIndex == index ? count : i);
    }
    for (int i = 0; i < count; i++) {
        auto& item = screenshotItems_[i];
        item->setPos(i * 256.0, 0.0);
    }
}

void MainWindow::HideToolTips() {
    memInfoChartView_->HideToolTip();
}

void MainWindow::UpdateMemInfoRange() {
    memInfoAxisY_->setRange(0, maxMemInfoValue_);
}

bool MainWindow::GetTreeWidgetItemShouldHide(QTreeWidgetItem* item) const {
    auto casted = static_cast<SortableTreeWidgetItem*>(item);
    auto library = casted->data(3, 0).toString();
    auto addr = casted->data(2, 0).toString();
    auto size = casted->Size();
    auto hide = false;
    switch(ui->memSizeComboBox->currentIndex()) {
    case 0: // all allocations
        hide = false;
        break;
    case 1: // large
        hide = size < 1048576;
        break;
    case 2: // medium
        hide = size >= 1048576 || size <= 1024;
        break;
    case 3: // small
        hide = size > 1024;
        break;
    }
    if (!hide) {
        if (ui->libraryComboBox->currentIndex() != 0) {
            hide = ui->libraryComboBox->currentText() != library;
        }
    }
    bool persistent = ui->allocComboBox->currentIndex() == 1;
    if (!hide && persistent) {
        auto time = casted->Time();
        auto it = freeAddrMap_.find(addr);
        if (it != freeAddrMap_.end()) {
            if (time < it.value()) {
                hide = true;
            }
        }
    }
    return hide;
}

void MainWindow::FilterTreeWidget() {
    int visibleCount = 0;
    int sizeInBytes = 0;
    for (int i = 0; i < ui->stackTreeWidget->topLevelItemCount(); i++) {
        auto item = ui->stackTreeWidget->topLevelItem(i);
        auto hide = GetTreeWidgetItemShouldHide(item);
        if (item->isHidden() != hide)
            item->setHidden(hide);
        if (!hide) {
            visibleCount++;
            sizeInBytes += static_cast<SortableTreeWidgetItem*>(item)->Size();
        }
    }
    filterDirty_ = false;
    ui->recordCountLineEdit->setText(QString::number(visibleCount) + "/" + SizeToString(sizeInBytes));
}

QString MainWindow::TryAddNewAddress(const QString& lib, const QString& addr) {
    if (!addr.startsWith("0x"))
        return addr;
    if (!symbloMap_.contains(lib))
        symbloMap_.insert(lib, {});
    auto& symblos = symbloMap_[lib];
    if (!symblos.contains(addr)) {
        symblos.insert(addr, "");
    } else {
        auto realName = symblos[addr];
        if (realName.size() > 0)
            return realName;
    }
    return addr;
}

void MainWindow::FixedUpdate() {
    if (filterDirty_)
        FilterTreeWidget();
    if (!isConnected_)
        return;
    if (time_ - lastScreenshotTime_ >= 5 && !screenshotProcess_->IsRunning()) {
        lastScreenshotTime_ = time_;
        screenshotProcess_->CaptureScreenshot();
    }
    if (!memInfoProcess_->IsRunning() && !memInfoProcess_->HasErrors()) {
        memInfoProcess_->DumpMemInfoAsync(ui->appNameLineEdit->text());
    }
    if (!stacktraceProcess_->IsConnecting() && !stacktraceProcess_->IsConnected()) {
        static int port = 8000;
        stacktraceProcess_->ConnectToServer(port++);
        Print("Connecting to application server ... ");
    }
    time_++;
}

void MainWindow::OnTimeSelectionChange(const QPointF& pos) {
    int index = GetScreenshotIndex(pos);
    if (index == -1)
        return;
    ShowScreenshotAt(index);
}

void MainWindow::OnSyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta) {
    memInfoChartView_->SyncScroll(sender, prevMouseX, delta);
}

void MainWindow::OnStackTreeWidgetContextMenu(const QPoint & pos) {
    QMenu menu(this);
    auto actionCollapse = new QAction("Expand");
    menu.addAction(actionCollapse);
    connect(actionCollapse, &QAction::triggered, [this]() {
        const auto& selections = ui->stackTreeWidget->selectedItems();
        for (auto selection : selections) {
            if (!selection->isExpanded()) {
                auto child = selection;
                while (child) {
                    child->setExpanded(true);
                    child = child->childCount() > 0 ? child->child(0) : nullptr;
                }
            }
        }
    });
    auto actionCopy = new QAction("Copy to Clipboard");
    menu.addAction(actionCopy);
    connect(actionCopy, &QAction::triggered, this, [this]() {
        const auto& selections = ui->stackTreeWidget->selectedItems();
        QString output;
        QTextStream stream(&output);
        for (auto selection : selections) {
            auto child = selection;
            while (child) {
                stream << child->text(3) << ", " << child->text(4) << endl;
                child = child->childCount() > 0 ? child->child(0) : nullptr;
            }
        }
        stream.flush();
        QApplication::clipboard()->setText(output);
    });
    menu.exec(ui->stackTreeWidget->viewport()->mapToGlobal(pos));
}

void MainWindow::StartAppProcessFinished(AdbProcess* process) {
    progressDialog_->setValue(progressDialog_->maximum());
    progressDialog_->close();
    auto startAppProcess = static_cast<StartAppProcess*>(process);
    if (!startAppProcess->Result()) {
        ConnectionFailed();
        Print("Error starting app by adb monkey");
        return;
    }
    isConnected_ = true;
    screenshotProcess_->SetExecutablePath(adbPath_);
    stacktraceProcess_->SetExecutablePath(adbPath_);
    memInfoProcess_->SetExecutablePath(adbPath_);
    lastScreenshotTime_ = time_ = 0;
    Print("Application Started!");
    stacktraceRetryCount_ = 30;
    memInfoProcess_->DumpMemInfoAsync(ui->appNameLineEdit->text());
}

void MainWindow::StartAppProcessErrorOccurred() {
    ConnectionFailed();
    progressDialog_->setValue(progressDialog_->maximum());
    progressDialog_->close();
    Print("Error starting app: " + startAppProcess_->ErrorStr());
}

void MainWindow::MemInfoProcessFinished(AdbProcess* process) {
    auto memInfoProcess = static_cast<MemInfoProcess*>(process);
    auto curMemInfo = memInfoProcess->GetMemInfo();
    maxMemInfoValue_ = std::max(maxMemInfoValue_, std::max(256, static_cast<int>(curMemInfo.Total * 1.2f)));
    UpdateMemInfoRange();
    memInfoSeries_[0]->append(time_, curMemInfo.Total);
    memInfoSeries_[1]->append(time_, curMemInfo.NativeHeap);
    memInfoSeries_[2]->append(time_, curMemInfo.GfxDev);
    memInfoSeries_[3]->append(time_, curMemInfo.EGLmtrack);
    memInfoSeries_[4]->append(time_, curMemInfo.GLmtrack);
    memInfoSeries_[5]->append(time_, curMemInfo.Unknown);
}

void MainWindow::MemInfoProcessErrorOccurred() {
    Print("Error occurred when dumping meminfo ...");
}

void MainWindow::ScreenshotProcessFinished(AdbProcess* process) {
    auto screenshotProcess = static_cast<ScreenshotProcess*>(process);
    auto image = screenshotProcess->GetScreenshot();
    if (!image.isNull()) {
        screenshots_.push_back(qMakePair(time_, screenshotProcess->GetScreenshotBytes()));
        ShowScreenshotAt(screenshots_.size() - 1);
    }
}

void MainWindow::ScreenshotProcessErrorOccurred() {
    Print("Error occurred when capturing screenshot ...");
}

void MainWindow::StacktraceDataReceived() {
    stacktraceRetryCount_ = 5;
    const auto& stacks = stacktraceProcess_->GetStackInfo();
    if (stacks.size() > 0) {
        for (const auto& stack : stacks) {
            if (stack.size() < 3)
                continue;
            auto root = stack[0].split(',');
            if (root.size() < 3)
                continue;
            const auto& rootTime = root[0];
            const auto& rootSize = root[1];
            const auto& rootMemAddr = root[2];
            const auto& rootLibrary = stack[1];
            const auto& rootFuncAddr = TryAddNewAddress(rootLibrary, stack[2]);
            StackRecord record;
            record.uuid_ = QUuid::createUuid();
            record.time_ = rootTime.toInt();
            record.size_ = rootSize.toInt();
            record.addr_ = rootMemAddr;
            // begin record full callstack, and find which lib starts this callstack
            auto& callStack = callStackMap_[record.uuid_];
            for (int i = 3; i < stack.size() && i + 1 < stack.size(); i += 2) {
                const auto& libName = stack[i];
                const auto& funcAddr = stack[i + 1];
                TryAddNewAddress(libName, funcAddr);
                callStack.append(libName);
                callStack.append(funcAddr);
            }
            auto callStackLib = callStack.size() > 0 ? callStack[0] : rootLibrary;
            // end record callstack
            if (!libraries_.contains(callStackLib))
                libraries_.insert(callStackLib);
            record.library_ = callStackLib;
            record.funcAddr_ = rootFuncAddr;
            stackRecords_.push_back(record);
        }
    }
    // read free call infos
    const auto& frees = stacktraceProcess_->GetFreeInfo();
    if (frees.size() > 0) {
        for (const auto& free : frees) {
            const auto address = free.second;
            auto curTime = freeAddrMap_[address];
            if (free.first > curTime) {
                freeAddrMap_[address] = free.first;
            }
        }
    }
}

void MainWindow::StacktraceConnectionLost() {
    if (!isCapturing_)
        return;
    Print(QString("Connection failed, retrying %1").arg(stacktraceRetryCount_));
    stacktraceRetryCount_--;
    if (stacktraceRetryCount_ <= 0)
        ConnectionFailed();
}

void MainWindow::AddressProcessFinished(AdbProcess* process) {
    progressDialog_->setValue(progressDialog_->value() + 1);
    auto addrProcess = static_cast<AddressProcess*>(process);
    if (addrProcess->GetConvertedCount() == 0)
        return;
    QTreeWidgetItemIterator it(ui->stackTreeWidget);
    while (*it) {
        auto item = *it;
        const auto& libName = item->text(3);
        const auto& funcAddr = item->text(4);
        if (funcAddr.startsWith("0x")) {
            auto mapIt = symbloMap_.find(libName);
            if (mapIt != symbloMap_.end()) {
                auto addrIt = mapIt->find(funcAddr);
                if (addrIt != mapIt->end()) {
                    auto name = addrIt.value();
                    if (name.size() > 0)
                        item->setText(4, name);
                }
            }
        }
        ++it;
    }
    on_stackTreeWidget_itemSelectionChanged();
}

void MainWindow::AddressProcessErrorOccurred() {
    progressDialog_->close();
    Print("Error occurred when reading address info ...");
}

void MainWindow::on_actionOpen_triggered() {
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("Open Profiler File"),
                                                    GetLastOpenDir(), tr("Loli Profiler Files (*.loli)"));
    if (QFileInfo::exists(fileName)) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            lastOpenDir_ = QFileInfo(fileName).dir().absolutePath();
            auto ecode = LoadFromFile(&file);
            if (ecode != static_cast<int>(IOErrorCode::NONE)) {
                QMessageBox::warning(this, "Warning", QString("Error reading file, ecode %1").arg(static_cast<int>(ecode)),
                                     QMessageBox::StandardButton::Ok);
            }
        }
        else {
            QMessageBox::warning(this, "Warning", "File not found!", QMessageBox::StandardButton::Ok);
        }
    }
}

void MainWindow::on_actionSave_triggered() {
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("Save Profiler File"),
                                                    GetLastOpenDir(), tr("Loli Profiler Files (*.loli)"));
    if (fileName.isEmpty())
        return;
    if (!fileName.endsWith("loli", Qt::CaseInsensitive))
        fileName += ".loli";
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::warning(this, "Warning", "Can't create file!", QMessageBox::StandardButton::Ok);
        return;
    }
    SaveToFile(&tempFile);
    if (QFileInfo::exists(fileName) && !QFile(fileName).remove()) {
        QMessageBox::warning(this, "Warning", "Error removing file!", QMessageBox::StandardButton::Ok);
        return;
    }
    if (!tempFile.rename(fileName)) {
        QMessageBox::warning(this, "Warning", "Error renaming file!", QMessageBox::StandardButton::Ok);
        return;
    }
    tempFile.setAutoRemove(false);
}

void MainWindow::on_actionExit_triggered() {
    this->close();
}

void MainWindow::on_actionMemory_Fragmentation_triggered() {
    qint32 count = static_cast<qint32>(ui->stackTreeWidget->topLevelItemCount());
    if (count == 0) {
        QMessageBox::warning(this, "Warning", "Record or open some data first!", QMessageBox::StandardButton::Ok);
        return;
    }
    ui->allocComboBox->setCurrentIndex(1);
    quint64 minAddr = 0xffffffff, maxAddr = 0;
    for (int i = 0; i < count; i++) {
        auto topItem = static_cast<SortableTreeWidgetItem*>(ui->stackTreeWidget->topLevelItem(i));
        auto addr = topItem->text(2).toULong(nullptr, 0);
        if (addr > maxAddr)
            maxAddr = addr;
        if (addr < minAddr)
            minAddr = addr;
    }
    quint32 sizeInMb = static_cast<quint32>(static_cast<double>(maxAddr - minAddr) / 1024 / 1024);
    QDialog fragDialog(this);
    auto layout = new QHBoxLayout();
    fragDialog.setLayout(layout);
    CustomGraphicsView* fragView = new CustomGraphicsView();
    QGraphicsScene* fragScene = new QGraphicsScene();
    QPen pen(QBrush(), 0.0);
    QBrush brush(QColor::fromRgb(200, 50, 50, 255));
    fragScene->addRect(0, 0, 200, sizeInMb, pen, QBrush(QColor::fromRgb(50, 200, 50, 255)));
    for (int i = 0; i < count; i++) {
        auto topItem = static_cast<SortableTreeWidgetItem*>(ui->stackTreeWidget->topLevelItem(i));
        if (topItem->isHidden())
            continue;
        auto addr = topItem->text(2).toULong(nullptr, 0) - minAddr;
        auto size = topItem->Size();
        auto addrInMb = static_cast<double>(addr) / 1024 / 1024;
        auto sizeInMb = static_cast<double>(size) / 1024 / 1024;
        fragScene->addRect(0, addrInMb, 200, sizeInMb, pen, brush);
    }
    fragView->setScene(fragScene);
    fragView->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
    fragView->setInteractive(false);
    fragView->show();
    layout->addWidget(fragView);
    layout->setMargin(0);
    fragDialog.setWindowTitle("Memory Fragmentation Viewer");
    fragDialog.resize(500, 400);
    fragDialog.exec();
}

void MainWindow::on_actionAbout_triggered() {
    QMessageBox::about(this, "About MoreFun Loli Profiler", "Copyright 2019 MoreFun Studio Group, Tencent.");
}

void MainWindow::on_sdkPushButton_clicked() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Directory"), "",
                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    ui->sdkPathLineEdit->setText(path);
}

void MainWindow::on_launchPushButton_clicked() {
    if (isConnected_) {
        Print("Stoping capture ...");
        auto count = stackRecords_.count();
        if (count > 0)
            Print(QString("Captured %1 records.").arg(count));
        ConnectionFailed();
        QList<QTreeWidgetItem*> topLevelItems;
        for (auto& record : stackRecords_) {
            topLevelItems.append(new SortableTreeWidgetItem(record, nullptr));
        }
        ui->stackTreeWidget->addTopLevelItems(topLevelItems);
        for (auto& library : libraries_)
            ui->libraryComboBox->addItem(library);
        FilterTreeWidget();
        return;
    }

    auto pythonPath = ui->pythonPathLineEdit->text();
    if (!QFile::exists(pythonPath)) {
        QMessageBox::warning(this, "Warning", "Please select python path first.");
        return;
    }

    progressDialog_->setWindowTitle("Launch Progress");
    progressDialog_->setLabelText("Preparing ...");
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(7);
    progressDialog_->setValue(0);
    progressDialog_->show();

    HideToolTips();

    libraries_.clear();
    ui->libraryComboBox->setCurrentIndex(0);
    for (int i = 1; i < ui->libraryComboBox->count(); i++)
        ui->libraryComboBox->removeItem(i);
    ui->sdkPushButton->setEnabled(true);
    ui->appNameLineEdit->setEnabled(false);
    ui->launchPushButton->setText("Stop Capture");
    ui->actionOpen->setEnabled(false);
    ui->statusBar->clearMessage();

    adbPath_ = ui->sdkPathLineEdit->text();
    adbPath_ = adbPath_.size() == 0 ? "adb" : adbPath_ + "/platform-tools/adb";

    ui->stackTreeWidget->clear();
    ui->stackTreeWidget->setSortingEnabled(false);
    for (auto& series : memInfoSeries_)
        series->clear();
    stackRecords_.clear();
    screenshots_.clear();
    symbloMap_.clear();
    freeAddrMap_.clear();
    callStackMap_.clear();
    callStackModel_->clear();

    maxMemInfoValue_ = 128;
    UpdateMemInfoRange();

    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath_);
    startAppProcess_->StartApp(ui->appNameLineEdit->text(), progressDialog_);

    isCapturing_ = true;
    Print("Starting application ...");
}

void MainWindow::on_chartScaleHSlider_valueChanged(int value) {
    memInfoChartView_->SetRangeScale(value);
    HideToolTips();
}

void MainWindow::on_stackTreeWidget_itemSelectionChanged() {
    callStackModel_->clear();
    callStackModel_->setHorizontalHeaderLabels(QStringList() << "Library" << "Function");
    auto selectedItems = ui->stackTreeWidget->selectedItems();
    if (selectedItems.size() == 0)
        return;
    auto selected = selectedItems.front();
    if (selected->childCount() == 0 && selected->parent() == nullptr) {
        auto& callStack = callStackMap_[static_cast<SortableTreeWidgetItem*>(selected)->Uuid()];
        auto parent = selected;
        for (int i = 0; i < callStack.size(); i += 2) {
            const auto& libName = callStack[i];
            const auto& funcAddr = callStack[i + 1];
            const auto& funcName = TryAddNewAddress(libName, funcAddr);
            auto item = new QTreeWidgetItem();
            item->setText(3, libName);
            item->setText(4, funcName);
            parent->addChild(item);
            parent = item;
        }
    }
    auto row = 0;
    while (selected != nullptr) {
        callStackModel_->setItem(row, 0, new QStandardItem(selected->text(3)));
        callStackModel_->setItem(row, 1, new QStandardItem(selected->text(4)));
        row++;
        if (selected->childCount() > 0) {
            selected = selected->child(0);
        } else {
            break;
        }
    }
}

void MainWindow::on_symbloPushButton_clicked() {
    auto symbloPath = QFileDialog::getOpenFileName(this, tr("Select Symblo File"),
                                                   GetLastSymbolDir(), tr("Library Files (*.sym *.sym.so *.so)"));
    if (!QFile::exists(symbloPath))
        return;
    lastSymbolDir_ = QFileInfo(symbloPath).dir().absolutePath();
    auto addr2linePath = ui->addr2LinePathLineEdit->text();
    QFile file(addr2linePath);
    if (!file.exists())
        return;
    QFileInfo info(symbloPath);
    auto soName = info.baseName() + ".so";
    auto it = symbloMap_.find(soName);
    if (it == symbloMap_.end())
        return;
    auto& addrMap = it.value();
    int requiredProcessCount = static_cast<int>(std::ceil(static_cast<float>(addrMap.size()) / 2000));
    QVector<AddressProcess*> avaliableProcesses;
    for (auto& process : addrProcesses_) {
        if (requiredProcessCount <= 0)
            break;
        if (!process->IsRunning()) {
            requiredProcessCount--;
            avaliableProcesses.push_back(process);
        }
    }
    for (int i = 0; i < requiredProcessCount; i++) {
        auto process = new AddressProcess(this);
        connect(process, &AddressProcess::ProcessFinished, this, &MainWindow::AddressProcessFinished);
        connect(process, &AddressProcess::ProcessErrorOccurred, this, &MainWindow::AddressProcessErrorOccurred);
        addrProcesses_.push_back(process);
        avaliableProcesses.push_back(process);
    }
    auto addrMapIt = addrMap.begin();
    for (auto& process : avaliableProcesses) {
        QStringList addrs;
        int count = 0;
        for (; addrMapIt != addrMap.end(); ++addrMapIt) {
            if (addrMapIt.value().size() == 0) {
                addrs.push_back(addrMapIt.key());
                count++;
            }
            if (count >= 2000)
                break;
        }
        process->SetExecutablePath(addr2linePath);
        process->DumpAsync(symbloPath, addrs, &addrMap);
    }
    progressDialog_->setWindowTitle("Symbol Load Progress");
    progressDialog_->setLabelText(QString("Loading symbols for %1 addresses by %2 process").arg(addrMap.size()).arg(avaliableProcesses.size()));
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(avaliableProcesses.size());
    progressDialog_->setValue(0);
    progressDialog_->show();
}

void MainWindow::on_pythonPushButton_clicked() {
    auto path = QFileDialog::getOpenFileName(this, tr("Select Executable Python"), QDir::homePath());
    ui->pythonPathLineEdit->setText(path);
}

void MainWindow::on_addr2LinePushButton_clicked() {
    auto path = QFileDialog::getOpenFileName(this, tr("Select Executable Addr2line"), QDir::homePath());
    ui->addr2LinePathLineEdit->setText(path);
}

void MainWindow::on_configPushButton_clicked() {
    QDialog editDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QHBoxLayout();
    auto textEdit = new QTextEdit(nullptr);
    QFile file(QApplication::applicationDirPath() + "/remote/loli.conf");
    if (!file.open(QIODevice::ReadOnly)) {
        textEdit->setText("5\n256\nlibunity,libil2cpp,");
    } else {
        textEdit->setText(file.readAll());
    }
    file.close();
    layout->setMargin(0);
    layout->addWidget(textEdit);
    editDialog.setLayout(layout);
    editDialog.setWindowModality(Qt::WindowModal);
    editDialog.setWindowTitle("Edit Configuration");
    editDialog.setMinimumSize(400, 300);
    editDialog.resize(400, 300);
    editDialog.exec();
    if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << textEdit->toPlainText();
    }
}

void MainWindow::on_memSizeComboBox_currentIndexChanged(int) {
    filterDirty_ = true;
}

void MainWindow::on_libraryComboBox_currentIndexChanged(int) {
    filterDirty_ = true;
}

void MainWindow::on_allocComboBox_currentIndexChanged(int) {
    filterDirty_ = true;
}
