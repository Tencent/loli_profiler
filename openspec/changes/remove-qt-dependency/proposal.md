## Why

The ImGui GUI migration (change `migrate-gui-to-imgui`) replaced the Qt Widgets/Charts presentation layer, but the **core profiling engine and CLI still depend on Qt5** (Qt5::Core/Network/Concurrent/Sql) for fundamental plumbing: `QString`/`QVector`/`QHash` containers, `QProcess` (adb/NDK subprocess), `QTcpSocket`/`QTcpServer` (device channel), `QDataStream` (.loli serialization), `QSettings` (config), `QtConcurrent` (parallel symbol translation), `QThread`/signals-slots (async completion), and `QUuid`. This keeps Qt as a heavy build/runtime dependency, blocks a clean single-binary distribution, and forces the new ImGui GUI to keep a `GuiDataBridge` Qt-aware seam. Removing Qt entirely makes the project a pure C++ + vendored-deps codebase.

## What Changes

- Replace Qt containers with STL (`std::string`/`std::vector`/`std::unordered_map`) throughout `src/` core and `include/`.
- Replace `QProcess`-based adb/NDK invocation with a thin cross-platform process runner (Windows `CreateProcess` / POSIX `posix_spawn`), vendored or in-house.
- Replace `QTcpSocket`/`QTcpServer` (the device stacktrace channel) with SFML `sf::TcpSocket`/`sf::TcpListener` (already vendored for the GUI) or a minimal socket wrapper.
- Replace `QDataStream` .loli serialization with an explicit, versioned, endian-explicit binary reader/writer (documented format). **BREAKING**: .loli read/write moves off QDataStream; must remain backward-compatible with existing .loli files (same byte layout, big-endian) so old captures still load.
- Replace `QSettings` config persistence with a small INI/JSON store (no Qt).
- Replace `QtConcurrent` parallel symbol translation with `std::async`/thread pool.
- Replace Qt signals/slots + `QObject` event delivery with plain `std::function` callbacks and a simple thread-safe event queue.
- Replace `QUuid` with a std-based id (or `std::array<uint8_t,16>`) for record identity.
- Port the CLI (`main_cli.cpp`, `cliprofiler`, `profilecomparator`, `clilogger`) off Qt so `LoliProfilerCLI` builds with no Qt.
- Complete the ImGui GUI's remaining Qt-boundary gaps now possible once core is Qt-free: **live-capture tree build** (currently only record-load builds the aggregated tree) and **`.loli` record saving** (`SaveRecord` is a stub).
- **BREAKING**: Qt5 removed from the build entirely (no `QT5Path`, no `find_package(Qt5)`). The legacy Qt GUI target (`LoliProfiler`, Widgets/Charts) is deleted — superseded by `LoliProfilerImGui`.
- Use one Python build driver (`scripts/build.py`) plus `Dockerfile`; update README, CLAUDE.md, and the build guides under `docs/` to drop Qt prerequisites.

## Capabilities

### New Capabilities
- `qt-free-core-engine`: Core profiling data pipeline (adb process control, device socket channel, record model, symbol resolution, smaps) implemented in portable C++ with STL + vendored libs, no Qt.
- `process-runner`: Cross-platform subprocess execution for adb/NDK tools (spawn, read stdout/stderr, wait, kill) replacing QProcess.
- `device-socket-channel`: TCP client/server for the in-device stacktrace channel replacing QTcpSocket/QTcpServer.
- `loli-record-serialization`: Explicit endian-explicit binary serializer/deserializer for the .loli format, backward-compatible with existing files, usable by both GUI and CLI.
- `core-async-events`: Qt-free async completion/notification mechanism (callbacks + thread-safe queue) replacing Qt signals/slots for process/data events.
- `config-persistence`: Qt-free config/settings store (INI/JSON) replacing QSettings, including capture config and SDK/NDK/python paths.
- `cli-no-qt`: The LoliProfilerCLI executable and its compare/dump features rebuilt on the Qt-free core.

### Modified Capabilities
<!-- No existing specs in openspec/specs/ to modify; the migrate-gui-to-imgui change specs live under its change dir. -->

## Impact

- **Affected code**: all of `src/` core (`adbprocess`, `stacktraceprocess`, `meminfoprocess`, `screenshotprocess`, `startappprocess`, `addressprocess`, `stacktracemodel`, `stacktraceproxymodel`, `configdialog`, `hashstring`, `pathutils`, `clilogger`, `cliprofiler`, `profilecomparator`, `smaps/`), `include/`, `src/main_cli.cpp`, `src/gui/guidatabridge.*` (bridge becomes unnecessary or collapses into plain ownership), `CMakeLists.txt`, build scripts, Docker.
- **Removed**: legacy Qt GUI (`src/mainwindow.*`, all `.ui`, `qdarkstyle/`, `thirdparty/qconsolewidget-master/`, Qt-model files), all Qt deps.
- **Dependencies**: drops Qt5 entirely; gains nothing new (SFML already vendored; process/socket/serialization helpers are in-house or header-only).
- **Build**: faster configure/build, smaller distribution, no Qt runtime DLLs.
- **Risk**: largest risk is the .loli format compatibility (must byte-match QDataStream layout) and the device socket protocol (must match the injected library's expectations). Both need careful regression testing against existing captures and a real device.
