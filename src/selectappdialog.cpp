#include "selectappdialog.h"
#include <QVBoxLayout>
#include <QListWidget>
#include <QKeyEvent>

ArrowLineEdit::ArrowLineEdit(QListWidget* listView, QWidget *parent)
    : QLineEdit(parent), listView_(listView) {
    connect(this, &QLineEdit::textChanged, this, &ArrowLineEdit::onTextChanged);
}

void ArrowLineEdit::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        auto selectedItems = listView_->selectedItems();
        QListWidgetItem* item = nullptr;
        if (selectedItems.count() > 0) {
            auto currentRow = listView_->row(selectedItems[0]);
            if (event->key() == Qt::Key_Down) {
                while (currentRow + 1 < listView_->count()) {
                    currentRow++;
                    auto curItem = listView_->item(currentRow);
                    if (!curItem->isHidden()) {
                        item = curItem;
                        break;
                    }
                }
            } else {
                while (currentRow - 1 >= 0) {
                    currentRow--;
                    auto curItem = listView_->item(currentRow);
                    if (!curItem->isHidden()) {
                        item = curItem;
                        break;
                    }
                }
            }
        }
        if (item)
            listView_->setCurrentItem(item);
    } else if (event->key() == Qt::Key_Right) {
        auto selectedItems = listView_->selectedItems();
        if (selectedItems.count() > 0) {
            setText(selectedItems[0]->text());
        }
    } else {
        QLineEdit::keyPressEvent(event);
    }
}

void ArrowLineEdit::onTextChanged(const QString& text) {
    // Use regular expression to search fuzzily
    // "Hello\n" -> ".*H.*e.*l.*l.*o.*\\.*n"
    QString pattern;
    for (auto i = 0; i < text.size(); i++) {
        pattern += QRegularExpression::escape(text[i]);
        if (i != text.size() - 1)
            pattern += ".*";
    }
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    auto first = true;
    for (int i = 0; i < listView_->count(); ++i) {
        auto item = listView_->item(i);
        if (item->text().contains(re)) {
            item->setHidden(false);
            item->setSelected(first);
            first = false;
        } else {
            item->setHidden(true);
            item->setSelected(false);
        }
    }
}

SelectAppDialog::SelectAppDialog(QWidget *parent , Qt::WindowFlags f)
    : QDialog(parent, f) {
    auto layout = new QVBoxLayout();
    layout->setMargin(2);
    layout->setSpacing(2);
    listWidget_ = new QListWidget();
    listWidget_->setSelectionMode(QListWidget::SelectionMode::SingleSelection);
    auto hbLayout = new QHBoxLayout();
    searchLineEdit_ = new ArrowLineEdit(listWidget_);
    connect(searchLineEdit_, &QLineEdit::returnPressed, [this]() {
        auto selected = listWidget_->selectedItems();
        if (selected.count() > 0 && callback_)
            callback_(selected[0]->text(), subProcessNameLineEdit_->text());
        close();
    });
    connect(listWidget_, &QListWidget::itemClicked, [this](QListWidgetItem *item) {
        callback_(item->text(), subProcessNameLineEdit_->text());
        close();
    });

    subProcessNameLineEdit_ = new QLineEdit();
    subProcessNameLineEdit_->setPlaceholderText("subProcessName");

    hbLayout->addWidget(searchLineEdit_);
    hbLayout->addWidget(subProcessNameLineEdit_);
    hbLayout->setStretchFactor(searchLineEdit_, 2);
    hbLayout->setStretchFactor(subProcessNameLineEdit_, 1);
    layout->addLayout(hbLayout);

    layout->addWidget(listWidget_);
    searchLineEdit_->setFocus();
    setLayout(layout);
    setWindowModality(Qt::WindowModal);
    setWindowTitle("Selection Application");
    setMinimumSize(400, 300);
    resize(400, 300);
}

void SelectAppDialog::SelectApp(QStringList apps, std::function<void(const QString&, const QString&)> outCallback) {
    listWidget_->clear();
    for (auto& app : apps) {
        auto lineParts = app.split(':');
        if (lineParts.count() > 1) {
            listWidget_->addItem(lineParts[1].trimmed());
        }
    }
    listWidget_->setCurrentItem(listWidget_->item(0));
    callback_ = outCallback;
}
