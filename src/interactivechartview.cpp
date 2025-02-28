#include "interactivechartview.h"
#include "charttooltipitem.h"
#include <QValueAxis>
#include <QStringBuilder>
#include <QRubberBand>
#include <QDebug>

using namespace QtCharts;

InteractiveChartView::InteractiveChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent) {
    toolTip_ = new ChartTooltipItem(chart);
    toolTip_->hide();
    rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
}

void InteractiveChartView::HideToolTip() {
    toolTip_->hide();
}

void InteractiveChartView::SyncScroll(QtCharts::QChartView* sender, int prevMouseX, int delta) {
    if (this == sender)
        return;
    prevMouseX_ = prevMouseX;
    auto fdelta = static_cast<double>(delta * rangeScale_) / 100;
    CurPos_ = qMax(CurPos_ + fdelta, 0.0);
    rangeMin_ = qRound(CurPos_);
    SetRangeScale(rangeScale_);
}

void InteractiveChartView::SetRangeScale(int scale) {
    rangeMax_ = rangeMin_ + 10 * scale;
    rangeScale_ = scale;
    auto hAxis = chart()->axes(Qt::Horizontal)[0];
    hAxis->setRange(rangeMin_, rangeMax_);
    HideToolTip();
    rubberBand_->hide();
    emit OnRubberBandHide();
}

bool InteractiveChartView::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        usingTouch_ = true;
        break;
    case QEvent::TouchEnd:
        usingTouch_ = false;
        break;
    case QEvent::Wheel:
        if (static_cast<QWheelEvent *>(event)->source() == Qt::MouseEventNotSynthesized)
            usingTouch_ = false;
        break;
    default:
        break;
    }
    return QChartView::event(event);
}

void InteractiveChartView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MouseButton::LeftButton) {
        mousePressed_ = true;
        shiftPressed_ = event->modifiers().testFlag(Qt::ShiftModifier);
        prevMouseX_ = event->x();
        rubberBand_->hide();
        emit OnRubberBandHide();
        if (usingTouch_ || shiftPressed_) {
            auto origin = event->pos();
            rubberBand_->setGeometry(QRect(origin, QSize()));
        }
    }
    else {
        mousePressed_ = false;
        shiftPressed_ = false;
    }
    QChartView::mousePressEvent(event);
}

void InteractiveChartView::mouseMoveEvent(QMouseEvent *event) {
    if (mousePressed_) {
        if (usingTouch_ || shiftPressed_) {
            rubberBand_->setGeometry(QRect(QPoint(prevMouseX_, 0), QPoint(event->pos().x(), height())).normalized());
            if (!rubberBand_->isVisible()) {
                HideToolTip();
                rubberBand_->show();
            }
        } else {
            auto delta = prevMouseX_ - event->x();
            prevMouseX_ = event->x();
            SyncScroll(nullptr, prevMouseX_, delta); // execute
            OnSyncScroll(this, prevMouseX_, delta); // broadcast
        }
    } else {
        auto axisX = static_cast<QValueAxis*>(chart()->axes(Qt::Horizontal)[0]);
        auto axisY = static_cast<QValueAxis*>(chart()->axes(Qt::Vertical)[0]);
        auto maxX = axisX->max(), minX = axisX->min();
        auto maxY = axisY->max(), minY = axisY->min();
        auto valPos = chart()->mapToValue(event->pos());
        auto valX = valPos.x();
        auto valY = valPos.y();
        auto showTooltip = false;
        if (valX <= maxX && valX >= minX && valY <= maxY && valY >= minY) {
            const auto& series = chart()->series();
            QString str;
            for (auto& serie : series) {
                if (ignoreSeries_.contains(serie))
                    continue;
                auto xySeries = static_cast<QXYSeries*>(serie);
                auto xySeriesCount = xySeries->count();
                if (xySeriesCount > 0) {
                    auto xySeriesMinX = xySeries->at(0).x();
                    auto xySeriesMaxX = xySeries->at(xySeriesCount - 1).x();
                    if (valX >= xySeriesMinX && valX < xySeriesMaxX) {
                        showTooltip = true;
                        str.append(QString("%1: %2\n").arg(serie->name()).arg(GetSeriesYFromX(xySeries, valX)));
                    }
                }
            }
            if (showTooltip) {
                OnSelectionChange(valPos);
                toolTip_->setText(str);
                valPos.setY(minY + (maxY - minY) / 2);
                toolTip_->setAnchor(valPos);
                toolTip_->setZValue(11);
                toolTip_->updateGeometry();
                toolTip_->show();
            }
        }
    }
    QChartView::mouseMoveEvent(event);
}

void InteractiveChartView::mouseReleaseEvent(QMouseEvent *event) {
    mousePressed_ = false;
    if ((usingTouch_ || shiftPressed_) && rubberBand_->isVisible()) {
        auto selectRect = rubberBand_->geometry();
        auto topRight = chart()->mapToValue(selectRect.topRight());
        auto bottomLeft = chart()->mapToValue(selectRect.bottomLeft());
        emit OnRubberBandSelected(bottomLeft.x(), topRight.x());
    }
    usingTouch_ = false;
    shiftPressed_ = false;
    QChartView::mouseReleaseEvent(event);
}

void InteractiveChartView::wheelEvent(QWheelEvent *event) {
    shiftPressed_ = false;
    if (usingTouch_) {
        auto delta = event->pixelDelta();
        if (!delta.isNull()) {
            prevMouseX_ = event->pos().x();
            SyncScroll(nullptr, prevMouseX_, delta.x()); // execute
            OnSyncScroll(this, prevMouseX_, delta.x()); // broadcast
            rubberBand_->hide();
            emit OnRubberBandHide();
        }
    }
    QChartView::wheelEvent(event);
}

// TODO: optimize by binary search ?
double InteractiveChartView::GetSeriesYFromX(QXYSeries *series, double x) const {
    int count = series->count();
    if (count == 0)
        return 0.0;
    if (count == 1)
        return series->at(0).y();
    double maxX = series->at(count - 1).x();
    double minX = series->at(0).x();
    if (x >= maxX)
        return series->at(count - 1).y();
    if (x <= minX)
        return series->at(0).y();
    for (int i = 0, j = 1; j < count; i++, j++) {
        double curX = series->at(i).x();
        double nextX = series->at(j).x();
        if (x >= curX && x < nextX) {
            return series->at(i).y();
        }
    }
    return series->at(count - 1).y();
}
