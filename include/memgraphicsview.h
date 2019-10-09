#ifndef MEMGRAPHICSVIEW_H
#define MEMGRAPHICSVIEW_H

#include <QGraphicsView>
#include <QGraphicsObject>

#include "customgraphicsview.h"

class MemSectionItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };
    MemSectionItem(double width, double size, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), size_(size) {
        setWidth(width);
    }
    int type() const override { return Type; }
    QRectF boundingRect() const override { return rect_; }
    void setWidth(double width);
    void addAllocation(double addr, double size);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QRectF rect_;
    double size_;
    QVector<QPair<double, double>> allocations_;
    const double rowHeight_ = 16;
};

class MemGraphicsView : public CustomGraphicsView {
    Q_OBJECT
public:
    MemGraphicsView(QWidget* parent = nullptr);

protected:
    void drawBackground(QPainter* painter, const QRectF& r) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
};

#endif // MEMGRAPHICSVIEW_H
