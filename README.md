# Overview

![](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/support-UnrealEngine4%20%7C%20UnityEngine-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/release-1.1.1-red.svg?style=flat)
![](https://img.shields.io/badge/android-5.0%20--%2010-blue.svg?style=flat)
![](https://img.shields.io/badge/arch-armeabi%20%7C%20armeabi--v7a%20%7C%20arm64--v8a-blue.svg?style=flat)

**LoliProfiler** is a **C/C++ memory profiling** tool for **Android** games and applications.

LoliProfiler supports profiling debuggable applications out of box. And offers engine specific (UnrealEngine4/Unity) modifications to enable profiling complex or production level games. 

![](res/images/macos.png)

![](res/images/treemap.gif)

# Features

* Profiler client supports Windows 10/7 and Mac OSX Mojave and newer.
* Work with debuggable applications out of box.
* Support attaching to running application.
* Support multiple back-trace implementations.
* Support profiling complex games by doing some mods with your game engine.
* Support detecting c++ code memory leaks(Tested with Unreal Engine 4.26).
* Support profiling release build applications on rooted devices.
* Multiple data view modes: tree map/call tree/memory fragmentation.
* Builtin adb console to be able to exec command.

# Documents

* [Quick Start Guide](docs/QUICK_START.md) [(Chinese)](docs/QUICK_START_CN.md)
* [Working With Game Engines](docs/GAME_ENGINE.md) [(Chinese)](docs/GAME_ENGINE_CN.md)
* [Trouble Shooting](docs/TROUBLE_SHOOTING.md)
* [Build Project](docs/BUILD.md)
* [How Does It Work](docs/PRINCIPLE.md) [(Chinese)](docs/PRINCIPLE_CN.md)

# Special Thanks

* [QT framework](https://www.qt.io/)
* [Perfetto](https://perfetto.dev/)
* [LZ4](https://github.com/lz4/lz4)
* [Chromium](https://chromium.googlesource.com/chromium/src/base/+/master/debug/stack_trace.cc)
* [JDWP injector](https://koz.io/library-injection-for-debuggable-android-apps/)
* [XHook](https://github.com/iqiyi/xHook)
* [Android native memory leak solution](https://developer.aliyun.com/article/708672)
* [YMTreeMap](https://github.com/yahoo/YMTreeMap)
* [qconsolewidget](https://github.com/gapost/qconsolewidget)
* Icons [smashicons](https://www.flaticon.com/authors/smashicons), [freepik](https://www.flaticon.com/authors/freepik)

# License

See [LICENSE File](LICENSE).
