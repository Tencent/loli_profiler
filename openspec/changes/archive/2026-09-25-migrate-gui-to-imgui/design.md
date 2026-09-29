## Context

LoliProfiler currently builds a Qt 5 Widgets/Charts GUI (`LoliProfiler` target) that shares core profiling sources (`adbprocess`, `stacktraceprocess`, `meminfoprocess`, `smaps/`, `lz4/`, `hashstring`, `pathutils`) with the CLI target (`LoliProfilerCLI`). The Qt GUI layer consists of `mainwindow`, three dialogs (`configdialog`, `deviceselectiondialog`, `selectappdialog`), a set of custom QGraphicsView-based visualizations (`memgraphicsview`, `treemapgraphicsview`, `customgraphicsview`, `interactivechartview`, `charttooltipitem`), Qt Charts usage, `stacktracemodel`/`stacktraceproxymodel` (Qt item models), the vendored `qconsolewidget`, and `qdarkstyle` theming.

The Qt dependency is the dominant cost in the GUI build: Qt Charts/Widgets licensing, `QT5Path` requirement, MOC/UIC/RCC toolchain, and large redistributables. This change replaces only the GUI layer. Core profiling logic and the CLI remain Qt-based for now (Qt5::Core is still used by core sources), so Qt remains a build dependency for the CLI — but the new ImGui GUI must not link Qt Widgets/Charts/OpenGL.

The work happens on a dedicated git branch (e.g. `imgui-gui`) so `master` remains stable during the migration.

## Goals / Non-Goals

**Goals:**

- New ImGui-based GUI executable with feature parity for the primary workflows: select device → select app → configure capture → start/stop profiling → view stacktraces/charts/treemap/smaps/screenshot; load and inspect `.loli` record files.
- Windowing via SFML (imgui-sfml binding) with an SDL fallback path isolated behind a thin platform abstraction.
- ImGui pinned to stable tag `v1.92.9b` (docking branch features where available), vendored in `thirdparty/`.
- Native file dialogs via nativefiledialog-extended (NFD).
- Theme system ported from Fury's `EditorThemes_inline.cpp.inc` (set of `Setup*Style()` functions) with runtime switching from menubar **File → Themes**.
- CMake build for the new GUI target on Windows and macOS; keep the existing Qt GUI buildable until the ImGui GUI reaches parity (compile-time switch), then retire it.

**Non-Goals:**

- Migrating the CLI (`LoliProfilerCLI`) away from Qt5::Core — out of scope.
- Changing the `.loli` record format, ADB protocol layer, or plugin/instrumentation code.
- Replacing `stacktracemodel`'s data structures; only its Qt-model interface is adapted for ImGui consumption.
- Mobile/embedded ImGui rendering; desktop only.
- Pixel-perfect recreation of Qt Charts interactions — equivalent functionality (zoom/pan/tooltip/selection) is acceptable.

## Decisions

### D1: ImGui v1.92.9b docking build, vendored

Vendor Dear ImGui (docking branch) at tag `v1.92.9b` under `thirdparty/imgui/` (submodule or source drop), built with docking and viewport features enabled. A root dockspace hosts all panels; nearly every panel (stacktrace, timeline, treemap, smaps, screenshot, console, capture status) is dockable/floatable. There is **no dashboard page** — the Qt mainwindow's dashboard layout is dropped; the workspace itself is the main window.

*Alternatives considered:* system/packaged ImGui (rejected — version drift; the requirement pins v1.92.9b); non-docking release (rejected — docking is a hard requirement).

### D2: SFML window backend via imgui-sfml, SDL as fallback behind a platform shim

Primary backend: SFML ≥ 2.6 (or 3.x) with the `imgui-sfml` binding, vendored under `thirdparty/`. All window/event/render-loop access goes through a small `IPlatform` interface (`CreateWindow`, `PollEvent`, `NewFrame`, `Render`, `Shutdown`) with `SfmlPlatform` as the default implementation. If SFML proves unfit (e.g. multi-viewport/docking support gaps — imgui-sfml historically lags on `ImGuiConfigFlags_ViewportsEnable`), an `SdlPlatform` using ImGui's official SDL backend is swapped in without touching panel code.

*Alternatives considered:* raw Win32/GLFW backends (rejected — user requested SFML/SDL); direct imgui-sfml calls in UI code (rejected — locks us to SFML and makes the fallback costly).

### D3: Renderer — OpenGL3 backend

SFML provides the GL context; render ImGui with the official `imgui_impl_opengl3` backend (SFML 3's imgui-sfml already wraps GL3; for SFML 2.6 use imgui-sfml's OpenGL2 path or GL3 manually — decide during bring-up). Custom visualizations (treemap, timeline charts) render via `ImDrawList` for maximum portability, not raw GL.

### D4: File dialogs — nativefiledialog-extended (NFD)

Vendor NFD under `thirdparty/nativefiledialog-extended/`. Wrap in a `FileDialogs` helper (`OpenFile(filters)`, `SaveFile(filters)`, `PickFolder()`) returning `std::optional<std::string>` (NFD is C; wrap in C++). Replaces `QFileDialog` usages in mainwindow/config flows.

*Alternatives considered:* `tinyfiledialogs` (rejected — user specified NFD); ImGui-based file browser (rejected — native dialogs match OS conventions and user expectation).

### D5: Theme system — compile-time registry of `Setup*Style()` functions

Port the theme functions from `D:\git\fury3d\engine\Fury\Editor\EditorThemes_inline.cpp.inc` into `src/gui/themes.cpp.inc`-style include (or plain `.cpp`), exposing a registry: `struct Theme { const char* name; void (*apply)(); }` and `constexpr Theme kThemes[]`. Menubar **File → Themes** is generated from the registry; selecting a theme calls its `apply()` immediately and persists the choice in the settings store. Includes at minimum: Dark (ImGui default with Fury sizing tweaks), Forest Green, and the other themes defined in the Fury file.

*Alternatives considered:* loading themes from .ini at runtime (deferred — nice-to-have; the inline-function approach matches the referenced implementation and requires no asset pipeline).

### D6: UI architecture — immediate-mode panels fed by a UI-facing snapshot layer

Core profiling classes are Qt-based (signals/slots, `QVariant` models). The ImGui layer must not `#include <QtWidgets>`; it consumes plain C++ snapshots:

- A `GuiDataBridge` (per-frame, read-only) converts core state — stacktrace records, memory timeline samples, smaps stats, process list — into POD structs (`std::vector`, `std::string`) owned by the GUI layer. Core code continues to emit Qt signals; a thin adapter at the bridge boundary (the only Qt-aware file in the GUI target, linking Qt5::Core only) copies updates into the snapshot store under a mutex.
- Panels (`StacktracePanel`, `TimelinePanel`, `TreemapPanel`, `SmapsPanel`, `ScreenshotPanel`, `ConsolePanel`, `CaptureStatusPanel`, `RunLaunchDialog`) are pure functions of the snapshot + UI state. This keeps ImGui code testable and Qt-free except at one seam.
- **No dashboard panel**: launch-time settings (device, app, capture options) live in a modal `RunLaunchDialog` opened from **File → Run/Launch…** and a toolbar Run button — not a persistent settings dashboard.

*Alternatives considered:* refactor core to Qt-free first (rejected as a prerequisite — too large; can happen incrementally later); ImGui reads Qt models directly (rejected — drags Widgets/Gui headers into every panel).

### D7: Charts/treemap — custom ImGui widgets, no ImPlot dependency initially

Timeline/fragmentation charts and the treemap are implemented as custom ImGui widgets using `ImDrawList` (rectangles, polylines, hover hit-testing, zoom via scroll region) — mirroring the existing custom QGraphicsView implementations. The screenshot panel is deliberately simplified versus the Qt version: it only previews the captured image (fit-to-window / 1:1 toggle) with no annotation or scrubbing extras. ImPlot may be evaluated later for line charts but is not a dependency of this change.

*Rationale:* the Qt Charts usage is already wrapped in custom views; porting the custom drawing logic to ImDrawList is more faithful than adapting to ImPlot's data model, and avoids another dependency.

### D7a: Stacktrace tree — flat-index virtualization, not recursive widgets

The Qt GUI used model/view (`stacktracemodel` + proxy) to stay responsive on large datasets. The ImGui port MUST NOT build one `ImGui::TreeNode` hierarchy per frame over the full dataset. Instead:

- The bridge flattens the visible portion of the call tree into a **flat row array** (index, depth, label, size, child-count, expansion state) whenever expansion/filter changes — never per frame.
- Rendering uses `ImGuiListClipper` over that flat array, so only visible rows are submitted.
- Expansion state is stored as a compact bitset/hash keyed by node id, surviving snapshot updates.
- Filtering uses the same flat representation (like the existing `stacktraceproxymodel` role).
- Acceptance: loading `C:\Users\xinhou\Downloads\output\2026.09.14-11.24.23.loli` (~395 MB) and scrolling/expanding the stacktrace tree stays interactive (no multi-second frame hitches); this file is the reference performance test case.

### D8: Build layout

- `CMakeLists.txt`: new target `LoliProfilerImGui` (name subject to final choice) gated by `option(BUILD_IMGUI_GUI ...)`, linking Qt5::Core (bridge only), SFML, ImGui, NFD. Existing `BUILD_GUI` (Qt) stays until parity is demonstrated, then the Qt GUI target and `.ui` files, `qdarkstyle/`, and Qt Charts/Widgets/OpenGL deps are removed in a follow-up commit on the same branch.
- Vendor deps under `thirdparty/` as git submodules where practical (`imgui` at v1.92.9b, `imgui-sfml`, `nativefiledialog-extended`, SFML via FetchContent or submodule).
- Branch: `imgui-gui` cut from `master`; PR back when parity + themes + dialogs are done.

### D9: Settings/config — keep existing config serialization

`configdialog` state persists through the existing config mechanism (INI/registry via Qt Core `QSettings` is acceptable at the bridge seam, or migrate to a small INI in the GUI layer — decide in implementation; default: reuse `QSettings` since Qt5::Core is already linked). Theme choice is an additional key in the same store.

## Risks / Trade-offs

- [imgui-sfml docking/viewport gaps break layout or multi-window docking] → Mitigation: D2's platform shim allows switching to SDL backend with minimal churn; validate docking (docking branch build) within the first milestone before building all panels.
- [Stacktrace tree unusable on large profiles if rendered naively (Qt model/view handled this)] → Mitigation: D7a flat-index + clipper design; validate against the ~395 MB sample `.loli` file before parity sign-off.
- [Feature-parity effort for treemap/timeline charts is underestimated — Qt GraphicsView code is intricate] → Mitigation: port visualizations incrementally (stacktrace table first, timeline second, treemap last); each panel ships usable-before-polished.
- [Qt signals → snapshot bridge introduces threading bugs (core profiling runs on worker threads)] → Mitigation: single well-defined bridge with mutex-protected snapshot handoff, following the existing threading model in docs/ARCH.md; no direct core access from ImGui frame code.
- [Qt still required on build machines for the CLI/core] → Accepted trade-off; full Qt removal is a separate future change.
- [NFD Linux portal/GTK dependency adds Linux build complexity] → Mitigation: Linux GUI support is best-effort this change; Windows/macOS are the parity targets.
- [SFML 2.6 vs 3.x API differences complicate vendoring] → Mitigation: pin one version (evaluate 3.x first; fall back to 2.6 if imgui-sfml compat lags) in the first milestone.

## Migration Plan

1. Cut branch `imgui-gui` from `master`.
2. Vendor dependencies; bring up empty ImGui (docking) window with dockspace, themes menu, and NFD open dialog (milestone M0).
3. Build bridge + Run/Launch dialog (device/app/config in one modal) + start/stop capture (M1).
4. Port stacktrace table/tree with virtualization (validate on the ~395 MB sample file); then timeline/fragmentation charts; then treemap; then smaps + simplified screenshot preview (M2–M4).
5. Record file loading via NFD; compare/dump flows unchanged (CLI).
6. Parity check + remove Qt GUI target, `.ui` files, `qdarkstyle/`; update `build.bat`/`build.sh` and docs (CLAUDE.md build section, README).
7. Rollback: the Qt GUI remains on `master`; the branch is additive until step 6, so abandoning the branch is a full rollback until then.

## Open Questions

- SFML 3.x vs 2.6: which to pin (depends on imgui-sfml compatibility at implementation time)?
- Does imgui-sfml at the pinned version support ImGui viewports/docking adequately, or do we flip to SDL at M0?
- Theme list: port all Fury themes or a curated subset (Dark, Forest Green, + N)? Default: port all — they're self-contained functions.
- GUI executable name: keep `LoliProfiler` (replace) or ship `LoliProfilerImGui` alongside until parity? Default: replace on the branch; master keeps Qt.
- Settings: keep `QSettings` at the bridge seam vs. move GUI settings to ImGui .ini — default to `QSettings` reuse.
