#ifndef STACKTREEWIDGET_H
#define STACKTREEWIDGET_H

#include <QTreeWidget>

class QMenu;
class StackTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    StackTreeWidget(QWidget *parent = nullptr);
    ~StackTreeWidget();

private:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QMenu* contextMenu_;
};

#endif // STACKTREEWIDGET_H
