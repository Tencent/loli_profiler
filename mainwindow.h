#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include "screenshotprocess.h"
#include "stacktraceprocess.h"
#include "addressprocess.h"
#include "startappprocess.h"
#include "fixedscrollarea.h"
#include "interactivechartview.h"

namespace Ui {
class MainWindow;
}

enum class IOErrorCode : qint32 {
    NONE = 0,
    MAGIC_NUMBER_MISSMATCH,
    VERSION_MISSMATCH,
    CORRUPTED_DATA,
};

class QStandardItemModel;
class QGraphicsPixmapItem;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void LoadSettings();
    void SaveSettings();

    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void Print(const QString& str);
    void SaveToFile(QFile *file);
    IOErrorCode LoadFromFile(QFile *file);
    QString GetLastOpenDir() const;

    void ConnectionFailed();
    void RemoveExistingSwapFiles();

    int GetScreenshotIndex(const QPointF& pos) const;
    void ShowScreenshotAt(int index);

    void HideToolTips();
    void UpdateStackTraceRange();

    QString TryAddNewAddress(const QString& lib, const QString& addr);

private slots:
    void FixedUpdate();

    void OnTimeSelectionChange(const QPointF& pos);
    void OnSyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta);

    void StartAppProcessFinished(AdbProcess* process);
    void StartAppProcessErrorOccurred();
    void ScreenshotProcessFinished(AdbProcess* process);
    void ScreenshotProcessErrorOccurred();
    void StacktraceProcessFinished(AdbProcess* process);
    void StacktraceProcessErrorOccurred();
    void AddressProcessFinished(AdbProcess* process);
    void AddressProcessErrorOccurred();

    void on_actionOpen_triggered();
    void on_sdkPushButton_clicked();
    void on_launchPushButton_clicked();
    void on_chartScaleHSlider_valueChanged(int value);
    void on_stackTreeWidget_itemSelectionChanged();
    void on_symbloPushButton_clicked();
    void on_addr2LinePushButton_clicked();

private:
    Ui::MainWindow *ui;
    QStandardItemModel *callStackModel_;
    QString adbPath_;
    QString appPid_;
    QString lastOpenDir_;
    QTimer* mainTimer_;
    int time_ = 0;

    // adb shell monkey -p packagename -c android.intent.category.LAUNCHER 1
    StartAppProcess *startAppProcess_;

    // screenshot
    ScreenshotProcess *screenshotProcess_;
    QVector<QGraphicsPixmapItem*> screenshotItems_;
    int lastScreenshotTime_ = 0;
    QVector<QPair<int, QByteArray>> screenshots_;

    // stacktrace process
    StackTraceProcess *stacktraceProcess_;
    int lastStackTraceTime_ = 0;
    int maxStackTraceCount_ = 0;

    // address process
    AddressProcess* addrProcess_;
    // <dllname, <func address, func name>>
    QHash<QString, QHash<QString, QString>> symbloMap_;

    // charts
    FixedScrollArea* scrollArea_;

    // stack trace chart
    QMap<QString, QtCharts::QLineSeries*> stackTraceSeries_;
    QtCharts::QValueAxis *stackTraceAxisX_;
    QtCharts::QValueAxis *stackTraceAxisY_;
    QtCharts::QChart *stackTraceChart_;
    InteractiveChartView *stackTraceChartView_;

    bool isConnected_ = false;
};

#endif // MAINWINDOW_H
