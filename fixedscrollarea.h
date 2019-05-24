#ifndef FIXEDSCROLLAREA_H
#define FIXEDSCROLLAREA_H

#include <QScrollArea>

class FixedScrollArea : public QScrollArea {
    Q_OBJECT
public:
    FixedScrollArea(QWidget* parent = nullptr)
        : QScrollArea(parent) {}

signals:
    void ScaleTriggered(int);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};

#endif // FIXEDSCROLLAREA_H
