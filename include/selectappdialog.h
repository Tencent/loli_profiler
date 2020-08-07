#ifndef SELECTAPPDIALOG_H
#define SELECTAPPDIALOG_H

#include <QWidget>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>

#include <functional>

class ArrowLineEdit : public QLineEdit {
    Q_OBJECT
public:
    ArrowLineEdit(QListWidget* listView, QWidget *parent = nullptr);
    void keyPressEvent(QKeyEvent *event);
private slots:
    void onTextChanged(const QString&);
private:
    QListWidget* listView_;
};

class SelectAppDialog : public QDialog {
    Q_OBJECT
public:
    SelectAppDialog(QWidget *parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
public:
    void SelectApp(QStringList apps,QLineEdit *appNameLineEdit);
private:
    QListWidget* listWidget_;
    QLineEdit* searchLineEdit_;
    std::function<void(const QString&)> callback_;
};

#endif // SELECTAPPDIALOG_H
