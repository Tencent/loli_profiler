#ifndef SCREENSHOTPROCESS_H
#define SCREENSHOTPROCESS_H

#include "adbprocess.h"
#include <QPixmap>

class ScreenshotProcess : public AdbProcess {
public:
    ScreenshotProcess(QObject* parent = nullptr);

    const QPixmap GetScreenshot() const {
        return screenshot_;
    }

    const QByteArray GetScreenshotBytes() const {
        return screenshotBytes_;
    }

    void CaptureScreenshot();

protected:
    void OnProcessFinihed() override;

private:
    QString appIdentifier_;
    QPixmap screenshot_;
    QByteArray screenshotBytes_;
};

#endif // SCREENSHOTPROCESS_H
