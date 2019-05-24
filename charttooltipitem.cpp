#include "charttooltipitem.h"
#include <QtGui/QPainter>
#include <QtGui/QFontMetrics>
#include <QValueAxis>

using namespace QtCharts;

ChartTooltipItem::ChartTooltipItem(QChart *chart)
    : QGraphicsItem (chart), chart_(chart) {}

void ChartTooltipItem::setText(const QString &text) {
    text_ = text;
    QFontMetrics metrics(font_);
    textRect_ = metrics.boundingRect(QRect(0, 0, 150, 150), Qt::AlignLeft, text_);
    textRect_.translate(5, 5);
    prepareGeometryChange();
}

void ChartTooltipItem::setAnchor(QPointF point) {
    anchor_ = point;
}

void ChartTooltipItem::updateGeometry() {
    prepareGeometryChange();
    setPos(chart_->mapToPosition(anchor_) + QPoint(10, -50));
}

QRectF ChartTooltipItem::boundingRect() const {
    auto anchor = mapFromParent(chart_->mapToPosition(anchor_));
    auto axisY = static_cast<QValueAxis*>(chart_->axes(Qt::Vertical)[0]);
    auto maxY = axisY->max(), minY = axisY->min();
    auto max = mapFromParent(chart_->mapToPosition(QPointF(anchor_.x(), maxY))).y();
    auto min = mapFromParent(chart_->mapToPosition(QPointF(anchor_.x(), minY))).y();
    QRectF rect;
    rect.setLeft(qMin(textRect_.left(), anchor.x()));
    rect.setRight(qMax(textRect_.right(), anchor.x()));
    rect.setTop(qMin(max, qMin(textRect_.top(), anchor.y())));
    rect.setBottom(qMax(min, qMax(textRect_.bottom(), anchor.y())));
    return rect;
}

void ChartTooltipItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    painter->setPen(Qt::black);
    painter->drawText(textRect_, text_);
    QPointF anchor = mapFromParent(chart_->mapToPosition(anchor_));
    auto axisY = static_cast<QValueAxis*>(chart_->axes(Qt::Vertical)[0]);
    auto maxY = axisY->max(), minY = axisY->min();
    auto max = mapFromParent(chart_->mapToPosition(QPointF(anchor_.x(), maxY))).y();
    auto min = mapFromParent(chart_->mapToPosition(QPointF(anchor_.x(), minY))).y();
    painter->drawLine(static_cast<int>(anchor.x()), static_cast<int>(max),
                      static_cast<int>(anchor.x()), static_cast<int>(min));
}
