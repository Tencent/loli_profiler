## Context

After the ImGui GUI migration, the Qt GUI presentation layer is replaced, but the profiling **core** (`src/adbprocess`, `stacktraceprocess`, `meminfoprocess`, `screenshotprocess`, `startappprocess`, `addressprocess`, `stacktracemodel`, `configdialog`, `hashstring`, `pathutils`, `smaps/`) and the **CLI** (`main_cli.cpp`, `cliprofiler`, `profilecomparator`, `clilogger`) still depend on Qt5 Core/Network/Concurrent/Sql. The new ImGui GUI links Qt only because it reuses these core sources via `GuiDataBridge`.

Key existing structures to preserve behavior:
- **`.loli` format**: Qt `QDataStream` binary (big-endian), field order documented in the `loli-record-format` memory and in `migrate-gui-to-imgui/design.md`. Layout: magic `0xA4B3C2D1`, version `106`, meminfo series, `QHash<quint32,QString>` string intern table, records, callstack map, symbol map, freeaddr map, screenshots, smaps sections.
- **Device channel**: `StackTraceProcess` connects via TCP to the injected in-device server (port 8000, forwarded over adb). `adb forward tcp:8000` + socket. Protocol is custom frames — must match the injected `libloli.so` exactly.
- **Async model**: core processes are `QObject`s emitting Qt signals; the GUI/CLI pump a Qt event loop. Symbol translation runs via `QtConcurrent`.
- **Config**: `loli3.conf` text file (key:value lines) read/written by `ConfigDialog`; SDK/NDK/python paths via `QSettings` ("MoreFun"/"LoliProfiler") + `PathUtils`.

## Goals / Non-Goals

**Goals:**
- Zero Qt includes/links anywhere in the build (GUI + CLI + core).
- `LoliProfilerImGui` and `LoliProfilerCLI` build and run with only vendored deps + STL.
- `.loli` files written by the new serializer are byte-compatible with the old format (old captures load in the new build; ideally new captures load in the old GUI too — same byte layout).
- Live capture + record save now fully work in the ImGui GUI (unblocked once the bridge is Qt-free).
- No behavior regression in the profiling data pipeline (records, stacktraces, smaps, screenshots, meminfo).

**Non-Goals:**
- Changing the profiling **protocol** or the injected `libloli.so` (device side stays as-is).
- Changing the `.loli` **semantic content** (same fields, same order) — only the serializer implementation changes.
- Rewriting the ImGui GUI panels (they're already Qt-free except the bridge seam).
- Cross-platform GUI polish beyond what exists (Windows/macOS/Linux build parity is maintained, not expanded).

## Decisions

**Global rules for every Qt replacement:**
- **Prefer SFML's facilities** first (it's already vendored). Sockets → SFML. If SFML doesn't cover a need, prefer a small vendored library next.
- **Before writing anything from scratch, surface the choice to the user first** — do not hand-roll a dependency without asking.
- **Reproduce the Qt original's behavior step-for-step** wherever a Qt flow encodes real ordering (launch sequence, device setup, serialization). See D10.

### D1: Containers — direct STL substitution
Replace `QString`→`std::string` (UTF-8), `QVector`→`std::vector`, `QHash`/`QSet`→`std::unordered_map`/`std::unordered_set`, `QStringList`→`std::vector<std::string>`, `QPair`→`std::pair`. Mechanical, file-by-file. `HashString`'s intern table becomes `std::unordered_map<uint32_t, std::string>` (keep the `qHash`-compatible hashcodes **only** where they're persisted in `.loli` — see D3).

*Alternative considered:* keep a `QString`-like wrapper — rejected; STL is simpler and the codebase is small enough for direct substitution.

### D2: Process execution — prefer SFML, else in-house (ask before scratch)
Replace `QProcess` with a cross-platform process runner. **Preference order: (1) SFML-provided facilities if any cover the need, (2) a small vendored library, (3) in-house ONLY as a last resort — and before writing anything from scratch, surface the decision to the user first.** SFML 3 has no process API, so the realistic options are a vendored `tiny-process-library`/`reproc` or a thin in-house wrapper over `CreateProcess`+pipes (Windows) / `posix_spawn`+pipes (POSIX). Match `AdbProcess`'s needs: program/args, working dir, capture stdout/stderr, blocking wait, async notify, kill.

*Alternatives:* vendored `reproc`/`tiny-process-library` preferred over hand-rolling; in-house only if neither fits (and only after asking).

### D3: Device socket — SFML `sf::TcpSocket`/`sf::TcpListener`
SFML is already vendored for the GUI and provides blocking/non-blocking TCP. Wrap it in a thin `DeviceChannel` that mimics `QTcpSocket`'s connect/read/write/readyRead usage in `StackTraceProcess`. The `adb forward` stays a subprocess call.

*Alternative:* raw BSD sockets / ASIO — rejected; SFML is already present and sufficient.

### D10: Capture launch flow — frame-by-frame parity with the Qt original
The Qt `MainWindow::on_launchPushButton_clicked` launch sequence is the reference behavior and MUST be reproduced step-for-step in the Qt-free core + ImGui GUI. Reference (from `src/mainwindow.cpp` ~line 1943):
1. Set the selected device serial on all ADB processes (stacktrace, startapp, meminfo, screenshot, address).
2. Prompt **Launch vs Attach** (inject mode).
3. Prompt **Enable Data Optimization** (strip non-persistent / useCache).
4. Show a progress dialog (7 steps: Preparing → push libloli.so → root check → push loli.conf → ...).
5. Reset all capture state (libraries, models, smaps, screenshots, symbol map, records cache, freeaddr map, callstack map, hashstring table, meminfo series, max meminfo = 128).
6. If useCache: clear the on-device/local `cache/` folder.
7. Read current settings (`ConfigDialog::GetCurrentSettings()`); set python + adb paths on StartAppProcess.
8. `StartAppProcess::StartApp(appName, subProcess, compiler, arch, inject, progress)`.
9. Mark capturing, log "Starting application ...".
The Qt-free port must drive these same steps (the progress dialog becomes an ImGui modal; the two prompts become dialog options or settings).

*Why:* the launch flow encodes real device-setup ordering (root check, file pushes, port forward timing); reordering or dropping a step causes subtle capture failures. Frame-by-frame parity de-risks the port.

### D11: Serialization correctness — differential test harness vs Qt output
To guarantee `.loli` byte-compat (D4), build a **test that compares what Qt produces vs. what we produce**: a small Qt-based reference writer (compiled against Qt, in a throwaway/test target) emits a known set of values (every field type used in .loli: qint32/quint32/quint64, QString, QByteArray, QHash, QVector, QPointF, QUuid-string, nested structures), and the new `LoliWriter` emits the same values; the test asserts the byte buffers are identical. Likewise the reader: feed both the Qt reader and our reader the same bytes and assert identical parsed values. Run against synthetic vectors AND the two real sample files.

*Why:* the .loli format's QString/QUuid/QHash encodings have non-obvious details (UTF-16 units, alignment, count prefixes); a differential test against Qt's own output is the only reliable way to catch encoding mismatches before they corrupt real captures.

## Risks / Trade-offs

### D4: `.loli` serialization — explicit little-endian-aware writer matching QDataStream byte layout
QDataStream writes Qt types with specific encodings: `qint32/quint32/quint64` big-endian, `QString` as `quint32 length` + UTF-16 data (actually UTF-16 code units, 2 bytes each), `QByteArray` as `quint32 length` + raw bytes, `QHash`/`QVector` with count prefixes, `QPointF` as two `double`s, `QUuid` via its string form in the current writer. We implement a `LoliWriter`/`LoliReader` that reproduces this byte-for-byte, plus unit tests that round-trip against the existing 395MB sample (`2026.09.14-11.24.23.loli`) and `6s_heap_0919.loli`.

This is the **highest-risk** decision. We must verify the exact QString/QUuid/QHash encodings by reading Qt 5.15 QDataStream source and validating against the sample files byte-by-byte.

*Alternative:* define a NEW format and keep a legacy Qt-reader — rejected; it would strand existing captures and the CLI `--compare`/`--dump` workflows rely on the shared format.

### D5: Async events — callback registry + lock-free-ish queue
Replace Qt signals/slots with a tiny `EventBus`/callback approach: core processes take `std::function` handlers; completion is marshalled to the UI thread via a thread-safe queue the GUI pumps each frame (it already pumps via the frame loop). The bridge's `GuiDataBridge` collapses into direct ownership of the core objects (no QObject/Q_OBJECT), keeping the snapshot-publish design.

*Alternative:* keep a minimal event-loop shim — rejected; the ImGui frame loop is the pump.

### D6: Concurrency — `std::async` / thread pool for symbol translation
`QtConcurrent::run` + `QFuture` → `std::async`/`std::future` (or a small persistent thread pool for the loader). `QFutureWatcher` completion → a done-flag polled on the UI pump (the GUI already polls `IsLoading()`).

### D7: Config — INI via a tiny header (or in-house parser)
`QSettings` → a small INI read/write (in-house, ~100 lines) for app settings; `loli3.conf` keeps its existing custom `key:value` text format (already hand-parsed, not QSettings). Paths (SDK/NDK/python) move into the INI store. Registry-backed QSettings on Windows → replace with a per-user config file next to the executable or `%APPDATA%`.

### D8: QUuid — `std::array<uint8_t,16>` or sequential IDs
Records use `QUuid` only as a map key for callstack association. Replace with a monotonically increasing `uint64_t` per record (sufficient — uuids aren't persisted semantically; the .loli writer emits a uuid *string* per record for format compatibility, which we can synthesize deterministically). Confirm from the format that uuid strings are only keys, not semantically meaningful.

### D9: Remove the legacy Qt GUI target
Delete `src/mainwindow.*`, all `.ui`, `qdarkstyle/`, `thirdparty/qconsolewidget-master/`, the Qt-model classes after the CLI is ported. `LoliProfilerImGui` becomes the GUI.

## Risks / Trade-offs

- [`.loli` byte-compat: subtle QString/QHash/QUuid encoding differences break loading old captures] → Mitigation: byte-level diff harness against the two known sample files; unit tests per field type; read Qt 5.15 QDataStream source to confirm encodings before writing the serializer.
- [Device socket protocol mismatch with injected libloli.so] → Mitigation: keep the protocol byte-identical; integration-test live capture against a real device early; keep the change to transport only (no protocol edits).
- [QtConcurrent→std::async thread-lifetime bugs during translation] → Mitigation: single well-defined translation entry point; join/detach discipline; the existing loader already isolates work onto one worker.
- [Large mechanical refactor introduces regressions in CLI compare/dump] → Mitigation: port CLI after core is Qt-free and run `--dump`/`--compare` on the sample files as a golden-output check (diff against current Qt CLI output).
- [Scope creep into GUI rewrites] → Non-goal; only the bridge seam changes, panels untouched.

## Migration Plan

1. Land on a new branch `remove-qt` cut from `imgui-gui` (which is the GUI-complete base). Keep `master` and `imgui-gui` intact as rollback.
2. Introduce the Qt-free helpers first (ProcessRunner, DeviceChannel, EventBus, INI config, Loli serializer) alongside Qt, behind the existing call sites.
3. Port core sources bottom-up (leaf utilities → process classes → stacktrace pipeline → config), keeping the build green at each step (Qt still linked until the last core file drops Qt).
4. Port the CLI; validate `--dump`/`--compare` golden output against the Qt CLI on the sample files.
5. Switch the ImGui GUI bridge to the Qt-free core; wire live-capture tree build + SaveRecord.
6. Remove Qt from CMake (`find_package(Qt5)`, Qt targets), delete legacy GUI, update build scripts/Docker/docs.
7. Regression pass: load both sample .loli files, live-capture on a device, CLI dump/compare.
8. Rollback: the change is isolated on `remove-qt`; `master` and `imgui-gui` remain Qt-based until merge.

## Open Questions

- Exact QDataStream encoding of `QString` (UTF-16) and `QUuid` in the writer — must be confirmed against Qt 5.15 source + sample bytes before implementing the serializer. (Resolve in D4 implementation.)
- Whether `loli3.conf` parsing should move to the INI store too or stay its own format. (Default: keep `loli3.conf` as-is to avoid breaking existing configs; INI is for app settings only.)
- Thread pool vs `std::async` for translation — decide during implementation; default to a small persistent pool to avoid thread churn on large loads.
- Linux process/socket implementation — SFML sockets are cross-platform; ProcessRunner needs a POSIX path (posix_spawn). Confirm CI/docker still builds without Qt.
