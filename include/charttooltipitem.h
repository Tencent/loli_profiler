#ifndef CHARTTOOLTIPITEM_H
#define CHARTTOOLTIPITEM_H

#include <QtWidgets/QGraphicsItem>
#include <QtGui/QFont>
#include <QtCharts/QChart>

class ChartTooltipItem : public QGraphicsItem {
public:
    ChartTooltipItem(QtCharts::QChart *chart);

    void setText(const QString &text);
    void setAnchor(QPointF point);

    void updateGeometry();
    QRectF boundingRect() const;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,QWidget *widget);

private:
    QtCharts::QChart *chart_;
    QString text_;
    QRectF textRect_;
    QPointF anchor_;
    QFont font_;
};

#endif // CHARTTOOLTIPITEM_H
