#ifndef INTERACTIVECHARTVIEW_H
#define INTERACTIVECHARTVIEW_H

#include <QGraphicsView>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QAbstractSeries>
#include <QXYSeries>

class ChartTooltipItem;

class InteractiveChartView : public QtCharts::QChartView {
    Q_OBJECT
public:
    InteractiveChartView(QtCharts::QChart *chart, QWidget *parent = nullptr);
    virtual void HideToolTip();
    void SyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta);
    void IgnoreSeries(QtCharts::QAbstractSeries* series) {
        ignoreSeries_.append(series);
    }
    void SetRangeScale(int scale);

signals:
    void OnSyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta);
    void OnSelectionChange(const QPointF& pos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    double GetSeriesYFromX(QtCharts::QXYSeries *series, double x) const;

protected:
    QVector<QtCharts::QAbstractSeries*> ignoreSeries_;
    ChartTooltipItem *toolTip_ = nullptr;
    bool mousePressed_ = false;
    int prevMouseX_ = 0;
    double CurPos_ = 0.0;
    int rangeMin_ = 0;
    int rangeMax_ = 10;
    int rangeScale_ = 10;
};

#endif // INTERACTIVECHARTVIEW_H
