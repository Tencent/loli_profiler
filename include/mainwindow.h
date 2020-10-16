#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QUuid>

#include "screenshotprocess.h"
#include "meminfoprocess.h"
#include "stacktraceprocess.h"
#include "stacktracemodel.h"
#include "stacktraceproxymodel.h"
#include "addressprocess.h"
#include "startappprocess.h"
#include "fixedscrollarea.h"
#include "interactivechartview.h"
#include "smaps/smapssection.h"

namespace Ui {
class MainWindow;
}

class QTreeWidgetItem;
class QStandardItemModel;
class QGraphicsPixmapItem;
class QProgressDialog;
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
    void ExportToText(QFile *file, bool optimal);
    void SaveToFile(QFile *file);
    int LoadFromFile(QFile *file);
    QString GetLastOpenDir() const;
    QString GetLastSymbolDir() const;

    void ConnectionFailed();

    int GetScreenshotIndex(const QPointF& pos) const;
    void ShowScreenshotAt(int index);
    void HideToolTips();
    void UpdateMemInfoRange();
    QString TryAddNewAddress(const QString& lib, quint64 addr);
    void ShowCallStack(const QModelIndex& index);
    void ShowSummary();
    void FilterStackTraceModel();
    void SwitchStackTraceModel(StackTraceProxyModel* model);
    void ReadSMapsFile(QFile* file);
    void GetMergedCallstacks(QList<QTreeWidgetItem*>& topLevelItems);
    void ResetFilters();
    void PushEmptySMapsFile();

    void ReadStacktraceData(const QVector<RawStackInfo>& stacks);
    void ReadStacktraceDataCache();
    void FilterPersistentRecords();
    void StopCaptureProcess();

    struct StacktraceData {
        QVector<QPair<HashString, quint64>> records_;
        QVector<QString> libraries_;
    };

    StacktraceData InterpretRecordsLibrary(int start, int count);
    void InterpretRecordLibrary(StackRecord& record, StacktraceData& data);
    void InterpretStacktraceData();

private slots:
    void FixedUpdate();

    void OnTimeSelectionChange(const QPointF& pos);
    void OnSyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta);
    void OnTimelineRubberBandSelected(double from, double to);
    void OnTimelineRubberBandHide();
    void OnStackTableViewContextMenu(const QPoint & pos);
    void OnDumpingLineNumbers();
    void OnStackTableViewSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

    void StartAppProcessFinished(AdbProcess* process);
    void StartAppProcessErrorOccurred();
    void MemInfoProcessFinished(AdbProcess* process);
    void MemInfoProcessErrorOccurred();
    void ScreenshotProcessFinished(AdbProcess* process);
    void ScreenshotProcessErrorOccurred();
    void StacktraceDataReceived();
    void StacktraceConnectionLost();
    void AddressProcessFinished(AdbProcess* process);
    void AddressProcessErrorOccurred();

    void on_actionOpen_triggered();
    void on_actionSave_triggered();
    void on_actionExit_triggered();
    void on_actionStat_SMaps_triggered();
    void on_actionVisualize_SMaps_triggered();
    void on_actionShow_Merged_Callstacks_triggered();
    void on_actionShow_Callstacks_In_TreeMap_triggered();
    void on_actionAbout_triggered();
    void on_launchPushButton_clicked();
    void on_chartScaleHSlider_valueChanged(int value);
    void on_symbloPushButton_clicked();
    void on_configPushButton_clicked();
    void on_selectAppToolButton_clicked();
    void on_memSizeComboBox_currentIndexChanged(int index);
    void on_libraryComboBox_currentIndexChanged(int index);
    void on_allocComboBox_currentIndexChanged(int index);
    void on_actionExport_To_Text_triggered();

private:
    Ui::MainWindow *ui;
    QProgressDialog *progressDialog_;
    QStandardItemModel *callStackModel_;
    StackTraceModel *stacktraceModel_;
    StackTraceModel *filteredStacktraceModel_;
    StackTraceProxyModel *stacktraceProxyModel_;
    StackTraceProxyModel *filteredStacktraceProxyModel_;
    QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap_;
    QSet<QString> libraries_;
    QString appPid_;
    QString appName_;
    QString subProcessName_;
    QString lastOpenDir_;
    QString lastSymbolDir_;
    QTimer* mainTimer_;
    QTimer* smapsTimer_;
    int time_ = 0;
    double minTime_ = 0;
    double maxTime_ = 0;

    // adb shell monkey -p packagename -c android.intent.category.LAUNCHER 1
    StartAppProcess *startAppProcess_;

    // screenshot
    ScreenshotProcess *screenshotProcess_;
    QGraphicsPixmapItem* screenshotItem_;
    int lastScreenshotTime_ = 0;
    QVector<QPair<int, QByteArray>> screenshots_;

    // stacktrace process
    StackTraceProcess *stacktraceProcess_;
    QVector<StackRecord> recordsCache_;

    // address process
    QVector<AddressProcess*> addrProcesses_;
    // <dllname, <func address, func name>>
    QHash<QString, QHash<quint64, QString>> symbloMap_;
    // <mem address, time>
    QHash<quint64, quint32> freeAddrMap_;

    // meminfo process
    MemInfoProcess* memInfoProcess_;
    int maxMemInfoValue_ = 128;
    QVector<QtCharts::QLineSeries*> memInfoSeries_;
    QtCharts::QValueAxis *memInfoAxisX_;
    QtCharts::QValueAxis *memInfoAxisY_;
    QtCharts::QChart *memInfoChart_;
    InteractiveChartView *memInfoChartView_;

    // charts
    FixedScrollArea* scrollArea_;

    QHash<QString, SMapsSection> sMapsSections_;

    // cache
    bool useCache_ = true;
    bool showJDWPErrorLog_ = true;

    bool isCapturing_ = false;
    bool isConnected_ = false;
};

#endif // MAINWINDOW_H
