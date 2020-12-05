#include "pathutils.h"

#include <QFile>

QString PathUtils::ndkPath_ = QString();
QString PathUtils::sdkPath_ = QString();

QString PathUtils::GetADBExecutablePath() {
    if (!sdkPath_.isEmpty() && QFile::exists(sdkPath_)) {
#ifdef Q_OS_WIN
        auto adbPath = sdkPath_ + "/platform-tools/adb.exe";
#else
        auto adbPath = sdkPath_ + "/platform-tools/adb";
#endif
        if (QFile::exists(adbPath))
            return adbPath;
    }
    return QString();
}

QString PathUtils::GetPythonExecutablePath() {
    if (!ndkPath_.isEmpty() && QFile::exists(ndkPath_)) {
        QString pythonPath;
#ifdef Q_OS_WIN
        pythonPath = ndkPath_ + "/prebuilt/windows-x86_64/bin/python.exe";
#else
        pythonPath = ndkPath_ + "/prebuilt/darwin-x86_64/bin/python";
#endif
        if (QFile::exists(pythonPath))
            return pythonPath;
    }
    return QString();
}

QString PathUtils::GetNDKToolPath(const QString& name, bool armv7) {
    if (ndkPath_.isEmpty() || !QFile::exists(ndkPath_)) {
        return QString();
    }
    QString toolPath;
    if (armv7) {
#ifdef Q_OS_WIN
        toolPath = ndkPath_ + "/toolchains/arm-linux-androideabi-4.9" +
            QString("/prebuilt/windows-x86_64/bin/arm-linux-androideabi-%1.exe").arg(name);
#else
        toolPath = ndkPath_ + "/toolchains/arm-linux-androideabi-4.9" +
            QString("/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-%1").arg(name);
#endif
        if (QFile::exists(toolPath))
            return toolPath;
    } else {
#ifdef Q_OS_WIN
        toolPath = ndkPath_ + "/toolchains/aarch64-linux-android-4.9" +
            QString("/prebuilt/windows-x86_64/bin/aarch64-linux-android-%1.exe").arg(name);
#else
        toolPath = ndkPath_ + "/toolchains/aarch64-linux-android-4.9" +
            QString("/prebuilt/darwin-x86_64/bin/aarch64-linux-android-%1").arg(name);
#endif
        if (QFile::exists(toolPath))
            return toolPath;
    }
    return QString();
}

void PathUtils::SetNDKPath(const QString& path) {
    if (path.isEmpty() || !QFile::exists(path))
        ndkPath_ = SearchAndroidNDK();
    else
        ndkPath_ = path;
}

void PathUtils::SetSDKPath(const QString& path) {
    if (path.isEmpty() || !QFile::exists(path))
        sdkPath_ = SearchAndroidSDK();
    else
        sdkPath_ = path;
}

QString PathUtils::GetEnvVar(const char* var) {
#ifdef Q_OS_WIN
    QString result;
    char* nameBuffer = nullptr;
    size_t nameSize = 0;
    if (_dupenv_s(&nameBuffer, &nameSize, var) == 0 && nameBuffer != nullptr) {
        result = QString(nameBuffer);
        free(nameBuffer);
    }
    return result;
#else
    return getenv(var);
#endif
}

QString PathUtils::SearchAndroidSDK() {
    QString androidHome = GetEnvVar("ANDROID_HOME");
    if (!androidHome.isEmpty() && QFile::exists(androidHome)) {
        return androidHome;
    }
#ifdef Q_OS_WIN
    QString username = GetEnvVar("USERNAME");
    //android studio default path
    QString sdkPath = "C:/Users/" + username + "/AppData/Local/Android/sdk";
    if (QFile::exists(sdkPath)) {
        //ndk-bundle\prebuilt\windows-x86_64\bin
        return sdkPath;
    }
    else{
        //nvidia debugger path//
        //NVPACK\android-sdk-windows//
        std::vector<QString> cvec = { "C", "D", "E", "F", "G" };
        for(auto iter: cvec){
            auto nvpack = iter.append(":/").append("NVPACK/android-sdk-windows");
            if (QFile::exists(nvpack)){
                return nvpack;
            }
        }
        //https://developer.android.com/ndk/downloads/index.html download path//
        //C/D/E/F/G:\NVPACK/android-sdk-windows//
//        if (QFile::exists(sdkPath.append(username).append("/Downloads"))){
//            isFindFlag = true;
//            //todo: some problem, the path name is android-ndk-r21, android-ndk-r16b. not const
//        }
    }
    //C:\Users\USER\Downloads
#else
    QString username = GetEnvVar("USER");
    QString sdkPath = "Users/" + username + "/Library/Android/sdk";
    if (QFile::exists(sdkPath)){
        return sdkPath;
    }
#endif
    return QString();
}

QString PathUtils::SearchAndroidNDK() {
    auto sdkPath = GetSDKPath();
    if (sdkPath.isEmpty() || !QFile::exists(sdkPath))
        return QString();
    std::vector<QString> pathes = {
        sdkPath + "/ndk-bundle",
        GetEnvVar("ANDROID_NDK_ROOT"),
        GetEnvVar("NDK_ROOT"),
        GetEnvVar("NDKROOT"),
    };
    for (auto& path : pathes) {
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}
