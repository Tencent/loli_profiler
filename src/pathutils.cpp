#include "pathutils.h"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>

QString PathUtils::ndkPath_ = QString();
QString PathUtils::sdkPath_ = QString();
QString PathUtils::pythonPath_ = QString();

const QString SETTINGS_PYTHON_PATH = "PythonPath";


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
    // Try to find Python in NDK first
    if (!ndkPath_.isEmpty() && QFile::exists(ndkPath_)) {
        QString pythonPath;
#if defined(Q_OS_WIN)
        pythonPath = ndkPath_ + "/prebuilt/windows-x86_64/bin/python.exe";
#elif defined(Q_OS_MACOS)
        pythonPath = ndkPath_ + "/prebuilt/darwin-x86_64/bin/python";
#elif defined(Q_OS_LINUX)
        pythonPath = ndkPath_ + "/prebuilt/linux-x86_64/bin/python";
#endif
        if (QFile::exists(pythonPath))
            return pythonPath;
    }
    
    // Fallback to user-specified Python path
    if (!pythonPath_.isEmpty() && QFile::exists(pythonPath_)) {
        return pythonPath_;
    }
    
    // If fallback path doesn't exist or is not set, prompt user to select Python 2.x
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle("Python Not Found");
    msgBox.setText("Python executable not found in NDK.");
    msgBox.setInformativeText("Please select a Python 2.x (e.g., Python 2.7) executable for the application to use.");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Ok);
    
    if (msgBox.exec() == QMessageBox::Ok) {
        QString selectedPath = QFileDialog::getOpenFileName(
            nullptr,
            "Select Python 2.x Executable",
            QString(),
#if defined(Q_OS_WIN)
            "Executable Files (*.exe);;All Files (*.*)"
#else
            "All Files (*)"
#endif
        );
        
        if (!selectedPath.isEmpty() && QFile::exists(selectedPath)) {
            pythonPath_ = selectedPath;
            SavePythonPathSettings();
            return pythonPath_;
        }
    }
    
    return QString();
}


namespace {
    #if defined(Q_OS_WIN)
        constexpr const char* NDK_PREBUILT_HOST = "windows-x86_64";
        constexpr const char* NDK_TOOL_EXT = ".exe";
    #elif defined(Q_OS_MACOS)
        constexpr const char* NDK_PREBUILT_HOST = "darwin-x86_64";
        constexpr const char* NDK_TOOL_EXT = "";
    #elif defined(Q_OS_LINUX)
        constexpr const char* NDK_PREBUILT_HOST = "linux-x86_64";
        constexpr const char* NDK_TOOL_EXT = "";
    #else
        #error "Unsupported OS"
    #endif

    inline QString MakeNDKToolPath(const QString& ndkPath, const char* toolchain, const char* prefix, const QString& name) {
        return ndkPath + toolchain + "/prebuilt/" + NDK_PREBUILT_HOST + "/bin/" + prefix + name + NDK_TOOL_EXT;
    }
}

QString PathUtils::GetNDKToolPath(const QString& name, bool armv7) {
    if (ndkPath_.isEmpty() || !QFile::exists(ndkPath_)) {
        return QString();
    }
    
    QString toolPath;
    if (armv7) {
        toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/arm-linux-androideabi-4.9", "arm-linux-androideabi-", name);
        if (QFile::exists(toolPath))
            return toolPath;
    } else {
        toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/aarch64-linux-android-4.9", "aarch64-linux-android-",  name);
        if (QFile::exists(toolPath))
            return toolPath;

        toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/llvm", "llvm-", name);
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

void PathUtils::LoadPythonPathSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    QString savedPath = settings.value(SETTINGS_PYTHON_PATH).toString();
    if (!savedPath.isEmpty() && QFile::exists(savedPath)) {
        pythonPath_ = savedPath;
    }
}

void PathUtils::SavePythonPathSettings() {
    QSettings settings("MoreFun", "LoliProfiler");
    if (!pythonPath_.isEmpty() && QFile::exists(pythonPath_)) {
        settings.setValue(SETTINGS_PYTHON_PATH, pythonPath_);
    }
}


QString PathUtils::GetEnvVar(const char* var) {
#ifdef Q_OS_WIN
    #if defined (__MINGW32__) || defined (__MINGW64__)
        return getenv(var);
    #else
        QString result;
        char* nameBuffer = nullptr;
        size_t nameSize = 0;
        if (_dupenv_s(&nameBuffer, &nameSize, var) == 0 && nameBuffer != nullptr) {
            result = QString(nameBuffer);
            free(nameBuffer);
        }
        return result;
    #endif
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
    QString sdkPath = "/Users/" + username + "/Library/Android/sdk";
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
