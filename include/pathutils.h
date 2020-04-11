#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <QWidget>
#include <QString>

class PathUtils {
public:
    static QString GetADBExecutablePath();
    static QString GetPythonExecutablePath();
    static QString GetAddr2lineExecutablePath(bool armv7 = true);
    static void SetNDKPath(const QString& path);
    static void SetSDKPath(const QString& path);
    static QString GetNDKPath() {
        return ndkPath_;
    }
    static QString GetSDKPath() {
        return sdkPath_;
    }
    static QString GetEnvVar(const char* var);
    static QString SearchAndroidSDK();
    static QString SearchAndroidNDK();

private:
    static QString ndkPath_;
    static QString sdkPath_;
};

#endif // PATHUTILS_H
