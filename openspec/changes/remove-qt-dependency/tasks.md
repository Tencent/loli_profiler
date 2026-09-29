## 1. Branch & Setup

- [x] 1.1 Cut branch `remove-qt` from `imgui-gui`; keep master/imgui-gui as rollback
- [x] 1.2 Add build option or staging layout so Qt-free core builds alongside Qt during the transition (build stays green each step)

## 2. Qt-free foundation helpers (alongside Qt, no behavior change)

Preference rule for ALL helpers: use SFML's facilities first; if SFML can't cover it, prefer a small vendored library; only write from scratch as a last resort — and ask the user before writing anything from scratch.

- [x] 2.1 Process runner replacing QProcess. SFML has no process API — evaluate vendored `reproc`/`tiny-process-library` first; in-house (CreateProcess/posix_spawn + pipes) only if neither fits, after asking the user. Needs: program/args/cwd, stdout+stderr capture, blocking wait + timeout, async notify, kill.
- [x] 2.2 `DeviceChannel`: TCP client/listener over **SFML sockets** (`sf::TcpSocket`/`sf::TcpListener`) mimicking QTcpSocket usage in the stacktrace channel
- [x] 2.3 `EventBus`/callback queue: std::function handlers + thread-safe queue pumped by the GUI frame loop / CLI loop
- [x] 2.4 Config store for app settings: prefer a tiny vendored INI/JSON lib over in-house (ask first if in-house); keep `loli3.conf` format as-is
- [x] 2.5 Thread-pool / std::async wrapper for parallel translation
- [x] 2.6 Record identity: replace QUuid with a std-based id (confirm .loli uuid-string compatibility)

## 3. .loli serializer (highest risk — do early with tests)

- [x] 3.1 Confirm exact QDataStream byte encodings (QString UTF-16, QUuid string, QHash/QVector/QByteArray/QPointF, qint/quint) from Qt 5.15 source + sample bytes
- [x] 3.2 Build a **differential test**: a Qt-based reference writer/reader emits a known set of values (every field type in .loli: qint32/quint32/quint64, QString, QByteArray, QHash, QVector, QPointF, QUuid-string, nested structures), and asserts the new `LoliWriter`/`LoliReader` produce byte-identical buffers / identical parsed values
- [x] 3.3 Implement `LoliReader`/`LoliWriter` reproducing the byte layout exactly (magic 0xA4B3C2D1, version 106, documented field order), validated by the differential test
- [x] 3.4 Byte-level + load-verification against `2026.09.14-11.24.23.loli` and `6s_heap_0919.loli` (record counts + aggregates match Qt reader)
- [x] 3.5 Implement ImGui GUI `SaveRecord` on the new serializer (replace stub); save→reload round-trip test

## 3b. Launch-flow parity harness (capture orchestration -> LoliCore APIs, see design D12)

The launch/capture orchestration produced here is a LoliCore API (UI-free,
callback/progress based), consumed by BOTH the ImGui GUI and the headless
CLI — the GUI is a viewer + control surface calling the same APIs the CLI
calls directly, not an owner of capture logic. The `loli_cli/` Python
package stays analysis-only.

- [x] 3b.1 Extract the Qt `on_launchPushButton_clicked` sequence into a documented step list (see design D10) and write the Qt-free launch driver (LoliCore API: step callbacks + progress reporting, no UI types) to reproduce it step-for-step
- [x] 3b.2 Verify the launch steps against a real device: push libloli.so, root check, push loli.conf, port forward, StartApp — in the same order as the Qt original
- [x] 3b.3 Replace the two Qt `QMessageBox` prompts (Launch vs Attach; Enable Data Optimization) with launch-API options (consumed by an ImGui dialog in the GUI, CLI flags in the headless exe) that produce the same `enableInject`/`useCache` outcomes.

## 4. Port core to Qt-free (bottom-up, build green each step)

- [x] 4.1 Leaf utilities: `hashstring`, `pathutils`, `smaps/` containers, `clilogger`
- [x] 4.2 Process classes: `adbprocess`, `addressprocess`, `meminfoprocess`, `screenshotprocess`, `startappprocess` onto ProcessRunner (as LoliCore APIs per D12)
- [x] 4.3 `stacktraceprocess` onto DeviceChannel + EventBus
- [x] 4.4 Models/config: `stacktracemodel`, `stacktraceproxymodel` (fold into the flat tree representation), `configdialog` state onto INI + loli3.conf
- [x] 4.5 Symbol translation onto the thread pool

## 5. Headless CLI on LoliCore (equal capture capability to the GUI, per D12)

The CLI exe has the same launch+capture ability as the GUI — it calls the
same LoliCore capture APIs directly (no IPC, no Python). `loli_cli/` (py,
analysis) is untouched.

- [x] 5.1 `main_cli.cpp`, `cliprofiler`, `profilecomparator` off Qt, capture orchestration via the LoliCore launch/capture APIs
- [ ] 5.2 Golden-output check: `--dump` and `--compare` on sample files match Qt CLI output
- [x] 5.3 CLI capture end-to-end on a device with no Qt runtime

## 6. ImGui GUI integration on Qt-free core

- [x] 6.1 Collapse `GuiDataBridge` Qt seam into direct ownership (no QObject/signals)
- [x] 6.2 Live-capture incremental tree build (stacktrace tree + treemap populate during capture, not just on load)
- [x] 6.3 Remove Qt event-loop pumping from `main_imgui.cpp`

## 7. Cleanup & release

- [x] 7.1 Remove legacy Qt GUI target, `mainwindow.*`, `.ui` files, `qdarkstyle/`, qconsolewidget, Qt model classes
- [x] 7.2 Remove all `find_package(Qt5)`/Qt linkage from CMakeLists.txt; keep the historical Qt differential sources outside active build targets.
- [x] 7.3 Replace platform build wrappers with one Qt-free Python driver; update Dockerfile, README, CLAUDE.md, and the build guides under `docs/`. Windows packaging passed; macOS/Linux execution remains in 11.6.
- [ ] 7.4 Regression pass: load both sample .loli files, live capture on device, CLI dump/compare, fresh-machine build
- [ ] 7.5 Merge `remove-qt` to master

## 8. Qt behavior parity and device validation

- [x] 8.1 Audit captured frame order, smaps address conversion, symbol lookup, and merged call tree against the old Qt implementation; compare the installed APK's `libUE4.so` build identity with the local APK and symbol `.so` before attributing bad names to missing DWARF.
- [x] 8.2 Run the uama CLI capture flow on the real device and validate representative raw and translated stacks against the matching binary.
- [x] 8.3 Restore treemap navigation: render the current parent as the graph's full-size root cell, make it clickable to navigate up, and remove the bottom Go Up button.
- [x] 8.4 Restore Qt-style search in stacktrace and treemap: preserve all nodes, advance to the next substring match on every Enter press, reveal the selected match, and wrap after the last match.
- [x] 8.5 Capture the UAM process from launch/injection onward, automate DLC skip and farmland entry with uama, and verify the saved capture includes startup allocations and substantial in-game data.
- [x] 8.6 Size the Run/Launch and Capture Configuration dialogs to their complete content without an outer scrollbar; keep their embedded app/pattern lists independently scrollable.
- [x] 8.7 Load the large iOS `6s_heap_0919.loli` sample and compare the `UDataTable::Serialize` call path and surrounding frames with the old Qt tree, allowing platform-specific outer frames.
- [x] 8.8 Audit the UAM capture against raw allocation/free records and the actual Android allocator: distinguish cumulative allocation traffic from live bytes, verify whether Binned3 allocations bypass libloli's hooks, and identify missing thread coverage.
- [x] 8.9 If allocator coverage is missing, select and verify a reversible Android allocator override (for example the existing `ansi.flag` path), then recapture from process launch and compare thread, callstack, and live-byte distributions before accepting the result.
- [x] 8.10 Make high-rate ANSI captures practical: keep free-event indexing and smaps resolution sublinear, validate a bounded sampling configuration against the full short probe, and ensure the resulting file can be opened and interpreted honestly in the GUI.
- [x] 8.11 Expose live versus cumulative allocation trees for saved captures in the ImGui GUI, defaulting to live when free records exist and labeling cumulative totals clearly.
- [x] 8.12 Correct Android `realloc` free events to reference the old pointer, rebuild the arm64 `libloli.so`, and verify live-byte accounting with a bounded allocator test before accepting the sampled capture as final.
- [x] 8.13 Start the CLI's timed duration only after launch/injection completes, so early adb-forward connections cannot consume the capture window before the app runs.
- [ ] 8.14 For production-allocator parity, build a matching Test APK and symbol `.so` with `bUseLoliProfiler=true`, then verify `FMallocBinned3` leaf frames without the ANSI override.

## 9. Qt control and inspection parity

- [x] 9.1 Restore launch-time All vs persistent-only capture retention with the same data effect as the Qt optimization prompt; keep the saved-file Live view a separate inspection control. Persistent mode now keeps one live record per address and restores arrival order at stop.
- [x] 9.2 Preflight adb before opening Run/Launch; show a clear ImGui failure modal with the command/error and retry action, and keep device/process launch errors inside the GUI. Windows child processes use CREATE_NO_WINDOW.
- [x] 9.3 Audit every capture configuration field and option against the Qt dialog and native Android hook, including arm architectures and malloc/mmap; make white/black lists editable through context menus and double click. LLVM x86/x86_64 hooks build and package.
- [x] 9.4 Correct smaps display units and compare representative VSS/RSS/PSS values with raw smaps and the Qt view. Raw smaps and .loli fields are kB; the GUI now converts kB to bytes only for display.
- [x] 9.5 Complete stacktrace and treemap search navigation: keep keyboard focus after Enter, show a three-second fading match-count notice, add Stacktrace previous/next controls and click selection, and reveal horizontally clipped selected names.
- [ ] 9.6 Compare Android `UDataTable::Serialize` paths against the user's forthcoming Qt capture before drawing a conclusion about missing frames.
- [ ] 9.7 Verify persistent-only capture, adb failure modal, x86/x86_64 runtime hooks, and keyboard search navigation on devices and interactively. The normal Run/Launch and Configuration dialogs passed 1280x720 screenshot checks.

## 10. Final de-Qt audit

- [x] 10.1 Restore Attach in the ImGui launch dialog and pass the selected option to `LaunchDriver`; rebuild verified. Device behavior remains part of 9.7.
- [x] 10.2 Run the Windows VS2022 build with local NDK and produce a verified `dist/LoliProfiler-windows.zip`; extraction and bundled `agentcli` auto-conversion passed. Fresh-machine validation remains part of 7.4.
- [x] 10.3 Remove active Qt CMake targets and orphaned Qt GUI source files while retaining historical differential references outside the build.
- [ ] 10.4 Run Qt-free CLI dump/compare against old Qt golden output. Offline smoke passed on `6s_heap_0919.loli`: dump includes `UDataTable::Serialize`, and self-compare reports zero changed/new allocations; exact Qt text parity is unverified.
- [ ] 10.5 Verify capture/session ownership under stop, save, load, and new-capture transitions. The detached save/load threads and watcher-thread session mutations are removed, and ProcessRunner reuse now joins prior watchers; runtime regression remains.
- [x] 10.6 Restore the old CLI's `--dump ... --out file.db` SQLite export. The Python `loli` auto-conversion and search work; the large iOS sample exports to an integrity-checked 436,293-node database.
- [ ] 10.7 Verify the repaired ImGui launch completion callback on a real device; the previous `joinable()` check could never signal successful injection.
- [x] 10.8 Make CLI `--enable-memory-optimization` retain only persistent allocations while capturing; default CLI capture keeps all records. Synthetic alloc/free, moved/in-place realloc, and delayed-event cases pass `CliRetentionTest`. Device validation remains in 9.7.
- [x] 10.9 Make the Python build driver and docs support macOS/Linux Qt-free targets and the `agentcli` layout without whole-tree cleanup. Real platform builds remain in 11.6.

## 11. Naming, presentation, and release polish

- [x] 11.1 Center and lock the Run/Launch and Capture Configuration dialogs, and make Programmer the default theme on first launch.
- [x] 11.2 Rename the Python package directory to `agentcli`, update imports, package metadata, release copies, and current docs while preserving the existing `loli` command. The wheel and module CLI build.
- [x] 11.3 Refresh README, quick-start, architecture, troubleshooting, and build docs with current ImGui screenshots and open-source components; move the Linux guide under `docs/`. Current Markdown links resolve.
- [x] 11.4 Produce and inspect a deployable Windows zip containing GUI, CLI, `agentcli`, icon, docs, Python injector, and Android hook libraries; extraction and offline `.loli` auto-conversion passed.
- [x] 11.5 Make the Python build/package driver support macOS/Linux Qt-free targets and the new layout; archive-layout fixture checks passed.
- [ ] 11.6 Run the macOS and Linux scripts on those operating systems, launch the GUI/CLI from extracted archives, and fix platform-specific build or runtime failures.

## 12. Python build orchestration

- [x] 12.1 Add one cross-platform Python build driver with an interactive menu when no arguments are given and explicit non-interactive modes for binaries, full release, and package-only runs.
- [x] 12.2 Discover installed SDK/NDK paths; offer pinned official command-line-tools/Platform-Tools and NDK installation only after an interactive yes/no choice or explicit download flag. Verify the bootstrap archive SHA-256 and leave Google license acceptance to `sdkmanager`.
- [x] 12.3 Use the Python driver directly on Windows, macOS, and Linux while preserving the existing package layout.
- [x] 12.4 Verify help, interactive choice, no-download behavior, and Windows native/full/package modes with installed tools; no tools were downloaded during this work.
- [ ] 12.5 Run the approved SDK/NDK bootstrap on a disposable machine and test macOS/Linux builds; those paths are implemented but cannot be exercised with the installed Windows toolchains.
- [x] 12.6 Remove redundant BAT/SH build entry points and helper scripts. Docker execution is in `scripts/build.py`; current docs invoke Python directly. Windows native/package modes, zip integrity, interactive dry run, and Docker command dry run passed.

## 13. Post-capture symbolization

- [x] 13.1 Offer Save and Symbolize when a GUI capture stops, and expose Symbolize Record in the File menu for saved/opened records.
- [x] 13.2 Run the bundled CLI `--symbolize` asynchronously with a user-selected library, report failures in the GUI, and load the symbolized output on success without overwriting the original capture.
- [x] 13.3 Resolve the CLI beside the GUI on Windows/Linux and beside the macOS app bundle; support macOS NDK `llvm-symbolizer` lookup.
- [ ] 13.4 Verify the complete stop, save, symbolize, and reopen flow on a real GUI capture. A separate Windows Release build and deployable zip pass, the GUI loads an offline fixture, and the CLI rejects a symbol library with no matching frames. The interactive device flow remains.
- [ ] 13.5 Validate GUI smaps finalization on the device: the code now reads smaps asynchronously and resolves frames before save, but the stop-time device path has not yet been exercised.
- [ ] 13.6 Keep the workspace visibly inactive during an active capture while leaving the toolbar usable; restore interaction on stop or connection loss, and size post-capture dialogs consistently. Implemented and built; interactive capture check remains.
- [ ] 13.7 Persist capture screenshots in the session data so save and offline symbolization retain them. Both `test.loli` and `test.symbolized.loli` contain zero screenshots, confirming the loss was before CLI symbolization; verify a new saved capture after the fix.
- [x] 13.8 The unchecked retention choice maps to persistent-only mode. The user's file has 3,468,463 records and 3,138,539 free-address entries; the first 300,000 records have no repeated addresses and no later free events among their sampled addresses. Keep callstack correctness open pending the old-GUI/APK comparison.

## 14. Old GUI parity against the matching APK

- [x] 14.1 Clear the explicitly named stale `libUE4.so.txt`, then drive a fresh CLI capture on the unlocked device with the matching `C:\Users\xinhou\Downloads\libUE4.so`; enter the login menu and open the map directly. The 180-second parity audit and APK/symbol Build ID check are recorded in `old-gui-parity.md`.
- [ ] 14.2 Restore Qt timeline time-range selection and filtering in the ImGui GUI. The selected interval now updates callstack/treemap views with inclusive time bounds and a clear action; a 120,003-record selftest selected 20,244 records, then restored 19,988 full-tree nodes on clear. Manual drag interaction remains.
- [ ] 14.3 Restore the old GUI's possible-memory-leaks analysis mode in the ImGui GUI, including its trigger and data semantics; validate on a saved record against the Qt implementation. The async analyzer, toolbar trigger, Tree/Treemap result window, Qt leaf-growth threshold, and focused synthetic parity test are implemented. The analyzer parsed the 120,003-record `uam_final_sym.loli` fixture and produced two growing roots for the middle third of capture time; interactive Qt comparison remains.
- [ ] 14.4 Run a Windows build and targeted comparison checks for the two UI modes, then update the user docs for any newly restored controls. Windows Release GUI build, focused leak-diff test, and saved-record range/leak/clear selftest pass (104 leak nodes); quick-start controls are documented. Interactive UI comparison remains.
- [x] 14.5 Make the Android hook server recover from a failed port bind and bound unsent allocation data when the CLI cannot drain it; validate a strict threshold-zero startup capture before claiming old-GUI parity. The arm64 device capture had zero overflow and acknowledged stop; all four LLVM and three GCC hook ABIs were rebuilt before release packaging.
- [x] 14.6 Restore Qt-compatible seconds for meminfo series and screenshots in CLI and GUI saves while keeping allocation record times in milliseconds. GUI loading normalizes both Qt seconds and early refactor milliseconds. An actual 180 MB GUI save round-trip changed meminfo X 1000/207000 to 1/207 and 41 screenshot times 6000/207000 to 6/207; reopening preserved range/leak results. The final CLI source writes seconds and the 75-tick smoke confirmed it.
- [x] 14.7 Select the outermost named DWARF inline caller for one symbol per captured PC, matching Qt nearest ELF symbol semantics. The 4.08M-record offline re-symbolized capture restored UDataTable::LoadStructData to 608,424 records / 83.57 MiB versus the old GUI 628,287 / 85.62 MiB, with the same middle callstack path.
- [x] 14.8 Complete a final full strict capture from process start with final CLI writer and symbol policy; map load, zero hook overflow, stop acknowledgement, Qt time units, 35 screenshots, and the 85.63 MiB DataTable branch are verified in `old-gui-parity.md`.
- [x] 14.9 Isolate `ndk-build` objects by NDK path/toolchain in `scripts/build.py` so full builds cannot link stale objects from another NDK. Windows full build and all LLVM/GCC ABI rebuilds passed; staged Release hooks and the 68-file zip match their source binaries by SHA-256.

## 15. Large-capture diagnostics and presentation

- [x] 15.1 Use the user's verified new-GUI screenshot as the README overview image (`docs/images/readme-overview.png`).
- [x] 15.2 Add an in-app Console panel for launch, capture, file I/O, symbolization, and errors; support opt-in timestamped file logging with `--log-file [path]`. Both explicit and default paths were exercised.
- [x] 15.3 Time and log record read/parse, snapshot conversion, full/live tree builds, treemap layout/display/picking, save, range/leak analysis, and launch steps; include RSS and record metadata in diagnostic logs.
- [x] 15.4 Benchmark `oldui.loli`, then optimize measured bottlenecks. Its 2.4 GB load fell from 71.5 s to 40.9 s by aliasing the identical live/cumulative tree when saved free events exclude no records; post-load RSS fell from ~3.53 to ~3.27 GiB. Treemap layout/render was ~7 ms on this file, so it cannot usefully parallelize with tree construction. On the 180 MB sample, UTF-8-safe binary-search label fitting cut the display pass from ~112 ms to ~6 ms; picking image readback now happens once per render rather than every hover frame. Mixed-record range/leak selftests still pass.
- [x] 15.5 Document Console and `--log-file [path]` in both quick starts and README, rebuild Windows binaries, and verify the 120,003-record range/leak selftest with timestamped file logging. The 69-file Windows zip passes integrity checks and contains the updated screenshots and binaries.
- [x] 15.6 Simplify capture controls: show only Run while idle and replace it with Stop Capture during capture; label the File menu command Run and retain Ctrl+R/Cmd+R. Updated quick-start text and screenshots.
- [x] 15.7 Independent read-only review of live-tree reuse found no correctness issue. In 10,000 generated record/free-event cases, all 2,959 eligible cases matched the separate live filter across tested time ranges, including duplicate frees. Diagnostic wording now states only what the file proves: saved free events exclude no records.

## 16. Unified logging and allocation-view control

- [x] 16.1 Replace separate GUI/CLI loggers with `LoliLogger` in the Qt-free core. It supports Debug/Info/Warn/Error, source-tagged stream records, a thread-safe Console ring, optional file/terminal sinks, and level filtering; GUI `--log-file` remains opt-in.
- [x] 16.2 Migrate GUI, CLI capture, CLI file-mode diagnostics, injector, file-dialog, and tree diagnostics to the shared API; remove obsolete logger implementations. CLI dump/compare report text and `.loli` formats remain unchanged.
- [x] 16.3 Move allocation view selection from the toolbar into a Stacktrace dropdown after the search arrows. `All Allocations` and `Persistent` drive Stacktrace, Treemap, and Leaks through one selection; both saved-view screenshots and the range/leak selftest pass.
- [x] 16.4 Update quick-start docs/screenshots and verify GUI/CLI builds, representative file operations, logging sinks, and allocation-view behavior. Windows GUI/CLI builds, GUI load/range selftests, Persistent/All screenshots, CLI dump/compare success/error logs and level filtering pass; the refreshed 69-file zip passes integrity checks.
- [x] 16.5 Commit Treemap search only on Enter: typed text stays separate from the active query, so typing does not rescan nodes or rerender the texture. Previous/next navigate the committed matches. Windows GUI build passes.

## 17. GUI capture timing and inspection polish

- [ ] 17.1 Fix GUI capture sampling to use a one-second wall clock while pumping allocation packets every frame; saved timeline and screenshot times must align with allocation record milliseconds. Code and Windows Release build pass; fresh device verification is pending because an external adb v40 daemon repeatedly replaced the configured SDK adb v41 server and reset the JDWP injector connection.
- [x] 17.2 Detect and approximately remap existing frame-count-timed GUI files on load, with an explicit diagnostic warning, so timeline selections can find allocation records. The 4274-second GUI chart is remapped to the 105.285-second allocation span; a selected interval contains 3,185,545 records.
- [x] 17.3 Remove the selected-range status line from Stacktrace; log range counts/bytes in Console instead. Range build logs the selected milliseconds, records, bytes, and persistent totals.
- [x] 17.4 Make Console log-only, with Copy all and Clear view in a context menu and auto-scroll only while already at the bottom. Move file-logging enable/path controls to Settings while keeping startup arguments available. Settings screenshot and isolated persisted-log startup smoke pass.
- [ ] 17.5 Rebuild and validate the new GUI file, a Qt-era file, a normal GUI capture clock, the Settings flow, and the release package. Both files pass selected-range/leak selftests, persisted logging passes an isolated startup smoke, and the 69-file Windows zip passes integrity check; fresh device capture remains pending for 17.1.
