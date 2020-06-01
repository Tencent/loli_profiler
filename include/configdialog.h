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
    void OnPasteClipboard();
    QString GetArchString() const;

    struct Settings {
        int threshold_ = 128;
        QStringList whitelist_;
        QStringList blacklist_;
        QString mode_ = "strict";
        QString type_ = "white list";
    };
    static Settings ParseConfigFile();
    static bool IsNoStackMode();
    static Settings GetCurrentSettings();
private:
    static bool CreateIfNoConfigFile(QWidget *parent = nullptr);

private slots:
    void on_ConfigDialog_finished(int result);
    void on_btnSDKFolder_clicked();
    void on_btnNDKFolder_clicked();
    void on_modeComboBox_currentIndexChanged(const QString &arg);
    void on_typeComboBox_currentIndexChanged(int index);

private:
    Ui::ConfigDialog *ui;
};

#endif // CONFIGDIALOG_H
