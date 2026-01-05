#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H

#include <QString>
#include <QStringList>

// Forward declarations to avoid Qt Widgets dependency
class QWidget;

#ifndef NO_GUI_MODE
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
#else
// CLI-only mode: ConfigDialog is just a namespace for settings
class ConfigDialog {
public:
#endif

    struct Settings {
        int threshold_ = 128;
        QString mode_ = "strict";
        QString build_ = "default";
        QString type_ = "white list";
        QString arch_ = "armeabi-v7a";
        QString compiler_ = "gcc";
        QString hook_ = "malloc";
        QStringList whitelist_;
        QStringList blacklist_;
        Settings() = default;
        Settings(const Settings&) = default;
    };
    static Settings ParseConfigFile();
    static bool IsNoStackMode();
    static Settings GetCurrentSettings();
    
#ifndef NO_GUI_MODE
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
#endif
};

#endif // CONFIGDIALOG_H
