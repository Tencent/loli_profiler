# macOS validation evidence

Validated on 2026-09-30: macOS 26.6.2 arm64, AppleClang 21/Xcode, pinned SFML 3.0.2, Homebrew Python 3.13.15, SDK Platform-Tools 37.0.1, and NDK r27 (27.0.12077973). Pinned submodule revisions are unchanged. Detailed capture evidence remains under ignored `build/macos-validation/`, `build/macos-device-validation/`, and `build/upstream-integration/`; this public report omits private capture filenames, device identifiers, symbols, and allocation statistics.

## Native and offline acceptance

- Native Release GUI app and CLI built through the Python driver. Mach-O arm64 binaries use system libraries/frameworks, with no Qt or build-tree dylib dependencies. Bundle metadata/icon and executable-relative resources were checked.
- All six supplied archived captures passed read-only validation: independent raw/live accounting, SQLite integrity/root totals/parent depth, text export, zero self-differences, cross comparison, and Python agent summary/hotspot/call-path queries. Source archive SHA-256 hashes remain unchanged.
- Native and extracted GUI load/range analysis passed, including range-tree byte accounting and rendering. Single-series iOS imports display Total memory without requiring six Android categories.
- Both native-only and capture-ready archives retained executable modes and passed CRC checks. All four LLVM Android hooks built with NDK r27 and matched staged/extracted copies.

## Fixes verified

1. Enable Objective-C/Objective-C++ in the common CMake scope; conditionally link `sfml-main`; use a supported macOS 13 deployment baseline and complete bundle metadata.
2. Resolve executable paths on macOS and stage local CLI/app runtime resources. Discover modern SDK/NDK/Python and ARMv7/ARM64 LLVM tools, including live symbolization.
3. Replace snapshot suffix-hash identity with exact parent/library/function identity. Synthetic colliding `Aa`/`BB` names and recursive paths remain distinct and acyclic; a previously nonterminating supplied export completes.
4. Render available memory series and load a macOS treemap font. Create missing configuration parent directories and omit generated runtime state from app archives.
5. Port the JDWP injector to Python 3 wire bytes/text, exact reads, packet ID matching, and queued asynchronous events. Protocol fixtures pass under Python 3.9 and 3.13; actual Python 3.13 injection succeeds.
6. Strip ARM64 return-PC authentication using baseline-compatible XPACLRI HINT before validation/recording. The Android hardware probe rejects signed null PCs while preserving unsigned addresses. A repeated actual capture removes fabricated high-address roots. This follows [Chromium's walker](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/base/debug/stack_trace.cc) and [Android simpleperf](https://android.googlesource.com/platform/system/extras/+/master/simpleperf/OfflineUnwinder.cpp).
7. Use ImGui's logical Control chords for all five application shortcuts: physical Command on macOS, Control elsewhere. Test wrong/additional modifiers, held-key repetition, and dialog key cleanup. Actual desktop checks opened Open/Save native dialogs and Run/Settings panels; SDK/NDK folder dialogs also clear stale key state.

## Real phone acceptance

After offline acceptance and the requested phone connection, CLI launch/JDWP injection, nonempty allocations/callstacks, login-to-requested-scene automation, orderly stop/smaps acknowledgment, save, matching-build symbolization, export, and GUI reopening passed. Capture used LLVM/malloc/strict/framepointer/arm64-v8a/threshold-zero/library whitelist and live-allocation retention. Final saved-data validation reconciles retained/live totals and checks free addresses, memory timelines, screenshots, mappings, resolved symbols, and absence of unstripped high-address frames. Original files are unchanged. No new phone capture was performed for the shortcut follow-up or upstream comparison integration.

## Upstream comparison integration

Merged `origin/remove-qt` feature `3d81a4e` into the current master work before committing. Its shared signed comparison API and separate dockable comparison viewer are retained. The exact identity fix remains in snapshot dump. Growth-only comparison expectations were updated to signed changed/new/removed stacks; lossy diff `.loli` export remains rejected as required upstream. The comparison target also conditionally links `sfml-main` on macOS, and Windows runtime staging retains all three executables.

The full Python build succeeds for `LoliProfilerImGui`, `LoliProfilerCLI`, `LoliProfilerCompare`, and all four hook ABIs. Five CTest cases pass: comparison-regressions, platform_paths, calltree_identity, gui_shortcuts, and jdwp_injector. The comparison test includes independent randomized references and signed/count-only/small changes, live frees, internal allocations, root skipping, malformed input, and facade transitions.

`tests/check_comparison_cli.py` passes actual CLI and GUI checks for Unicode/spaced paths, strict arguments, input protection, signed live totals, docked/stacked geometry, empty/identical rendering, independent searches, and exact GUI/CLI report parity. A representative supplied saved capture passes the updated snapshot/SQLite/text/self-comparison/agent validator. Package and detached-launch checks verify the new executable in extracted runtime layout. Build and check logs are retained locally under `build/upstream-integration/`.

Reproduce:

```sh
python3.13 scripts/build.py --mode full --non-interactive
cmake -S . -B build/cmake -DBUILD_PLATFORM_TESTS=ON -DBUILD_COMPARISON_TESTS=ON
cmake --build build/cmake --target LoliComparisonTests LoliPlatformPathsTest LoliCallTreeTest LoliGuiShortcutsTest --parallel
ctest --test-dir build/cmake --output-on-failure
python3.13 tests/check_comparison_cli.py --cli build/cmake/LoliProfilerCLI --gui build/cmake/LoliProfilerCompare
python3.13 scripts/validate_samples.py --cli build/cmake/LoliProfilerCLI --capture /path/to/capture.loli --out-dir build/sample-validation
```

Intel/universal binaries, execution on macOS 13, and public distribution signing/notarization were not tested.
