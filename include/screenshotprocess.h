#ifndef SCREENSHOTPROCESS_H
#define SCREENSHOTPROCESS_H

#include "adbprocess.h"
#include <QByteArray>

#ifndef NO_GUI_MODE
#include <QPixmap>
#else
// Forward declaration for CLI mode
class QPixmap;
#endif

class ScreenshotProcess : public AdbProcess {
public:
    ScreenshotProcess(QObject* parent = nullptr);

#ifndef NO_GUI_MODE
    const QPixmap GetScreenshot() const;
#endif

    const QByteArray GetScreenshotBytes() const {
        return screenshotBytes_;
    }

    void CaptureScreenshot();

protected:
    void OnProcessFinihed() override;

private:
    QString appIdentifier_;
#ifndef NO_GUI_MODE
    QPixmap screenshot_;
#endif
    QByteArray screenshotBytes_;
};

#endif // SCREENSHOTPROCESS_H
