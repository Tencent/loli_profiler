#include "screenshotprocess.h"
#include <QBuffer>
#include <QDebug>

ScreenshotProcess::ScreenshotProcess(QObject* parent)
    : AdbProcess(parent) {
}

void ScreenshotProcess::CaptureScreenshot() {
    QStringList arguments;
    arguments << "exec-out" << "screencap -p";
    ExecuteAsync(arguments);
}

void ScreenshotProcess::OnProcessFinihed() {
    auto ba = process_->readAll();
    process_->close();
    if (screenshot_.loadFromData(ba)) {
        screenshot_ = screenshot_.scaled(256, 256, Qt::KeepAspectRatio);
        screenshotBytes_ = QByteArray();
        QBuffer buffer(&screenshotBytes_);
        buffer.open(QIODevice::WriteOnly);
        screenshot_.save(&buffer, "JPG", 50);
    }
}
