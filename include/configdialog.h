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
    void LoadConfigFile();
    void ReadCurrentSettings();
    void WriteCurrentSettings();
    void OnPasteClipboard();
    void SaveConfigFile();

    struct Settings {
        int threshold_ = 128;
        QString mode_ = "strict";
        QString build_ = "default";
        QString type_ = "white list";
        QString arch_ = "armeabi-v7a";
        QString compiler_ = "gcc";
        QStringList whitelist_;
        QStringList blacklist_;
        Settings() = default;
        Settings(const Settings&) = default;
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
    void on_btnLoad_clicked();
    void on_btnSave_clicked();

private:
    Ui::ConfigDialog *ui;
};

#endif // CONFIGDIALOG_H
