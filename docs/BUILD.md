# Build

## Requirement

* QT 5.15 / 5.14 / 5.12 (Requires QtCharts plugin)
* CMake
* Android NDK r16b / r20
* Visual Studio 2017 / XCode / Command line tools

MacOS:

```shell
export QT5Path=/Users/yourname/Qt5.14.1
export Ndk_R16_CMD=/android-ndk-r16b/ndk-build
export Ndk_R20_CMD=/android-ndk-r20b/ndk-build
sh build.sh
```

Windows:

```bash
set QT5Path="D:/SDK/QT/5.14.1/msvc2017_64"
set MSBUILD_EXE="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"
set Ndk_R16_CMD="/android-ndk-r16b/ndk-build.cmd"
set Ndk_R20_CMD="/android-ndk-r20b/ndk-build.cmd"
build.bat
```

