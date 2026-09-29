#ifndef PATHUTILSLITE_H
#define PATHUTILSLITE_H

#include <string>

// Qt-free replacement for include/pathutils.h (class PathUtils).
//
// Ports the pure-path logic (SDK/NDK/python lookup, env vars, file checks).
// The GUI-only QFileDialog/QMessageBox python-prompt is NOT ported (callers
// own any user interaction); the QSettings-backed persistence goes through
// AppSettings (same keys: "AndroidSDK", "AndroidNDK", "PythonPath").
//
// NOTE: the Qt PathUtils read the Windows registry via QSettings. This
// Qt-free store reads loli_settings.json next to the executable instead, so
// one-time re-selection of SDK/NDK/python paths is expected when switching
// builds (see design D7).
class PathUtilsLite {
public:
    static std::string GetADBExecutablePath();
    static std::string GetPythonExecutablePath();
    static std::string GetNDKToolPath(const std::string& name, bool armv7 = true);
    static void SetNDKPath(const std::string& path);
    static void SetSDKPath(const std::string& path);
    static const std::string& GetNDKPath() { return ndkPath_; }
    static const std::string& GetSDKPath() { return sdkPath_; }
    static std::string GetEnvVar(const char* var);
    static std::string SearchAndroidSDK();
    static std::string SearchAndroidNDK();
    static void LoadPythonPathSettings();
    static void SavePythonPathSettings();

    // File-exists helper (QFile::exists parity; also true for directories).
    static bool FileExists(const std::string& path);

private:
    static std::string ndkPath_;
    static std::string sdkPath_;
    static std::string pythonPath_;
};

#endif // PATHUTILSLITE_H
