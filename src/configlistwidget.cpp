#include "configlistwidget.h"

#include <QApplication>
#include <QMimeData>
#include <QClipboard>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMenu>
#include <QHBoxLayout>
#include <QTextEdit>

ConfigListWidget::ConfigListWidget(QWidget *parent)
    : QListWidget(parent) {
    connect(this, &ConfigListWidget::customContextMenuRequested, this, &ConfigListWidget::OnContextMenuRequested);
}

void ConfigListWidget::OnNewItemClicked() {
    addItem("libfoo");
    auto curItem = item(count() - 1);
    curItem->setFlags(curItem->flags() | Qt::ItemIsEditable);
}

void ConfigListWidget::OnNewItemsClicked() {
    QDialog dialog(this);
    dialog.setWindowTitle("Append New Items");
    dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setLayout(new QHBoxLayout());
    dialog.setFixedSize(300, 300);
    auto textEditor = new QTextEdit(&dialog);
    textEditor->setAcceptRichText(false);
    textEditor->setPlaceholderText("Type library names here, without(.so), "
                                   "you can separate libraries by comma ','. "
                                   "Hit ESC to exit this dialog.");
    dialog.layout()->addWidget(textEditor);
    dialog.layout()->setMargin(2);
    dialog.exec();
    ParseString(textEditor->toPlainText());
}

void ConfigListWidget::OnDeleteItemsClicked() {
    auto items = selectedItems();
    for (auto curItem : items) {
        delete takeItem(row(curItem));
    }
}

void ConfigListWidget::OnClearItemsClicked() {
    while (count() > 0) {
        delete takeItem(0);
    }
}

void ConfigListWidget::OnContextMenuRequested(const QPoint &pos) {
    QMenu menu;
    menu.addAction("Append New Item", [this]{ OnNewItemClicked(); });
    menu.addAction("Append New Items", [this]{ OnNewItemsClicked(); });
    menu.addAction("Delete Selected Items", [this]{ OnDeleteItemsClicked(); });
    menu.addAction("Clear All Items", [this]{ OnClearItemsClicked(); });
    menu.exec(mapToGlobal(pos));
}

void ConfigListWidget::ParseString(const QString& textData) {
    auto parts = textData.split(',', QString::SplitBehavior::SkipEmptyParts);
    for (auto& part : parts) {
        if (part.size() > 0) {
            addItem(part);
            auto curItem = item(count() - 1);
            curItem->setFlags(curItem->flags() | Qt::ItemIsEditable);
        }
    }
}

void ConfigListWidget::OnPasteClipboard() {
    const auto clipboard = QApplication::clipboard();
    const auto mimeData = clipboard->mimeData();
    if (!mimeData->hasText()) {
        QMessageBox::warning(this, "Warning", "Invalide clipboard data!");
        return;
    }
    ParseString(mimeData->text());
}

void ConfigListWidget::keyPressEvent(QKeyEvent* event) {
    if (event->type() == QKeyEvent::KeyPress) {
        if (event->matches(QKeySequence::Paste)) {
            OnPasteClipboard();
            event->accept();
        } else if (event->matches(QKeySequence::Delete)) {
            OnDeleteItemsClicked();
            event->accept();
        }
    }
    QListWidget::keyPressEvent(event);
}
