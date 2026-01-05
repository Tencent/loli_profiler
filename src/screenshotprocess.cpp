#include "screenshotprocess.h"
#include <QBuffer>
#include <QDebug>

#ifndef NO_GUI_MODE
#include <QPixmap>

ScreenshotProcess::ScreenshotProcess(QObject* parent)
    : AdbProcess(parent) {
}

const QPixmap ScreenshotProcess::GetScreenshot() const {
    return screenshot_;
}
#else
ScreenshotProcess::ScreenshotProcess(QObject* parent)
    : AdbProcess(parent) {
}
#endif

void ScreenshotProcess::CaptureScreenshot() {
    QStringList arguments;
    // Inject device serial if set
    if (!deviceSerial_.isEmpty()) {
        arguments << "-s" << deviceSerial_;
    }
    arguments << "exec-out" << "screencap -p";
    ExecuteAsync(arguments);
}

void ScreenshotProcess::OnProcessFinihed() {
    auto ba = process_->readAll();
    process_->close();
#ifndef NO_GUI_MODE
    if (screenshot_.loadFromData(ba)) {
        screenshot_ = screenshot_.scaled(256, 256, Qt::KeepAspectRatio);
        screenshotBytes_ = QByteArray();
        QBuffer buffer(&screenshotBytes_);
        buffer.open(QIODevice::WriteOnly);
        screenshot_.save(&buffer, "JPG", 50);
    }
#else
    // CLI mode: just compress the raw data
    screenshotBytes_ = ba;
#endif
}
