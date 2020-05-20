#ifndef CONFIGLISTWIDGET_H
#define CONFIGLISTWIDGET_H

#include <QListWidget>

class QKeyEvent;
class ConfigListWidget : public QListWidget {
    Q_OBJECT
public:
    ConfigListWidget(QWidget *parent = nullptr);

private slots:
    void OnNewItemClicked();
    void OnNewItemsClicked();
    void OnDeleteItemsClicked();
    void OnClearItemsClicked();
    void OnContextMenuRequested(const QPoint &pos);

signals:
    void OnPasteText(ConfigListWidget* widget);

protected:
    void ParseString(const QString& str);
    void OnPasteClipboard();
    void keyPressEvent(QKeyEvent* event) override;
};

#endif // CONFIGLISTWIDGET_H
