#include "deviceselectiondialog.h"
#include "ui_deviceselectiondialog.h"

DeviceSelectionDialog::DeviceSelectionDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DeviceSelectionDialog)
{
    ui->setupUi(this);
    setWindowTitle("Select Android Device");
}

DeviceSelectionDialog::~DeviceSelectionDialog()
{
    delete ui;
}

void DeviceSelectionDialog::SetDevices(const QList<DeviceInfo>& devices)
{
    devices_ = devices;
    ui->deviceListWidget->clear();
    
    for (const auto& device : devices_) {
        ui->deviceListWidget->addItem(device.GetDisplayText());
    }
    
    if (!devices_.isEmpty()) {
        ui->deviceListWidget->setCurrentRow(0);
    }
}

QString DeviceSelectionDialog::GetSelectedDeviceSerial() const
{
    return selectedSerial_;
}

void DeviceSelectionDialog::on_buttonBox_accepted()
{
    int currentRow = ui->deviceListWidget->currentRow();
    if (currentRow >= 0 && currentRow < devices_.size()) {
        selectedSerial_ = devices_[currentRow].serial;
    }
    accept();
}

void DeviceSelectionDialog::on_buttonBox_rejected()
{
    reject();
}
