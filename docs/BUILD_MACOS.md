# macOS build and validation

The Qt-free GUI and CLI build natively on Apple Silicon with Xcode command-line tools, CMake 3.24+, and macOS 13 or newer. Python 3.10+ is required for the Python agent and sample validator. Apple's `/usr/bin/python3` may be 3.9; the tested interpreter is Homebrew `python3.13`.

## Native GUI and CLI

```sh
git submodule update --init --recursive
python3.13 scripts/build.py --mode native --non-interactive
build/cmake/LoliProfilerCLI --help
open build/cmake/LoliProfilerImGui.app
```

The driver stages injector/config/icon files beside the CLI and inside `LoliProfilerImGui.app/Contents/Resources/`, along with any already-built Android hooks. The macOS GUI stores settings, layout, and default logs in `~/Library/Application Support/LoliProfiler/`, outside the signed bundle, and migrates earlier in-bundle settings when user settings do not yet exist. The CLI retains executable-relative settings and runtime paths. The injector reads logging configuration beside its script and writes logs in its working directory. The default deployment target is macOS 13.0; an existing build cache with the former 10.13 target needs an explicit update:

```sh
cmake -S . -B build/cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
```

macOS has one release package, `LoliProfiler-macos.zip`, for both phone capture and saved-record analysis. To package already-built desktop binaries and Android hooks:

```sh
python3.13 scripts/package_release.py --platform macos
ditto -x -k dist/LoliProfiler-macos.zip build/macos-release-test
open build/macos-release-test/LoliProfiler/LoliProfilerImGui.app
```

The ZIP contains the main GUI, CLI, comparison viewer, injector, Python agent, icon, docs, and all four LLVM Android hook libraries. Packaging fails if required hooks are missing and removes the obsolete desktop-only ZIP after a successful release. Phone capture also requires configured Android tools. `ditto` retains executable permissions; ZIP utilities that discard modes may require restoring them. Generated settings, logs, and caches are omitted from the app bundle archive. `--mode native` remains a developer build mode for compiling desktop targets; it does not create a separate release package.

Packaging ad-hoc signs the complete app after staging resources and verifies its seal again after ZIP extraction. This requires macOS and `codesign`; no paid signing identity is needed. Check an extracted app with:

```sh
codesign --verify --deep --strict dist/LoliProfiler/LoliProfilerImGui.app
```

Ad-hoc signing does not establish an Apple-verified publisher or notarization. For a trusted downloaded release, macOS may block opening until the user tries opening it and then selects **System Settings > Privacy & Security > Open Anyway**. See [Apple's app-opening guidance](https://support.apple.com/en-us/102445). A signature verification failure must be fixed by rebuilding/repackaging; do not bypass it as a routine installation step. Developer ID signing and notarization are needed for distribution without this manual approval.

GUI shortcuts use Command on macOS and Control on Windows/Linux for Open (O), Run (R), Save (S), Settings (,), and Quit (Q). The handler uses ImGui's normalized primary modifier, preserving native text-editing shortcuts.

## Saved-capture regression

```sh
python3.13 scripts/validate_samples.py \
  --cli build/cmake/LoliProfilerCLI \
  --samples /path/to/HeapMemoryProfiler/output \
  --out-dir build/macos-validation

cmake -S . -B build/cmake -DBUILD_PLATFORM_TESTS=ON
cmake --build build/cmake --target LoliPlatformPathsTest LoliCallTreeTest --parallel
ctest --test-dir build/cmake --output-on-failure

build/cmake/LoliProfilerImGui.app/Contents/MacOS/LoliProfilerImGui \
  --smoke-test --open /absolute/path/to/capture.loli \
  --log-file /absolute/path/to/gui-smoke.log
```

The validator extracts one `.loli` member from each `.loli.zip` archive into the output directory, verifies original archive hashes, and checks every capture serially. Checks include independent raw/live allocation accounting, SQLite integrity and root totals, text export, zero self-differences, cross-capture comparison, and Python summary/hotspot/call-path queries. Results are in `report.json`, with databases and command logs alongside it.

For a standalone device capture, replace `--samples` with `--capture /absolute/path/device-capture.loli`. The same checks run without changing the input file, and the report also includes free addresses, frame/symbol counts, timeline bounds, screenshots, and smaps sections.

`--smoke-test` opens the actual GUI, waits for tree publication, selects the middle third of allocation time, checks range-tree byte accounting, renders the panels, saves `<log-path>.png`, and exits with status zero only on success. It times out after 120 seconds and requires a desktop session and `--open`. Single-series iOS imports display their Total-memory timeline in MB; unavailable Android categories are omitted.

CLI tree construction now uses exact parent path, library, and function identity. The former 32-bit hash identity could merge unrelated paths and loop forever on a supplied capture. Regenerate existing exported `.db` caches to obtain corrected trees; node IDs can change where legacy collisions were merged. The `.loli` layout and SQLite table schema are unchanged.

## Capture-ready release and final phone test

Provide an installed NDK and SDK Platform-Tools, then run:

```sh
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME=/path/to/android-ndk
python3.13 scripts/build.py --mode full --non-interactive
```

This builds all four LLVM hook ABIs, refreshes local runtime copies, and writes `dist/LoliProfiler-macos.zip`. `Ndk_R20_CMD` may point to `ndk-build`; `Ndk_R16_CMD` is optional for legacy GCC hooks. Current NDKs use LLVM tools for both ARMv7 and ARM64. Core discovery supports `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, and SDK side-by-side NDK directories.

Missing tools can be installed through the interactive driver or an explicit `--download-missing` flag. Downloads require agreement to [Google's Android SDK terms](https://developer.android.com/studio#terms); the driver never accepts `sdkmanager` license prompts automatically. Non-interactive native builds do not download Android tools.

After offline work, connect an unlocked Android phone with USB debugging and authorize this Mac. Supply a debuggable target package (and matching symbols if symbolization will be tested). Configure `loli3.conf` for the phone's architecture, `compiler:llvm`, and appropriate library patterns; the legacy default `compiler:gcc` requires separately built GCC hooks. Then verify:

```sh
"$ANDROID_HOME/platform-tools/adb" devices -l
build/cmake/LoliProfilerCLI --app com.example.game \
  --out build/device-capture.loli --duration 30 --verbose \
  --log-file build/device-capture.log
build/cmake/LoliProfilerCLI --dump build/device-capture.loli \
  --out build/device-capture.db
python3.13 -m agentcli.cli summary build/device-capture.db --json
```

Acceptance requires successful injection, nonempty allocation records/callstacks, an orderly stop/save, integrity-checked export, and reopening in the GUI. It remains a separate acceptance step; saved iOS samples do not establish Android capture correctness.

The injector supports Python 3, including packet fragmentation and asynchronous JDWP events. ARM64 frame-pointer hooks strip return-address authentication before validation, preventing signed null addresses from appearing as fabricated tree roots. To repeat the hardware regression on an ARM64 Android phone with an NDK installed:

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android23-clang++" \
  -std=c++11 -O2 -Wall -Wextra -Werror -static-libstdc++ \
  -I plugins/Android/jni tests/android_framepointer.cpp \
  plugins/Android/jni/loli_utils.cpp -o build/android-framepointer-test
"$ANDROID_HOME/platform-tools/adb" push build/android-framepointer-test /data/local/tmp/
"$ANDROID_HOME/platform-tools/adb" shell chmod 755 /data/local/tmp/android-framepointer-test
"$ANDROID_HOME/platform-tools/adb" shell /data/local/tmp/android-framepointer-test
```

The signed-null fixtures require a phone supporting ARM64 pointer authentication; this is a hardware acceptance probe rather than a native macOS CTest.

## Upstream comparison feature

The native/full drivers also build `LoliProfilerCompare`, the independent dockable Base/Comparer/Diff viewer. The main GUI's **File > Compare Captures** launches the packaged executable beside the CLI (or beside the GUI app bundle). Snapshot dump retains collision-safe identity and its SQLite schema. File comparison now uses the upstream shared signed live-allocation engine, retaining negative, count-only, and small changes. Diff export is text; `.loli` diff export is rejected. See [comparison commands](CLI_COMPARE_MODE.md).

Focused verification:

```sh
cmake -S . -B build/cmake -DBUILD_PLATFORM_TESTS=ON -DBUILD_COMPARISON_TESTS=ON
cmake --build build/cmake --target LoliComparisonTests LoliPlatformPathsTest LoliCallTreeTest LoliGuiShortcutsTest --parallel
ctest --test-dir build/cmake --output-on-failure
python3.13 tests/check_comparison_cli.py --cli build/cmake/LoliProfilerCLI --gui build/cmake/LoliProfilerCompare
```

## Verified scope

On 2026-09-30: macOS 26.6.2 arm64, AppleClang 21/Xcode, pinned SFML 3.0.2, Python 3.13.15, SDK Platform-Tools 37.0.1, and NDK r27 (27.0.12077973). Native GUI/CLI, all four LLVM hook ABIs, six supplied archived captures, the extracted full release, and real UE4 phone launch/injection, scene entry, stop/save, symbolization, export, and GUI reopening passed. Source archives remain unchanged. The Python 3 injector and ARM64 authenticated-return-PC fixes were verified on the phone.

After merging the shared signed comparison feature, all three desktop targets build and all five CTest cases pass. Synthetic comparison CLI/GUI checks verify signed live accounting, Unicode/spaced paths, strict arguments, input protection, docked/stacked layouts, identical-file rendering, and exact GUI/CLI report parity. A representative saved capture passes snapshot/SQLite/agent/self-comparison regression with the new engine. Private capture paths, statistics, and symbols are kept in local build evidence; public documentation records reproducible commands and pass/fail scope. Intel/universal binaries, execution on macOS 13, and Developer ID signing/notarization remain untested.

The reported damaged-app warning was reproduced as an invalid bundle signature; extracted files matched the ZIP exactly. The repaired full ZIP passes strict recursive signature verification after extraction. The repaired local app opens through LaunchServices, displays its window, quits cleanly, and retains its seal after external layout writes. The bundled injector reads its configuration from Resources and writes logs in an external working directory. These focused packaging checks require no repeated phone capture. Local download quarantine was cleared only on the verified artifacts built on this Mac; downloaded copies remain subject to Gatekeeper's publisher/notarization approval.
