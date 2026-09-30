#include "pathutilslite.h"
#include "appsettings.h"

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

std::string PathUtilsLite::ndkPath_;
std::string PathUtilsLite::sdkPath_;
std::string PathUtilsLite::pythonPath_;

namespace {
constexpr const char* SETTINGS_PYTHON_PATH = "PythonPath";
constexpr const char* SETTINGS_SDK_PATH = "AndroidSDK";
constexpr const char* SETTINGS_NDK_PATH = "AndroidNDK";

#if defined(_WIN32)
constexpr const char* NDK_PREBUILT_HOST = "windows-x86_64";
constexpr const char* NDK_TOOL_EXT = ".exe";
constexpr const char* ADB_RELATIVE = "/platform-tools/adb.exe";
constexpr const char* NDK_PYTHON_RELATIVE = "/prebuilt/windows-x86_64/bin/python.exe";
#elif defined(__APPLE__)
constexpr const char* NDK_PREBUILT_HOST = "darwin-x86_64";
constexpr const char* NDK_TOOL_EXT = "";
constexpr const char* ADB_RELATIVE = "/platform-tools/adb";
constexpr const char* NDK_PYTHON_RELATIVE = "/prebuilt/darwin-x86_64/bin/python";
#elif defined(__linux__)
constexpr const char* NDK_PREBUILT_HOST = "linux-x86_64";
constexpr const char* NDK_TOOL_EXT = "";
constexpr const char* ADB_RELATIVE = "/platform-tools/adb";
constexpr const char* NDK_PYTHON_RELATIVE = "/prebuilt/linux-x86_64/bin/python";
#else
#error "Unsupported OS"
#endif

inline std::string MakeNDKToolPath(const std::string& ndkPath, const char* toolchain,
                                   const char* prefix, const std::string& name) {
    return ndkPath + toolchain + "/prebuilt/" + NDK_PREBUILT_HOST + "/bin/" + prefix +
           name + NDK_TOOL_EXT;
}
} // namespace

bool PathUtilsLite::FileExists(const std::string& path) {
    if (path.empty())
        return false;
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    return ::access(path.c_str(), F_OK) == 0;
#endif
}

std::string PathUtilsLite::GetEnvVar(const char* var) {
#ifdef _WIN32
    char* nameBuffer = nullptr;
    std::size_t nameSize = 0;
    std::string result;
    if (_dupenv_s(&nameBuffer, &nameSize, var) == 0 && nameBuffer != nullptr) {
        result.assign(nameBuffer);
        free(nameBuffer);
    }
    return result;
#else
    const char* v = getenv(var);
    return v ? std::string(v) : std::string();
#endif
}

std::string PathUtilsLite::GetADBExecutablePath() {
    if (!sdkPath_.empty() && FileExists(sdkPath_)) {
        const std::string adbPath = sdkPath_ + ADB_RELATIVE;
        if (FileExists(adbPath))
            return adbPath;
    }
    return std::string();
}

std::string PathUtilsLite::GetPythonExecutablePath() {
    // Try to find Python in NDK first
    if (!ndkPath_.empty() && FileExists(ndkPath_)) {
        const std::string pythonPath = ndkPath_ + NDK_PYTHON_RELATIVE;
        if (FileExists(pythonPath))
            return pythonPath;
    }

    // Fallback to user-specified Python path. (The Qt original also prompts
    // the user with a file dialog here in GUI mode; that interaction belongs
    // to the UI layer and is not ported.)
    if (!pythonPath_.empty() && FileExists(pythonPath_))
        return pythonPath_;
    // Current NDKs may no longer bundle Python. Resolve the host interpreter
    // for CLI capture too, where no file-selection dialog can supply it.
    const std::string path = GetEnvVar("PATH");
#ifdef _WIN32
    const char separator = ';';
    const std::vector<std::string> names = {"python.exe", "python3.exe"};
#else
    const char separator = ':';
    const std::vector<std::string> names = {"python3", "python"};
#endif
    for (const auto& name : names) {
        std::size_t start = 0;
        while (start < path.size()) {
            const auto end = path.find(separator, start);
            const auto directory = path.substr(start, end - start);
            const auto candidate = (std::filesystem::path(directory) / name).string();
            if (!directory.empty() && FileExists(candidate))
                return candidate;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
    return std::string();
}

std::string PathUtilsLite::GetNDKToolPath(const std::string& name, bool armv7) {
    if (ndkPath_.empty() || !FileExists(ndkPath_))
        return std::string();

    std::string toolPath;
    if (armv7) {
        toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/arm-linux-androideabi-4.9",
                                   "arm-linux-androideabi-", name);
        if (FileExists(toolPath))
            return toolPath;
    } else {
        toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/aarch64-linux-android-4.9",
                                   "aarch64-linux-android-", name);
        if (FileExists(toolPath))
            return toolPath;

    }
    // Modern NDKs provide architecture-independent LLVM tools for every ABI.
    toolPath = MakeNDKToolPath(ndkPath_, "/toolchains/llvm", "llvm-", name);
    if (FileExists(toolPath))
        return toolPath;
    return std::string();
}

void PathUtilsLite::SetNDKPath(const std::string& path) {
    if (path.empty() || !FileExists(path))
        ndkPath_ = SearchAndroidNDK();
    else
        ndkPath_ = path;
    AppSettings settings;
    settings.Set(SETTINGS_NDK_PATH, ndkPath_);
    settings.Sync();
}

void PathUtilsLite::SetSDKPath(const std::string& path) {
    if (path.empty() || !FileExists(path))
        sdkPath_ = SearchAndroidSDK();
    else
        sdkPath_ = path;
    AppSettings settings;
    settings.Set(SETTINGS_SDK_PATH, sdkPath_);
    settings.Sync();
}

void PathUtilsLite::LoadPythonPathSettings() {
    AppSettings settings;
    const std::string savedPath = settings.Get(SETTINGS_PYTHON_PATH);
    if (!savedPath.empty() && FileExists(savedPath))
        pythonPath_ = savedPath;
}

void PathUtilsLite::SavePythonPathSettings() {
    if (pythonPath_.empty() || !FileExists(pythonPath_))
        return;
    AppSettings settings;
    settings.Set(SETTINGS_PYTHON_PATH, pythonPath_);
    settings.Sync();
}

std::string PathUtilsLite::SearchAndroidSDK() {
    for (const char* key : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        const std::string path = GetEnvVar(key);
        if (!path.empty() && FileExists(path))
            return path;
    }
#ifdef _WIN32
    const std::string username = GetEnvVar("USERNAME");
    // android studio default path
    const std::string sdkPath = "C:/Users/" + username + "/AppData/Local/Android/sdk";
    if (FileExists(sdkPath))
        return sdkPath;
    // nvidia debugger path: NVPACK\android-sdk-windows on C/D/E/F/G
    const std::vector<const char*> drives = { "C", "D", "E", "F", "G" };
    for (const char* drive : drives) {
        const std::string nvpack = std::string(drive) + ":/NVPACK/android-sdk-windows";
        if (FileExists(nvpack))
            return nvpack;
    }
#elif defined(__APPLE__)
    const std::string sdkPath = GetEnvVar("HOME") + "/Library/Android/sdk";
#else
    const std::string sdkPath = GetEnvVar("HOME") + "/Android/Sdk";
#endif
#ifndef _WIN32
    if (FileExists(sdkPath))
        return sdkPath;
#endif
    return std::string();
}

std::string PathUtilsLite::SearchAndroidNDK() {
    const std::vector<std::string> paths = {
        GetEnvVar("ANDROID_NDK_HOME"),
        GetEnvVar("ANDROID_NDK_ROOT"),
        GetEnvVar("NDK_ROOT"),
        GetEnvVar("NDKROOT"),
    };
    for (const auto& path : paths) {
        if (!path.empty() && FileExists(path))
            return path;
    }
    const std::string sdkPath = GetSDKPath().empty() ? SearchAndroidSDK() : GetSDKPath();
    if (sdkPath.empty())
        return {};
    namespace fs = std::filesystem;
    std::vector<std::pair<std::vector<int>, std::string>> installed;
    std::error_code ec;
    fs::directory_iterator iterator(fs::path(sdkPath) / "ndk", ec);
    const fs::directory_iterator end;
    const std::regex digits("[0-9]+");
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const auto path = iterator->path();
        if (!FileExists((path / "ndk-build").string()) &&
            !FileExists((path / "ndk-build.cmd").string()))
            continue;
        const std::string version = path.filename().string();
        std::vector<int> parts;
        for (std::sregex_iterator match(version.begin(), version.end(), digits), last;
             match != last; ++match)
            parts.push_back(std::stoi(match->str()));
        installed.emplace_back(std::move(parts), path.string());
    }
    if (!installed.empty()) {
        std::sort(installed.begin(), installed.end());
        return installed.back().second;
    }
    if (FileExists(sdkPath + "/ndk-bundle"))
        return sdkPath + "/ndk-bundle";
    return std::string();
}
