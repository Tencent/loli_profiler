#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "timeprofiler.h"
#include "configdialog.h"
#include "customgraphicsview.h"
#include "treemapgraphicsview.h"
#include "memgraphicsview.h"
#include "selectappdialog.h"
#include "smaps/statsmapsdialog.h"
#include "smaps/visualizesmapsdialog.h"
#include "pathutils.h"
#include "hashstring.h"

#include <QClipboard>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTableWidget>
#include <QTreeWidget>
#include <QProgressDialog>
#include <QScrollBar>
#include <QGLWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QRegExp>

#include <algorithm>
#include <cmath>
#include <vector>
#include <limits>

#define APP_MAGIC 0xA4B3C2D1
#define APP_VERSION 106

#define ANDROID_SDK_NOTFOUND_MSG "Android SDK not found. Please select Android SDK's location in configuration panel."
#define ANDROID_NDK_NOTFOUND_MSG "Android NDK not found. Please select Android NDK's location in configuration panel."
#define STRIP_NON_PERSISTENT_MSG "Enable memory optimization? Turn this on for large projects that produces massive amount of data. "\
                                    "This will optimize loli-profiler's memory usage by data streaming."

enum class IOErrorCode : qint32 {
    NONE = 0,
    MAGIC_NUMBER_MISSMATCH,
    VERSION_MISSMATCH,
    CORRUPTED_DATA,
};

using namespace QtCharts;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow) {
    ui->setupUi(this);

    maxTime_ = std::numeric_limits<double>::max();

    progressDialog_ = new QProgressDialog(this, Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setAutoClose(true);
    progressDialog_->setCancelButton(nullptr);
    progressDialog_->setSizeGripEnabled(false);
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
    connect(stacktraceProcess_, &StackTraceProcess::SMapsDumped, [this]() {
        smapsTimer_->stop();
        StopCaptureProcess();
    });

    // setup screenshot view
    ui->screenshotGraphicsView->setScene(new QGraphicsScene());
    ui->screenshotGraphicsView->setCenter(QPointF());
    screenshotItem_ = new QGraphicsPixmapItem();
    ui->screenshotGraphicsView->scene()->addItem(screenshotItem_);

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
    connect(memInfoChartView_, &InteractiveChartView::OnRubberBandSelected, this, &MainWindow::OnTimelineRubberBandSelected);
    connect(memInfoChartView_, &InteractiveChartView::OnRubberBandHide, this, &MainWindow::OnTimelineRubberBandHide);

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

    filteredStacktraceModel_ = new StackTraceModel(this);
    filteredStacktraceProxyModel_ = new StackTraceProxyModel(filteredStacktraceModel_, this);
    stacktraceModel_ = new StackTraceModel(this);
    stacktraceProxyModel_ = new StackTraceProxyModel(stacktraceModel_, this);
    SwitchStackTraceModel(stacktraceProxyModel_);
    connect(ui->stackTableView, &QTableView::customContextMenuRequested, this, &MainWindow::OnStackTableViewContextMenu);

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
    mainTimer_->start(1000);

    smapsTimer_ = new QTimer(this);
    smapsTimer_->setSingleShot(true);
    smapsTimer_->setInterval(10000);
    connect(smapsTimer_, &QTimer::timeout, [this]() {
        Print("Failed to pull smaps data.");
        StopCaptureProcess();
    });
}

MainWindow::~MainWindow() {
    delete ui;
}

const QString SETTINGS_WINDOW_W = "Window_W";
const QString SETTINGS_WINDOW_H = "Window_H";
const QString SETTINGS_APPNAME = "AppName";
const QString SETTINGS_MAIN_SPLITER = "Main_Spliter";
const QString SETTINGS_UPPER_SPLITER = "Upper_Spliter";
const QString SETTINGS_LOWER__SPLITER = "Lower_Spliter";
const QString SETTINGS_SCALEHSLIDER = "ChartScaleHSlider";
const QString SETTINGS_LASTOPENDIR = "lastopen_dir";
const QString SETTINGS_LASTSYMBOLDIR = "lastsymbol_dir";
const QString SETTINGS_ARCH = "target_arch";
const QString SETTINGS_COMPILER = "target_compiler";
const QString SETTINGS_ANDROIDSDK = "AndroidSDK";
const QString SETTINGS_ANDROIDNDK = "AndroidNDK";

void MainWindow::LoadSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    int windowWidth = settings.value(SETTINGS_WINDOW_W).toInt();
    int windowHeight = settings.value(SETTINGS_WINDOW_H).toInt();
    if (windowWidth > 0 && windowHeight > 0) {
        this->resize(windowWidth, windowHeight);
    }
    ui->appNameLineEdit->setText(settings.value(SETTINGS_APPNAME).toString());
    ui->main_splitter->restoreState(settings.value(SETTINGS_MAIN_SPLITER).toByteArray());
    ui->upper_splitter->restoreState(settings.value(SETTINGS_UPPER_SPLITER).toByteArray());
    ui->lower_splitter->restoreState(settings.value(SETTINGS_LOWER__SPLITER).toByteArray());
    ui->chartScaleHSlider->setValue(settings.value(SETTINGS_SCALEHSLIDER, 10).toInt());
    auto lastOpenDir = settings.value(SETTINGS_LASTOPENDIR).toString();
    if (QDir(lastOpenDir).exists())
        lastOpenDir_ = lastOpenDir;
    auto lastSymbolDir = settings.value(SETTINGS_LASTSYMBOLDIR).toString();
    if (QDir(lastSymbolDir).exists())
        lastSymbolDir_ = lastSymbolDir;
    targetArch_ = settings.value(SETTINGS_ARCH, "armeabi-v7a").toString();
    targetCompiler_ = settings.value(SETTINGS_COMPILER, "gcc").toString();
    PathUtils::SetSDKPath(settings.value(SETTINGS_ANDROIDSDK).toString());
    PathUtils::SetNDKPath(settings.value(SETTINGS_ANDROIDNDK).toString());
}

void MainWindow::SaveSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    settings.setValue(SETTINGS_WINDOW_W, this->width());
    settings.setValue(SETTINGS_WINDOW_H, this->height());
    settings.setValue(SETTINGS_APPNAME, ui->appNameLineEdit->text());
    settings.setValue(SETTINGS_MAIN_SPLITER, ui->main_splitter->saveState());
    settings.setValue(SETTINGS_UPPER_SPLITER, ui->upper_splitter->saveState());
    settings.setValue(SETTINGS_LOWER__SPLITER, ui->lower_splitter->saveState());
    settings.setValue(SETTINGS_SCALEHSLIDER, ui->chartScaleHSlider->value());
    if (QDir(lastOpenDir_).exists())
        settings.setValue(SETTINGS_LASTOPENDIR, lastOpenDir_);
    if (QDir(lastSymbolDir_).exists())
        settings.setValue(SETTINGS_LASTSYMBOLDIR, lastSymbolDir_);
    settings.setValue(SETTINGS_ANDROIDSDK, PathUtils::GetSDKPath());
    settings.setValue(SETTINGS_ANDROIDNDK, PathUtils::GetNDKPath());
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

void MainWindow::ExportToText(QFile* file, bool optimal) {
    QTextStream stream(file);
    qint32 count = stacktraceModel_->rowCount();
    stream << "[seq,time,size,addr,library,funcaddr]" << endl;
    for (int i = 0; i < count; i++) {
        auto& record = stacktraceModel_->recordAt(i);
        stream << record.seq_ << ',' << record.time_ << ',' << record.size_ << ','
               << record.addr_ << ',' << record.library_.Get() << ',' << record.funcAddr_ << endl;
        auto it = callStackMap_.find(record.uuid_);
        if (it != callStackMap_.end()) {
            auto stackCount = it.value().size();
            for (int i = 0; i < stackCount; i++) {
                const auto& libName = it.value()[i].first.Get();
                const auto& funcAddr = it.value()[i].second;
                if (optimal) {
                    stream << libName << ',' << QString("0x%1").arg(funcAddr, 0, 16) << ',';
                } else {
                    const auto& funcName = TryAddNewAddress(libName, funcAddr);
                    stream << libName << ',' << funcName << ',';
                }
            }
            if (it.value().size() > 0) {
                stream << endl;
            }
        }
        stream << endl;
    }
    if (optimal) {
        stream << "[stackAddrMap]" << endl;
        for (auto libIt = symbloMap_.begin(); libIt != symbloMap_.end(); ++libIt) {
            stream << libIt.key() << ":" << endl;
            auto& addrs = libIt.value();
            for (auto addrIt = addrs.begin(); addrIt != addrs.end(); ++addrIt) {
                stream << QString("0x%1").arg(addrIt.key(), 0, 16) << ", " << addrIt.value() << endl;
            }
        }
        stream << endl;
    }
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
    // string hashes
    stream << HashString::hashmap_;
    // callstack tree view
    qint32 count = stacktraceModel_->rowCount();
    stream << count;
    for (int i = 0; i < count; i++) {
        auto& record = stacktraceModel_->recordAt(i);
        stream << record.uuid_.toString();
        stream << record.seq_;
        stream << record.time_;
        stream << record.size_;
        stream << record.addr_;
        stream << record.funcAddr_;
        stream << record.library_.hashcode_;
    }
    // callstack map
    stream << static_cast<qint32>(callStackMap_.size());
    for (auto it = callStackMap_.begin(); it != callStackMap_.end(); ++it) {
        stream << it.key().toString();
        auto& callstacks = it.value();
        stream << static_cast<qint32>(callstacks.size());
        for (auto stack : callstacks) {
            stream << stack.first.hashcode_ << stack.second;
        }
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
        stream << static_cast<quint32>(it.value());
    }
    // screen shots
    stream << static_cast<qint32>(screenshots_.size());
    for (auto& pair : screenshots_) {
        stream << static_cast<qint32>(pair.first);
        stream << pair.second;
    }
    // smaps
    stream << static_cast<qint32>(sMapsSections_.size());
    for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
        stream << it.key();
        auto& section = it.value();
        stream << static_cast<qint32>(section.addrs_.size());
        for (auto& addr : section.addrs_) {
            stream << addr.start_;
            stream << addr.end_;
            stream << addr.offset_;
        }
        stream << section.virtual_;
        stream << section.rss_;
        stream << section.pss_;
        stream << section.privateClean_;
        stream << section.privateDirty_;
        stream << section.sharedClean_;
        stream << section.sharedDirty_;
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
    // string hashes
    HashString::hashmap_.clear();
    stream >> HashString::hashmap_;
    // callstack tree view
    QSet<QString> libraries;
    filteredStacktraceModel_->clear();
    stacktraceModel_->clear();
    ResetFilters();
    SwitchStackTraceModel(stacktraceProxyModel_);
    stream >> value;
    QVector<StackRecord> records;
    for (int i = 0; i < value; i++) {
        StackRecord record;
        QString str;
        stream >> str;
        record.uuid_ = QUuid::fromString(str);
        stream >> record.seq_;
        stream >> record.time_;
        stream >> record.size_;
        stream >> record.addr_;
        stream >> record.funcAddr_;
        stream >> record.library_.hashcode_;
        if (!libraries.contains(record.library_.Get()))
            libraries.insert(record.library_.Get());
        records.push_back(record);
    }
    ui->recordCountLineEdit->setText("");
    ui->libraryComboBox->setCurrentIndex(0);
    for (int i = 1; i < ui->libraryComboBox->count(); i++)
        ui->libraryComboBox->removeItem(i);
    for (auto& library : libraries)
        ui->libraryComboBox->addItem(library);
    // callstack map
    callStackMap_.clear();
    stream >> value;
    QVector<QPair<HashString, quint64>> callstack;
    for (int i = 0; i < value; i++) {
        QString uuid;
        qint32 len;
        stream >> uuid >> len;
        callstack.clear();
        for (int j = 0; j < len; j++) {
            QPair<HashString, quint64> pair;
            stream >> pair.first.hashcode_ >> pair.second;
            callstack.push_back(pair);
        }
        callStackMap_.insert(QUuid::fromString(uuid), callstack);
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
        quint64 key;
        QString value;
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
        quint64 addr;
        quint32 seq;
        stream >> addr >> seq;
        freeAddrMap_.insert(addr, seq);
    }
    stacktraceModel_->append(records);
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
    // smaps
    sMapsSections_.clear();
    stream >> value;
    sMapsSections_.reserve(value);
    for (int i = 0; i < value; i++) {
        QString name;
        stream >> name;
        SMapsSection section;
        qint32 size;
        stream >> size;
        quint64 start, end, offset;
        for (int i = 0; i < size; i++) {
            stream >> start;
            stream >> end;
            stream >> offset;
            section.addrs_.push_back(SMapsSectionAddr(start, end, offset));
        }
        stream >> section.virtual_;
        stream >> section.rss_;
        stream >> section.pss_;
        stream >> section.privateClean_;
        stream >> section.privateDirty_;
        stream >> section.sharedClean_;
        stream >> section.sharedDirty_;
        sMapsSections_.insert(name, section);
    }
    ShowSummary();
    OnTimelineRubberBandHide();
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
    ui->launchPushButton->setEnabled(true);
    ui->menuFile->setEnabled(true);
    ui->menuTools->setEnabled(true);
    ui->configPushButton->setEnabled(true);
    ui->symbloPushButton->setEnabled(true);
    ui->toolBar->setEnabled(true);
    ui->stackTableView->setSortingEnabled(true);
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
    if (index < 0 || index >= screenshotCount) {
        screenshotItem_->setVisible(false);
        return;
    }
    QPixmap pixmap;
    pixmap.loadFromData(screenshots_[index].second, "JPG");
    screenshotItem_->setPixmap(pixmap);
    screenshotItem_->setVisible(true);
    screenshotItem_->setPos(-pixmap.width() / 2, -pixmap.height() / 2);
}

void MainWindow::HideToolTips() {
    memInfoChartView_->HideToolTip();
}

void MainWindow::UpdateMemInfoRange() {
    memInfoAxisY_->setRange(0, maxMemInfoValue_);
}

QString MainWindow::TryAddNewAddress(const QString& lib, quint64 addr) {
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
    return QString("0x%1").arg(addr, 0, 16);
}

void MainWindow::ShowCallStack(const QModelIndex& index) {
    if (!index.isValid())
        return;
    auto proxyModel = static_cast<StackTraceProxyModel*>(ui->stackTableView->model());
    auto srcModel = static_cast<StackTraceModel*>(proxyModel->sourceModel());
    auto& selectedRecord = srcModel->recordAt(proxyModel->mapToSource(index).row());
    auto& callstack = callStackMap_[selectedRecord.uuid_];
    for (int i = 0; i < callstack.size(); i++) {
        const auto& libName = callstack[i].first.Get();
        const auto& funcAddr = callstack[i].second;
        const auto& funcName = TryAddNewAddress(libName, funcAddr);
        callStackModel_->setItem(i, 0, new QStandardItem(libName));
        callStackModel_->setItem(i, 1, new QStandardItem(funcName));
    }
}

void MainWindow::ShowSummary() {
    auto model = ui->stackTableView->model();
    auto rowCount = model->rowCount();
    quint64 size = 0;
    for (int i = 0; i < rowCount; i++) {
        size += model->data(model->index(i, 1), Qt::UserRole).toUInt();
    }
    ui->recordCountLineEdit->setText(QString("%1 / %2").arg(rowCount).arg(sizeToString(size)));
}

void MainWindow::FilterStackTraceModel() {
    auto sizeFilter = ui->memSizeComboBox->currentIndex();
    auto libraryFilter = ui->libraryComboBox->currentIndex() == 0 ? QString() : ui->libraryComboBox->currentText();
    auto persistentFilter = ui->allocComboBox->currentIndex() == 1;
//    TimerProfiler profler("FilterStackTraceModel");
    filteredStacktraceModel_->clear();
    QVector<StackRecord> filteredRecords;
    int recordCount = stacktraceModel_->rowCount();
    for (int i = 0; i < recordCount; i++) {
        const auto& record = stacktraceModel_->recordAt(i);
        auto timeInSecond = record.time_ / 1000;
        if (timeInSecond < minTime_ || timeInSecond > maxTime_)
            continue;
        if (sizeFilter > 0) {
            auto size = record.size_;
            switch(sizeFilter) {
            case 1: // large
                if (size < 1048576)
                    continue;
                break;
            case 2: // medium
                if (size >= 1048576 || size <= 1024)
                    continue;
                break;
            case 3: // small
                if (size > 1024)
                    continue;
                break;
            }
        }
        if (libraryFilter.size() > 0) {
            if (record.library_.Get() != libraryFilter)
                continue;
        }
        if (persistentFilter) {
            auto it = freeAddrMap_.find(record.addr_);
            if (it != freeAddrMap_.end()) {
                if (record.seq_ < it.value()) {
                    continue;
                }
            }
        }
        filteredRecords.push_back(record);
    }
    filteredStacktraceModel_->append(filteredRecords);
    SwitchStackTraceModel(filteredStacktraceProxyModel_);
}

void MainWindow::SwitchStackTraceModel(StackTraceProxyModel* model) {
    if (model == ui->stackTableView->model())
        return;
    ui->stackTableView->setModel(model);
    connect(ui->stackTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::OnStackTableViewSelectionChanged);
}

QString Demangle(const QString& name) {
    auto slashIndex = name.lastIndexOf('/');
    if (slashIndex > 0)
        return name.right(name.size() - slashIndex - 1);
    return name;
}

void MainWindow::ReadSMapsFile(QFile* file) {
    QTextStream stream(file);
    SMapsSection total;
    SMapsSection* curSection = nullptr;
    while (!stream.atEnd()) {
        auto line = stream.readLine();
        if (line.isEmpty())
            continue;
        auto strList = line.split(' ', QString::SplitBehavior::SkipEmptyParts);
        if (strList.size() >= 6) {
            auto libName = Demangle(strList[5]);
            curSection = &sMapsSections_[libName];
            auto addrs = strList[0].split('-');
            if (addrs.size() > 1) {
                auto start = addrs[0].toULongLong(nullptr, 16);
                auto end = addrs[1].toULongLong(nullptr, 16);
                auto offset = strList[2].toULongLong(nullptr, 16);
                curSection->addrs_.push_back(SMapsSectionAddr(start, end, offset));
            }
        } else if (strList.size() >= 2) {
            if (curSection == nullptr)
                continue;
            auto word = strList[0];
            auto value = strList[1].toUInt();
            if (word == "Size:") {
                curSection->virtual_ += value;
                total.virtual_ += value;
            } else if (word == "Rss:") {
                curSection->rss_ += value;
                total.rss_ += value;
            } else if (word == "Pss:") {
                curSection->pss_ += value;
                total.pss_ += value;
            } else if (word == "Shared_Clean:") {
                curSection->sharedClean_ += value;
                total.sharedClean_ += value;
            } else if (word == "Shared_Dirty:") {
                curSection->sharedDirty_ += value;
                total.sharedDirty_ += value;
            } else if (word == "Private_Dirty:") {
                curSection->privateDirty_ += value;
                total.privateDirty_ += value;
            } else if (word == "Private_Clean:") {
                curSection->privateClean_ += value;
                total.privateClean_ += value;
            }
        }
    }
}

void MainWindow::GetMergedCallstacks(QList<QTreeWidgetItem*>& topLevelItems) {
    auto model = filteredStacktraceModel_;
    auto count = model->rowCount();
    if (count == 0) {
        count = stacktraceModel_->rowCount();
        if (count == 0) {
            QMessageBox::information(this, "Show Merged Callstakcs", "No callstack record, open or capture some records first!");
            return;
        } else {
            model = stacktraceModel_;
        }
    }
    // CustomTreeWidhgetItem
    class CustomTreeWidgetItem : public QTreeWidgetItem {
    public:
        CustomTreeWidgetItem(QString funcName, quint64 size, QTreeWidget* parent = nullptr)
            : QTreeWidgetItem(parent), funcName_(funcName), size_(size) {
            setData(0, Qt::DisplayRole, funcName_);
            setData(1, Qt::DisplayRole, sizeToString(size_));
            setData(1, Qt::UserRole, size_);
            setData(2, Qt::DisplayRole, count_);
        }
        void setSize(quint64 size) {
            size_ = size;
            setData(1, Qt::DisplayRole, sizeToString(size_));
            setData(1, Qt::UserRole, size_);
        }
        void setCount(quint64 count) {
            count_ = count;
            setData(2, Qt::DisplayRole, count_);
        }
        quint64 size() const { return size_; }
        quint64 count() const { return count_; }
        bool operator<(const QTreeWidgetItem &other) const {
            auto casted = static_cast<const CustomTreeWidgetItem&>(other);
            int column = treeWidget()->sortColumn();
            if (column == 0) {
                return funcName_ < casted.funcName_;
            } else if (column == 1) {
                return size_ < casted.size_;
            } else {
                return count_ < casted.count_;
            }
        }
    private:
        QString funcName_;
        quint64 size_;
        quint64 count_ = 1;
    };
    QHash<uint, CustomTreeWidgetItem*> itemMap;
    for (int i = 0; i < count; i++) {
        const auto& record = model->recordAt(i);
        const auto& callstacks = callStackMap_[record.uuid_];
        CustomTreeWidgetItem* child = nullptr;
        QStringList callstackNames;
        for (int j = 0; j < callstacks.size(); j ++) {
            const auto& libName = callstacks[j].first.Get();
            const auto& funcAddr = callstacks[j].second;
            const auto& funcName = TryAddNewAddress(libName, funcAddr);
            callstackNames << funcName;
        }
        for (auto it = callstackNames.begin(); it != callstackNames.end(); ++it) {
            auto curHash = qHashRange(it, callstackNames.end());
            auto itemIt = itemMap.find(curHash);
            CustomTreeWidgetItem* item = nullptr;
            if (itemIt != itemMap.end()) {
                item = itemIt.value();
                auto parent = item;
                while (parent != nullptr) {
                    parent->setSize(parent->size() + static_cast<quint64>(record.size_));
                    parent->setCount(parent->count() + 1);
                    parent = static_cast<CustomTreeWidgetItem*>(parent->parent());
                }
                if (child != nullptr)
                    item->addChild(child);
                break;
            }
            item = new CustomTreeWidgetItem(*it, static_cast<quint64>(record.size_));
            itemMap.insert(curHash, item);
            if (child != nullptr)
                item->addChild(child);
            child = item;
        }
        if (child != nullptr && child->parent() == nullptr)
            topLevelItems.push_back(child);
    }
}

void MainWindow::ResetFilters() {
   minTime_ = 0;
   maxTime_ = std::numeric_limits<double>::max();
   ui->memSizeComboBox->setCurrentIndex(0);
   ui->allocComboBox->setCurrentIndex(0);
   ui->libraryComboBox->setCurrentIndex(0);
}

void MainWindow::PushEmptySMapsFile() {
    QTemporaryFile smapsFile;
    smapsFile.open();
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    AdbProcess::SetArguments(&process, QStringList() << "push" << smapsFile.fileName() << "/data/local/tmp/smaps.txt");
    process.start();
    process.waitForStarted();
    process.waitForFinished();
    process.close();
}

void MainWindow::ReadStacktraceData(const QVector<RawStackInfo>& stacks) {
    auto isNoStack = ConfigDialog::IsNoStackMode();
    if (stacks.size() > 0) {
        for (const auto& stack : stacks) {
            StackRecord record;
            record.uuid_ = QUuid::createUuid();
            record.seq_ = stack.seq_;
            record.time_ = stack.time_;
            record.size_ = stack.size_;
            record.addr_ = stack.addr_;
            if (isNoStack) {
                record.library_ = HashString(stack.library_);
            } else {
                auto& callstack = callStackMap_[record.uuid_];
                for (int i = 0; i < stack.stacktraces_.size(); i++) {
                    callstack.append(qMakePair(QString(), stack.stacktraces_[i]));
                }
            }
            recordsCache_.push_back(record);
        }
    }
}

void MainWindow::ReadStacktraceDataCache() {
    auto cachePath = QApplication::applicationDirPath() + "/cache";
    QDir cacheDir(cachePath);
    auto files = cacheDir.entryList(QDir::Filter::Files, QDir::SortFlag::Time);
    quint32 recordCount = 0;
    QVector<RawStackInfo> stacks;
    for (auto filePath : files) {
        QFile file(cachePath + "/" + filePath);
        stacks.clear();
        if (file.open(QFile::OpenModeFlag::ReadOnly)) {
            QDataStream stream(&file);
            while (!stream.atEnd()) {
                qint32 size;
                stream >> size;
                recordCount += size;
                for (int i = 0; i < size; i++) {
                    RawStackInfo stack;
                    stream >> stack.seq_ >> stack.addr_ >> stack.size_ >> stack.time_
                           >> stack.library_.hashcode_ >> stack.recType_ >> stack.stacktraces_;
                    // ignore freed records
                    auto it = freeAddrMap_.find(stack.addr_);
                    if (it != freeAddrMap_.end()) {
                        if (stack.seq_ < it.value())
                            continue;
                    }
                    stacks.push_back(stack);
                }
            }
            ReadStacktraceData(stacks);
            file.remove();
        }
    }
    Print(QString("Cached %1 records.").arg(recordCount));
}

void MainWindow::FilterPersistentRecords() {
    recordsCache_.erase(std::remove_if(recordsCache_.begin(), recordsCache_.end(), [this](const StackRecord& record) {
        auto it = freeAddrMap_.find(record.addr_);
        if (it != freeAddrMap_.end()) {
            if (record.seq_ < it.value())
                return true;
        }
        return false;
    }), recordsCache_.end());
}

void MainWindow::StopCaptureProcess() {
    ConnectionFailed();
    progressDialog_->setWindowTitle("Stop Capture Progress");
    progressDialog_->setLabelText(QString("Stopping capture ..."));
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(2);
    progressDialog_->setValue(0);
    progressDialog_->show();
    progressDialog_->setValue(1);
    progressDialog_->setLabelText("Requesting smaps info from device.");
    auto readSMaps = false;
    auto smapsPath = QCoreApplication::applicationDirPath() + "/smaps.txt";
    QProcess process;
    process.setProgram(PathUtils::GetADBExecutablePath());
    AdbProcess::SetArguments(&process, QStringList() << "pull" << "/data/local/tmp/smaps.txt" << smapsPath);
    process.start();
    process.waitForStarted();
    process.waitForFinished();
    process.close();
    QFile file(smapsPath);
    if (file.exists()) {
        if (file.open(QFile::OpenModeFlag::ReadOnly)) {
            ReadSMapsFile(&file);
            readSMaps = true;
        }
        file.remove();
    }
    if (!readSMaps) {
        Print("Failed to cat proc/pid/smaps");
    } else {
        if (useCache_) {
            progressDialog_->setLabelText("Reading cached record files ...");
            ReadStacktraceDataCache();
        }
        InterpretStacktraceData();
    }
    Print(QString("Captured %1 records.").arg(stacktraceModel_->rowCount()));
    for (auto& library : libraries_)
        ui->libraryComboBox->addItem(library);
    OnTimelineRubberBandHide();
    ShowSummary();
    progressDialog_->setValue(2);
}

static QVector<QPair<HashString, SMapsSection*>> sMapsCache_;

void MainWindow::InterpretRecordLibrary(StackRecord& record, StacktraceData& data) {
    auto it = callStackMap_.find(record.uuid_);
    if (it == callStackMap_.end())
        return;
    auto& callStack = it.value();
    for (int i = 0; i < callStack.size(); i++) {
        auto& libName = callStack[i].first;
        auto& funcAddr = callStack[i].second;
        bool found = false;
        for (int i = 0; i < sMapsCache_.size(); i++) {
            const auto& cache = sMapsCache_[i];
            quint64 baseAddr;
            if (cache.second->Contains(funcAddr, record.size_, baseAddr)) {
                funcAddr = funcAddr - baseAddr;
                libName = cache.first;
                data.records_.push_back(qMakePair(libName, funcAddr));
                found = true;
                break;
            }
        }
        if (!found)
            libName = HashString(qHash("unknown"));
    }
    if (callStack.size() == 0)
        return;
    record.library_ = callStack[0].first;
    record.funcAddr_ = callStack[0].second;
    int level = 1;
    while (record.library_.Get() == "libloli.so" && level < callStack.size()) {
        record.library_ = callStack[level].first;
        record.funcAddr_ = callStack[level].second;
        level++;
    }
    if (record.library_.hashcode_ == 0) {
        record.library_ = callStack[0].first = HashString(qHash("unknown"));
    }
    if (!data.libraries_.contains(record.library_.Get()))
        data.libraries_.push_back(record.library_.Get());
}

MainWindow::StacktraceData MainWindow::InterpretRecordsLibrary(int start, int count) {
    StacktraceData data;
    for (int i = 0; i < count; i++)
        InterpretRecordLibrary(recordsCache_[start + i], data);
    return data;
}

void MainWindow::InterpretStacktraceData() {
    if (ConfigDialog::IsNoStackMode()) {
        for (int i = 0; i < recordsCache_.size(); i++) {
            auto& record = recordsCache_[i];
            if (!libraries_.contains(record.library_.Get())) {
                libraries_.insert(record.library_.Get());
            }
        }
    } else {
//        TimerProfiler profiler("Interpret Recs");
        HashString::hashmap_.insert(qHash("unknown"), "unknown");
        for (auto it = sMapsSections_.begin(); it != sMapsSections_.end(); ++it) {
            auto& library = it.key();
            auto& sections = it.value();
            if (library.endsWith(".so")) {
                sMapsCache_.push_back(qMakePair(HashString(library), &sections));
            }
        }
        auto threadCount = std::max(2, QThread::idealThreadCount());
        auto payload = recordsCache_.size() / threadCount;
        QVector<QFuture<StacktraceData>> futures;
        int currentIndex = 0;
        for (int i = 0; i < threadCount - 1; i++) {
            futures.push_back(QtConcurrent::run(this, &MainWindow::InterpretRecordsLibrary, currentIndex, payload));
            currentIndex += payload;
        }
        futures.push_back(QtConcurrent::run(this, &MainWindow::InterpretRecordsLibrary, currentIndex, recordsCache_.size() - currentIndex));
        progressDialog_->setWindowTitle("Translating data");
        progressDialog_->setLabelText(QString("Translating %1 records by %2 jobs").arg(recordsCache_.size()).arg(threadCount));
        progressDialog_->setMinimum(0);
        progressDialog_->setMaximum(threadCount);
        progressDialog_->setValue(0);
        progressDialog_->show();
        QCoreApplication::instance()->sendPostedEvents();
        for (int i = 0; i < futures.size(); i++) {
            progressDialog_->setValue(i);
            auto future = futures[i];
            if (!future.isFinished())
                futures[i].waitForFinished();
            const auto& trace = future.result();
            for (const auto& record : trace.records_) {
                TryAddNewAddress(record.first.Get(), record.second);
            }
            for (const auto& library : trace.libraries_) {
                if (!libraries_.contains(library)) {
                    libraries_.insert(library);
                }
            }
        }
        progressDialog_->hide();
    }
    ResetFilters();
    stacktraceModel_->append(recordsCache_);
    FilterStackTraceModel();
    recordsCache_.clear();
    sMapsCache_.clear();
}

void MainWindow::FixedUpdate() {
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
        stacktraceProcess_->ConnectToServer(port_);
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

void MainWindow::OnTimelineRubberBandSelected(double from, double to) {
    const auto eplison = 1e-6;
    if (std::abs(minTime_ - from) > eplison || std::abs(maxTime_ - to) > eplison) {
        minTime_ = from;
        maxTime_ = to;
        FilterStackTraceModel();
        ShowSummary();
    }
}

void MainWindow::OnTimelineRubberBandHide() {
    OnTimelineRubberBandSelected(0.0, std::numeric_limits<double>::max());
}

void MainWindow::OnStackTableViewContextMenu(const QPoint & pos) {
    QMenu menu(this);
    auto actionCopy = new QAction("Copy to Clipboard");
    actionCopy->setShortcut(QKeySequence::Copy);
    menu.addAction(actionCopy);
    connect(actionCopy, &QAction::triggered, this, [this]() {
        const auto& indexes = ui->stackTableView->selectionModel()->selection().indexes();
        if (indexes.size() == 0)
            return;
        QString output;
        QTextStream stream(&output);
        auto selectedIndex = indexes.front();
        if (!selectedIndex.isValid())
            return;
        auto proxyModel = static_cast<StackTraceProxyModel*>(ui->stackTableView->model());
        auto srcModel = static_cast<StackTraceModel*>(proxyModel->sourceModel());
        auto& selectedRecord = srcModel->recordAt(proxyModel->mapToSource(selectedIndex).row());
        auto& callStack = callStackMap_[selectedRecord.uuid_];
        for (int i = 0; i < callStack.size(); i++) {
            const auto& libName = callStack[i].first.Get();
            const auto& funcAddr = callStack[i].second;
            const auto& funcName = TryAddNewAddress(libName, funcAddr);
            stream << libName << ", " << funcName << endl;
        }
        stream.flush();
        QApplication::clipboard()->setText(output);
    });
    menu.exec(ui->stackTableView->viewport()->mapToGlobal(pos));
}

void MainWindow::OnStackTableViewSelectionChanged(const QItemSelection &selected, const QItemSelection &) {
    callStackModel_->clear();
    callStackModel_->setHorizontalHeaderLabels(QStringList() << "Library" << "Function");
    if (selected.indexes().size() == 0)
        return;
    ShowCallStack(selected.indexes().front());
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
    port_++;
    screenshotProcess_->SetExecutablePath(PathUtils::GetADBExecutablePath());
    stacktraceProcess_->SetExecutablePath(PathUtils::GetADBExecutablePath());
    stacktraceProcess_->ForwardPort(port_);
    memInfoProcess_->SetExecutablePath(PathUtils::GetADBExecutablePath());
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
    Print("View logs under executable's folder for more details.");
    QFile file(QCoreApplication::applicationDirPath() + "/profiler.log");
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    QDialog errorLog(this);
    errorLog.setLayout(new QVBoxLayout(&errorLog));
    auto loglist = new QListWidget(&errorLog);
    errorLog.layout()->addWidget(loglist);
    errorLog.layout()->setMargin(0);
    QTextStream stream(file.readAll());
    QString line;
    while (stream.readLineInto(&line)) {
        loglist->addItem(line);
    }
    loglist->setCurrentRow(loglist->count() - 1);
    errorLog.setWindowTitle("Process injection log");
    errorLog.resize(600, 400);
    errorLog.setMinimumSize(600, 400);
    errorLog.exec();
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
    if (!isConnected_ || !isCapturing_)
        return;
    stacktraceRetryCount_ = 5;
    const auto& stacks = stacktraceProcess_->GetStackInfo();
    const auto& frees = stacktraceProcess_->GetFreeInfo();
    if (useCache_) {
        static quint32 cacheIndex = 0;
        auto cacheDirPath = QApplication::applicationDirPath() + "/cache";
        if (!QDir(cacheDirPath).exists()) {
            QDir().mkdir(cacheDirPath);
        }
        auto cachePath = QString("%1/cache_%2.bin").arg(cacheDirPath).arg(cacheIndex);
        QFile cacheFile(cachePath);
        if (!cacheFile.open(QFile::OpenModeFlag::WriteOnly | QFile::OpenModeFlag::Append)) {
            Print("Failed to open cache file: " + cachePath);
            return;
        }
        QDataStream stream(&cacheFile);
        stream << static_cast<qint32>(stacks.size());
        for (auto& stack : stacks) {
            stream << stack.seq_ << stack.addr_ << stack.size_ << stack.time_
                   << stack.library_.hashcode_ << stack.recType_ << stack.stacktraces_;
        }
        cacheFile.flush();
        if (cacheFile.size() > 1024 * 1024 * 512) {
            cacheIndex++;
        }
        cacheFile.close();
    } else {
        ReadStacktraceData(stacks);
    }
    // read free call infos
    if (frees.size() > 0) {
        for (const auto& free : frees) {
            const auto address = free.second;
            auto curSeq = freeAddrMap_[address];
            if (free.first > curSeq) {
                freeAddrMap_[address] = free.first;
            }
        }
    }
}

void MainWindow::StacktraceConnectionLost() {
    if (!isCapturing_)
        return;
    if (stacktraceRetryCount_ > 0) {
        static int maxRetryCount = 0;
        if (!progressDialog_->isVisible()) {
            maxRetryCount = stacktraceRetryCount_;
            progressDialog_->setWindowTitle("Connection Lost");
            progressDialog_->setLabelText("Retrying ...");
            progressDialog_->setMinimum(0);
            progressDialog_->setMaximum(maxRetryCount);
            progressDialog_->setValue(0);
            progressDialog_->show();
        }
        progressDialog_->setValue(maxRetryCount - stacktraceRetryCount_);
    }
//    Print(QString("Connection failed, retrying %1").arg(stacktraceRetryCount_));
    stacktraceRetryCount_--;
    if (stacktraceRetryCount_ <= 0) {
        ConnectionFailed();
        progressDialog_->close();
    }
}

void MainWindow::AddressProcessFinished(AdbProcess* process) {
    progressDialog_->setValue(progressDialog_->value() + 1);
    auto addrProcess = static_cast<AddressProcess*>(process);
    if (addrProcess->GetConvertedCount() == 0)
        return;
    auto selectedIndexes = ui->stackTableView->selectionModel()->selection().indexes();
    if (selectedIndexes.size() > 0)
        ShowCallStack(selectedIndexes.front());
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


void MainWindow::on_actionStat_SMaps_triggered() {
    if (sMapsSections_.size() == 0) {
        QMessageBox::warning(this, "Warning", "No smaps data found!", QMessageBox::StandardButton::Ok);
        return;
    }
    StatSmapsDialog fragDialog;
    fragDialog.ShowSmap(sMapsSections_);
}

void MainWindow::on_actionVisualize_SMaps_triggered() {
    if (sMapsSections_.size() == 0) {
        QMessageBox::warning(this, "Warning", "No smaps data found!", QMessageBox::StandardButton::Ok);
        return;
    }
    if (ui->libraryComboBox->currentIndex() != 0 || ui->allocComboBox->currentIndex() != 1) {
        if (QMessageBox::warning(this,
                                 "Warning", "Visualizing smaps requires filter changes (All Libraries, Persistent), are you sure?",
                                 QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No) == QMessageBox::StandardButton::No) {
            return;
        }
    }
    ui->libraryComboBox->setCurrentIndex(0); // show all libraries
    ui->allocComboBox->setCurrentIndex(1); // show only persisient
    // show dialog
    VisualizeSmapsDialog fragDialog;
    auto curModel = ui->stackTableView->model();

    fragDialog.VisualizeSmap(sMapsSections_, curModel);
}

void MainWindow::on_actionShow_Merged_Callstacks_triggered() {
    QList<QTreeWidgetItem*> topLevelItems;
    GetMergedCallstacks(topLevelItems);
    if (topLevelItems.size() == 0)
        return;
    QDialog fragDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout(&fragDialog);
    layout->setSpacing(2);
    fragDialog.setLayout(layout);
    auto treeWidget = new QTreeWidget(&fragDialog);
    treeWidget->setHeaderLabels(QStringList() << "Function" << "Size" << "Count");
    treeWidget->addTopLevelItems(topLevelItems);
    treeWidget->resizeColumnToContents(0);
    treeWidget->header()->resizeSections(QHeaderView::ResizeMode::ResizeToContents);
    treeWidget->setSortingEnabled(true);
    auto searchLineEdit = new QLineEdit(&fragDialog);
    searchLineEdit->setPlaceholderText("Type keyword to do fuzzy search, then press enter to review one by one.");
    searchLineEdit->setClearButtonEnabled(true);
    QString prevKeyword;
    int matchIndex = 0;
    QList<QTreeWidgetItem*> matches;
    connect(searchLineEdit, &QLineEdit::editingFinished, [&](){
        auto keyword = searchLineEdit->text();
        if (prevKeyword == keyword)
            return;
        treeWidget->clearSelection();
        matches.clear();
        matchIndex = 0;
        prevKeyword = keyword;
        if (keyword.size() == 0)
            return;
        auto startTime = sclock::now();
        matches = treeWidget->findItems(keyword, Qt::MatchContains | Qt::MatchRecursive, 0);
        auto ms = std::chrono::duration<double, std::milli>(sclock::now() - startTime).count();
        ui->statusBar->showMessage(QString("Found %1 matches in %2 ms, press enter to review one by one").arg(matches.size()).arg(ms), 10000);
    });
    connect(searchLineEdit, &QLineEdit::returnPressed, [&](){
        if (matches.size() <= 0) {
            return;
        }
        auto item = matches[matchIndex];
        item->setSelected(true);
        auto rowCount = 1;
        auto curItem = item->parent();
        while (curItem) {
            rowCount++;
            if (!curItem->isExpanded())
                curItem->setExpanded(true);
            curItem = curItem->parent();
        }
        // scroll to item
        treeWidget->scrollToItem(item, QAbstractItemView::ScrollHint::PositionAtCenter);
        auto itemRect = treeWidget->visualItemRect(item);
        auto hscrollbar = treeWidget->horizontalScrollBar();
        hscrollbar->setValue(hscrollbar->value() + itemRect.x());
        matchIndex++;
        if (matchIndex > matches.size() - 1) {
            matchIndex = 0;
        }
    });
    layout->addWidget(treeWidget);
    layout->addWidget(searchLineEdit);
    layout->setMargin(0);
    fragDialog.setWindowTitle("Show Merged Callstacks");
    fragDialog.resize(900, 400);
    fragDialog.setMinimumSize(900, 400);
    fragDialog.exec();
}

void MainWindow::on_actionShow_Callstacks_In_TreeMap_triggered() {
    QList<QTreeWidgetItem*> topLevelItems;
    GetMergedCallstacks(topLevelItems);
    if (topLevelItems.size() == 0)
        return;
    QDialog fragDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout(&fragDialog);
    layout->setSpacing(2);
    fragDialog.setLayout(layout);
    auto treeMap = new TreeMapGraphicsView(topLevelItems);
    treeMap->Generate(nullptr, QRectF(0, 0, 1024, 512), 10);
    layout->addWidget(treeMap);
    layout->setMargin(0);
    fragDialog.setWindowTitle("Show Callstacks TreeMap");
    fragDialog.resize(1024, 512);
    fragDialog.setMinimumSize(1024, 512);
    fragDialog.exec();
}

void MainWindow::on_actionAbout_triggered() {
    QMessageBox::about(this, "About Loli Profiler", "Copyright 2020 Tencent.");
}

void MainWindow::on_launchPushButton_clicked() {
    if (isConnected_) {
        // create a temporary smaps file for apk because apk doesn't have the permission to create new file
        PushEmptySMapsFile();
        auto type = static_cast<quint8>(loliCommands::SMAPS_DUMP);
        // this will trigger StopCaptureProcess()
        stacktraceProcess_->Send(reinterpret_cast<const char*>(&type), 1);
        smapsTimer_->start();
        ui->launchPushButton->setEnabled(false);
        return;
    }

    auto adbPath = PathUtils::GetADBExecutablePath();
    if (adbPath.isEmpty() || !QFile::exists(adbPath)) {
        QMessageBox::warning(this, "Warning", ANDROID_SDK_NOTFOUND_MSG);
        return;
    }
    auto pythonPath = PathUtils::GetPythonExecutablePath();
    if (pythonPath.isEmpty() || !QFile::exists(pythonPath)) {
        QMessageBox::warning(this, "Warning", ANDROID_NDK_NOTFOUND_MSG);
        return;
    }

    auto launchModeInfo = QMessageBox::information(this, "Launch Mode",
                                         "Launch new instance or inject running app?", "Launch", "Inject", "Cancel", 0, 2);
    bool enableInject = launchModeInfo == 1;
    if (launchModeInfo == 2) {
        return;
    }

    auto enableCacheInfo = QMessageBox::information(
                this, "Enable Data Optimization?", STRIP_NON_PERSISTENT_MSG, "Yes", "No", "Cancel", 0, 2);
    useCache_ = enableCacheInfo == 0;
    if (enableCacheInfo == 2) {
        return;
    }

    // make sure config file exists
    ConfigDialog::ParseConfigFile();

    progressDialog_->setWindowTitle("Launch Progress");
    progressDialog_->setLabelText("Preparing ...");
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(7);
    progressDialog_->setValue(0);
    progressDialog_->show();

    HideToolTips();

    libraries_.clear();
    filteredStacktraceModel_->clear();
    stacktraceModel_->clear();
    sMapsSections_.clear();
    SwitchStackTraceModel(stacktraceProxyModel_);
    ResetFilters();
    while (ui->libraryComboBox->count() > 1)
        ui->libraryComboBox->removeItem(ui->libraryComboBox->count() - 1);
    ui->recordCountLineEdit->setText("");
    ui->appNameLineEdit->setEnabled(false);
    ui->launchPushButton->setText("Stop Capture");
    ui->configPushButton->setEnabled(false);
    ui->symbloPushButton->setEnabled(false);
    ui->toolBar->setEnabled(false);
    ui->menuFile->setEnabled(false);
    ui->menuTools->setEnabled(false);
    ui->statusBar->clearMessage();

    ui->stackTableView->setSortingEnabled(false);
    for (auto& series : memInfoSeries_)
        series->clear();
    screenshots_.clear();
    symbloMap_.clear();
    recordsCache_.clear();
    freeAddrMap_.clear();
    callStackMap_.clear();
    callStackModel_->clear();
    HashString::hashmap_.clear();

    maxMemInfoValue_ = 128;
    UpdateMemInfoRange();

    if (useCache_) { // clear cache folder
        auto cachePath = QApplication::applicationDirPath() + "/cache";
        auto cacheDir = QDir(cachePath);
        if (cacheDir.exists()) {
            auto files = cacheDir.entryList(QDir::Filter::Files);
            for (auto fileName : files) {
                cacheDir.remove(fileName);
            }
        }
    }

    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath);
    startAppProcess_->StartApp(ui->appNameLineEdit->text(), targetCompiler_, targetArch_, enableInject, progressDialog_);

    isCapturing_ = true;
    Print("Starting application ...");
}

void MainWindow::on_chartScaleHSlider_valueChanged(int value) {
    memInfoChartView_->SetRangeScale(value);
    HideToolTips();
}

void MainWindow::on_symbloPushButton_clicked() {
    auto symbloPath = QFileDialog::getOpenFileName(this, tr("Select Symblo File"),
                                                   GetLastSymbolDir(), tr("Library Files (*.sym *.sym.so *.so)"));
    if (!QFile::exists(symbloPath))
        return;
    lastSymbolDir_ = QFileInfo(symbloPath).dir().absolutePath();
    auto nmPath = PathUtils::GetNDKToolPath("nm", targetArch_ != "arm64-v8a");
    if (nmPath.isEmpty() || !QFile::exists(nmPath)) {
        QMessageBox::warning(this, "Warning", ANDROID_NDK_NOTFOUND_MSG);
        return;
    }
    QFileInfo info(symbloPath);
    auto soName = info.baseName() + ".so";
    auto it = symbloMap_.find(soName);
    if (it == symbloMap_.end())
        return;
    QElapsedTimer timer;
    timer.start();

    progressDialog_->setWindowTitle("Symbol Load Progress");
    progressDialog_->setLabelText("Dumping symbol map from so library.");
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(3);
    progressDialog_->setValue(0);
    progressDialog_->show();

    QString symbolMapFilePath = symbloPath + ".txt";
    {
//        TimerProfiler profile("NM");
        QProcess nmProcess;
        // -n, --numeric-sort     Sort symbols numerically by address
        // -C, --demangle
        // -S, --print-size       Print size of defined symbols
        AdbProcess::SetArguments(&nmProcess, QStringList() << "-nCS" << symbloPath);
        nmProcess.setStandardOutputFile(symbolMapFilePath);
        nmProcess.setProgram(nmPath);
        nmProcess.start();
        if (!nmProcess.waitForStarted()) {
            progressDialog_->hide();
            QMessageBox::warning(this, "Warning", "ERROR starting nm process!");
            return;
        }
        while (!nmProcess.waitForFinished(3000)) {
            QCoreApplication::instance()->sendPostedEvents();
        }
    }

    progressDialog_->setValue(1);
    progressDialog_->setLabelText("Reading & Parsing symbol map.");
    QCoreApplication::instance()->sendPostedEvents();

    struct SymbolRecord {
        QString name;
        quint64 addr;
        quint32 size;
    };
    QVector<SymbolRecord> sortedRecords;

    {
//        TimerProfiler profile("Load Symbol");
        QFile symbolMapFile(symbolMapFilePath);
        if (!symbolMapFile.open(QFile::OpenModeFlag::ReadOnly)) {
            progressDialog_->hide();
            QMessageBox::warning(this, "Warning", QString("ERROR reading file: %1").arg(symbolMapFilePath));
            return;
        }
        QRegExp recordRx("([0-9a-z]+)\\s([0-9a-z]+)\\s(\\w)\\s(.+)");
        QTextStream in(&symbolMapFile);
        while (!in.atEnd()) {
           QString line = in.readLine();
           if (recordRx.indexIn(line) < 0)
               continue;
           SymbolRecord record;
           record.name = recordRx.cap(4);
           record.size = recordRx.cap(2).toUInt(nullptr, 16);
           record.addr = recordRx.cap(1).toULong(nullptr, 16);
           sortedRecords.push_back(record);
        }
        symbolMapFile.close();
    }

    progressDialog_->setValue(2);
    progressDialog_->setLabelText(QString("Loaded %1 symbols, translating ....").arg(sortedRecords.size()));
    QCoreApplication::instance()->sendPostedEvents();

    if (sortedRecords.size() > 0) {
//        TimerProfiler profile("Translate Symbol");
        auto& addrMap = it.value();
        auto addrMapIt = addrMap.begin();
        for (; addrMapIt != addrMap.end(); ++addrMapIt) {
            if (addrMapIt.value().size() != 0)
                continue;
            auto addr = addrMapIt.key();
            int left = 0;
            int right = sortedRecords.size() - 1;
            while (left != right) {
                auto mid = static_cast<int>(std::ceil(static_cast<float>(left + right) / 2));
                auto& midRecord = sortedRecords[mid];
                if (addr >= midRecord.addr && addr <= midRecord.addr + midRecord.size) {
                    addrMap[addrMapIt.key()] = midRecord.name;
                    break;
                } else if (midRecord.addr + midRecord.size > addr) {
                    right = mid - 1;
                } else {
                    left = mid;
                }
            }
        }
        auto selectedIndexes = ui->stackTableView->selectionModel()->selection().indexes();
        if (selectedIndexes.size() > 0)
            ShowCallStack(selectedIndexes.front());
    } else {
        Print("Symbols not found, make sure this so has symbols!");
    }

    progressDialog_->setValue(3);
    progressDialog_->hide();
    Print(QString("Symbols loaded in %1 seconds.").arg(timer.elapsed() / 1000));
}

void MainWindow::on_configPushButton_clicked() {
    QSettings settings("MoreFun", "LoliProfiler");
    targetArch_ = settings.value(SETTINGS_ARCH).toString();
    targetCompiler_ = settings.value(SETTINGS_COMPILER).toString();
    ConfigDialog dialog(this);
    dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.LoadConfigFile(targetArch_, targetCompiler_);
    connect(&dialog, &QDialog::finished, [this, &dialog, &settings](int) {
        targetArch_ = dialog.GetArchString();
        targetCompiler_ = dialog.GetCompilerString();
        settings.setValue(SETTINGS_ARCH, targetArch_);
        settings.setValue(SETTINGS_COMPILER, targetCompiler_);
    });
    dialog.exec();
}

void MainWindow::on_selectAppToolButton_clicked() {
    auto adbPath = PathUtils::GetADBExecutablePath();
    if (adbPath.isEmpty() || !QFile::exists(adbPath)) {
        QMessageBox::warning(this, "Warning", ANDROID_SDK_NOTFOUND_MSG);
        return;
    }

    QProcess process;
    process.setProgram(adbPath);
    AdbProcess::SetArguments(&process, QStringList() << "shell" << "pm" << "list" << "packages");
    process.start();
    if (!process.waitForStarted()) {
        Print("error start adb shell pm list packages, make sure your device is connected!");
        return;
    }
    if (!process.waitForFinished()) {
        Print("error finishing adb shell pm list packages!");
        return;
    }
    auto pkgStrs = QString(process.readAll());
    auto lines = pkgStrs.split('\n', QString::SkipEmptyParts);
    if (lines.count() == 0) {
        QMessageBox::warning(this, "Warning", "No device is connected!");
        return;
    }

    SelectAppDialog selectAppDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    selectAppDialog.SelectApp(lines, ui->appNameLineEdit);
    selectAppDialog.exec();
}

void MainWindow::on_memSizeComboBox_currentIndexChanged(int) {
    FilterStackTraceModel();
    ShowSummary();
}

void MainWindow::on_libraryComboBox_currentIndexChanged(int) {
    FilterStackTraceModel();
    ShowSummary();
}

void MainWindow::on_allocComboBox_currentIndexChanged(int) {
    FilterStackTraceModel();
    ShowSummary();
}

void MainWindow::on_actionExport_To_Text_triggered() {
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("Save Text File"),
                                                    GetLastOpenDir(), tr("Text files (*.txt)"));
    if (fileName.isEmpty())
        return;
    if (!fileName.endsWith("txt", Qt::CaseInsensitive))
        fileName += ".txt";
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::warning(this, "Warning", "Can't create file!", QMessageBox::StandardButton::Ok);
        return;
    }
    bool optimal = QMessageBox::information(this, "Export Option",
                             "Export smaller text file?",
                             QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No) == QMessageBox::StandardButton::Yes;
    ExportToText(&tempFile, optimal);
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
