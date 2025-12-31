#ifndef DEVICESELECTIONDIALOG_H
#define DEVICESELECTIONDIALOG_H

#include <QDialog>

namespace Ui {
class DeviceSelectionDialog;
}

struct DeviceInfo {
    QString serial;
    QString model;
    QString device;
    QString state;
    
    QString GetDisplayText() const {
        if (model.isEmpty() && device.isEmpty()) {
            return serial + " (" + state + ")";
        }
        return serial + " - " + model + " (" + device + ")";
    }
};

class DeviceSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceSelectionDialog(QWidget *parent = nullptr);
    ~DeviceSelectionDialog();

    void SetDevices(const QList<DeviceInfo>& devices);
    QString GetSelectedDeviceSerial() const;

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::DeviceSelectionDialog *ui;
    QList<DeviceInfo> devices_;
    QString selectedSerial_;
};

#endif // DEVICESELECTIONDIALOG_H
