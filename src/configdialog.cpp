#include "include/configdialog.h"
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

ConfigDialog::ConfigDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::ConfigDialog) {
    ui->setupUi(this);
}

ConfigDialog::~ConfigDialog() {
    delete ui;
}

void ConfigDialog::LoadConfigFile(const QString& arch) {
    ui->lineEditSDKFolder->setText(PathUtils::GetSDKPath());
    ui->lineEditNDKFolder->setText(PathUtils::GetNDKPath());
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    auto threshold = 128;
    auto libraries = QStringList() << "libunity" << "libil2cpp";
    auto mode = QString("strict");
    auto type = QString("white list");
    QFile file(cfgPath + "/loli2.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(file.readAll());
        QString line;
        int lineNum = 0;
        while (stream.readLineInto(&line)) {
            auto words = line.split(':');
            if (words.size() < 2)
                continue;
            if (words[0] == "threshold") {
                threshold = words[1].toInt();
            } else if (words[0] == "libraries") {
                libraries = words[1].split(',', QString::SplitBehavior::SkipEmptyParts);
            } else if (words[0] == "mode") {
                mode = words[1];
            } else if (words[0] == "type") {
                type = words[1];
            }
            lineNum++;
        }
    }
    file.close();
    ui->modeComboBox->setCurrentText(mode);
    ui->archComboBox->setCurrentText(arch);
    ui->typeComboBox->setCurrentText(type);
    ui->libraryListWidget->addItems(libraries);
    for (int i = 0; i < ui->libraryListWidget->count(); i++) {
        auto item = ui->libraryListWidget->item(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    ui->libraryListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);
    ui->libraryListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->libraryListWidget->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
    connect(ui->libraryListWidget, &QListWidget::customContextMenuRequested, [&](const QPoint &pos){
        QMenu menu;
        menu.addAction("New", [this]() {
            ui->libraryListWidget->addItem("libfoo");
            auto item = ui->libraryListWidget->item(ui->libraryListWidget->count() - 1);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        });
        menu.addAction("Delete", [this]() {
            auto items = ui->libraryListWidget->selectedItems();
            for (auto item : items) {
                delete ui->libraryListWidget->takeItem(ui->libraryListWidget->row(item));
            }
        });
        menu.addAction("Clear", [this]() {
            while (ui->libraryListWidget->count() > 0) {
                delete ui->libraryListWidget->takeItem(0);
            }
        });
        menu.addAction("Paste", [this]() { OnPasteClipboard(); });
        menu.addAction("Clear And Paste", [this]() {
            while (ui->libraryListWidget->count() > 0) {
                delete ui->libraryListWidget->takeItem(0);
            }
            OnPasteClipboard();
        });
        menu.exec(ui->libraryListWidget->mapToGlobal(pos));
    });
    ui->thresholdSpinBox->setValue(threshold);
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
    for (auto& part : parts) {
        if (part.size() > 0) {
            ui->libraryListWidget->addItem(part);
            auto item = ui->libraryListWidget->item(ui->libraryListWidget->count() - 1);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }
}

QString ConfigDialog::GetArchString() const {
    return ui->archComboBox->currentText();
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
            stream << "threshold:256" << endl << "libraries:libunity,libil2cpp" << endl << "mode:strict" << endl << "type:white list";
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
    QFile file(cfgPath + "/loli2.conf");
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "threshold:" << ui->thresholdSpinBox->value() << endl;
        stream << "libraries:";
        auto numLibs = ui->libraryListWidget->count();
        for (int i = 0; i < numLibs; i++) {
            stream << ui->libraryListWidget->item(i)->text();
            if (i != numLibs - 1) stream << ',';
        }
        stream << endl;
        stream << "mode:" << ui->modeComboBox->currentText();
        stream << endl;
        stream << "type:" << ui->typeComboBox->currentText();
        stream.flush();
    }
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
