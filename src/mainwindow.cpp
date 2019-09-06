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
#include <QTableWidget>
#include <QProgressDialog>
#include <QScrollBar>
#include <QGLWidget>
#include <QStatusBar>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <vector>

#define APP_MAGIC 0xA4B3C2D1
#define APP_VERSION 103

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

    filteredStacktraceModel_ = new StackTraceModel(this);
    filteredStacktraceProxyModel_ = new StackTraceProxyModel(filteredStacktraceModel_, this);
    stacktraceModel_ = new StackTraceModel(this);
    stacktraceProxyModel_ = new StackTraceProxyModel(stacktraceModel_, this);
    SwitchStackTraceModel(stacktraceProxyModel_);
    connect(ui->stackTableView, &QTableView::customContextMenuRequested, this, &MainWindow::OnStackTableViewContextMenu);

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
    mainTimer_->start(1000);
}

MainWindow::~MainWindow() {
    delete ui;
}

const QString SETTINGS_WINDOW_W = "Window_W";
const QString SETTINGS_WINDOW_H = "Window_H";
const QString SETTINGS_APPNAME = "AppName";
const QString SETTINGS_SDKPATH = "SdkPath";
const QString SETTINGS_MAIN_SPLITER = "Main_Spliter";
const QString SETTINGS_UPPER_SPLITER = "Upper_Spliter";
const QString SETTINGS_LOWER__SPLITER = "Lower_Spliter";
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
}

void MainWindow::SaveSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    settings.setValue(SETTINGS_WINDOW_W, this->width());
    settings.setValue(SETTINGS_WINDOW_H, this->height());
    settings.setValue(SETTINGS_APPNAME, ui->appNameLineEdit->text());
    settings.setValue(SETTINGS_SDKPATH, ui->sdkPathLineEdit->text());
    settings.setValue(SETTINGS_ADDR2LINEPATH, ui->addr2LinePathLineEdit->text());
    settings.setValue(SETTINGS_PYTHONPATH, ui->pythonPathLineEdit->text());
    settings.setValue(SETTINGS_MAIN_SPLITER, ui->main_splitter->saveState());
    settings.setValue(SETTINGS_UPPER_SPLITER, ui->upper_splitter->saveState());
    settings.setValue(SETTINGS_LOWER__SPLITER, ui->lower_splitter->saveState());
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
    qint32 count = stacktraceModel_->rowCount();
    stream << count;
    for (int i = 0; i < count; i++) {
        auto& record = stacktraceModel_->recordAt(i);
        stream << record.uuid_.toString();
        stream << static_cast<qint32>(record.time_);
        stream << static_cast<qint32>(record.size_);
        stream << record.addr_;
        stream << record.library_;
        stream << record.funcAddr_;
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
    // callstack tree view
    QSet<QString> libraries;
    filteredStacktraceModel_->clear();
    stacktraceModel_->clear();
    SwitchStackTraceModel(stacktraceProxyModel_);
    stream >> value;
    QVector<StackRecord> records;
    for (int i = 0; i < value; i++) {
        StackRecord record;
        QString str;
        stream >> str;
        record.uuid_ = QUuid::fromString(str);
        qint32 size;
        stream >> size;
        record.time_ = size;
        stream >> size;
        record.size_ = size;
        stream >> str;
        record.addr_ = str;
        stream >> str;
        record.library_ = str;
        if (!libraries.contains(str))
            libraries.insert(str);
        stream >> str;
        record.funcAddr_ = str;
        records.push_back(record);
    }
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
    screenshotItem_->setPos(0, 0);
}

void MainWindow::HideToolTips() {
    memInfoChartView_->HideToolTip();
}

void MainWindow::UpdateMemInfoRange() {
    memInfoAxisY_->setRange(0, maxMemInfoValue_);
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

void MainWindow::ShowCallStack(const QModelIndex& index) {
    if (!index.isValid())
        return;
    auto proxyModel = static_cast<StackTraceProxyModel*>(ui->stackTableView->model());
    auto srcModel = static_cast<StackTraceModel*>(proxyModel->sourceModel());
    auto& selectedRecord = srcModel->recordAt(proxyModel->mapToSource(index).row());
    auto& callStack = callStackMap_[selectedRecord.uuid_];
    for (int i = 0, row = 0; i < callStack.size(); i += 2, row++) {
        const auto& libName = callStack[i];
        const auto& funcAddr = callStack[i + 1];
        const auto& funcName = TryAddNewAddress(libName, funcAddr);
        callStackModel_->setItem(row, 0, new QStandardItem(libName));
        callStackModel_->setItem(row, 1, new QStandardItem(funcName));
    }
}

void MainWindow::ShowSummary() {
    auto model = ui->stackTableView->model();
    auto rowCount = model->rowCount();
    quint32 size = 0;
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
            if (record.library_ != libraryFilter)
                continue;
        }
        if (persistentFilter) {
            auto addr = record.addr_;
            auto time = record.time_;
            auto it = freeAddrMap_.find(addr);
            if (it != freeAddrMap_.end()) {
                if (time < it.value()) {
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
            curSection = &sMapsSections_[strList[5]];
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

bool MainWindow::CreateIfNoConfigFile() {
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    QFile file(cfgPath + "/loli.conf");
    if (!file.exists()) {
        if (!QDir(cfgPath).mkpath(cfgPath)) {
            QMessageBox::warning(this, "Warning", "Can't create application data path: " + cfgPath);
            return false;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << "256\nlibunity,libil2cpp";
            stream.flush();
            return true;
        } else {
            return false;
        }
    }
    return true;
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
        for (int i = 0, row = 0; i < callStack.size(); i += 2, row++) {
            const auto& libName = callStack[i];
            const auto& funcAddr = callStack[i + 1];
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
        QVector<StackRecord> records;
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
            records.push_back(record);
        }
        stacktraceModel_->append(records);
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

class MemoryTableWidgetItem : public QTableWidgetItem {
public:
    MemoryTableWidgetItem(quint32 size) : QTableWidgetItem(), size_(size) { setText(sizeToString(size)); }
    bool operator< (const QTableWidgetItem &other) const { return size_ < static_cast<const MemoryTableWidgetItem&>(other).size_; }
    quint32 size_ = 0;
};

class MemoryTableWidget : public QTableWidget {
public:
    MemoryTableWidget(int rows, int columns, QWidget* parent) : QTableWidget(rows, columns, parent) {}
    void keyPressEvent(QKeyEvent *event) override {
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
};

void MainWindow::on_actionStat_SMaps_triggered() {
    if (sMapsSections_.size() == 0) {
        QMessageBox::warning(this, "Warning", "No smaps data found!", QMessageBox::StandardButton::Ok);
        return;
    }
    QDialog fragDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout(&fragDialog);
    fragDialog.setLayout(layout);
    auto tableWidget = new MemoryTableWidget(sMapsSections_.size(), 8, &fragDialog);
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
    auto statusBar = new QStatusBar(&fragDialog);
    statusBar->showMessage(QString("VM: %1, Rss: %2, Pss: %3, PC: %4, PD: %5, SC: %6, SD: %7")
                             .arg(sizeToString(total.virtual_ * 1024), sizeToString(total.rss_ * 1024), sizeToString(total.pss_ * 1024), sizeToString(total.privateClean_ * 1024),
                                  sizeToString(total.privateDirty_ * 1024), sizeToString(total.sharedClean_ * 1024), sizeToString(total.sharedDirty_ * 1024)));
    layout->addWidget(tableWidget);
    layout->addWidget(statusBar);
    layout->setMargin(0);
    fragDialog.setWindowTitle("Stat proc/pid/smaps");
    fragDialog.resize(900, 400);
    fragDialog.setMinimumSize(900, 400);
    fragDialog.exec();
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
    QDialog fragDialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout(&fragDialog);
    layout->setSpacing(2);
    fragDialog.setLayout(layout);
    auto statusBar = new QStatusBar(&fragDialog);
    auto fragView = new CustomGraphicsView(&fragDialog);
    auto fragScene = new QGraphicsScene(&fragDialog);
    auto sectionComboBox = new QComboBox(&fragDialog);
    auto curModel = ui->stackTableView->model();
    connect(sectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int index){
        fragScene->clear();
        auto sectionit = sMapsSections_.find(sectionComboBox->itemText(index));
        if (sectionit == sMapsSections_.end()) {
            return;
        }
        QPen pen(QBrush(), 0.0);
        QBrush allocBrush(QColor::fromRgb(200, 50, 50, 255));
        QBrush bgBrush(QColor::fromRgb(50, 200, 50, 255));
        auto& section = *sectionit;
        auto sectionsCount = section.addrs_.size();
        auto recordsCount = 0;
        auto totalUsedSize = 0ul;
        auto totalSize = 0ul;
        for (int i = 0; i < sectionsCount; i++) {
            auto& addr = section.addrs_[i];
            auto offset = addr.offset_;
            auto start = addr.start_ - offset;
            auto end = addr.end_ - offset;
            auto size = end - start;
            auto sizeInKb = static_cast<double>(size) / 1024;
            auto y = i * 35;
            auto height = 30;
            auto freeRect = fragScene->addRect(0, y, sizeInKb, height, pen, bgBrush);
            auto rowCount = curModel->rowCount();
            auto usedSize = 0ul;
            for (int i = 0; i < rowCount; i++) {
                auto recSize = curModel->data(curModel->index(i, 1), Qt::UserRole).toUInt();
                auto recAddr = curModel->data(curModel->index(i, 2)).toString().toULongLong(nullptr, 0);
                if (recAddr >= start && recAddr < end) {
                    auto recAddrInKb = (recAddr - start) / 1024;
                    auto recSizeInKb = recSize / 1024;
                    fragScene->addRect(recAddrInKb, y, recSizeInKb, height, pen, allocBrush)->setToolTip(QString("Size: %1").arg(sizeToString(recSize)));
                    recordsCount++;
                    usedSize += recSize;
                }
            }
            freeRect->setToolTip(QString("Total: %1 Used: %2 (%3%)")
                                 .arg(sizeToString(static_cast<quint32>(size)), sizeToString(usedSize), QString::number((static_cast<double>(usedSize) / size) * 100.0)));
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
    auto sectionNames = visibleSections.toList();
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
    fragDialog.setWindowTitle("Visualize proc/pid/smaps");
    fragDialog.resize(900, 400);
    fragDialog.setMinimumSize(900, 400);
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
    adbPath_ = ui->sdkPathLineEdit->text();
    adbPath_ = adbPath_.size() == 0 ? "adb" : adbPath_ + "/platform-tools/adb";

    if (isConnected_) {
        progressDialog_->setWindowTitle("Stop Capture Progress");
        progressDialog_->setLabelText(QString("Stopping capture ..."));
        progressDialog_->setMinimum(0);
        progressDialog_->setMaximum(2);
        progressDialog_->setValue(0);
        progressDialog_->show();
        Print(QString("Captured %1 records.").arg(stacktraceModel_->rowCount()));
        ConnectionFailed();
        for (auto& library : libraries_)
            ui->libraryComboBox->addItem(library);
        FilterStackTraceModel();
        ShowSummary();
        progressDialog_->setValue(1);
        progressDialog_->setLabelText("Requesting smaps info from device.");
        QProcess process;
        process.start(adbPath_ + " shell run-as " + ui->appNameLineEdit->text() +
                      " \"cat /proc/" + memInfoProcess_->GetAppPid() + "/smaps > /data/local/tmp/smaps.txt\"");
        auto readSMaps = false;
        if (process.waitForStarted()) {
            if (process.waitForFinished()) {
                if (process.readAll().size() == 0) {
                    process.close();
                    auto smapsPath = QCoreApplication::applicationDirPath() + "/smaps.txt";
                    process.start(adbPath_ + " pull /data/local/tmp/smaps.txt " + smapsPath);
                    process.waitForStarted();
                    process.waitForFinished();
                    QFile file(smapsPath);
                    if (file.exists()) {
                        if (file.open(QFile::OpenModeFlag::ReadOnly)) {
                            ReadSMapsFile(&file);
                            readSMaps = true;
                        }
                        file.remove();
                    }
                }
            }
        }
        if (!readSMaps) {
            Print("Failed to cat proc/pid/smaps");
        }
        progressDialog_->setValue(2);
        return;
    }

    auto pythonPath = ui->pythonPathLineEdit->text();
    if (!QFile::exists(pythonPath)) {
        QMessageBox::warning(this, "Warning", "Please select python path first.");
        return;
    }

    if (!CreateIfNoConfigFile())
        return;

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
    ui->memSizeComboBox->setCurrentIndex(0);
    ui->allocComboBox->setCurrentIndex(0);
    ui->libraryComboBox->setCurrentIndex(0);
    while (ui->libraryComboBox->count() > 1)
        ui->libraryComboBox->removeItem(ui->libraryComboBox->count() - 1);
    ui->sdkPushButton->setEnabled(true);
    ui->appNameLineEdit->setEnabled(false);
    ui->launchPushButton->setText("Stop Capture");
    ui->actionOpen->setEnabled(false);
    ui->statusBar->clearMessage();

    ui->stackTableView->setSortingEnabled(false);
    for (auto& series : memInfoSeries_)
        series->clear();
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
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    if (!CreateIfNoConfigFile())
        return;
    QFile file(cfgPath + "/loli.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
        stream.flush();
    }
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
