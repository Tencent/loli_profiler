#include "mainwindow.h"
#include "ui_mainwindow.h"

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

#include <algorithm>

using namespace QtCharts;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // setup adb process
    startAppProcess_ = new StartAppProcess(this);
    connect(startAppProcess_, &StartAppProcess::ProcessFinished, this, &MainWindow::StartAppProcessFinished);
    connect(startAppProcess_, &StartAppProcess::ProcessErrorOccurred, this, &MainWindow::StartAppProcessErrorOccurred);

    screenshotProcess_ = new ScreenshotProcess(this);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessFinished, this, &MainWindow::ScreenshotProcessFinished);
    connect(screenshotProcess_, &ScreenshotProcess::ProcessErrorOccurred, this, &MainWindow::ScreenshotProcessErrorOccurred);

    stacktraceProcess_ = new StackTraceProcess(this);
    connect(stacktraceProcess_, &StackTraceProcess::ProcessFinished, this, &MainWindow::StacktraceProcessFinished);
    connect(stacktraceProcess_, &StackTraceProcess::ProcessErrorOccurred, this, &MainWindow::StacktraceProcessErrorOccurred);

    addrProcess_ = new AddressProcess(this);
    connect(addrProcess_, &AddressProcess::ProcessFinished, this, &MainWindow::AddressProcessFinished);
    connect(addrProcess_, &AddressProcess::ProcessErrorOccurred, this, &MainWindow::AddressProcessErrorOccurred);

    // setup screenshot view
    ui->screenshotGraphicsView->setScene(new QGraphicsScene());
    for (int i = 0; i < 5; i++) {
        auto item = new QGraphicsPixmapItem();
        screenshotItems_.push_back(item);
        ui->screenshotGraphicsView->scene()->addItem(item);
    }

    // setup stacktrace chart
    stackTraceChart_ = new QChart();
    stackTraceChart_->setTitle("memory io calls");
    stackTraceChart_->legend()->show();
    stackTraceChart_->legend()->setAlignment(Qt::AlignBottom);

    stackTraceAxisX_ = new QValueAxis();
    stackTraceAxisX_->setLabelFormat("%i");
    stackTraceAxisX_->setTickCount(11);
    stackTraceAxisX_->setRange(0, 100);
    stackTraceChart_->addAxis(stackTraceAxisX_, Qt::AlignBottom);

    stackTraceAxisY_ = new QValueAxis();
    stackTraceAxisY_->setLabelFormat("%i");
    stackTraceAxisY_->setRange(0, 1024);
    stackTraceChart_->addAxis(stackTraceAxisY_, Qt::AlignLeft);

    stackTraceChartView_ = new InteractiveChartView(stackTraceChart_);
    stackTraceChartView_->setRenderHint(QPainter::Antialiasing);
    stackTraceChartView_->setContentsMargins(0, 0, 0, 0);
    stackTraceChartView_->setFixedHeight(250);

    freeSeries_ = new QLineSeries();
    freeSeries_->setName("loliFree");
    stackTraceSeries_.insert("loliFree", freeSeries_);
    stackTraceChart_->addSeries(freeSeries_);
    freeSeries_->attachAxis(stackTraceAxisX_);
    freeSeries_->attachAxis(stackTraceAxisY_);

    connect(stackTraceChartView_, &InteractiveChartView::OnSyncScroll, this, &MainWindow::OnSyncScroll);
    connect(stackTraceChartView_, &InteractiveChartView::OnSelectionChange, this, &MainWindow::OnTimeSelectionChange);

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
    scrollArea_->widget()->layout()->addWidget(stackTraceChartView_);
    scrollArea_->widget()->setFixedSize(100, 250);
    connect(scrollArea_, &FixedScrollArea::ScaleTriggered, [this](int delta) {
        ui->chartScaleHSlider->setValue(
                    ui->chartScaleHSlider->value() + (delta > 0 ? 1 : -1) * ui->chartScaleHSlider->singleStep());
    });

    ui->chartWidget->layout()->setContentsMargins(0, 0, 0, 0);
    ui->chartWidget->layout()->setSpacing(0);
    ui->chartWidget->layout()->addWidget(scrollArea_);

    callStackModel_ = new QStandardItemModel();
    callStackModel_->setHorizontalHeaderLabels(QStringList() << "Function" << "Library");
    ui->callStackTableView->setModel(callStackModel_);
    ui->callStackTableView->horizontalHeader()->setStretchLastSection(true);
    ui->callStackTableView->verticalHeader()->setVisible(false);
    ui->callStackTableView->show();

    LoadSettings();

    connect(ui->stackTreeWidget, &QTreeWidget::customContextMenuRequested, this, &MainWindow::OnStackTreeWidgetContextMenu);

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
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
const QString SETTINGS_ADDR2LINEPATH = "addr2line_path";

void MainWindow::LoadSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    int windowWidth = settings.value(SETTINGS_WINDOW_W).toInt();
    int windowHeight = settings.value(SETTINGS_WINDOW_H).toInt();
    if (windowWidth > 0 && windowHeight > 0) {
        this->resize(windowWidth, windowHeight);
    }
    ui->appNameLineEdit->setText(settings.value(SETTINGS_APPNAME).toString());
    ui->sdkpathLineEdit->setText(settings.value(SETTINGS_SDKPATH).toString());
    ui->addr2LinePathLineEdit->setText(settings.value(SETTINGS_ADDR2LINEPATH).toString());
    ui->splitter->restoreState(settings.value(SETTINGS_SPLITER).toByteArray());
    ui->splitter_2->restoreState(settings.value(SETTINGS_SPLITER_2).toByteArray());
    ui->splitter_3->restoreState(settings.value(SETTINGS_SPLITER_3).toByteArray());
    ui->chartScaleHSlider->setValue(settings.value(SETTINGS_SCALEHSLIDER, 10).toInt());
    auto lastOpenDir = settings.value(SETTINGS_LASTOPENDIR).toString();
    if (QDir(lastOpenDir).exists())
        lastOpenDir_ = lastOpenDir;
}

void MainWindow::SaveSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    settings.setValue(SETTINGS_WINDOW_W, this->width());
    settings.setValue(SETTINGS_WINDOW_H, this->height());
    settings.setValue(SETTINGS_APPNAME, ui->appNameLineEdit->text());
    settings.setValue(SETTINGS_SDKPATH, ui->sdkpathLineEdit->text());
    settings.setValue(SETTINGS_ADDR2LINEPATH, ui->addr2LinePathLineEdit->text());
    settings.setValue(SETTINGS_SPLITER, ui->splitter->saveState());
    settings.setValue(SETTINGS_SPLITER_2, ui->splitter_2->saveState());
    settings.setValue(SETTINGS_SPLITER_3, ui->splitter_3->saveState());
    settings.setValue(SETTINGS_SCALEHSLIDER, ui->chartScaleHSlider->value());
    if (QDir(lastOpenDir_).exists())
        settings.setValue(SETTINGS_LASTOPENDIR, lastOpenDir_);
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

void MainWindow::SaveToFile(QFile *) {

}

class SortableTreeWidgetItem : public QTreeWidgetItem {
public:
    SortableTreeWidgetItem(QTreeWidget* parent) : QTreeWidgetItem(parent){}
private:
    bool operator<(const QTreeWidgetItem &other) const {
        int column = treeWidget()->sortColumn();
        return data(column, 0).toInt() < other.data(column, 0).toInt();
    }
};

IOErrorCode MainWindow::LoadFromFile(QFile *) {

    return IOErrorCode::NONE;
}

QString MainWindow::GetLastOpenDir() const {
    return QDir(lastOpenDir_).exists() ? lastOpenDir_ : QDir::homePath();
}

void MainWindow::ConnectionFailed() {
    isConnected_ = false;
    mainTimer_->stop();
    if (screenshotProcess_->IsRunning())
        screenshotProcess_->Process()->kill();
    if (stacktraceProcess_->IsRunning())
        stacktraceProcess_->Process()->kill();
    ui->appNameLineEdit->setEnabled(true);
    ui->launchPushButton->setText("Connect");
    ui->sdkPushButton->setEnabled(true);
    ui->actionOpen->setEnabled(true);
}

void MainWindow::RemoveExistingSwapFiles() {
    QProcess process;
    QStringList arguments;
    arguments << "shell" << "rm" << "-f" << "/storage/emulated/0/Android/data/" + ui->appNameLineEdit->text() + "/files/loli.csv";
    process.start(adbPath_, arguments);
    if (!process.waitForStarted())
        return;
    if (!process.waitForFinished())
        return;
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
    stackTraceChartView_->HideToolTip();
}

void MainWindow::UpdateStackTraceRange() {
    auto maxValue = std::max(maxStackTraceCount_, std::max(64, static_cast<int>(maxStackTraceCount_ * 1.2f)));
    stackTraceAxisY_->setRange(0, maxValue);
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
    if (time_ - lastStackTraceTime_ >= 10 && !stacktraceProcess_->IsRunning()) {
        lastStackTraceTime_ = time_;
        stacktraceProcess_->DumpAsync(ui->appNameLineEdit->text());
    }
    if (time_ - lastScreenshotTime_ >= 5 && !screenshotProcess_->IsRunning()) {
        lastScreenshotTime_ = time_;
        screenshotProcess_->CaptureScreenshot();
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
    stackTraceChartView_->SyncScroll(sender, prevMouseX, delta);
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
                child->setExpanded(true);
                child = child->childCount() > 0 ? child->child(0) : nullptr;
            }
        }
        stream.flush();
        QApplication::clipboard()->setText(output);
    });
    menu.exec(ui->stackTreeWidget->mapToGlobal(pos));
}

void MainWindow::StartAppProcessFinished(AdbProcess* process) {
    auto startAppProcess = static_cast<StartAppProcess*>(process);
    if (!startAppProcess->Result()) {
        ConnectionFailed();
        Print("Error starting app by adb monkey");
        return;
    }
    isConnected_ = true;
    screenshotProcess_->SetExecutablePath(adbPath_);
    stacktraceProcess_->SetExecutablePath(adbPath_);
    lastScreenshotTime_ = time_;
    lastStackTraceTime_ = time_ + 5;
    mainTimer_->start(1000);
    Print("Connected!");
}

void MainWindow::StartAppProcessErrorOccurred() {
    ConnectionFailed();
    Print("Error starting app by adb monkey!");
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

void MainWindow::StacktraceProcessFinished(AdbProcess* process) {
    auto stacktraceProcess = static_cast<StackTraceProcess*>(process);
    const auto& stacks = stacktraceProcess->GetStackInfo();
    if (stacks.size() > 0) {
        QHash<int, QHash<QString, int>> funcMap;
        for (const auto& stack : stacks) {
            if (stack.size() < 3)
                continue;
            auto root = stack[0].split('|');
            if (root.size() < 3)
                continue;
            const auto& rootTime = root[0];
            const auto& rootSize = root[1];
            const auto& rootMem = root[2];
            const auto& rootLib = stack[1];
            const auto& rootAddr = TryAddNewAddress(rootLib, stack[2]);
            persistentAddrs_.insert(rootMem);
            QTreeWidgetItem* parentItem = new SortableTreeWidgetItem(ui->stackTreeWidget);
            parentItem->setText(0, rootTime);
            parentItem->setData(0, 0, QVariant(rootTime.toInt()));
            parentItem->setText(1, rootSize);
            parentItem->setData(1, 0, QVariant(rootSize.toInt()));
            parentItem->setText(2, rootMem);
            parentItem->setData(2, 0, QVariant(rootMem));
            parentItem->setText(3, rootLib);
            parentItem->setText(4, rootAddr);
            auto hide = GetTreeWidgetItemShouldHide(parentItem);
            if (parentItem->isHidden() != hide)
                parentItem->setHidden(hide);
            auto castedTime = static_cast<int>(std::ceil(rootTime.toInt() * 0.001f));
            if (!funcMap.contains(castedTime))
                funcMap.insert(castedTime, QHash<QString, int>());
            auto& timeFuncMap = funcMap[castedTime];
            if (!timeFuncMap.contains(rootAddr))
                timeFuncMap.insert(rootAddr, 1);
            else
                timeFuncMap[rootAddr]++;
            for (int i = 3; i < stack.size() && i + 1 < stack.size(); i += 2) {
                const auto& libName = stack[i];
                const auto& funcName = TryAddNewAddress(libName, stack[i + 1]);
                auto curItem = new QTreeWidgetItem();
                curItem->setText(3, libName);
                curItem->setText(4, funcName);
                parentItem->addChild(curItem);
                parentItem = curItem;
            }
        }
        for (auto it = funcMap.constBegin(); it != funcMap.constEnd(); ++it) {
            const auto& time = it.key();
            const auto& timeFuncMap = it.value();
            for (auto funcIt = timeFuncMap.begin(); funcIt != timeFuncMap.end(); ++funcIt) {
                const auto& funcName = funcIt.key();
                const auto& count = funcIt.value();
                if (!stackTraceSeries_.contains(funcName)) {
                    auto series = new QLineSeries();
                    series->setName(funcName);
                    stackTraceSeries_.insert(funcName, series);
                    stackTraceChart_->addSeries(series);
                    series->attachAxis(stackTraceAxisX_);
                    series->attachAxis(stackTraceAxisY_);
                }
                if (count > maxStackTraceCount_)
                    maxStackTraceCount_ = count;
                auto& series = stackTraceSeries_[funcName];
                series->append(time, static_cast<double>(count));
            }
        }
    }
    // read free call infos
    const auto& frees = stacktraceProcess_->GetFreeInfo();
    if (frees.size() > 0) {
        QMap<int, int> funcMap;
        for (const auto& free : frees) {
            auto time = free.first;
            const auto address = free.second;
            persistentAddrs_.remove(address);
            auto castedTime = static_cast<int>(std::ceil(time * 0.001f));
            if (!funcMap.contains(castedTime))
                funcMap[castedTime] = 1;
            else
                funcMap[castedTime]++;
        }
        for (auto it = funcMap.constBegin(); it != funcMap.constEnd(); ++it) {
            auto time = it.key();
            auto count = it.value();
            if (count > maxStackTraceCount_)
                maxStackTraceCount_ = count;
            freeSeries_->append(time, count);
        }
    }
    UpdateStackTraceRange();
}

void MainWindow::StacktraceProcessErrorOccurred() {

}

void MainWindow::AddressProcessFinished(AdbProcess* process) {
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
    Print("Error occurred when reading address info ...");
}

void MainWindow::on_actionOpen_triggered() {
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("Open Profiler File"),
                                                    GetLastOpenDir(), tr("Loli Profiler Files (*.csv)"));
    if (QFileInfo::exists(fileName)) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            lastOpenDir_ = QFileInfo(fileName).dir().absolutePath();
            auto ecode = LoadFromFile(&file);
            if (ecode != IOErrorCode::NONE) {
                QMessageBox::warning(this, "Warning", QString("Error reading file, ecode %1").arg(static_cast<int>(ecode)),
                                     QMessageBox::StandardButton::Ok);
            }
        }
        else {
            QMessageBox::warning(this, "Warning", "File not found!", QMessageBox::StandardButton::Ok);
        }
    }
}

void MainWindow::on_sdkPushButton_clicked() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Directory"), "",
                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    ui->sdkpathLineEdit->setText(path);
}

void MainWindow::on_launchPushButton_clicked() {
    if (isConnected_) {
        Print("Stoping capture ...");
        ConnectionFailed();
        return;
    }

    ui->sdkPushButton->setEnabled(true);

    ui->appNameLineEdit->setEnabled(false);
    ui->launchPushButton->setText("Stop Capture");
    ui->actionOpen->setEnabled(false);
    ui->statusBar->clearMessage();

    adbPath_ = ui->sdkpathLineEdit->text();
    adbPath_ = adbPath_.size() == 0 ? "adb" : adbPath_ + "/platform-tools/adb";

    ui->stackTreeWidget->clear();
    screenshots_.clear();
    symbloMap_.clear();

    RemoveExistingSwapFiles();

    startAppProcess_->SetExecutablePath(adbPath_);
    startAppProcess_->StartApp(ui->appNameLineEdit->text());

    Print("Starting application ...");
}

void MainWindow::on_chartScaleHSlider_valueChanged(int value) {
    stackTraceChartView_->SetRangeScale(value);
    HideToolTips();
}

void MainWindow::on_stackTreeWidget_itemSelectionChanged() {
    callStackModel_->clear();
    callStackModel_->setHorizontalHeaderLabels(QStringList() << "Function" << "Library");
    auto selectedItems = ui->stackTreeWidget->selectedItems();
    if (selectedItems.size() == 0)
        return;
    auto selected = selectedItems.front();
    auto row = 0;
    while (selected != nullptr) {
        callStackModel_->setItem(row, 0, new QStandardItem(selected->text(4)));
        callStackModel_->setItem(row, 1, new QStandardItem(selected->text(3)));
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
                                                   QDir::homePath(), tr("Library Files (*.sym *.sym.so *.so)"));
    if (!QFile::exists(symbloPath))
        return;
    auto addr2linePath = ui->addr2LinePathLineEdit->text();
    QFile file(addr2linePath);
    if (!file.exists())
        return;
    QFileInfo info(symbloPath);
    auto soName = info.baseName() + ".so";
    auto it = symbloMap_.find(soName);
    if (it == symbloMap_.end())
        return;
    addrProcess_->SetExecutablePath(addr2linePath);
    addrProcess_->DumpAsync(symbloPath, &it.value());
}

void MainWindow::on_addr2LinePushButton_clicked() {
    auto path = QFileDialog::getOpenFileName(this, tr("Select Executable Addr2line"), QDir::homePath());
    ui->addr2LinePathLineEdit->setText(path);
}

void MainWindow::on_modeComboBox_currentIndexChanged(int) {
    on_memSizeComboBox_currentIndexChanged(ui->memSizeComboBox->currentIndex());
}

void MainWindow::on_memSizeComboBox_currentIndexChanged(int) {
    for (int i = 0; i < ui->stackTreeWidget->topLevelItemCount(); i++) {
        auto item = ui->stackTreeWidget->topLevelItem(i);
        auto hide = GetTreeWidgetItemShouldHide(item);
        if (item->isHidden() != hide)
            item->setHidden(hide);
    }
}

bool MainWindow::GetTreeWidgetItemShouldHide(QTreeWidgetItem* item) const {
    auto checkPersistent = ui->modeComboBox->currentIndex() == 1;
    auto addr = item->data(2, 0).toString();
    auto size = item->data(1, 0).toInt();
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
    if (!hide && checkPersistent) {
        hide = !persistentAddrs_.contains(addr);
    }
    return hide;
}
