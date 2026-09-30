# Build and package

LoliProfiler is Qt-free. It builds `LoliProfilerImGui` (GUI), `LoliProfilerCLI` (headless capture and export), the Android `libloli.so` hooks, and the Python `agentcli` package.

## Requirements

- CMake 3.24 or newer and a C++17 compiler. Windows builds use Visual Studio 2022.
- Python 3.8 or newer for packaging; Python 3.10 or newer to run `agentcli`.
- JDK 17 or newer only if the build driver needs to install Android packages through Google's `sdkmanager`.
- An Android NDK for capture-ready packages. Set `ANDROID_NDK_HOME` or `Ndk_R20_CMD` to its `ndk-build` executable. An older `Ndk_R16_CMD` is optional for legacy GCC/armeabi hooks.
- On Linux, the graphics development libraries listed in the [Linux guide](BUILD_LINUX.md). On macOS, Xcode command-line tools.

No Qt SDK or runtime is needed.

## Windows

```bat
python scripts\build.py
```

Without arguments, `scripts/build.py` opens an interactive menu: full release, native binaries only, package existing binaries, or install missing Android tools. A full build configures VS2022, builds all three executables (profiling GUI, CLI, comparison GUI) and Android hooks, and creates `dist/LoliProfiler-windows.zip`. Android object files are kept in NDK-specific directories under `build/ndk-obj`, so switching NDK versions does not reuse incompatible intermediates.

For CI or another non-interactive shell, specify a mode and installed paths:

```bat
python scripts\build.py --mode full --non-interactive --sdk C:\Android\Sdk --ndk C:\Android\Sdk\ndk\27.0.12077973
python scripts\build.py --mode native --non-interactive
```

The driver searches `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME`, and standard SDK locations. If tools are missing in interactive mode, it asks before downloading. It verifies the pinned official Android command-line-tools archive, then uses Google's `sdkmanager` to install Platform-Tools and a side-by-side NDK. The script **does not accept Google licenses automatically**; `sdkmanager` prompts for them. Non-interactive runs never download unless `--download-missing` is specified. Build and dist directories are preserved.

To package binaries and hooks already built:

```bat
python scripts\package_release.py --platform windows --build-dir build\cmake --out-dir dist
```

## macOS

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
python3 scripts/build.py
```

With no arguments the same interactive menu appears. Use `python3 scripts/build.py --mode full --non-interactive` for automation with an installed NDK. A complete build writes `dist/LoliProfiler-macos.zip`. See [macOS notes](BUILD_MACOS.md). This path is statically checked but awaits an on-macOS run.

## Linux

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
python3 scripts/build.py
```

With no arguments the same interactive menu appears. Use `python3 scripts/build.py --mode full --non-interactive` for automation, or add `--docker` for the container build. Both produce `dist/LoliProfiler-linux.zip` when the build succeeds. See the [Linux guide](BUILD_LINUX.md). This path is statically checked but awaits a Linux run.

## Archive contents

Each zip has a `LoliProfiler/` root with the profiling GUI, native CLI, standalone `LoliProfilerCompare` window, Python injector and config, `remote/llvm/<abi>/libloli.so` for ARM and x86, optional legacy GCC hooks, the `agentcli/` Python package, app icon, README, and analysis scripts. On macOS the GUI's runtime files are also inside `LoliProfilerImGui.app/Contents/MacOS/`; the comparison executable is beside the bundle.

The native CLI can export `.loli` to text or SQLite: `LoliProfilerCLI --dump capture.loli --out capture.db`. The installed Python command remains `loli`; `agentcli` is also available. See [CLI mode](CLI_MODE.md).
