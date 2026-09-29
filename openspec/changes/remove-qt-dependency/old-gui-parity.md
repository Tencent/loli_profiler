# Old Qt GUI / new CLI parity on UAM Test APK

Validated on 2026-09-29 with unlocked Android device `5dc63c3e`, the
`/storage/emulated/0/UE4Game/ansi.flag` allocator override, arm64/LLVM,
`mode:strict`, `threshold:0`, and `--enable-memory-optimization` (retain
allocations live at stop). The CLI launched and injected from process start.
At the login screen, `uama gm open_map map_name=farmland_715_main
player_start=0` opened the map directly. The in-game screenshot is
`build/cmake/device_check/shot_20260929_113857_full-qt-in-game.png`.

The final command, with the SDK adb v41 and uama also pinned to that SDK:

```powershell
& '.\build\cmake\Release\LoliProfilerCLI.exe' --app com.tencent.mf.uam --out 'E:\git\loli_profiler\build\cmake\cli_newapk_full_qt_parity_20260929.loli' --symbol 'C:\Users\xinhou\Downloads\libUE4.so' --device 5dc63c3e --duration 180 --enable-memory-optimization --verbose
```

| Measure | Old Qt `oldui.loli` | Final CLI capture |
|---|---:|---:|
| Capture time | 235 s | 180 s |
| Retained records | 3,487,403 | 3,585,818 |
| Retained bytes | 1,201.84 MiB | 1,250.79 MiB |
| Screenshots | 46 (6–232 s) | 35 (6–177 s) |
| `UDataTable::Serialize` subtree | 628,288 / 85.62 MiB | 628,453 / 85.63 MiB |
| Nested `UDataTable::LoadStructData` | 628,287 / 85.62 MiB | 628,452 / 85.63 MiB |
| `UScriptStruct::SerializeItem` | 709,070 / 59.24 MiB | 711,959 / 59.92 MiB |

The middle callstack path matches: `UScriptStruct::SerializeItem` →
`UDataTable::LoadStructData` → `UDataTable::Serialize(FStructuredArchiveRecord)`
→ `UDataTable::Serialize(FArchive&)` →
`FAsyncPackage::EventDrivenSerializeExport`. The DataTable subtree differs
by 165 retained records and 0.01 MiB across the independent runs. Total
retained memory is not expected to be identical because capture durations and
game states differ. The new CLI stored meminfo and screenshot positions in
Qt-compatible seconds; allocation record times remain milliseconds.

`llvm-symbolizer` translated 109,464 distinct addresses. It selects the
outermost named DWARF inline frame for each captured PC, matching the old
GUI's one-frame-per-PC nearest-symbol tree. Focused symbolizer validation
verified that `0x129b22dc` maps to
`UDataTable::LoadStructData`, with inlined `TSet::Reserve` and
`TMapBase::Reserve` beneath it in DWARF.

The Android hook logged `Server Recv: 0` and `Dumping smaps` at
`11:39:45.540`, and no transport-overflow or bind-failure messages appeared
for the valid run. The filtered device log is
`build/cmake/cli_parity_device_loli.log`. The final `.loli` is
2,523,659,844 bytes. The extracted
APK `libUE4.so` SHA-256 (`6a451ddebc034d888cabdf27c8e2cdb8f0610cea97cad11b82426fcef0d59712`)
matches the device-installed library exactly. The APK library and the
separate 3.47 GB symbol file both have GNU Build ID
`2e2a7111d509a57d204494f61fd7b388f0cadd74`.

The saved captures were audited with `tests/audit_capture.py`; the final
CLI output is `build/cmake/cli_parity_audit.txt`. The hook fix was rebuilt
for all four LLVM ABIs and all three legacy GCC ABIs. The Windows release
zip and both local runtime locations contain the rebuilt libraries.
