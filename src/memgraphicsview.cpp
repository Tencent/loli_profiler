#include "memgraphicsview.h"

#include <QDebug>

#include <cmath>

void MemSectionItem::setWidth(double width) {
    rect_.setX(pos().x());
    rect_.setY(pos().y());
    rect_.setWidth(width);
    rect_.setHeight(std::ceil(size_ / width) * rowHeight_);
    prepareGeometryChange();
}

void MemSectionItem::addAllocation(double addr, double size) {
    bool merged = false;
    for (int i = allocations_.size() - 1; i > 0; i--) {
        auto& curAddr = allocations_[i];
        if (addr < curAddr.first)
            continue;
        if (addr >= curAddr.first && addr + size <= curAddr.first + curAddr.second) {
            merged = true;
            curAddr.second = std::max(curAddr.second, addr - curAddr.first + size);
        }
    }
    if (!merged)
        allocations_.push_back(qMakePair(addr, size));
    prepareGeometryChange();
}

void MemSectionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    auto freeColor = QColor(100, 255, 100);
    auto allocColor = QColor(255, 100, 100);
    painter->setPen(Qt::PenStyle::NoPen);
    painter->setBrush(freeColor);
    painter->drawRect(rect_);
    const auto startX = rect_.x(), startY = rect_.y(), width = rect_.width();
    painter->setBrush(allocColor);
    for (auto& pair : allocations_) {
        auto row = static_cast<int>(std::floor(pair.first / width));
        auto curY = row * rowHeight_;
        auto curX = pair.first - row * width;
        auto curSize = pair.second;
        while (curSize > 0) {
            auto growth = std::min(width - curX, curSize);
            painter->drawRect(static_cast<int>(startX + curX), static_cast<int>(startY + curY),
                                     static_cast<int>(growth), static_cast<int>(rowHeight_));
            curSize -= growth;
            curY += rowHeight_;
            curX = 0;
        }
    }
}

MemGraphicsView::MemGraphicsView(QWidget* parent)
    : CustomGraphicsView(parent) {
}

void MemGraphicsView::drawBackground(QPainter* painter, const QRectF& r) {
    QGraphicsView::drawBackground(painter, r);
}

void MemGraphicsView::drawForeground(QPainter *painter, const QRectF &r) {
    QGraphicsView::drawForeground(painter, r);
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
    QPen pfine(QColor(60, 60, 60), 1.0);
    painter->setPen(pfine);
    drawGrid(16);
    QPen p(QColor(25, 25, 25), 1.0);
    painter->setPen(p);
    drawGrid(160);
}
