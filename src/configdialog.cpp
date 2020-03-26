#include "include/configdialog.h"
#include "ui_configdialog.h"

#include <QDir>
#include <QFile>
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
    auto cfgPath = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).first();
    auto threshold = 128l;
    auto libraries = QStringList() << "libunity" << "libil2cpp";
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
                threshold = words[1].toLong();
            } else if (words[0] == "libraries") {
                libraries = words[1].split(',', QString::SplitBehavior::SkipEmptyParts);
            }
            lineNum++;
        }
    }
    file.close();
    ui->archComboBox->setCurrentText(arch);
    ui->libraryListWidget->addItems(libraries);
    for (int i = 0; i < ui->libraryListWidget->count(); i++) {
        auto item = ui->libraryListWidget->item(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    ui->libraryListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);
    ui->libraryListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->libraryListWidget, &QListWidget::customContextMenuRequested, [&](const QPoint &pos){
        QMenu menu;
        menu.addAction("New", [&]() {
            ui->libraryListWidget->addItem("libfoo");
            auto item = ui->libraryListWidget->item(ui->libraryListWidget->count() - 1);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        });
        menu.addAction("Delete", [&]() {
            for (int i = 0; i < ui->libraryListWidget->selectedItems().size(); ++i)
                delete ui->libraryListWidget->takeItem(ui->libraryListWidget->currentRow());
        });
        menu.exec(ui->libraryListWidget->mapToGlobal(pos));
    });
    ui->thresholdSpinBox->setValue(threshold);
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
            stream << "threshold:256" << endl << "libraries:libunity,libil2cpp";
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
        stream.flush();
    }
    file.close();
}
