#ifndef CUSTOMGRAPHICSVIEW_H
#define CUSTOMGRAPHICSVIEW_H

#include <QGraphicsView>

class QMouseEvent;
class CustomGraphicsView : public QGraphicsView
{
public:
    CustomGraphicsView(QWidget *parent = nullptr);

    void setCenter(const QPointF &pos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter* painter, const QRectF& r) override;
    void showEvent(QShowEvent *event) override;

private:
    QPointF clickPos_ = {};
    QPointF sceneOrigin_ = {};
    QPoint mousePressPos_ = {};
};

#endif // CUSTOMGRAPHICSVIEW_H
