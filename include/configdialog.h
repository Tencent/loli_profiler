#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QDialog>

namespace Ui {
class ConfigDialog;
}

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget *parent = nullptr);
    ~ConfigDialog();
    void LoadConfigFile(const QString& arch);
    QString GetArchString() const;

    static bool CreateIfNoConfigFile(QWidget *parent = nullptr);
private slots:
    void on_ConfigDialog_finished(int result);

private:
    Ui::ConfigDialog *ui;
};

#endif // CONFIGDIALOG_H
