#include "stacktreewidget.h"

#include <QMenu>

StackTreeWidget::StackTreeWidget(QWidget *parent)
    : QTreeWidget (parent) {
    contextMenu_ = new QMenu(this);
}

StackTreeWidget::~StackTreeWidget() {
    delete contextMenu_;
}

void StackTreeWidget::contextMenuEvent(QContextMenuEvent *event) {

    QTreeWidget::contextMenuEvent(event);
}
