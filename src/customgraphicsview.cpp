#include "customgraphicsview.h"

#include <QGestureEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QTreeWidget>
#include <QLineEdit>
#include <QWidgetAction>
#include <QHeaderView>
#include <QDebug>

#include <cmath>

CustomGraphicsView::CustomGraphicsView(QWidget *parent)
    : QGraphicsView (parent) {
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(QColor(53, 53, 53));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setCacheMode(QGraphicsView::CacheBackground);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
}

void CustomGraphicsView::setCenter(const QPointF &pos) {
    auto newRect = sceneRect();
    newRect.setX(pos.x());
    newRect.setY(pos.y());
    setSceneRect(newRect);
}

bool CustomGraphicsView::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        usingTouch_ = true;
        break;
    case QEvent::Wheel:
        if (static_cast<QWheelEvent *>(event)->source() == Qt::MouseEventNotSynthesized)
            usingTouch_ = false;
        break;
    case QEvent::Gesture:
        return gestureEvent(static_cast<QGestureEvent *>(event));
    default:
        break;
    }
    return QGraphicsView::event(event);
}

bool CustomGraphicsView::gestureEvent(QGestureEvent *event) {
    if (QGesture *pinch = event->gesture(Qt::PinchGesture)) {
        auto pinchGes = static_cast<QPinchGesture *>(pinch);
        auto factor = pinchGes->scaleFactor();
        if (transform().m11() > 2.0 && factor > 1.0)
            return false;
        scale(factor, factor);
    }
    return true;
}

void CustomGraphicsView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        mousePressPos_ = event->pos();
        clickPos_ = mapToScene(event->pos());
    } else {
        mousePressPos_ = QPoint(-65536, -65536);
    }
    QGraphicsView::mousePressEvent(event);
}

void CustomGraphicsView::mouseMoveEvent(QMouseEvent *event) {
    if (scene()->mouseGrabberItem() == nullptr && event->buttons() & Qt::LeftButton) {
        if ((event->modifiers() & Qt::ShiftModifier) == 0) {
            QPointF difference = clickPos_ - mapToScene(event->pos());
            setSceneRect(sceneRect().translated(difference.x(), difference.y()));
        }
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CustomGraphicsView::wheelEvent(QWheelEvent *event) {
    if (usingTouch_) {
        auto delta = event->pixelDelta();
        if (!delta.isNull()) {
            setSceneRect(sceneRect().translated(-delta.x(), -delta.y()));
        }
    } else {
        QPoint delta = event->angleDelta();
        if (delta.y() == 0) {
            event->ignore();
            return;
        }
        double const d = delta.y() / std::abs(delta.y());
        if (d > 0.0) {
            double const step   = 1.2;
            double const factor = std::pow(step, 1.0);
            QTransform t = transform();
            if (t.m11() > 2.0)
                return;
            scale(factor, factor);
        } else {
            double const step   = 1.2;
            double const factor = std::pow(step, -1.0);
            scale(factor, factor);
        }
    }
    QGraphicsView::wheelEvent(event);
}

void CustomGraphicsView::drawBackground(QPainter *painter, const QRectF &r) {
    QGraphicsView::drawBackground(painter, r);
    auto drawGrid = [&](double gridStep) {
        QRect   windowRect = rect();
        QPointF tl = mapToScene(windowRect.topLeft());
        QPointF br = mapToScene(windowRect.bottomRight());
        double left   = std::floor(tl.x() / gridStep - 0.5);
        double right  = std::floor(br.x() / gridStep + 1.0);
        double bottom = std::floor(tl.y() / gridStep - 0.5);
        double top    = std::floor(br.y() / gridStep + 1.0);
        // vertical lines
        for (int xi = int(left); xi <= int(right); ++xi) {
            QLineF line(xi * gridStep, bottom * gridStep, xi * gridStep, top * gridStep );
            painter->drawLine(line);
        }
        // horizontal lines
        for (int yi = int(bottom); yi <= int(top); ++yi) {
            QLineF line(left * gridStep, yi * gridStep, right * gridStep, yi * gridStep );
            painter->drawLine(line);
        }
    };
    QBrush bBrush = backgroundBrush();
    QPen pfine(QColor(60, 60, 60), 1.0);
    painter->setPen(pfine);
    drawGrid(15);
    QPen p(QColor(25, 25, 25), 1.0);
    painter->setPen(p);
    drawGrid(150);
}

void CustomGraphicsView::showEvent(QShowEvent *event) {
    auto curRect = rect();
    auto width = curRect.width();
    auto height = curRect.height();
    scene()->setSceneRect(QRectF(curRect.x() - width / 2, curRect.y() - height / 2, width, height));
    QGraphicsView::showEvent(event);
}
