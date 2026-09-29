## Why

The LoliProfiler GUI is built on Qt 5 (Widgets + Charts + OpenGL), which carries heavy licensing/packaging constraints, large runtime dependencies, slow build times, and makes the GUI hard to iterate on. Migrating the GUI to Dear ImGui (v1.92.9b stable) on an SFML window backend removes the Qt dependency from the interactive profiler, shrinks the distributable, and makes the UI fully self-contained and portable — while keeping the CLI and all core profiling logic untouched.

## What Changes

- Replace the Qt-based GUI (`LoliProfiler` executable) with a Dear ImGui **v1.92.9b** (stable) GUI, on a new branch (e.g. `imgui-gui`).
- Use **SFML** as the window/event backend (via `imgui-sfml`); fall back to **SDL2/SDL3** (via ImGui's official SDL backends) only if SFML proves unfit (e.g. docking/viewport or rendering issues).
- Replace Qt file dialogs with **nativefiledialog-extended (NFD)** for open/save file dialogs.
- Add a **theme system** ported from `D:\git\fury3d\engine\Fury\Editor\EditorThemes_inline.cpp.inc` (inline `Setup*Style()` functions), switchable at runtime via the menubar **File → Themes** submenu.
- Reimplement existing GUI functionality in ImGui: **Run/Launch dialog** (File menu item + toolbar button) that collects all launch settings (device, app, capture config) in one place — the Qt dashboard-style mainwindow layout is dropped in favor of a docking workspace; stacktrace table/tree, treemap view, memory charts (allocation timeline, fragmentation, smaps), simplified screenshot preview, and record file loading.
- **BREAKING**: Qt 5 is dropped as a dependency for the GUI build. `QT5Path` is no longer required to build the new GUI executable. Qt `.ui` files, `qdarkstyle`, and Qt-specific widgets are removed/replaced.
- The **LoliProfilerCLI** executable and all core profiling logic (ADB processes, parsing, serialization) remain unchanged; the ImGui GUI consumes the same core libraries.

## Capabilities

### New Capabilities

- `imgui-shell`: Application shell — SFML/SDL window creation, ImGui frame loop, docking layout, menubar, and platform/event plumbing that hosts all GUI panels.
- `theme-support`: Runtime theme system with named themes (ported from Fury editor themes) switchable from the menubar File → Themes submenu, applied to `ImGuiStyle`.
- `native-file-dialogs`: File open/save dialogs via nativefiledialog-extended (NFD) replacing Qt dialogs.
- `profiling-control-ui`: Run/Launch dialog (opened from a menubar File item and a toolbar Run button) that consolidates device selection, app selection, and capture configuration, plus start/stop capture control — replacing the Qt dashboard mainwindow layout.
- `memory-visualization-ui`: ImGui-based visualizations — performance-virtualized stacktrace table/tree (handling profiles of hundreds of MB), treemap, allocation/fragmentation charts, smaps overview, and a simplified screenshot preview panel (image view only) — replacing Qt Charts/GraphicsView widgets.

### Modified Capabilities

<!-- No existing specs in openspec/specs to modify. -->

## Impact

- **Affected code**: `src/mainwindow.*`, `src/configdialog.*`, `src/deviceselectiondialog.*`, `src/selectappdialog.*`, `src/*graphicsview.*`, `src/interactivechartview.*`, `src/stacktrace(model|proxymodel).*`, `src/main.cpp`, `CMakeLists.txt`, `loliprofiler.pro` (retired), `qdarkstyle/`, build scripts (`build.bat`, `build.sh`).
- **Unchanged**: `src/adbprocess.*`, `src/stacktraceprocess.*`, `src/meminfoprocess.*`, `src/smaps/`, `src/lz4/`, `src/cliprofiler.*`, `src/profilecomparator.*`, `main_cli.cpp`, plugins, and the `.loli` record format.
- **New dependencies**: Dear ImGui v1.92.9b, SFML (≥2.6 / 3.x) + `imgui-sfml` binding (or SDL as fallback), nativefiledialog-extended. All vendored under `thirdparty/`.
- **Platforms**: Windows and macOS GUI builds; Linux GUI support maintained if feasible (SFML/SDL are cross-platform).
- **Build**: CMake gains a new GUI target; Qt Kit requirement removed from the GUI build path; CI/build scripts updated.
- **Risk**: Feature parity with Qt Charts treemap/timeline views is the largest effort; Qt Charts interaction (zoom, tooltip, selection) must be reimplemented with ImGui draw lists/plotting or a small charting helper. The stacktrace tree must remain interactive on large profiles (validated against `C:\Users\xinhou\Downloads\output\2026.09.14-11.24.23.loli`, ~395 MB) — Qt's model/view virtualization must be matched with ImGui clipper/flat-index techniques.
