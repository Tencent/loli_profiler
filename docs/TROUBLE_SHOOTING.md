# Troubleshooting

## Device or ADB is unavailable

Set **File > Settings > Android SDK** to the SDK used by Android Studio or Unreal, then check `platform-tools/adb devices -l` from that same SDK. Authorize USB debugging on the device. LoliProfiler checks ADB before opening Run/Launch and shows failures in an ImGui dialog with Retry.

ADB clients share a host server on port 5037. A second Platform-Tools version, an `adb kill-server`, or another program changing the same forwarded port can interrupt a capture. Compare the `adb version` and `adb forward --list` output from the SDK path selected in Settings; use one server version during a capture. See the [Android ADB guide](https://developer.android.com/tools/adb).

## Capture starts but has few or no allocation records

Check that the package is debuggable or the device is rooted, the correct ABI hook exists under `remote/<compiler>/<arch>/libloli.so`, and the configured white/black list includes the allocation library. For Unreal builds using `FMallocBinned3`, allocations may bypass `malloc` hooks unless the game build enables the LoliProfiler allocator integration. See [game-engine integration](GAME_ENGINE.md).

Use `adb logcat -s Loli` and `adb logcat -s xhook` to inspect hook and injection messages. Keep the native profiler log when reporting a launch failure.

## Call stacks show addresses or unlikely functions

Use a symbol `.so` from the **same APK build**. Set the NDK path for `llvm-symbolizer`, then run:

```text
LoliProfilerCLI --symbolize capture.loli --symbol libUE4.so --out capture-symbolized.loli
```

Where that `.so` lacks DWARF line information, the nearest-symbol fallback can only show approximate names. This does not mean the allocation size or thread is correct; compare raw frames and allocator coverage when validating a capture.

## Check a saved file

Open it in the ImGui GUI, or export a report with `LoliProfilerCLI --dump capture.loli --out report.txt`. For indexed queries, export `report.db` and run `python -m agentcli.cli summary report.db`. In the Stacktrace panel, choose **Persistent** to show allocations still live at stop or **All Allocations** to inspect cumulative traffic when those records were retained.

## An older GUI capture shows an overlong timeline

An early ImGui capture path counted rendered frames as seconds for meminfo and screenshots. Allocation record timestamps were still in milliseconds, so selecting a late chart range could show zero allocations. Current builds sample against a one-second wall clock. When loading a file with the old frame-count pattern, the GUI logs a warning and approximately rescales its chart and screenshots to the last allocation time. The exact original sample times were not saved; make a new capture when precise timeline boundaries matter.
