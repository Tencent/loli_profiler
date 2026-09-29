# Linux build

The Linux build uses the same Qt-free C++ core as Windows. It builds the ImGui/SFML GUI, headless CLI, Android hook libraries, and `dist/LoliProfiler-linux.zip`.

## Host dependencies

Use CMake 3.24+, a C++17 compiler, Python 3.8+ for packaging (3.10+ for `agentcli`), and an Android NDK. A graphical build needs X11/OpenGL development packages and GTK 3 for native file dialogs. On Debian/Ubuntu, the relevant packages include `build-essential`, `pkg-config`, `libgtk-3-dev`, `libx11-dev`, `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libudev-dev`, `libgl1-mesa-dev`, and `libfreetype6-dev`.

Set `ANDROID_NDK_HOME` to a recent NDK containing `ndk-build`, or set `Ndk_R20_CMD` directly. `Ndk_R16_CMD` is optional and builds legacy GCC hooks when available.

## Build and package

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
python3 scripts/build.py
```

With no arguments the Python build driver shows an interactive menu. `python3 scripts/build.py --mode full --non-interactive` runs the complete build in automation when the SDK/NDK is installed. It builds `LoliProfilerImGui`, `LoliProfilerCLI`, and ARM/x86 LLVM hooks, then verifies and writes `dist/LoliProfiler-linux.zip`. Existing `build/` and `dist/` contents are preserved except for replacement of that named zip. Missing Android tools are only downloaded after an interactive confirmation or an explicit `--download-missing` flag; Google's `sdkmanager` handles license prompts.

For a container build:

```sh
python3 scripts/build.py --docker --mode full --non-interactive
```

The Docker image provides the GUI development dependencies and an NDK. The built archive is written to the host's `dist/` through the mounted workspace. The container path has not yet been run in the final Qt-free release check.

## Run

After extracting the zip, use the binaries from its `LoliProfiler/` directory:

```sh
./LoliProfilerCLI --dump capture.loli --out capture.db
python3 -m agentcli.cli summary capture.db
```

The GUI needs a desktop session with OpenGL. Headless capture needs Android SDK Platform-Tools (`adb`) and Python for the JDWP injector. Set the SDK path in the GUI or `ANDROID_HOME` for command-line use. See [Build and package](BUILD.md) and [Quick start](QUICK_START.md).
