# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LoliProfiler is a C/C++ memory profiling tool for Android games and applications built with Qt. It connects to Android devices via ADB to capture and analyze memory allocation patterns, stack traces, and system memory information.

The project builds three executables:
- **LoliProfiler** — Full GUI application (Qt 5 Widgets/Charts) with interactive profiling and visualization
- **LoliProfilerImGui** — Experimental Dear ImGui GUI (branch `imgui-gui`): SFML + ImGui (docking) frontend, Qt-free panels. Set to replace the Qt GUI.
- **LoliProfilerCLI** — Console application for automated profiling and CI/CD integration

All executables share core profiling logic and configuration files. The CLI and ImGui GUI exclude the heavy GUI dependencies (Widgets, Charts, OpenGL).

For architecture details, data structures, threading model, and development patterns see **[docs/ARCH.md](docs/ARCH.md)**.

## Build Commands

### Prerequisites
Set these environment variables before building:
- `QT5Path` — Path to Qt 5.12/5.14/5.15 installation
- `MSBUILD_EXE` — Path to MSBuild (Windows only)
- `Ndk_R16_CMD` — Path to Android NDK r16b ndk-build
- `Ndk_R20_CMD` — Path to Android NDK r20/r25 ndk-build

### Build All
**Windows:** `build.bat`  
**macOS:** `sh build.sh`  
**Linux:** `./build_linux_with_docker.sh`

### Build Outputs
- GUI: `./build/cmake/bin/release/LoliProfiler.exe` (Windows) or `LoliProfiler.app` (macOS)
- ImGui GUI: `build/imgui_cfg/Release/LoliProfilerImGui.exe` (see below)
- CLI: `./build/cmake/bin/release/LoliProfilerCLI.exe` (Windows) or `LoliProfilerCLI` (macOS/Linux)
- Final package: `./dist/`

## ImGui GUI (branch `imgui-gui`)

The experimental ImGui-based GUI lives under `src/gui/`, built via the CMake
option `BUILD_IMGUI_GUI` (default ON) into target `LoliProfilerImGui`.

Dependencies are vendored as submodules under `thirdparty/` (pinned):
- `imgui` — Dear ImGui **v1.92.9b-docking** (docking branch, dockspace layout)
- `SFML` — 3.0.2 (window/event backend; static, no audio/network)
- `imgui-sfml` — master (v3.0 + ImGui 1.92 `GetTexID()` compatibility fix)
- `nativefiledialog-extended` — native file dialogs (NFD)

**SFML 3 / imgui-sfml require C++17** — the project global is C++14, so C++17 is
set per-target on `LoliProfilerImGui` only (do not change the global standard).

Architecture: a root ImGui dockspace hosts dockable panels (Capture Status,
Stacktrace, Timeline, Treemap, Smaps, Screenshot, Console). Launch settings live
in the modal Run/Launch dialog (File → Run/Launch… or the toolbar) — there is no
dashboard page. `src/gui/guidatabridge.{h,cpp}` is the **only Qt-aware file** in
the GUI target: it owns the core ADB/stacktrace/meminfo/screenshot processes and
converts their Qt signals into Qt-free POD snapshots (`guisnapshot.h`) that the
panels consume. The stacktrace tree uses a flat-index + `ImGuiListClipper`
design (`stacktracetree.{h,cpp}`) so large profiles stay interactive.

Build (Windows):
```bash
cmake -S . -B build/imgui_cfg -DBUILD_GUI=OFF -DCMAKE_PREFIX_PATH="<QT5Path>"
cmake --build build/imgui_cfg --target LoliProfilerImGui --config Release
# Run (Qt DLLs must be on PATH for the Qt5::Core-using bridge):
PATH="<QT5Path>/bin:$PATH" ./build/imgui_cfg/Release/LoliProfilerImGui.exe
```

Themes are switchable at runtime via File → Themes (12 themes ported from the
Fury editor), persisted via QSettings.


## CLI Quick Reference

```bash
# Profile for 60 seconds with symbol translation
LoliProfilerCLI --app com.example.game --out profile.loli --symbol /path/to/lib.so --duration 60

# Profile until Ctrl+C
LoliProfilerCLI --app com.example.game --out profile.loli --verbose

# Profile with memory optimization (streams data to disk, recommended for large projects)
LoliProfilerCLI --app com.example.game --out profile.loli --enable-memory-optimization

# Compare two profiles to detect memory regressions
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt

# Compare with skipped root levels (for system libs without symbols)
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt --skip-root-levels 2

# Dump a single .loli file to text (hierarchical call tree with absolute values)
LoliProfilerCLI --dump profile.loli --out dump.txt --skip-root-levels 2
```
