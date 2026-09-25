# Tasks

## 1. Branch & Dependency Setup

- [x] 1.1 Create branch `imgui-gui` from `master`
- [x] 1.2 Vendor Dear ImGui (docking branch) at tag `v1.92.9b` under `thirdparty/imgui/` (submodule or source drop)
- [x] 1.3 Vendor SFML + imgui-sfml binding under `thirdparty/` (evaluate SFML 3.x first, fall back to 2.6); vendor nativefiledialog-extended under `thirdparty/nativefiledialog-extended/`
- [x] 1.4 Add CMake option `BUILD_IMGUI_GUI` and new target linking Qt5::Core (bridge only), SFML, ImGui (with docking), OpenGL3 backend, NFD; keep existing Qt GUI target buildable

## 2. Application Shell (M0)

- [x] 2.1 Implement `IPlatform` abstraction (`CreateWindow`/`PollEvent`/`NewFrame`/`Render`/`Shutdown`) with `SfmlPlatform` implementation; stub `SdlPlatform` interface points for the fallback
- [x] 2.2 Bring up main window: ImGui context, OpenGL3 renderer, frame loop, root dockspace with docking/viewports enabled
- [x] 2.3 Menubar with File → Run/Launch…, Open Record, Themes submenu (populated from registry), Exit; toolbar with Run/Launch button
- [x] 2.4 Validate docking + panel floating works on SFML backend; if imgui-sfml docking/viewports are broken, switch to `SdlPlatform` (ImGui official SDL backend) — record the decision in design.md
- [x] 2.5 Wire NFD C++ wrapper (`FileDialogs::OpenFile/SaveFile/PickFolder` returning `std::optional<std::string>`); verify open dialog with `.loli` filter and cancel path

## 3. Theme System

- [x] 3.1 Port theme functions from Fury `EditorThemes_inline.cpp.inc` into `src/gui/themes.cpp` with a `Theme{name, apply}` registry (include at minimum Dark and Forest Green; port remaining Fury themes)
- [x] 3.2 Generate File → Themes submenu from the registry; apply selected theme immediately
- [x] 3.3 Persist selected theme name in settings store and restore on launch

## 4. Data Bridge

- [x] 4.1 Implement `GuiDataBridge`: Qt-signal adapter (only Qt-aware file in GUI target) copying core updates into a mutex-protected POD snapshot store (`std::vector`/`std::string`)
- [x] 4.2 Snapshot types: device list, app/process list, capture config/state, stacktrace records, timeline samples, smaps stats, screenshot image bytes
- [x] 4.3 Verify bridge against live ADB capture and `.loli` record loading paths (reuse CLI/core record reader)

## 5. Run/Launch Dialog & Capture Control (M1)

- [x] 5.1 Implement modal `RunLaunchDialog`: device selection, app selection, capture configuration (parity with Qt config dialog options); opened from File menu and toolbar Run button
- [x] 5.2 Wire dialog confirm → start profiling via existing core (`startappprocess`/`stacktraceprocess`); Stop control + dockable capture status panel (elapsed time, sample counts)
- [x] 5.3 Record loading: File → Open Record → NFD dialog → populate panels from `.loli` file without a device

## 6. Stacktrace Tree (M2) — performance-critical

- [x] 6.1 Implement flat-index visible-row builder from stacktrace snapshot (index, depth, label, size, child count, expansion state); rebuild only on expansion/filter/data change
- [x] 6.2 Render tree/table with `ImGuiListClipper`; expansion state keyed by node id surviving snapshot updates
- [x] 6.3 Port filter/search behavior from `stacktraceproxymodel` onto the flat representation
- [x] 6.4 Performance validation: load `C:\Users\xinhou\Downloads\output\2026.09.14-11.24.23.loli` (~395 MB), scroll/expand/filter tree — must stay interactive (no multi-second stalls)

## 7. Charts, Treemap, Smaps, Screenshot (M3–M4)

- [x] 7.1 Timeline chart panel: ImDrawList line/area rendering, hover tooltip, zoom/pan
- [ ] 7.2 Fragmentation chart panel: same widget toolkit as 7.1
- [x] 7.3 Treemap panel: ImDrawList rectangles, hover highlight + tooltip (path, size)
- [x] 7.4 Smaps panels: per-section stats table + summary visualization
- [x] 7.5 Screenshot panel: simplified preview — image fit-to-window with 1:1 toggle (no annotation/scrubbing extras)
- [x] 7.6 Console panel: capture log output (replace qconsolewidget usage)

## 8. Parity & Cleanup

- [x] 8.1 Parity pass: every Qt GUI workflow (device → app → capture → inspect; record load) reproducible in ImGui GUI
- [ ] 8.2 Remove Qt GUI target, `.ui` files, `qdarkstyle/`, vendored qconsolewidget, and Qt Widgets/Charts/OpenGL deps from the GUI build path
- [ ] 8.3 Update `build.bat`, `build.sh`, `BUILD_LINUX.md`, README, and CLAUDE.md build section (QT5Path no longer needed for GUI)
- [ ] 8.4 Final performance regression check on sample `.loli`; merge `imgui-gui` back to master
