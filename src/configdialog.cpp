#include "configdialog.h"
#ifndef NO_GUI_MODE
#include "ui_configdialog.h"
#include "selectappdialog.h"
#include <QClipboard>
#include <QMimeData>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QListWidget>
#include <QLineEdit>
#endif

#include "pathutils.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QSettings>
#include <QDebug>

static ConfigDialog::Settings currentSettings_;
static QMap<QString, ConfigDialog::Settings> savedSettings_;
static bool settingsInitialized_ = false;

#ifndef NO_GUI_MODE
ConfigDialog::ConfigDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::ConfigDialog) {
    ui->setupUi(this);
}

ConfigDialog::~ConfigDialog() {
    delete ui;
}

void ConfigDialog::LoadConfigFile() {
    ui->lineEditSDKFolder->setText(PathUtils::GetSDKPath());
    ui->lineEditNDKFolder->setText(PathUtils::GetNDKPath());
    ParseConfigFile();
    ReadCurrentSettings();
}

void ConfigDialog::ReadCurrentSettings() {
    ui->compilerComboBox->setCurrentText(currentSettings_.compiler_);
    ui->modeComboBox->setCurrentText(currentSettings_.mode_);
    ui->buildComboBox->setCurrentText(currentSettings_.build_);
    ui->archComboBox->setCurrentText(currentSettings_.arch_);
    ui->hookComboBox->setCurrentText(currentSettings_.hook_);
    ui->blacklistWidget->clear();
    ui->blacklistWidget->addItems(currentSettings_.blacklist_);
    ui->whitelistWidget->clear();
    ui->whitelistWidget->addItems(currentSettings_.whitelist_);
    for (int i = 0; i < ui->blacklistWidget->count(); i++) {
        auto item = ui->blacklistWidget->item(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    for (int i = 0; i < ui->whitelistWidget->count(); i++) {
        auto item = ui->whitelistWidget->item(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    ui->thresholdSpinBox->setValue(currentSettings_.threshold_);
    ui->typeComboBox->setCurrentText(currentSettings_.type_);
    ui->libraryStackedWidget->setCurrentIndex(ui->typeComboBox->currentIndex());
}

void ConfigDialog::WriteCurrentSettings() {
    currentSettings_.mode_ = ui->modeComboBox->currentText();
    currentSettings_.build_ = ui->buildComboBox->currentText();
    currentSettings_.type_ = ui->typeComboBox->currentText();
    currentSettings_.threshold_ = ui->thresholdSpinBox->value();
    currentSettings_.arch_ = ui->archComboBox->currentText();
    currentSettings_.compiler_ = ui->compilerComboBox->currentText();
    currentSettings_.hook_ = ui->hookComboBox->currentText();
    auto numLibs = ui->whitelistWidget->count();
    currentSettings_.whitelist_.clear();
    for (int i = 0; i < numLibs; i++) {
        currentSettings_.whitelist_ << ui->whitelistWidget->item(i)->text();
    }
    currentSettings_.blacklist_.clear();
    numLibs = ui->blacklistWidget->count();
    for (int i = 0; i < numLibs; i++) {
        currentSettings_.blacklist_ << ui->blacklistWidget->item(i)->text();
    }
}

void ConfigDialog::OnPasteClipboard() {
    const auto clipboard = QApplication::clipboard();
    const auto mimeData = clipboard->mimeData();
    if (!mimeData->hasText()) {
        QMessageBox::warning(this, "Warning", "Invalide clipboard data!");
        return;
    }
    auto textData = mimeData->text();
    auto parts = textData.split(',', QString::SkipEmptyParts);
    auto listwidget = ui->libraryStackedWidget->currentIndex() == 0 ? ui->whitelistWidget : ui->blacklistWidget;
    for (auto& part : parts) {
        if (part.size() > 0) {
            listwidget->addItem(part);
            auto item = listwidget->item(listwidget->count() - 1);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }
}

void ConfigDialog::SaveConfigFile() {
    auto cfgPaths = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    auto cfgPath = cfgPaths.first();
    WriteCurrentSettings();
    QFile file(cfgPath + "/loli3.conf");
    file.remove();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream(&file);
        auto saveSettings = [](QTextStream& stream, const Settings& settings) {
            stream << "threshold:" << settings.threshold_ << endl;
            stream << "whitelist:";
            auto numLibs = settings.whitelist_.count();
            for (int i = 0; i < numLibs; i++) {
                stream << settings.whitelist_[i];
                if (i != numLibs) stream << ',';
            }
            stream << endl;
            stream << "blacklist:";
            numLibs = settings.blacklist_.count();
            for (int i = 0; i < numLibs; i++) {
                stream << settings.blacklist_[i];
                if (i != numLibs) stream << ',';
            }
            stream << endl;
            stream << "mode:" << settings.mode_ << endl;
            stream << "build:" << settings.build_ << endl;
            stream << "type:" << settings.type_ << endl;
            stream << "arch:" << settings.arch_ << endl;
            stream << "compiler:" << settings.compiler_ << endl;
            stream << "hook:" << settings.hook_ << endl;
        };
        saveSettings(stream, currentSettings_);
        for (auto it = savedSettings_.begin(); it != savedSettings_.end(); ++it) {
            stream << "saved:" << it.key() << endl;
            saveSettings(stream, it.value());
        }
        stream.flush();
    }
    file.close();
}
#endif // NO_GUI_MODE

ConfigDialog::Settings ConfigDialog::ParseConfigFile() {
#ifndef NO_GUI_MODE
    if (!CreateIfNoConfigFile(nullptr)) {
        return currentSettings_;
    }
#endif
    
    // Read existing config file
    auto cfgPathList = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    auto cfgPathStr = cfgPathList.first();
    QFile file(cfgPathStr + "/loli3.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    Settings* settings = &currentSettings_;
    savedSettings_.clear();
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(file.readAll());
        QString line;
        while (stream.readLineInto(&line)) {
            auto words = line.split(':');
            if (words.size() < 2)
                continue;
            if (words[0] == "threshold") {
                settings->threshold_ = words[1].toInt();
            } else if (words[0] == "whitelist") {
                settings->whitelist_ = words[1].split(',', QString::SkipEmptyParts);
            } else if (words[0] == "blacklist") {
                settings->blacklist_ = words[1].split(',', QString::SkipEmptyParts);
            } else if (words[0] == "mode") {
                settings->mode_ = words[1];
            } else if (words[0] == "build") {
                settings->build_ = words[1];
            } else if (words[0] == "type") {
                settings->type_ = words[1];
            } else if (words[0] == "arch") {
                settings->arch_ = words[1];
            } else if (words[0] == "compiler") {
                settings->compiler_ = words[1];
            } else if (words[0] == "hook") {
                settings->hook_ = words[1];
            } else if (words[0] == "saved") {
                settings = &savedSettings_[words[1]];
            }
        }
    }
    file.close();
    settingsInitialized_ = true;
    return currentSettings_;
}

ConfigDialog::Settings ConfigDialog::GetCurrentSettings() {
    if (!settingsInitialized_) {
        ParseConfigFile();
    }
    return currentSettings_;
}

bool ConfigDialog::IsNoStackMode() {
    return GetCurrentSettings().mode_ == "nostack";
}

#ifndef NO_GUI_MODE
bool ConfigDialog::CreateIfNoConfigFile(QWidget *parent) {
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    QFile file(cfgPath + "/loli3.conf");
    if (!file.exists()) {
        if (!QDir(cfgPath).mkpath(cfgPath)) {
            QMessageBox::warning(parent, "Warning", "Can't create application data path: " + cfgPath);
            return false;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << "threshold:256" << endl <<
                      "whitelist:libunity,libil2cpp" << endl <<
                      "blacklist:libloli,libart,libc++,libc,libcutils" << endl <<
                      "mode:strict" << endl << "build:default" << endl << "type:white list" << endl <<
                      "arch:armeabi-v7a" << endl << "compiler:gcc";
            stream.flush();
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void ConfigDialog::on_ConfigDialog_finished(int) {
    SaveConfigFile();
}

void ConfigDialog::on_btnSDKFolder_clicked() {
    QString oldPath = PathUtils::GetSDKPath();
    QString startPath = QDir::homePath();
    if (!oldPath.isEmpty() && QFile::exists(oldPath))
        startPath = oldPath;
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Android SDK Directory"), startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty()) {
        PathUtils::SetSDKPath(path);
        ui->lineEditSDKFolder->setText(path);
    }
}

void ConfigDialog::on_btnNDKFolder_clicked() {
    QString oldPath = PathUtils::GetNDKPath();
    QString startPath = QDir::homePath();
    if (!oldPath.isEmpty() && QFile::exists(oldPath))
        startPath = oldPath;
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Android NDK Directory"), startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty()) {
        PathUtils::SetNDKPath(path);
        ui->lineEditNDKFolder->setText(path);
    }
}

void ConfigDialog::on_modeComboBox_currentIndexChanged(const QString &arg) {
    ui->thresholdSpinBox->setEnabled(arg != "nostack");
    ui->buildComboBox->setEnabled(arg != "nostack");
}

void ConfigDialog::on_typeComboBox_currentIndexChanged(int index) {
    ui->libraryStackedWidget->setCurrentIndex(index);
}

void ConfigDialog::on_btnLoad_clicked() {
    QDialog dialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    QListWidget* listWidget = new QListWidget(&dialog);
    auto layout = new QVBoxLayout();
    layout->setMargin(2);
    layout->setSpacing(2);
    listWidget->setSelectionMode(QListWidget::SelectionMode::SingleSelection);
    listWidget->addItems(savedSettings_.keys());
    listWidget->setContextMenuPolicy(Qt::ContextMenuPolicy::ActionsContextMenu);
    QAction* deleteAction = new QAction("Delete Selected", listWidget);
    bool anyItemRemoved = false;
    connect(deleteAction, &QAction::triggered, [&](){
        auto selectedItems = listWidget->selectedItems();
        if (selectedItems.size() == 0)
            return;
        auto item = selectedItems[0];
        savedSettings_.remove(item->text());
        delete listWidget->takeItem(listWidget->row(item));
        anyItemRemoved = true;
    });
    listWidget->addAction(deleteAction);
    connect(listWidget, &QListWidget::itemActivated, [&](QListWidgetItem* item) {
        currentSettings_ = savedSettings_[item->text()];
        ReadCurrentSettings();
        dialog.close();
    });
    layout->addWidget(listWidget);
    listWidget->setFocus();
    dialog.setLayout(layout);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle("Load Saved Settings");
    dialog.setMinimumSize(400, 300);
    dialog.exec();
    if (anyItemRemoved) {
        SaveConfigFile();
    }
}

void ConfigDialog::on_btnSave_clicked() {
    QDialog dialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout();
    layout->setMargin(2);
    layout->setSpacing(2);
    auto listWidget = new QListWidget();
    listWidget->setSelectionMode(QListWidget::SelectionMode::SingleSelection);
    listWidget->addItems(savedSettings_.keys());
    listWidget->setCurrentItem(listWidget->item(0));
    auto lineEdit = new ArrowLineEdit(listWidget);
    lineEdit->setPlaceholderText("Type name here and press enter to commit.");
    connect(listWidget, &QListWidget::itemActivated, [&](QListWidgetItem *item) {
        lineEdit->setText(item->text());
        lineEdit->setFocus();
    });
    connect(lineEdit, &QLineEdit::returnPressed, [&]() {
        auto key = lineEdit->text();
        if (key.size() == 0) {
            return;
        }
        if (savedSettings_.contains(key)) {
            if (QMessageBox::question(&dialog, "Attention",
                                      QString("%1 exists, override?").arg(key)) == QMessageBox::No) {
                return;
            }
        }
        WriteCurrentSettings();
        savedSettings_[key] = currentSettings_;
        dialog.close();
    });
    layout->addWidget(lineEdit);
    layout->addWidget(listWidget);
    lineEdit->setFocus();
    dialog.setLayout(layout);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle("Saved Current Settings");
    dialog.setMinimumSize(300, 50);
    dialog.exec();
}
#endif // NO_GUI_MODE
