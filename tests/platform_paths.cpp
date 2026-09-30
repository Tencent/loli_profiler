#include "appsettings.h"
#include "launchdriver.h"
#include "pathutilslite.h"
#include "processrunner.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void Env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void File(const fs::path& path, const std::string& contents = "fixture") {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

int main(int argc, char** argv) {
    const auto executable = fs::weakly_canonical(argv[0]);
    const auto originalCwd = fs::current_path();
    const auto scratch = fs::temp_directory_path() / ("loli platform paths " +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        Require(argc > 0, "missing executable path");
        fs::create_directories(scratch / "unrelated cwd");
        fs::current_path(scratch / "unrelated cwd");
        AppSettings settings;
        Require(fs::equivalent(fs::path(settings.GetFilePath()).parent_path(),
                               executable.parent_path()), "settings follow working directory");
        const auto sdk = scratch / "Android SDK";
        const auto ndk = sdk / "ndk" / "27.0.12077973";
#ifdef _WIN32
        const std::string host = "windows-x86_64";
        const std::string ext = ".exe";
        const std::string ndkBuild = "ndk-build.cmd";
#elif defined(__APPLE__)
        const std::string host = "darwin-x86_64";
        const std::string ext;
        const std::string ndkBuild = "ndk-build";
#else
        const std::string host = "linux-x86_64";
        const std::string ext;
        const std::string ndkBuild = "ndk-build";
#endif
        const auto adb = sdk / "platform-tools" / ("adb" + ext);
        File(adb);
        File(sdk / "ndk" / "9.0.0" / ndkBuild);
        File(ndk / ndkBuild);
        const auto tools = ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin";
        File(tools / ("llvm-nm" + ext));
        File(tools / ("llvm-symbolizer" + ext));
        Env("ANDROID_HOME", "");
        Env("ANDROID_SDK_ROOT", sdk.string());
        for (const char* key : {"ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "NDK_ROOT", "NDKROOT"})
            Env(key, "");
        PathUtilsLite::SetSDKPath("");
        Require(PathUtilsLite::GetSDKPath() == sdk.string(), "SDK_ROOT discovery failed");
        Require(fs::equivalent(PathUtilsLite::GetADBExecutablePath(), adb), "adb discovery failed");
        PathUtilsLite::SetNDKPath("");
        Require(PathUtilsLite::GetNDKPath() == ndk.string(), "numeric side-by-side NDK selection failed");
        Require(fs::equivalent(PathUtilsLite::GetNDKToolPath("nm", true), tools / ("llvm-nm" + ext)), "ARMv7 LLVM fallback failed");
        Require(fs::equivalent(PathUtilsLite::GetNDKToolPath("nm", false), tools / ("llvm-nm" + ext)), "ARM64 LLVM fallback failed");
        Require(fs::equivalent(PathUtilsLite::GetNDKToolPath("symbolizer"), tools / ("llvm-symbolizer" + ext)), "host symbolizer discovery failed");
        const auto explicitNdk = scratch / "standalone NDK";
        fs::create_directories(explicitNdk);
        Env("ANDROID_NDK_HOME", explicitNdk.string());
        PathUtilsLite::SetSDKPath((scratch / "missing").string());
        PathUtilsLite::SetNDKPath("");
        Require(PathUtilsLite::GetNDKPath() == explicitNdk.string(), "NDK_HOME discovery failed");
#ifndef _WIN32
        const auto python = scratch / "host bin" / "python3";
        File(python);
        Env("PATH", python.parent_path().string() + ":/usr/bin:/bin");
        Require(PathUtilsLite::GetPythonExecutablePath() == python.string(), "host Python fallback failed");

        // Fail deliberately at adb's first push, after recording its cwd.
        // This checks LaunchDriver's actual subprocess path without a device.
        const auto recorded = scratch / "launch cwd.txt";
        Env("LOLI_TEST_WORKDIR_RECORD", recorded.string());
        File(adb, "#!/bin/sh\npwd > \"$LOLI_TEST_WORKDIR_RECORD\"\nexit 1\n");
        fs::permissions(adb, fs::perms::owner_exec, fs::perm_options::add);
        LaunchDriver driver;
        LaunchDriver::Config config;
        config.adbPath = adb.string();
        config.compiler = "llvm";
        config.arch = "arm64-v8a";
        Require(!driver.Run(config, {}, {}), "fixture adb unexpectedly succeeded");
        std::string cwd;
        std::ifstream incoming(recorded);
        std::getline(incoming, cwd);
        Require(fs::equivalent(cwd, executable.parent_path()), "injector runtime follows working directory");

        ProcessRunner process;
        process.SetProgram("/bin/echo");
        process.SetArguments({"space ' quote $ literal"});
        Require(process.Start() && process.WaitForFinished(5000), "quoted process failed");
        Require(process.ReadAllStdout() == "space ' quote $ literal\n", "process argument quoting corrupted");
#endif
        std::cout << "PASS platform SDK/NDK/Python/settings/launch paths\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::current_path(originalCwd);
    fs::remove_all(scratch);
    fs::remove(executable.parent_path() / "loli_settings.json");
    return result;
}
