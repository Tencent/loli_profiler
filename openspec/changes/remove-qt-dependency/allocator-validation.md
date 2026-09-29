# Android allocator capture validation

## Why `uam_process_start_v40.loli` is incomplete

The 240-second Binned3 capture contains 137,189 records. The GUI's old `Size`
column summed all allocations, including freed blocks. In that file the
`ZSTD_compress_usingCDict_internal` path accounts for about 735 MiB of
cumulative allocation traffic, but **0 MiB live at capture end**. The whole
file has only 6.85 MiB live in profiler records while meminfo reports about
102 MiB Native Heap at the end.

This is also an allocator coverage problem. `UAGame.Target.cs` does not enable
`bUseLoliProfiler`; the UBT default is false and its Android rule omits
`USE_LOLI_PROFILER` for Test unless explicitly enabled. The installed Test APK
uses the arm64 `FMallocBinned3` path in `AndroidPlatformMemory.cpp` unless the
existing `/storage/emulated/0/UE4Game/ansi.flag` override is present.
`libloli.so` hooks libc allocation imports from `libUE4.so`, so Binned3's
internal allocations are absent from that capture. No `FMallocBinned3` frame
appears in its translated symbol map.

## ANSI allocator probe

The device had no allocator flag initially. A temporary `ansi.flag` selects
`FMallocAnsi` in the existing APK, without rebuilding it. With strict mode and
threshold 0, a roughly 10-second startup probe produced 18,199,468 records,
3,912.24 MiB gross allocated, and 272.35 MiB live. Of its callstacks, 99.99%
include `AnsiMalloc` or `FMallocAnsi`. The unsampled `.loli` is 8.29 GB and is
too large for routine GUI work.

The built-in loose-mode Poisson sampler at a 16 KiB interval makes the probe
reviewable. A comparable short run saved 258,840 records in 118 MB and
estimated 271.05 MiB live, within 0.5% of the strict probe's live total.
Gross allocation totals differed by about 9% across the two separate runs;
sampled byte totals are estimates and record counts are sample counts.
ANSI mode changes the game's allocator. It validates profiler coverage and
callstack plumbing, but it does not reproduce Binned3's production memory
behavior. An instrumented Test APK is needed for exact Binned3 parity.

A first 240-second sampled ANSI run from startup through farmland saved 1,412,117
records in 800 MB. It estimated 992.90 MiB live; meminfo ended at about
1,260 MiB Native Heap and 1,970 MiB total. The async loading path held about
258.51 MiB live, while the Zstd compression path again held 0 MiB live. This
run used the **old realloc hook** and is retained only as a comparison.

## Corrected hook and accepted capture

`plugins/Android/jni/loli.cpp` previously emitted a free event for realloc's
new pointer. It now emits the old pointer when realloc succeeds (or frees on
zero-size realloc). The rebuilt arm64 `libloli.so` is staged in both the plugin
and Release remote directories. Its SHA-256 is
`B2F6F279F39DE105E5021C163B161F38515DCA46846D24C83BB45966D439A2F1`;
the device copy used for capture matched by MD5.

After the device was manually unlocked, a new 240-second launch-to-farmland
capture with this hook saved 1,289,370 records in 749 MB. It resolved
28,265,911 frames through smaps and symbolized 25,876 distinct UE4 addresses.
The 16 KiB sampled capture estimated **988.25 MiB live**; meminfo ended at
1,254 MiB Native Heap and 1,905 MiB total. The async loading path held
259.80 MiB live, and the Zstd compression path held 0 MiB live despite
722.04 MiB cumulative allocation traffic. Uama's staged screenshots confirm
the DLC dialog, login, farmland, and late in-game stages.

The old/new live estimates differ by 4.65 MiB across separate game runs.
This is consistent with the corrected hook, but is not an isolated measurement
of the realloc fix. The corrected capture is
`build/cmake/uam_ansi_realloc_verified_full.loli`; its GUI live view was opened
and checked. The gross/live path audit can be repeated with
`python openspec/changes/remove-qt-dependency/tests/audit_capture.py <file.loli>`.

## Device state and next allocator parity check

`adb shell input` is denied `INJECT_EVENTS`, and changing stay-awake settings
is denied `WRITE_SECURE_SETTINGS`; the user manually unlocked the device for
the final run. The temporary `ansi.flag` has been removed, the game stopped,
and the strict-mode capture config and Android SDK path restored. For exact
`FMallocBinned3` production behavior, a matching Test APK and symbols must be
rebuilt with `bUseLoliProfiler=true` and captured without the ANSI override.

Unreal Editor is running with adb v40 on port 5037. The capture tools used
that same SDK adb while testing; the saved profiler SDK path was restored to
its original v41 value afterward. A resumed device run must align those adb
versions or close the editor first.
