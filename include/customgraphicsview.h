#ifndef CUSTOMGRAPHICSVIEW_H
#define CUSTOMGRAPHICSVIEW_H

#include <QGraphicsView>

class QMouseEvent;
class QGestureEvent;
class CustomGraphicsView : public QGraphicsView
{
public:
    CustomGraphicsView(QWidget *parent = nullptr);

    void setCenter(const QPointF &pos);

protected:
    bool event(QEvent* event) override;
    bool gestureEvent(QGestureEvent *event);
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter* painter, const QRectF& r) override;
    void showEvent(QShowEvent *event) override;

private:
    QPointF clickPos_ = {};
//    QPointF sceneOrigin_ = {};
    QPoint mousePressPos_ = {};
    bool usingTouch_ = false;
};

#endif // CUSTOMGRAPHICSVIEW_H
