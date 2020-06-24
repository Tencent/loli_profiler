#include "configdialog.h"
#include "ui_configdialog.h"
#include "pathutils.h"

#include <QClipboard>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QMenu>
#include <QTextStream>
#include <QSettings>
#include <QDebug>

static ConfigDialog::Settings currentSettings_;
static bool settingsInitialized_ = false;

ConfigDialog::ConfigDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::ConfigDialog) {
    ui->setupUi(this);
}

ConfigDialog::~ConfigDialog() {
    delete ui;
}

void ConfigDialog::LoadConfigFile(const QString& arch, const QString& compiler) {
    ui->lineEditSDKFolder->setText(PathUtils::GetSDKPath());
    ui->lineEditNDKFolder->setText(PathUtils::GetNDKPath());
    ParseConfigFile();
    ui->compilerComboBox->setCurrentText(compiler);
    ui->modeComboBox->setCurrentText(currentSettings_.mode_);
    ui->buildComboBox->setCurrentText(currentSettings_.build_);
    ui->archComboBox->setCurrentText(arch);
    ui->typeComboBox->setCurrentText(currentSettings_.type_);
    ui->blacklistWidget->addItems(currentSettings_.blacklist_);
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
    ui->libraryStackedWidget->setCurrentIndex(ui->typeComboBox->currentIndex());
}

void ConfigDialog::OnPasteClipboard() {
    const auto clipboard = QApplication::clipboard();
    const auto mimeData = clipboard->mimeData();
    if (!mimeData->hasText()) {
        QMessageBox::warning(this, "Warning", "Invalide clipboard data!");
        return;
    }
    auto textData = mimeData->text();
    auto parts = textData.split(',', QString::SplitBehavior::SkipEmptyParts);
    auto listwidget = ui->libraryStackedWidget->currentIndex() == 0 ? ui->whitelistWidget : ui->blacklistWidget;
    for (auto& part : parts) {
        if (part.size() > 0) {
            listwidget->addItem(part);
            auto item = listwidget->item(listwidget->count() - 1);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }
}

QString ConfigDialog::GetArchString() const {
    return ui->archComboBox->currentText();
}

QString ConfigDialog::GetCompilerString() const {
    return ui->compilerComboBox->currentText();
}

ConfigDialog::Settings ConfigDialog::ParseConfigFile() {
    if (!CreateIfNoConfigFile(nullptr)) {
        return currentSettings_;
    }
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    QFile file(cfgPath + "/loli2.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(file.readAll());
        QString line;
        while (stream.readLineInto(&line)) {
            auto words = line.split(':');
            if (words.size() < 2)
                continue;
            if (words[0] == "threshold") {
                currentSettings_.threshold_ = words[1].toInt();
            } else if (words[0] == "whitelist") {
                currentSettings_.whitelist_ = words[1].split(',', QString::SplitBehavior::SkipEmptyParts);
            } else if (words[0] == "blacklist") {
                currentSettings_.blacklist_ = words[1].split(',', QString::SplitBehavior::SkipEmptyParts);
            } else if (words[0] == "mode") {
                currentSettings_.mode_ = words[1];
            } else if (words[0] == "build") {
                currentSettings_.build_ = words[1];
            } else if (words[0] == "type") {
                currentSettings_.type_ = words[1];
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

bool ConfigDialog::CreateIfNoConfigFile(QWidget *parent) {
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    QFile file(cfgPath + "/loli2.conf");
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
                      "mode:strict" << endl << "build:default" << endl << "type:white list";
            stream.flush();
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void ConfigDialog::on_ConfigDialog_finished(int) {
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    QStringList libraries;
    QFile file(cfgPath + "/loli2.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "threshold:" << ui->thresholdSpinBox->value() << endl;
        stream << "whitelist:";
        auto numLibs = ui->whitelistWidget->count();
        for (int i = 0; i < numLibs; i++) {
            libraries << ui->whitelistWidget->item(i)->text();
            stream << ui->whitelistWidget->item(i)->text();
            if (i != numLibs - 1) stream << ',';
        }
        stream << endl;
        stream << "blacklist:";
        numLibs = ui->blacklistWidget->count();
        for (int i = 0; i < numLibs; i++) {
            libraries << ui->blacklistWidget->item(i)->text();
            stream << ui->blacklistWidget->item(i)->text();
            if (i != numLibs - 1) stream << ',';
        }
        stream << endl;
        stream << "mode:" << ui->modeComboBox->currentText();
        stream << endl;
        stream << "build:" << ui->buildComboBox->currentText();
        stream << endl;
        stream << "type:" << ui->typeComboBox->currentText();
        stream.flush();
    }
    currentSettings_.mode_ = ui->modeComboBox->currentText();
    currentSettings_.build_ = ui->buildComboBox->currentText();
    currentSettings_.type_ = ui->typeComboBox->currentText();
    currentSettings_.whitelist_ = libraries;
    currentSettings_.threshold_ = ui->thresholdSpinBox->value();
    file.close();
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
