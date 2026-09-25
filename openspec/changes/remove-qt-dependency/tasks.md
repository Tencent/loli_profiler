## 1. Branch & Setup

- [ ] 1.1 Cut branch `remove-qt` from `imgui-gui`; keep master/imgui-gui as rollback
- [ ] 1.2 Add build option or staging layout so Qt-free core builds alongside Qt during the transition (build stays green each step)

## 2. Qt-free foundation helpers (alongside Qt, no behavior change)

- [ ] 2.1 `ProcessRunner`: cross-platform subprocess (CreateProcess/posix_spawn), pipes for stdout/stderr, blocking wait + timeout, async completion callback, kill
- [ ] 2.2 `DeviceChannel`: TCP client/listener over SFML sockets mimicking QTcpSocket usage in the stacktrace channel
- [ ] 2.3 `EventBus`/callback queue: std::function handlers + thread-safe queue pumped by the GUI frame loop / CLI loop
- [ ] 2.4 INI/JSON config store (in-house) for app settings; keep `loli3.conf` format as-is
- [ ] 2.5 Thread-pool / std::async wrapper for parallel translation
- [ ] 2.6 Record identity: replace QUuid with a std-based id (confirm .loli uuid-string compatibility)

## 3. .loli serializer (highest risk — do early with tests)

- [ ] 3.1 Confirm exact QDataStream byte encodings (QString UTF-16, QUuid string, QHash/QVector/QByteArray/QPointF, qint/quint) from Qt 5.15 source + sample bytes
- [ ] 3.2 Implement `LoliReader`/`LoliWriter` reproducing the byte layout exactly (magic 0xA4B3C2D1, version 106, documented field order)
- [ ] 3.3 Byte-level + load-verification tests against `2026.09.14-11.24.23.loli` and `6s_heap_0919.loli` (record counts + aggregates match Qt reader)
- [ ] 3.4 Implement ImGui GUI `SaveRecord` on the new serializer (replace stub); save→reload round-trip test

## 4. Port core to Qt-free (bottom-up, build green each step)

- [ ] 4.1 Leaf utilities: `hashstring`, `pathutils`, `smaps/` containers, `clilogger`
- [ ] 4.2 Process classes: `adbprocess`, `addressprocess`, `meminfoprocess`, `screenshotprocess`, `startappprocess` onto ProcessRunner
- [ ] 4.3 `stacktraceprocess` onto DeviceChannel + EventBus
- [ ] 4.4 Models/config: `stacktracemodel`, `stacktraceproxymodel` (fold into the flat tree representation), `configdialog` state onto INI + loli3.conf
- [ ] 4.5 Symbol translation onto the thread pool

## 5. Port CLI

- [ ] 5.1 `main_cli.cpp`, `cliprofiler`, `profilecomparator` off Qt
- [ ] 5.2 Golden-output check: `--dump` and `--compare` on sample files match Qt CLI output
- [ ] 5.3 CLI capture end-to-end on a device with no Qt runtime

## 6. ImGui GUI integration on Qt-free core

- [ ] 6.1 Collapse `GuiDataBridge` Qt seam into direct ownership (no QObject/signals)
- [ ] 6.2 Live-capture incremental tree build (stacktrace tree + treemap populate during capture, not just on load)
- [ ] 6.3 Remove Qt event-loop pumping from `main_imgui.cpp`

## 7. Cleanup & release

- [ ] 7.1 Remove legacy Qt GUI target, `mainwindow.*`, `.ui` files, `qdarkstyle/`, qconsolewidget, Qt model classes
- [ ] 7.2 Remove all `find_package(Qt5)`/Qt linkage from CMakeLists.txt
- [ ] 7.3 Update `build.bat`, `build.sh`, `build_linux_with_docker.sh`, `Dockerfile`, README, CLAUDE.md, BUILD_LINUX.md (drop Qt prerequisites)
- [ ] 7.4 Regression pass: load both sample .loli files, live capture on device, CLI dump/compare, fresh-machine build
- [ ] 7.5 Merge `remove-qt` to master
