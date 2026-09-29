# macOS build

The Python build driver builds the Qt-free ImGui/SFML app bundle, headless CLI, Android hook libraries, and `dist/LoliProfiler-macos.zip`. It requires Xcode command-line tools, CMake 3.24+, Python 3.10+, and an Android NDK with `ndk-build`.

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
python3 scripts/build.py
```

With no arguments the Python build driver shows an interactive menu. `python3 scripts/build.py --mode full --non-interactive` is the scripted equivalent when the NDK is installed. `Ndk_R20_CMD` may be set instead of `ANDROID_NDK_HOME`; `Ndk_R16_CMD` is optional for legacy GCC/armeabi hooks. Missing Android tools require an interactive confirmation or explicit `--download-missing`, and Google licenses remain for `sdkmanager` to prompt. The package contains both the app bundle and the CLI. The GUI's Python injector, hook libraries, and cat icon are placed relative to its executable inside the bundle; the CLI also has its own copies at the archive root.

This script and package layout have passed static checks on Windows, but the macOS build and launch still need an on-macOS test. See [Build and package](BUILD.md) and [Quick start](QUICK_START.md).
