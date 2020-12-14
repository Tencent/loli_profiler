#include "treemapgraphicsview.h"
#include <QTreeWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <cmath>

// TreeMap

void TreeMap::Tessellate(QVector<qulonglong>& values, QRectF rect, QVector<QRectF>& rects) {
    double total = 0;
    for (auto value : values)
        total += value;
    QVector<double> weights;
    for (auto value : values)
        weights.push_back(static_cast<double>(value) / total);
    auto rectArea = rect.width() * rect.height();
    QVector<double> areas;
    for (auto weight : weights) {
        areas.push_back(weight * rectArea);
    }
    auto canvas = rect;
    while (areas.size() > 0) {
        auto remainingCanvas = canvas;
        QVector<QRectF> newRects;
        Tessellate(areas, canvas, remainingCanvas, newRects);
        for (auto& newRect : newRects)
            rects.push_back(newRect);
        canvas = remainingCanvas;
        for (int i = 0; i < newRects.size(); i++)
            areas.erase(areas.begin());
    }
}

void TreeMap::Tessellate(QVector<double>& areas, QRectF rect, QRectF& remaining, QVector<QRectF>& rects) {
    auto direction = LayoutDirection::HORIZONTAL;
    auto length = 0.0;
    if (rect.width() >= rect.height()) {
        direction = LayoutDirection::HORIZONTAL;
        length = rect.height();
    } else {
        direction = LayoutDirection::VERTICAL;
        length = rect.width();
    }
    auto aspectRatio = std::numeric_limits<double>::max();
    auto groupWeightAccumulator = 0.0;
    QVector<double> acceptedAreas;
    for (auto area : areas) {
        auto worstAspectRatio = WorstAspectRatio(acceptedAreas, groupWeightAccumulator, area, length, aspectRatio);
        if (worstAspectRatio > aspectRatio) {
            break;
        } else {
            acceptedAreas.push_back(area);
            groupWeightAccumulator += area;
            aspectRatio = worstAspectRatio;
        }
    }
    auto computedWidth = groupWeightAccumulator / length;
    auto lengthOffset = direction == LayoutDirection::HORIZONTAL ? rect.y() : rect.x();
    for (auto area : acceptedAreas) {
        auto height = area / computedWidth;
        auto thisOffset = lengthOffset;
        lengthOffset += height;
        QRectF layoutRect;
        if (direction == LayoutDirection::HORIZONTAL) {
            layoutRect.setX(rect.x());
            layoutRect.setY(thisOffset);
            layoutRect.setWidth(computedWidth);
            layoutRect.setHeight(height);
        } else {
            layoutRect.setX(thisOffset);
            layoutRect.setY(rect.y());
            layoutRect.setWidth(height);
            layoutRect.setHeight(computedWidth);
        }
        rects.push_back(layoutRect);
    }
    if (direction == LayoutDirection::HORIZONTAL) {
        remaining.setX(rect.x() + computedWidth);
        remaining.setY(rect.y());
        remaining.setWidth(rect.width() - computedWidth);
        remaining.setHeight(rect.height());
    } else {
        remaining.setX(rect.x());
        remaining.setY(rect.y() + computedWidth);
        remaining.setWidth(rect.width());
        remaining.setHeight(rect.height() - computedWidth);
    }
}

double TreeMap::WorstAspectRatio(QVector<double>& weights, double groupWeight, double proposedWeight, 
    double length, double limit) {
    auto computedGroupWeight = groupWeight + proposedWeight;
    auto width = computedGroupWeight / length;
    auto worstAspect = AspectRatio(width, proposedWeight / width);
    for (auto weight : weights) {
        auto thisAspect = AspectRatio(width, weight / width);
        worstAspect = std::max(thisAspect, worstAspect);
        if (worstAspect > limit)
            break;
    }
    return worstAspect;
}

double TreeMap::AspectRatio(double edge1, double edge2) {
    return edge1 > edge2 ? edge1 / edge2 : edge2 / edge1;
}

// TreeMapNode

TreeMapNode::TreeMapNode(QRectF rect, QString title, QGraphicsItem *parent)
    : QGraphicsObject(parent), rect_(rect), title_(title) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::MouseButton::LeftButton);
    setToolTip(title);
}

QRectF TreeMapNode::contentRect() const {
    auto spacing = 8;
    return QRectF(rect_.x() + spacing, rect_.y() + 25 + spacing,
                  rect_.width() - spacing * 2, rect_.height() - 25 - spacing * 2);
}
void TreeMapNode::setRect(const QRectF& rect) {
    rect_ = rect;
    prepareGeometryChange();
}

void TreeMapNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    auto penWidth = 2;
    auto colorDiff = std::min(150, static_cast<int>(zValue()) * 10);
    auto bgColor = QColor(200 - colorDiff, 200 - colorDiff, 200 - colorDiff);
    auto titleColor = QColor(255 - colorDiff, 255 - colorDiff, 255 - colorDiff);
    if (hovered_) {
        bgColor.setBlue(50);
        titleColor.setBlue(50);
    }
    painter->setPen(QPen(QColor(0, 0, 0), penWidth));
    if (rect_.width() >= 40 && rect_.height() >= 30) {
        QFontMetrics fm(painter->font());
        QRectF bgRect(rect_.x() - penWidth, rect_.y() - penWidth, rect_.width() - penWidth * 2, rect_.height() - penWidth * 2);
        painter->setBrush(bgColor);
        painter->drawRect(bgRect);
        painter->setBrush(titleColor);
        painter->drawRect(QRectF(bgRect.x(), bgRect.y() + 25, bgRect.width(), bgRect.height() - 25));
        auto elidedTitle = fm.elidedText(title_, Qt::TextElideMode::ElideRight, static_cast<int>(bgRect.width()));
        painter->drawText(QPointF(bgRect.topLeft().x() + penWidth, bgRect.topLeft().y() + fm.height() + penWidth), elidedTitle);
    } else {
        QRectF bgRect(rect_.x() - penWidth, rect_.y() - penWidth, rect_.width() - penWidth * 2, rect_.height() - penWidth * 2);
        painter->setBrush(bgColor);
        painter->drawRect(bgRect);
    }
}

void TreeMapNode::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    QGraphicsItem::hoverEnterEvent(event);
    hovered_ = true;
    prepareGeometryChange();
}
void TreeMapNode::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    QGraphicsItem::hoverLeaveEvent(event);
    hovered_ = false;
    prepareGeometryChange();
}
void TreeMapNode::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    event->accept();
    emit onClicked();
}

// TreeMapGraphicsView

TreeMapGraphicsView::TreeMapGraphicsView(QList<QTreeWidgetItem*>& topLevelItems, QWidget *parent)
    : QGraphicsView(parent), mainTimer_(new QTimer(this)), topLevelItems_(topLevelItems) {
    setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    prevWidth_ = viewport()->width();
    prevHeight_ = viewport()->height();
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
    mainTimer_->start(1000);
}

TreeMapGraphicsView::~TreeMapGraphicsView() {
    for (auto item : topLevelItems_)
        delete item;
}

void TreeMapGraphicsView::Generate(QTreeWidgetItem* parent, QRectF rect, int depth) {
    if (scene_ == nullptr) {
        scene_ = new QGraphicsScene(this);
        setScene(scene_);
    }
    targetItem_ = parent;
    targetDepth_ = depth;
    scene_->clear();
    itemInfoMap_.clear();
    if (targetDepth_ <= 0)
        return;
    auto curMemSize = 0ull;
    auto childCount = GetChildCount(parent);
    for (int i = 0; i < childCount; i++) {
        curMemSize += GetChild(parent, i)->data(1, Qt::UserRole).toULongLong();
    }
    ItemInfo curInfo;
    curInfo.treeItem_ = parent;
    curInfo.rect_ = rect;
    curInfo.size_ = curMemSize;
    curInfo.depth_ = 0;
    curInfo.node_ = GetTreeMapNode(curInfo, rect);
    itemInfoMap_.insert(parent, curInfo);
    Generate(parent, depth);
}

void TreeMapGraphicsView::Generate(QTreeWidgetItem* parentTreeItem, int maxDepth) {
    auto& parentTreeItemInfo = itemInfoMap_[parentTreeItem];
    auto totalSize = 0ull;
    auto childCount = GetChildCount(parentTreeItem);
    for (int i = 0; i < childCount; i++) {
        totalSize += GetChild(parentTreeItem, i)->data(1, Qt::UserRole).toULongLong();
    }
    QList<ItemInfo> childItems;
    for (int i = 0; i < childCount; i++) {
        auto child = GetChild(parentTreeItem, i);
        auto size = child->data(1, Qt::UserRole).toULongLong();
        if ((static_cast<double>(size) / totalSize) < 0.1)
            continue;
        ItemInfo childItemInfo;
        childItemInfo.treeItem_ = child;
        childItemInfo.size_ = size;
        childItemInfo.depth_ = parentTreeItemInfo.depth_ + 1;
        itemInfoMap_[child] = childItemInfo;
        childItems.push_back(childItemInfo);
    }

    // sort in descending order
    std::sort(childItems.begin(), childItems.end(), [](const ItemInfo& a, const ItemInfo& b) {
        return a.size_ < b.size_;
    });

    QVector<qulonglong> sizes;
    for (auto item : childItems)
        sizes.push_back(item.size_);

    QVector<QRectF> newRects;
    TreeMap::Tessellate(sizes, parentTreeItemInfo.node_->contentRect(), newRects);

    for (int i = 0; i < childItems.size(); i++) {
        auto newRect = newRects[i];
        auto childItem = childItems[i];
        childItem.rect_ = newRect;
        childItem.node_ = GetTreeMapNode(childItem, newRect);
        itemInfoMap_[childItem.treeItem_] = childItem;
        if (childItem.rect_.width() > 40 && childItem.rect_.height() > 40) {
            if (parentTreeItemInfo.depth_ + 1 < maxDepth) {
                Generate(childItem.treeItem_, maxDepth);
            }
        }
    }
}

QTreeWidgetItem* TreeMapGraphicsView::GetChild(QTreeWidgetItem* item, int index) const {
    if (item)
        return item->child(index);
    else
        return topLevelItems_[index];
}

int TreeMapGraphicsView::GetChildCount(QTreeWidgetItem* item) const {
    if (item)
        return item->childCount();
    else
        return topLevelItems_.size();
}

TreeMapNode* TreeMapGraphicsView::GetTreeMapNode(ItemInfo& info, QRectF rect) {
    QString title;
    if (info.treeItem_) {
        auto countStr = info.treeItem_->data(2, Qt::DisplayRole).toString();
        auto sizeStr = info.treeItem_->data(1, Qt::DisplayRole).toString();
        auto nameStr = info.treeItem_->data(0, Qt::DisplayRole).toString();
        title = sizeStr + " (" + countStr + "): " + nameStr;
    }
    auto node = new TreeMapNode(rect, title);
    scene_->addItem(node);
    node->setZValue(info.depth_);
    connect(node, &TreeMapNode::onClicked, [&, info]() {
        if (info.depth_ == 0) {
            if (info.treeItem_)
                Generate(info.treeItem_->parent(), QRectF(0, 0, prevWidth_, prevHeight_), 10);
        } else {
            Generate(info.treeItem_, QRectF(0, 0, prevWidth_, prevHeight_), 10);
        }
    });
    return node;
}

void TreeMapGraphicsView::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    fitInView(scene_->sceneRect());
}

void TreeMapGraphicsView::FixedUpdate() {
    if (prevWidth_ != viewport()->width() || prevHeight_ != viewport()->height()) {
        prevWidth_ = viewport()->width();
        prevHeight_ = viewport()->height();
        Generate(targetItem_, QRectF(0, 0, prevWidth_, prevHeight_), targetDepth_);
    }
}
