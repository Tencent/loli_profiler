# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LoliProfiler is a C/C++ memory profiling tool for Android games and applications. It connects to Android devices via ADB to capture and analyze memory allocation patterns, stack traces, and system memory information.

The project is **Qt-free** (since the `remove-qt` change): pure C++ with vendored dependencies only. It builds three executables:
- **LoliProfilerImGui** — the GUI (Dear ImGui docking + SFML): dockable panels (Stacktrace, Timeline, Treemap, Smaps, Screenshot, Console). Launch settings live in the modal Run/Launch dialog (File → Run or the toolbar Run button).
- **LoliProfilerCLI** — headless console application with the SAME capture capability as the GUI (launch/attach, capture, save .loli) plus `--dump`/`--compare` file processing, for automated profiling and CI/CD integration.

- **LoliProfilerCompare** is the standalone ImGui comparison window. File > Compare and the toolbar Compare button launch it independently. It accepts two positional captures or `--base` and `--compare`. Its three dockable Base/Comparer/Diff panels default to columns, persist layout in `loli_compare_imgui.ini`, and have separate search/selection/expansion state with search footers. Its compact toolbar has fixed-width filenames and theme Settings; full paths are in the title/tooltips and loading/allocation status is in the bottom bar.

All three executables are built from the **LoliCore** static library (Qt-free core: adb tools, device socket channel, .loli serializer, launch driver, capture engine, symbol translation, config stores, comparison). `agentcli/` (Python) is the analysis-only heap explorer (`loli` command).

For architecture details, data structures, threading model, and development patterns see **[docs/ARCH.md](docs/ARCH.md)**.

## Build Commands

### Prerequisites
Set these environment variables before building:
- `MSBUILD_EXE` — Path to MSBuild (Windows only)
- `Ndk_R16_CMD` — Path to Android NDK r16b ndk-build
- `Ndk_R20_CMD` — Path to Android NDK r20/r25 ndk-build

No Qt installation is needed.

### Build All
**All platforms:** `python scripts/build.py` (interactive) or
`python scripts/build.py --mode full --non-interactive` (installed tools).
For a Linux Docker build, add `--docker`.

No-argument runs show an interactive menu. Downloads need a prompt or explicit
`--download-missing`; the driver never accepts Android SDK licenses silently.

### Build Outputs
- GUI: `./build/cmake/bin/release/LoliProfilerImGui.exe` (Windows)
- CLI: `./build/cmake/bin/release/LoliProfilerCLI.exe` (Windows) or `LoliProfilerCLI` (macOS/Linux)
- Comparison GUI: `./build/cmake/bin/release/LoliProfilerCompare.exe` (Windows) or `LoliProfilerCompare` (macOS/Linux)
- Final package: `./dist/`

### Direct CMake (development)
```bash
cmake -S . -B build/imgui_cfg -DCMAKE_BUILD_TYPE=Release   # Windows: -G "Visual Studio 17 2022" -A x64
cmake --build build/imgui_cfg --target LoliProfilerImGui --config Release
cmake --build build/imgui_cfg --target LoliProfilerCLI --config Release
```

## Vendored Dependencies (thirdparty/, pinned submodules)
- `imgui` — Dear ImGui v1.92.9b-docking (docking branch, dockspace layout)
- `SFML` — 3.0.2 (window/event/graphics backend AND networking for the device channel; static, no audio)
- `imgui-sfml` — master (v3.0 + ImGui 1.92 `GetTexID()` compatibility fix)
- `nativefiledialog-extended` — native file dialogs (NFD)
- `tiny-process-library` — v2.0.0 (ProcessRunner backend, replaces QProcess)
- `rapidjson` — v1.1.0 (AppSettings JSON store)
- `sqlite` — plain amalgamation 3.53.4 (for `--dump ... --out profile.db`)

The global C++ standard is C++14; SFML 3, LoliCore, imgui-sfml, and the GUI
targets use C++17 per-target.

## Architecture Notes

- `LoliCore` (`LOLI_QTFREE_SRCS` in CMakeLists.txt) holds every core service.
  New core code must be Qt-free (STL + vendored deps only).
- `profilecomparison.h` is the shared comparison API: exact library-qualified symbol paths, saved-free filtering, signed 64-bit inclusive/self bytes and counts. CLI and standalone GUI use `CompareFiles` and `WriteComparisonReport`. Do not reintroduce hash-only identity, thresholded leaf diffs, or lossy signed `.loli` exports.
- Comparison Settings contains theme selection and infrequent root skipping. Documentation screenshots must show the comparer with no capture files opened; do not publish private sample paths, allocation data, or symbols. Credit the comparison reference fork `leoin2012/loli_profiler` and its Loli Compare contributor `shuchangliu`.
- **Design D12** (see `openspec/changes/remove-qt-dependency/design.md`): capture
  orchestration is a LoliCore API; the GUI and the headless CLI are equal
  consumers calling the same APIs. The Python `agentcli/` package is
  analysis-only.
- `src/gui/guidatabridge.{h,cpp}` is the GUI's capture/session engine: it owns
  the LoliCore services, converts results into Qt-free POD snapshots
  (`guisnapshot.h`), and is driven by `Tick()` from the ImGui frame loop.
- The `.loli` record format is Qt-QDataStream-compatible big-endian binary
  (magic 0xA4B3C2D1, version 106); the Qt-free serializer
  (`lolistream/lolirecord`) is byte-identical, validated by differential tests
  (historical Qt reference tests remain under `openspec/`, outside active build targets).
- The stacktrace tree uses a flat-index + `ImGuiListClipper` design
  (`stacktracetree.{h,cpp}`) so large profiles stay interactive.
- Themes are switchable at runtime via File > Settings, persisted via
  `AppSettings` (`loli_settings.json` next to the executable).
- Known Windows limitation: `ProcessRunner` (CreateProcess) does not execute
  `.bat`/`.cmd` scripts directly — only relevant for ndk-build.cmd-style tools.

## CLI Quick Reference

```bash
# Profile for 60 seconds with symbol translation
LoliProfilerCLI --app com.example.game --out profile.loli --symbol /path/to/lib.so --duration 60

# Profile until Ctrl+C
LoliProfilerCLI --app com.example.game --out profile.loli --verbose

# Compare two profiles to detect memory regressions
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt

# Compare with skipped root levels (for system libs without symbols)
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt --skip-root-levels 2

# Dump a single .loli file to text (hierarchical call tree with absolute values)
LoliProfilerCLI --dump profile.loli --out dump.txt --skip-root-levels 2
```
