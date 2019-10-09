#ifndef TREEMAPGRAPHICSVIEW_H
#define TREEMAPGRAPHICSVIEW_H

#include <QGraphicsView>
#include <QGraphicsObject>
#include <QMap>

class QGraphicsScene;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

class TreeMap {
public:
    static void Tessellate(QVector<qulonglong>& values, QRectF rect, QVector<QRectF>& rects);
private:
    static void Tessellate(QVector<double>& areas, QRectF rect, QRectF& remaining, QVector<QRectF>& rects);
    static double WorstAspectRatio(QVector<double>& weights, double groupWeight, double proposedWeight, double length, double limit);
    static double AspectRatio(double edge1, double edge2);
    enum class LayoutDirection {
        HORIZONTAL,
        VERTICAL
    };
};

class TreeMapNode : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 1 };
    TreeMapNode(QRectF rect, QString title, QGraphicsItem *parent = nullptr);
    int type() const override { return Type; }
    QRectF boundingRect() const override { return rect_; }
    QRectF contentRect() const;
    void setRect(const QRectF& rect);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void onClicked();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override ;

private:
    QRectF rect_;
    QString title_;
    bool hovered_ = false;
};

class TreeMapGraphicsView : public QGraphicsView {
    Q_OBJECT
private:
    struct ItemInfo {
        QTreeWidgetItem* treeItem_ = nullptr;
        TreeMapNode* node_ = nullptr;
        QRectF rect_;
        qulonglong size_ = 0ull;
        int depth_ = 0;
    };
public:
    TreeMapGraphicsView(QList<QTreeWidgetItem*>& topLevelItems, QWidget *parent = nullptr);
    ~TreeMapGraphicsView() override;
    void Generate(QTreeWidgetItem* parent, QRectF rect, int depth);

protected:
    void Generate(QTreeWidgetItem* item, int maxDepth);
    QTreeWidgetItem* GetChild(QTreeWidgetItem* item, int index) const;
    int GetChildCount(QTreeWidgetItem* item) const;
    TreeMapNode* GetTreeMapNode(ItemInfo& item, QRectF rect);

    void showEvent(QShowEvent *event) override;

private slots:
    void FixedUpdate();

private:
    QTimer* mainTimer_;
    QList<QTreeWidgetItem*>& topLevelItems_;
    QGraphicsScene* scene_ = nullptr;
    QMap<QTreeWidgetItem*, ItemInfo> itemInfoMap_;
    int prevWidth_ = 0;
    int prevHeight_ = 0;
    QTreeWidgetItem* targetItem_ = nullptr;
    int targetDepth_ = -1;

    QPen rectPen { QColor(255, 255, 255) };
    QBrush rectBrush { QColor(0, 0, 0) };
};

#endif // TREEMAPGRAPHICSVIEW_H
